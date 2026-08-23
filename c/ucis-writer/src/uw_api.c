/* uw_api.c - the ucis_* entry points.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Work items 1.8, 2.1 - 2.6 and 3.2 of docs/ucis-writer-impl-plan.md.
 *
 * Write-streaming rules enforced here, from UCIS 1.0 Annex A.14:
 *   - every `parent` argument must be NULL; the parent is the implicit
 *     current scope
 *   - strings are not copied except into the two resident tables
 *   - objects are finished by the next create call, or explicitly by
 *     ucis_WriteStream / ucis_WriteStreamScope */

#include "uw_api.h"
#include "uw_cvg.h"
#include "uw_stack.h"
#include "uw_tables.h"
#include "uw_text.h"
#include "uw_xml.h"

#include <stdlib.h>
#include <string.h>

/* ---- FILE* sink ------------------------------------------------------- */

static int uw_file_sink_write(void* ctx, const char* data, size_t len)
{
    return fwrite(data, 1, len, (FILE*)ctx) == len ? 0 : -1;
}

static int uw_file_sink_close(void* ctx)
{
    return fclose((FILE*)ctx);
}

/* ---- construction ----------------------------------------------------- */

static uw_db_t* uw_db_new(const ucisWriterSinkT* sink)
{
    uw_db_t* db = (uw_db_t*)calloc(1, sizeof(uw_db_t));
    char     now[24];

    if (db == NULL) {
        return NULL;
    }
    db->bufstore = (char*)malloc(UCIS_WRITER_BUFSZ);
    if (db->bufstore == NULL) {
        free(db);
        return NULL;
    }
    db->magic     = UW_DB_MAGIC;
    db->path_sep  = '/';
    db->phase     = UW_PHASE_TABLES;
    uw_buf_init(&db->buf, db->bufstore, UCIS_WRITER_BUFSZ, sink);
    uw_filetab_init(&db->files);

    uw_now_iso8601(now, sizeof(now));
    db->written_time = uw_strdup(db, now);
    db->written_by   = uw_strdup(db, UCIS_WRITER_VENDOR_ID " " UCIS_WRITER_VENDOR_TOOL);

    /* The root element opens immediately but stays pending, so that
     * ucis_writer_set_written_by / _time can still change its attributes right
     * up until the first thing is written into the document. */
    uw_el_begin(db, "UCIS", 0);
    return db;
}

/* Terminate the root's attribute list. Called once, when the document body
 * starts, because writtenBy/writtenTime are settable until then. */
UW_INTERNAL int uw_root_seal(uw_db_t* db)
{
    uw_elem_t* root;
    if (db->depth < 1) {
        return db->buf.status;
    }
    root = &db->stack[0];
    if (!root->pending) {
        return db->buf.status;
    }
    uw_text_attr(&db->buf, "ucisVersion", UCIS_WRITER_UCIS_VERSION, NULL);
    uw_text_attr(&db->buf, "writtenBy",
                 db->written_by ? db->written_by : UCIS_WRITER_VENDOR_TOOL,
                 &db->warnings);
    uw_text_attr(&db->buf, "writtenTime",
                 uw_is_datetime(db->written_time) ? db->written_time
                                                  : "1970-01-01T00:00:00Z",
                 &db->warnings);
    return db->buf.status;
}

/* Everything that writes into the document body funnels through here: seal
 * the root, then flush the resident tables if they have not gone out yet. */
UW_INTERNAL int uw_body(uw_db_t* db)
{
    if (uw_root_seal(db) != UCIS_WRITER_OK) {
        return db->buf.status;
    }
    return uw_tables_flush(db);
}

ucisT ucis_writer_OpenSinkStream(const ucisWriterSinkT* sink)
{
    if (sink == NULL || sink->write == NULL) {
        return NULL;
    }
    return (ucisT)uw_db_new(sink);
}

ucisT ucis_OpenWriteStream(const char* name)
{
    ucisWriterSinkT sink;
    FILE*           fp;
    uw_db_t*        db;

    if (name == NULL) {
        return NULL;
    }
    fp = fopen(name, "wb");
    if (fp == NULL) {
        return NULL;
    }
    sink.write = uw_file_sink_write;
    sink.close = uw_file_sink_close;
    sink.ctx   = fp;

    db = uw_db_new(&sink);
    if (db == NULL) {
        fclose(fp);
        return NULL;
    }
    db->own_file = fp;
    return (ucisT)db;
}

int ucis_writer_set_written_by(ucisT dbh, const char* who)
{
    uw_db_t* db = uw_db_check(dbh);
    if (db == NULL || who == NULL) {
        return -1;
    }
    if (db->phase != UW_PHASE_TABLES || !db->stack[0].pending) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "writtenBy set after the document body started", NULL) ? -1 : 0;
    }
    free(db->written_by);
    db->written_by = uw_strdup(db, who);
    return (db->err || db->buf.status) ? -1 : 0;
}

int ucis_writer_set_written_time(ucisT dbh, const char* xsd_datetime)
{
    uw_db_t* db = uw_db_check(dbh);
    if (db == NULL || xsd_datetime == NULL) {
        return -1;
    }
    if (!uw_is_datetime(xsd_datetime)) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "not an xsd:dateTime", xsd_datetime) ? -1 : 0;
    }
    if (!db->stack[0].pending) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "writtenTime set after the document body started", NULL) ? -1 : 0;
    }
    free(db->written_time);
    db->written_time = uw_strdup(db, xsd_datetime);
    return (db->err || db->buf.status) ? -1 : 0;
}

/* ---- lifecycle -------------------------------------------------------- */

int ucis_WriteStream(ucisT dbh)
{
    uw_db_t* db = uw_db_check(dbh);
    if (db == NULL) {
        return -1;
    }
    /* "Flush the in-flight object": terminate its start tag so no further
     * attribute can be attached to it. Children are still permitted. */
    uw_el_commit(db);
    return (db->err || db->buf.status) ? -1 : 0;
}

int ucis_WriteStreamScope(ucisT dbh)
{
    uw_db_t* db = uw_db_check(dbh);
    if (db == NULL) {
        return -1;
    }
    if (db->depth <= 1) {
        /* Depth 1 is the root, which only ucis_Close may close. */
        uw_fail(db, UCIS_WRITER_ERR_UNBALANCED,
                "ucis_WriteStreamScope with no scope open", NULL);
        return -1;
    }
    /* Close through any wrappers this library opened -- coverage-kind
     * elements have no counterpart in the UCIS call sequence, so a caller
     * balancing its own scopes must not have to know they exist. */
    while (db->depth > 1) {
        int owned = db->stack[db->depth - 1].owned;
        if (uw_el_end(db) != UCIS_WRITER_OK) {
            break;
        }
        if (!owned) {
            break;
        }
    }
    return (db->err || db->buf.status) ? -1 : 0;
}

int ucis_Close(ucisT dbh)
{
    uw_db_t*    db = uw_db_check(dbh);
    const char* unbalanced;
    int         status;

    if (db == NULL) {
        return -1;
    }
    /* An unbalanced document is a caller bug worth reporting, but the file
     * still gets closed properly: half a document that no reader accepts
     * helps nobody diagnose anything. So finish the document first and latch
     * the error afterwards -- latching it now would suppress the very output
     * we are trying to salvage. */
    unbalanced = db->depth > 1 ? db->stack[db->depth - 1].tag : NULL;

    uw_body(db);                 /* emits the tables even for an empty document */
    if (db->inst_count == 0) {
        uw_placeholder_instance(db);
    }
    uw_el_unwind(db, 0);
    if (db->pretty) {
        uw_buf_putc(&db->buf, '\n');
    }
    uw_buf_finish(&db->buf);
    if (unbalanced != NULL) {
        uw_fail(db, UCIS_WRITER_ERR_UNBALANCED,
                "scope still open at ucis_Close", unbalanced);
    }

    /* Either class of failure makes the return non-zero; the caller finds out
     * which from ucis_writer_error before calling this. */
    status = db->err ? db->err : db->buf.status;

    uw_filetab_free(&db->files);
    uw_hist_free_all(db);
    uw_cvptab_reset(&db->cvpbins);
    free(db->written_by);
    free(db->written_time);
    free(db->cur_du);
    free(db->vendor_id);
    free(db->vendor_tool);
    free(db->vendor_version);
    free(db->bufstore);
    db->magic = 0;
    free(db);

    return status == UCIS_WRITER_OK ? 0 : -1;
}

int ucis_SetPathSeparator(ucisT dbh, char separator)
{
    uw_db_t* db = uw_db_check(dbh);
    if (db == NULL) {
        return -1;
    }
    db->path_sep = separator;
    return 0;
}

char ucis_GetPathSeparator(ucisT dbh)
{
    uw_db_t* db = uw_db_check(dbh);
    return db ? db->path_sep : '/';
}

/* ---- file handles ----------------------------------------------------- */

ucisFileHandleT ucis_CreateFileHandle(ucisT dbh, const char* filename,
                                      const char* fileworkdir)
{
    uw_db_t* db = uw_db_check(dbh);
    if (db == NULL) {
        return NULL;
    }
    return (ucisFileHandleT)uw_filetab_intern(db, filename, fileworkdir);
}

ucisFileHandleT ucis_CreateSrcFileHandle(ucisT dbh, ucisScopeT du_scope,
                                         const char* filename,
                                         const char* fileworkdir)
{
    /* UCIS-XML has one flat SOURCE_FILE table, not the per-DU tables the C
     * API allows, so the DU association has nowhere to go in this backend. */
    (void)du_scope;
    return ucis_CreateFileHandle(dbh, filename, fileworkdir);
}

const char* ucis_GetFileName(ucisT dbh, ucisFileHandleT filehandle)
{
    uw_file_t* f = (uw_file_t*)filehandle;
    (void)dbh;
    return f ? f->name : NULL;
}

/* ---- history nodes ---------------------------------------------------- */

ucisHistoryNodeT ucis_CreateHistoryNode(ucisT dbh, ucisHistoryNodeT parent,
                                        char* logicalname, char* physicalname,
                                        ucisHistoryNodeKindT kind)
{
    uw_db_t*   db = uw_db_check(dbh);
    uw_hist_t* h;

    if (db == NULL) {
        return NULL;
    }
    if (db->phase != UW_PHASE_TABLES) {
        uw_fail(db, UCIS_WRITER_ERR_SEALED,
                "history node created after the first instance", logicalname);
        return NULL;
    }
    h = uw_hist_create(db, logicalname, physicalname, kind);
    if (h != NULL && parent != NULL) {
        h->has_parent = 1;
        h->parent_id  = ((uw_hist_t*)parent)->id;
    }
    return (ucisHistoryNodeT)h;
}

int ucis_SetTestData(ucisT dbh, ucisHistoryNodeT node, ucisTestDataT* data)
{
    uw_db_t*   db = uw_db_check(dbh);
    uw_hist_t* h  = (uw_hist_t*)node;

    if (db == NULL) {
        return -1;
    }
    if (h == NULL || data == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE, "ucis_SetTestData", NULL) ? -1 : 0;
    }
    h->has_testdata = 1;
    h->teststatus   = data->teststatus;
    h->simtime      = data->simtime;
    h->cputime      = data->cputime;
    h->cost         = data->cost;
    h->compulsory   = data->compulsory;

    free(h->timeunit);     h->timeunit     = uw_strdup(db, data->timeunit);
    free(h->runcwd);       h->runcwd       = uw_strdup(db, data->runcwd);
    free(h->seed);         h->seed         = uw_strdup(db, data->seed);
    free(h->cmd);          h->cmd          = uw_strdup(db, data->cmd);
    free(h->args);         h->args         = uw_strdup(db, data->args);
    free(h->date);         h->date         = uw_strdup(db, data->date);
    free(h->username);     h->username     = uw_strdup(db, data->username);
    free(h->toolcategory); h->toolcategory = uw_strdup(db, data->toolcategory);

    return (db->err || db->buf.status) ? -1 : 0;
}

/* ---- instances -------------------------------------------------------- */

ucisScopeT ucis_CreateInstanceByName(ucisT dbh, ucisScopeT parent,
                                     const char* name, ucisSourceInfoT* srcinfo,
                                     int weight, ucisSourceT source,
                                     ucisScopeTypeT type, char* du_name,
                                     int flags)
{
    uw_db_t*   db = uw_db_check(dbh);
    uw_file_t* f;
    uint32_t   fileid = 0;
    uint32_t   line   = 0;

    (void)weight;
    (void)source;
    (void)flags;

    if (db == NULL) {
        return NULL;
    }
    if (parent != NULL) {
        uw_fail(db, UCIS_WRITER_ERR_USAGE,
                "write-streaming requires a NULL parent", name);
        return NULL;
    }
    if (type != UCIS_INSTANCE) {
        uw_fail(db, UCIS_WRITER_ERR_USAGE,
                "ucis_CreateInstanceByName expects UCIS_INSTANCE", name);
        return NULL;
    }
    if (uw_body(db) != UCIS_WRITER_OK) {
        return NULL;
    }
    /* instanceCoverages is a flat list under the root, related by
     * @parentInstanceId rather than by nesting, so an instance always closes
     * whatever came before it. */
    if (uw_el_unwind(db, 1) != UCIS_WRITER_OK) {
        return NULL;
    }
    if (uw_el_begin(db, "instanceCoverages", UCIS_INSTANCE) != UCIS_WRITER_OK) {
        return NULL;
    }

    uw_text_attr(&db->buf, "name", name ? name : "(unnamed)", &db->warnings);
    /* @key must be unique across the document. The hierarchical instance name
     * already is, and reusing it keeps the document self-describing. */
    uw_text_attr(&db->buf, "key", name ? name : "(unnamed)", &db->warnings);
    uw_text_attr(&db->buf, "moduleName", du_name, &db->warnings);
    /* @instanceId is a free counter. @parentInstanceId is deliberately absent:
     * reconstructing it would need a resident name-to-id table growing with
     * the design's instance count, to encode a relationship the hierarchical
     * @name already carries. See D10. */
    uw_text_attr_u64(&db->buf, "instanceId", db->inst_count + 1u);

    if (srcinfo != NULL) {
        f = (uw_file_t*)srcinfo->filehandle;
        if (f != NULL) {
            fileid = f->id;
        }
        line = srcinfo->line > 0 ? (uint32_t)srcinfo->line : 0u;
    }
    /* Instances routinely have no source location — Verilator's coverage.dat
     * carries none — so a clamp here is not a warning. */
    uw_el_set_id(db, fileid, line, 1u, 0);
    db->inst_count++;
    db->inst_depth = db->depth;
    db->cur_kind   = UW_KIND_NONE;
    db->block_mode = 0;
    /* CG_ID/@moduleName is required and has no argument of its own; the design
     * unit of the instance the covergroup lives in is what it means. */
    uw_copy_bounded(db, db->cg_module, sizeof(db->cg_module), du_name);

    return (ucisScopeT)uw_el_top(db);
}

/* ---- scopes ----------------------------------------------------------- */

ucisScopeT ucis_CreateScope(ucisT dbh, ucisScopeT parent, const char* name,
                            ucisSourceInfoT* srcinfo, int weight,
                            ucisSourceT source, ucisScopeTypeT type,
                            ucisFlagsT flags)
{
    uw_db_t* db = uw_db_check(dbh);

    (void)weight;
    (void)source;
    (void)flags;

    if (db == NULL) {
        return NULL;
    }
    if (parent != NULL) {
        uw_fail(db, UCIS_WRITER_ERR_USAGE,
                "write-streaming requires a NULL parent", name);
        return NULL;
    }

    if (type & UCIS_DU_ANY) {
        /* UCIS-XML has no design-unit element (D8). The name is the only part
         * that survives, as @moduleName on instances created after it, so we
         * remember it and emit nothing. */
        free(db->cur_du);
        db->cur_du = uw_strdup(db, name);
        return (ucisScopeT)db;   /* a handle that is valid but not an element */
    }
    if (type == UCIS_INSTANCE) {
        return ucis_CreateInstanceByName(db, NULL, name, srcinfo, weight,
                                         source, type,
                                         db->cur_du, (int)flags);
    }
    if (type == UCIS_EXPR || type == UCIS_COND) {
        if (uw_open_expr(db, name, srcinfo) != UCIS_WRITER_OK) {
            return NULL;
        }
        return (ucisScopeT)uw_el_top(db);
    }
    if (type == UCIS_BRANCH) {
        if (uw_open_branch_statement(db, name, srcinfo) != UCIS_WRITER_OK) {
            return NULL;
        }
        return (ucisScopeT)uw_el_top(db);
    }
    if (type == UCIS_COVERGROUP) {
        if (uw_open_covergroup(db, name, srcinfo) != UCIS_WRITER_OK) {
            return NULL;
        }
        return (ucisScopeT)uw_el_top(db);
    }
    if (type == UCIS_COVERINSTANCE) {
        if (uw_open_coverinstance(db, name, srcinfo) != UCIS_WRITER_OK) {
            return NULL;
        }
        return (ucisScopeT)uw_el_top(db);
    }
    if (type == UCIS_COVERPOINT) {
        if (uw_open_coverpoint(db, name) != UCIS_WRITER_OK) {
            return NULL;
        }
        return (ucisScopeT)uw_el_top(db);
    }
    if (type == UCIS_CROSS) {
        if (uw_open_cross(db, name) != UCIS_WRITER_OK) {
            return NULL;
        }
        return (ucisScopeT)uw_el_top(db);
    }

    uw_fail(db, UCIS_WRITER_ERR_USAGE,
            "scope type not supported by this backend yet", name);
    return NULL;
}

/* ---- coveritems ------------------------------------------------------- */

int ucis_CreateNextCover(ucisT dbh, ucisScopeT parent, const char* name,
                         ucisCoverDataT* data, ucisSourceInfoT* sourceinfo)
{
    uw_db_t* db = uw_db_check(dbh);

    if (db == NULL) {
        return -1;
    }
    if (data == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "ucis_CreateNextCover with NULL data", name) ? -1 : 0;
    }
    /* `parent` is accepted but ignored: in write-streaming the coveritem
     * always attaches to the current scope, and there is no other scope we
     * could reach. Rejecting a non-NULL value would break callers that pass
     * the handle they just received, which is the natural thing to write. */
    (void)parent;

    if (data->type & UCIS_STMTBIN) {
        uw_emit_statement(db, name, data, sourceinfo);
    } else if (data->type & UCIS_BRANCHBIN) {
        uw_emit_branch(db, name, data, sourceinfo);
    } else if (data->type & UCIS_TOGGLEBIN) {
        uw_emit_toggle_bin(db, name, data);
    } else if (data->type & (UCIS_EXPRBIN | UCIS_CONDBIN)) {
        uw_emit_expr_bin(db, name, data);
    } else if (data->type & (UCIS_CVGBIN | UCIS_IGNOREBIN | UCIS_ILLEGALBIN |
                             UCIS_DEFAULTBIN)) {
        uw_emit_cvg_bin(db, name, data);
    } else {
        uw_fail(db, UCIS_WRITER_ERR_USAGE,
                "coveritem type not supported by this backend yet", name);
    }
    return (db->err || db->buf.status) ? -1 : 0;
}

ucisScopeT ucis_CreateToggle(ucisT dbh, ucisScopeT parent, const char* name,
                             const char* canonical_name, ucisFlagsT flags,
                             ucisToggleMetricT toggle_metric,
                             ucisToggleTypeT toggle_type,
                             ucisToggleDirT toggle_dir)
{
    uw_db_t* db = uw_db_check(dbh);

    (void)flags;
    /* The metric constrains which bin names are legal (spec 6.7.1) but has no
     * slot in TOGGLE_OBJECT, so it survives only as the bin names themselves. */
    (void)toggle_metric;

    if (db == NULL) {
        return NULL;
    }
    if (parent != NULL) {
        uw_fail(db, UCIS_WRITER_ERR_USAGE,
                "write-streaming requires a NULL parent", name);
        return NULL;
    }
    if (uw_open_toggle(db, name, canonical_name, toggle_type, toggle_dir)
        != UCIS_WRITER_OK) {
        return NULL;
    }
    return (ucisScopeT)uw_el_top(db);
}

/* ---- misc ------------------------------------------------------------- */

const char* ucis_ComposeDUName(const char* library_name,
                               const char* primary_name,
                               const char* secondary_name)
{
    /* UCIS 1.0: library.primary(secondary). The returned string is valid
     * until the next call, which is the standard's own contract. */
    static char buf[512];
    size_t      n = 0;
    size_t      i;
    const char* parts[3];
    const char* seps[3];

    parts[0] = library_name;   seps[0] = "";
    parts[1] = primary_name;   seps[1] = ".";
    parts[2] = secondary_name; seps[2] = "(";

    for (i = 0; i < 3; ++i) {
        size_t sl, pl;
        if (parts[i] == NULL || parts[i][0] == '\0') {
            continue;
        }
        sl = (n == 0) ? 0 : strlen(seps[i]);
        pl = strlen(parts[i]);
        if (n + sl + pl + 2 >= sizeof(buf)) {
            break;
        }
        memcpy(buf + n, seps[i], sl);
        n += sl;
        memcpy(buf + n, parts[i], pl);
        n += pl;
        if (i == 2) {
            buf[n++] = ')';
        }
    }
    buf[n] = '\0';
    return buf;
}
