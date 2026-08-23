/* uw_desc.c - the descriptor API entry points (decision D15).
 * SPDX-License-Identifier: Apache-2.0
 *
 * Every function here takes everything its element needs in one struct, so the
 * element is written in a single pass and nothing has to be held open waiting
 * for a late property. That is the whole reason this layer exists; see D15.
 *
 * The emitters underneath are shared with the UCIS compatibility layer -- they
 * are the invariant part, the mapping from a coverage construct to the XSD's
 * element for it, and neither API changes them. */

#include "uw_api.h"
#include "uw_cvg.h"
#include "uw_stack.h"
#include "uw_tables.h"
#include "uw_text.h"
#include "uw_xml.h"

#include <stdlib.h>
#include <string.h>

/* ---- defaults ---------------------------------------------------------
 *
 * Zero means "not set" for every field, so these are memset-equivalent today.
 * They exist anyway, for two reasons. They are the only initialiser form that
 * is clean from C99 through C++20 (D15's language-reach table), and they are
 * where a non-zero schema default would live if one is ever needed -- without
 * that, every emitter would have to re-derive it. */

#define UW_DEFAULTS(type, fn)      \
    type fn(void)                  \
    {                              \
        type d;                    \
        memset(&d, 0, sizeof(d));  \
        return d;                  \
    }

UW_DEFAULTS(uw_test_desc_t,        uw_test_defaults)
UW_DEFAULTS(uw_instance_desc_t,    uw_instance_defaults)
UW_DEFAULTS(uw_bin_desc_t,         uw_bin_defaults)
UW_DEFAULTS(uw_statement_desc_t,   uw_statement_defaults)
UW_DEFAULTS(uw_branch_desc_t,      uw_branch_defaults)
UW_DEFAULTS(uw_toggle_desc_t,      uw_toggle_defaults)
UW_DEFAULTS(uw_expr_desc_t,        uw_expr_defaults)
UW_DEFAULTS(uw_covergroup_desc_t,  uw_covergroup_defaults)
UW_DEFAULTS(uw_cginstance_desc_t,  uw_cginstance_defaults)
UW_DEFAULTS(uw_coverpoint_desc_t,  uw_coverpoint_defaults)
UW_DEFAULTS(uw_cross_desc_t,       uw_cross_defaults)

/* ---- shared helpers ---------------------------------------------------- */

/* Every entry point returns 0 or -1 in the UCIS convention; the reason is on
 * the database, not in the return value. */
static int uw_rc(uw_db_t* db)
{
    return (db->err || db->buf.status) ? -1 : 0;
}

static void uw_src_of(const uw_src_t* s, ucisSourceInfoT* out)
{
    out->filehandle = (s != NULL) ? (ucisFileHandleT)s->file : NULL;
    out->line       = (s != NULL) ? s->line : 0;
    out->token      = (s != NULL) ? s->token : 0;
}

static ucisCoverTypeT uw_bin_cover_type(uw_bin_kind_t kind)
{
    switch (kind) {
        case UW_BIN_DEFAULT: return UCIS_DEFAULTBIN;
        case UW_BIN_IGNORE:  return UCIS_IGNOREBIN;
        case UW_BIN_ILLEGAL: return UCIS_ILLEGALBIN;
        default:             return UCIS_CVGBIN;
    }
}

static void uw_cover_of(const uw_bin_desc_t* d, ucisCoverDataT* data,
                        ucisCoverTypeT type)
{
    memset(data, 0, sizeof(*data));
    data->type       = type;
    data->flags      = UCIS_IS_64BIT;
    data->data.int64 = d->count;
    if (d->goal > 0) {
        data->flags |= UCIS_HAS_GOAL;
        data->goal   = d->goal;
    }
    if (d->weight > 0) {
        data->flags |= UCIS_HAS_WEIGHT;
        data->weight = d->weight;
    }
    if (d->excluded) {
        data->flags |= UCIS_EXCLUDED;
    }
}

/* ---- lifecycle --------------------------------------------------------
 *
 * These forward to the UCIS spellings for now. The direction reverses when the
 * compat layer moves out: the descriptor API becomes the implementation and
 * ucis_* becomes the wrapper. Keeping it this way round during the transition
 * means the tree never has two copies of the open/close logic. */

uw_db_t* uw_open(const char* path)
{
    return (uw_db_t*)ucis_OpenWriteStream(path);
}

uw_db_t* uw_open_sink(const ucisWriterSinkT* sink)
{
    return (uw_db_t*)ucis_writer_OpenSinkStream(sink);
}

int uw_close(uw_db_t* db)          { return ucis_Close((ucisT)db); }
int uw_end(uw_db_t* db)            { return ucis_WriteStreamScope((ucisT)db); }

ucisWriterStatusT uw_error(uw_db_t* db)   { return ucis_writer_error((ucisT)db); }
const char* uw_error_string(uw_db_t* db)  { return ucis_writer_error_string((ucisT)db); }
const char* uw_status_name(ucisWriterStatusT s) { return ucis_writer_status_name(s); }
unsigned long uw_warnings(uw_db_t* db)    { return ucis_writer_warnings((ucisT)db); }
uint64_t uw_bytes_written(uw_db_t* db)    { return ucis_writer_bytes_written((ucisT)db); }
int uw_set_pretty(uw_db_t* db, int on)    { return ucis_writer_set_pretty((ucisT)db, on); }
int uw_set_written_by(uw_db_t* db, const char* w)   { return ucis_writer_set_written_by((ucisT)db, w); }
int uw_set_written_time(uw_db_t* db, const char* t) { return ucis_writer_set_written_time((ucisT)db, t); }

/* ---- tables ------------------------------------------------------------ */

uw_file_t* uw_file(uw_db_t* dbh, const char* path, const char* workdir)
{
    uw_db_t* db = uw_db_check((ucisT)dbh);
    if (db == NULL) {
        return NULL;
    }
    return uw_filetab_intern(db, path, workdir);
}

uw_test_t* uw_test(uw_db_t* dbh, const uw_test_desc_t* d)
{
    uw_db_t*   db = uw_db_check((ucisT)dbh);
    uw_hist_t* h;

    if (db == NULL) {
        return NULL;
    }
    if (d == NULL) {
        uw_fail(db, UCIS_WRITER_ERR_USAGE, "uw_test with NULL descriptor", NULL);
        return NULL;
    }
    if (db->phase != UW_PHASE_TABLES) {
        uw_fail(db, UCIS_WRITER_ERR_SEALED,
                "test record created after the first instance", d->name);
        return NULL;
    }
    h = uw_hist_create(db, d->name, d->physical_name,
                       (ucisHistoryNodeKindT)(d->kind ? (int)d->kind
                                                      : UCIS_HISTORYNODE_TEST));
    if (h == NULL) {
        return NULL;
    }

    h->has_testdata = 1;
    h->teststatus   = d->passed ? UCIS_TESTSTATUS_OK : UCIS_TESTSTATUS_ERROR;
    h->compulsory   = d->compulsory;
    h->simtime      = d->sim_time;
    h->cputime      = d->cpu_time;
    h->cost         = d->cost;

    h->timeunit       = uw_strdup(db, d->time_unit);
    h->runcwd         = uw_strdup(db, d->run_cwd);
    h->seed           = uw_strdup(db, d->seed);
    h->cmd            = uw_strdup(db, d->cmd);
    h->args           = uw_strdup(db, d->args);
    h->date           = uw_strdup(db, d->date);
    h->username       = uw_strdup(db, d->user_name);
    h->toolcategory   = uw_strdup(db, d->tool_category);
    h->comment        = uw_strdup(db, d->comment);
    h->vendor_id      = uw_strdup(db, d->vendor_id);
    h->vendor_tool    = uw_strdup(db, d->vendor_tool);
    h->vendor_version = uw_strdup(db, d->vendor_version);

    return (uw_test_t*)h;
}

/* ---- hierarchy --------------------------------------------------------- */

int uw_instance(uw_db_t* dbh, const uw_instance_desc_t* d)
{
    uw_db_t*        db = uw_db_check((ucisT)dbh);
    ucisSourceInfoT si;

    if (db == NULL) {
        return -1;
    }
    if (d == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "uw_instance with NULL descriptor", NULL) ? -1 : 0;
    }
    uw_src_of(&d->src, &si);
    ucis_CreateInstanceByName(db, NULL, d->name, &si, 1, UCIS_OTHER,
                              UCIS_INSTANCE, (char*)d->du_name,
                              UCIS_INST_ONCE);
    return uw_rc(db);
}

/* ---- code coverage ----------------------------------------------------- */

int uw_statement(uw_db_t* dbh, const uw_statement_desc_t* d)
{
    uw_db_t*        db = uw_db_check((ucisT)dbh);
    ucisSourceInfoT si;
    ucisCoverDataT  data;
    uw_bin_desc_t   b;

    if (db == NULL) {
        return -1;
    }
    if (d == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "uw_statement with NULL descriptor", NULL) ? -1 : 0;
    }
    b = uw_bin_defaults();
    b.count    = d->count;
    b.weight   = d->weight;
    b.excluded = d->excluded;
    uw_cover_of(&b, &data, UCIS_STMTBIN);
    uw_src_of(&d->src, &si);

    uw_emit_statement(db, d->name, &data, &si);
    return uw_rc(db);
}

int uw_branch(uw_db_t* dbh, const uw_branch_desc_t* d)
{
    uw_db_t*        db = uw_db_check((ucisT)dbh);
    ucisSourceInfoT si;

    if (db == NULL) {
        return -1;
    }
    if (d == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "uw_branch with NULL descriptor", NULL) ? -1 : 0;
    }
    uw_src_of(&d->src, &si);
    if (uw_open_branch_statement(db, d->name, &si) != UCIS_WRITER_OK) {
        return -1;
    }
    /* @statementType is required and defaults to "if". The descriptor states
     * it up front, so unlike the UCIS path there is no window in which the
     * element is already committed and the answer arrives too late. */
    if (d->is_case) {
        uw_text_attr(&db->buf, "statementType", "case", NULL);
    }
    if (d->has_else) {
        uw_text_attr(&db->buf, "hasElse", "true", NULL);
    }
    return uw_rc(db);
}

int uw_toggle(uw_db_t* dbh, const uw_toggle_desc_t* d)
{
    uw_db_t* db = uw_db_check((ucisT)dbh);

    if (db == NULL) {
        return -1;
    }
    if (d == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "uw_toggle with NULL descriptor", NULL) ? -1 : 0;
    }
    uw_open_toggle(db, d->name, d->canonical_name,
                   (ucisToggleTypeT)d->type, (ucisToggleDirT)d->dir);
    return uw_rc(db);
}

int uw_toggle_bit(uw_db_t* dbh, const char* name)
{
    uw_db_t*   db  = uw_db_check((ucisT)dbh);
    uw_elem_t* top;

    if (db == NULL) {
        return -1;
    }
    top = uw_el_top(db);
    if (top == NULL || top->type != UCIS_TOGGLE) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "uw_toggle_bit outside a toggle object", name) ? -1 : 0;
    }
    /* uw_open_toggle opens a bit rather than an object when one is already
     * open, which is exactly the nesting UCIS uses for a vector. */
    uw_open_toggle(db, name, NULL, UCIS_TOGGLE_TYPE_NET,
                   UCIS_TOGGLE_DIR_INTERNAL);
    return uw_rc(db);
}

int uw_expr(uw_db_t* dbh, const uw_expr_desc_t* d)
{
    uw_db_t*        db = uw_db_check((ucisT)dbh);
    ucisSourceInfoT si;

    if (db == NULL) {
        return -1;
    }
    if (d == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "uw_expr with NULL descriptor", NULL) ? -1 : 0;
    }
    uw_src_of(&d->src, &si);
    if (uw_open_expr(db, d->name, &si) != UCIS_WRITER_OK) {
        return -1;
    }
    if (d->terms != NULL) {
        uw_copy_bounded(db, db->expr_terms, sizeof(db->expr_terms), d->terms);
        db->expr_terms_explicit = 1;
    }
    return uw_rc(db);
}

int uw_expr_metric(uw_db_t* dbh, const char* name)
{
    uw_db_t*   db = uw_db_check((ucisT)dbh);
    uw_elem_t* top;

    if (db == NULL) {
        return -1;
    }
    top = uw_el_top(db);
    if (top == NULL || (top->type != UCIS_EXPR && top->type != UCIS_COND)) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "uw_expr_metric outside an expression", name) ? -1 : 0;
    }
    uw_open_expr(db, name, NULL);
    return uw_rc(db);
}

/* ---- functional coverage ----------------------------------------------- */

/* Copy the option fields a descriptor carries into the staging area the
 * pending element's <options> will be written from. Only fields the caller set
 * are marked: every option attribute has a schema default, and emitting one we
 * were never told would assert a value nobody chose. */
static void uw_stage_opts(uw_db_t* db, int weight, int goal, int at_least,
                          int auto_bin_max, int detect_overlap,
                          int num_print_missing, int per_instance,
                          int merge_instances, int have_cgi)
{
    uw_opts_t* o = &db->opts;

    if (weight > 0)       { o->weight = weight;     o->set |= UW_OPTSET_WEIGHT; }
    if (goal > 0)         { o->goal = goal;         o->set |= UW_OPTSET_GOAL; }
    if (at_least > 0)     { o->at_least = at_least; o->set |= UW_OPTSET_AT_LEAST; }
    if (auto_bin_max > 0) { o->auto_bin_max = auto_bin_max;
                            o->set |= UW_OPTSET_AUTO_BIN_MAX; }
    if (detect_overlap)   { o->detect_overlap = 1;
                            o->set |= UW_OPTSET_DETECT_OVERLAP; }
    if (num_print_missing > 0) { o->num_print_missing = num_print_missing;
                                 o->set |= UW_OPTSET_PRINT_MISSING; }
    if (have_cgi) {
        if (per_instance)    { o->per_instance = 1;
                               o->set |= UW_OPTSET_PER_INSTANCE; }
        if (merge_instances) { o->merge_instances = 1;
                               o->set |= UW_OPTSET_MERGE_INST; }
    }
}

int uw_covergroup(uw_db_t* dbh, const uw_covergroup_desc_t* d)
{
    uw_db_t*        db = uw_db_check((ucisT)dbh);
    ucisSourceInfoT si;

    if (db == NULL) {
        return -1;
    }
    if (d == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "uw_covergroup with NULL descriptor", NULL) ? -1 : 0;
    }
    uw_src_of(&d->src, &si);
    uw_open_covergroup(db, d->name, &si);
    return uw_rc(db);
}

int uw_cginstance(uw_db_t* dbh, const uw_cginstance_desc_t* d)
{
    uw_db_t*        db = uw_db_check((ucisT)dbh);
    ucisSourceInfoT si;

    if (db == NULL) {
        return -1;
    }
    if (d == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "uw_cginstance with NULL descriptor", NULL) ? -1 : 0;
    }
    uw_src_of(&d->src, &si);
    if (uw_open_coverinstance(db, d->name, &si) != UCIS_WRITER_OK) {
        return -1;
    }
    uw_stage_opts(db, d->weight, d->goal, d->at_least, d->auto_bin_max,
                  d->detect_overlap, d->num_print_missing, d->per_instance,
                  d->merge_instances, 1);
    return uw_rc(db);
}

int uw_coverpoint(uw_db_t* dbh, const uw_coverpoint_desc_t* d)
{
    uw_db_t* db = uw_db_check((ucisT)dbh);

    if (db == NULL) {
        return -1;
    }
    if (d == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "uw_coverpoint with NULL descriptor", NULL) ? -1 : 0;
    }
    if (uw_open_coverpoint(db, d->name) != UCIS_WRITER_OK) {
        return -1;
    }
    if (d->expr_string != NULL) {
        uw_text_attr(&db->buf, "exprString", d->expr_string, &db->warnings);
    }
    uw_stage_opts(db, d->weight, d->goal, d->at_least, d->auto_bin_max,
                  d->detect_overlap, 0, 0, 0, 0);
    return uw_rc(db);
}

int uw_cross(uw_db_t* dbh, const uw_cross_desc_t* d)
{
    uw_db_t* db = uw_db_check((ucisT)dbh);
    size_t   i;

    if (db == NULL) {
        return -1;
    }
    if (d == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "uw_cross with NULL descriptor", NULL) ? -1 : 0;
    }
    if (uw_open_cross(db, d->name) != UCIS_WRITER_OK) {
        return -1;
    }
    for (i = 0; i < d->n_crossed; ++i) {
        if (uw_cross_add_cvp(db, d->crossed[i]) != UCIS_WRITER_OK) {
            return -1;
        }
    }
    uw_stage_opts(db, d->weight, d->goal, d->at_least, 0, 0,
                  d->num_print_missing, 0, 0, 0);
    return uw_rc(db);
}

/* ---- bins --------------------------------------------------------------
 *
 * One entry point for every kind of bin, routed by the scope it lands in
 * rather than by a type constant the caller has to supply. The scope already
 * determines which element is legal, so asking the caller to say it again is
 * asking them to be wrong. */

int uw_bin(uw_db_t* dbh, const uw_bin_desc_t* d)
{
    uw_db_t*        db = uw_db_check((ucisT)dbh);
    uw_elem_t*      top;
    ucisCoverDataT  data;
    ucisSourceInfoT si;

    if (db == NULL) {
        return -1;
    }
    if (d == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "uw_bin with NULL descriptor", NULL) ? -1 : 0;
    }
    top = uw_el_top(db);
    if (top == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "uw_bin with no scope open", d->name) ? -1 : 0;
    }

    if (top->type == UCIS_BRANCH) {
        uw_cover_of(d, &data, UCIS_BRANCHBIN);
        uw_src_of(&d->src, &si);
        uw_emit_branch(db, d->name, &data, &si);
    } else if (top->type == UCIS_TOGGLE) {
        uw_cover_of(d, &data, UCIS_TOGGLEBIN);
        uw_emit_toggle_bin(db, d->name, &data);
    } else if (top->type == UCIS_EXPR || top->type == UCIS_COND) {
        uw_cover_of(d, &data, UCIS_EXPRBIN);
        uw_emit_expr_bin(db, d->name, &data);
    } else if (top->type == UCIS_COVERPOINT || top->type == UCIS_CROSS) {
        uw_cover_of(d, &data, uw_bin_cover_type(d->kind));
        uw_emit_cvg_bin(db, d->name, &data);
    } else {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "no scope open that can hold a bin", d->name) ? -1 : 0;
    }
    return uw_rc(db);
}
