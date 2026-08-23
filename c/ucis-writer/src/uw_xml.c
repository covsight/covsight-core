/* uw_xml.c - UCIS construct to XSD element dispatch.
 * SPDX-License-Identifier: Apache-2.0 */

#include "uw_xml.h"
#include "uw_stack.h"
#include "uw_text.h"

#include <string.h>

static const char* uw_kind_tag(unsigned kind)
{
    switch (kind) {
        case UW_KIND_TOGGLE: return "toggleCoverage";
        case UW_KIND_BLOCK:  return "blockCoverage";
        case UW_KIND_COND:   return "conditionCoverage";
        case UW_KIND_BRANCH: return "branchCoverage";
        case UW_KIND_FSM:    return "fsmCoverage";
        case UW_KIND_ASSERT: return "assertionCoverage";
        case UW_KIND_CVG:    return "covergroupCoverage";
        default:             return "userAttr";
    }
}

UW_INTERNAL int uw_kind_open(uw_db_t* db, unsigned kind)
{
    const char* tag = uw_kind_tag(kind);

    if (db->inst_depth == 0) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "coverage created outside any instance", tag);
    }
    if (db->cur_kind == kind) {
        /* Same kind as the last item: keep the wrapper, discard any scope
         * the caller left open beneath it. */
        return uw_el_unwind(db, db->inst_depth + 1);
    }
    UW_TRY(uw_el_unwind(db, db->inst_depth));
    /* uw_stage now applies to the instance element, which is innermost. It is
     * what turns "you interleaved your coverage kinds" into a diagnosable
     * error instead of a document readers silently reject. */
    UW_TRY(uw_stage(db, kind, tag));
    db->cur_kind   = kind;
    db->block_mode = 0;
    return uw_el_begin(db, tag, 0);
}

UW_INTERNAL uint64_t uw_cover_count(uw_db_t* db, const ucisCoverDataT* data)
{
    if (data->flags & UCIS_IS_VECTOR) {
        /* BIN_CONTENTS/@coverageCount is a single nonNegativeInteger; a
         * vector bin has no scalar to put there. The data is not lost
         * silently -- it is counted. */
        db->warnings++;
        return 0u;
    }
    if (data->flags & UCIS_IS_64BIT) {
        return data->data.int64;
    }
    return (uint64_t)data->data.int32;
}

UW_INTERNAL int uw_emit_obj_attrs(uw_db_t* db, const char* alias,
                                  const ucisCoverDataT* data)
{
    uw_buf_t* b = &db->buf;

    /* STATEMENT and friends carry no @name, so the caller's UOR name goes to
     * @alias -- the only string slot objAttributes offers. */
    uw_text_attr(b, "alias", alias, &db->warnings);
    if (data != NULL) {
        if (data->flags & UCIS_EXCLUDED) {
            uw_text_attr(b, "excluded", "true", NULL);
        }
        if ((data->flags & UCIS_HAS_WEIGHT) && data->weight != 1) {
            uw_text_attr_i64(b, "weight", data->weight);
        }
    }
    return b->status;
}

UW_INTERNAL int uw_emit_bin(uw_db_t* db, const char* tag, const char* name,
                            const ucisCoverDataT* data)
{
    uw_buf_t* b = &db->buf;

    if (uw_el_begin(db, tag, 0) != UCIS_WRITER_OK) {
        return b->status;
    }
    if (data != NULL && (data->flags & UCIS_HAS_GOAL) && data->goal > 0) {
        uw_text_attr_i64(b, "coverageCountGoal", data->goal);
    }
    if (uw_el_begin(db, "contents", 0) != UCIS_WRITER_OK) {
        return b->status;
    }
    uw_text_attr(b, "nameComponent", name, &db->warnings);
    uw_text_attr_u64(b, "coverageCount",
                     data ? uw_cover_count(db, data) : 0u);
    if (uw_el_end(db) != UCIS_WRITER_OK) {   /* </contents> */
        return b->status;
    }
    return uw_el_end(db);                     /* </bin> */
}

UW_INTERNAL int uw_emit_statement(uw_db_t* db, const char* name,
                                  const ucisCoverDataT* data,
                                  const ucisSourceInfoT* srcinfo)
{
    uint32_t fileid = 0;
    uint32_t line   = 0;

    UW_TRY(uw_kind_open(db, UW_KIND_BLOCK));
    /* BLOCK_COVERAGE is an xsd:choice of process+ | block+ | statement+, so a
     * blockCoverage that has emitted statements cannot also emit blocks. */
    if (db->block_mode != 0 && db->block_mode != UCIS_STMTBIN) {
        return uw_fail(db, UCIS_WRITER_ERR_ORDER,
                       "blockCoverage already contains a different block form",
                       "statement");
    }
    db->block_mode = UCIS_STMTBIN;

    UW_TRY(uw_el_begin(db, "statement", 0));
    uw_emit_obj_attrs(db, name, data);

    if (srcinfo != NULL) {
        uw_file_t* f = (uw_file_t*)srcinfo->filehandle;
        if (f != NULL) {
            fileid = f->id;
        }
        line = srcinfo->line > 0 ? (uint32_t)srcinfo->line : 0u;
    }
    /* A statement bin without a line number has lost the thing that makes it
     * useful, so unlike an instance this clamp is worth a warning. */
    uw_el_set_id(db, fileid, line, 1u, 1);

    UW_TRY(uw_emit_bin(db, "bin", NULL, data));
    return uw_el_end(db);                     /* </statement> */
}

/* ---- condition and expression ----------------------------------------- */

UW_INTERNAL void uw_copy_bounded(uw_db_t* db, char* dst, size_t cap,
                                 const char* src)
{
    size_t n;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        db->warnings++;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

UW_INTERNAL int uw_open_expr(uw_db_t* db, const char* name,
                             const ucisSourceInfoT* srcinfo)
{
    uw_elem_t* top = uw_el_top(db);
    uint32_t   fileid = 0;
    uint32_t   line   = 0;

    /* A second expression scope inside an open one is an input-contribution
     * metric scope (spec 6.5.4.1). UCIS-XML has no element for that level --
     * EXPR requires bin+ directly -- so it becomes a virtual scope: it
     * balances against ucis_WriteStreamScope but emits nothing, and its name
     * rides along on each bin as @typeComponent. See D11. */
    if (top != NULL && (top->type == UCIS_EXPR || top->type == UCIS_COND)) {
        uw_copy_bounded(db, db->metric, sizeof(db->metric), name);
        return uw_el_begin_virtual(db, top->type);
    }

    UW_TRY(uw_kind_open(db, UW_KIND_COND));
    UW_TRY(uw_el_begin(db, "expr", UCIS_EXPR));

    uw_text_attr(&db->buf, "name", name, &db->warnings);
    uw_text_attr(&db->buf, "key", name, &db->warnings);

    /* Default the operand list to the scope name; UCIS_STR_EXPR_TERMS
     * overwrites it if the caller supplies one. subExpr is minOccurs="1", so
     * there always has to be something. */
    uw_copy_bounded(db, db->expr_terms, sizeof(db->expr_terms), name);
    db->expr_terms_explicit = 0;
    db->metric[0] = '\0';

    {
        uw_elem_t* el = uw_el_top(db);
        el->needs_expr = 1;
        el->expr_index = ++db->expr_count;
    }
    if (srcinfo != NULL) {
        uw_file_t* f = (uw_file_t*)srcinfo->filehandle;
        if (f != NULL) {
            fileid = f->id;
        }
        line = srcinfo->line > 0 ? (uint32_t)srcinfo->line : 0u;
    }
    uw_el_set_id(db, fileid, line, 1u, 1);
    return db->buf.status;
}

UW_INTERNAL int uw_emit_expr_bin(uw_db_t* db, const char* name,
                                 const ucisCoverDataT* data)
{
    uw_elem_t* top = uw_el_top(db);

    if (top == NULL || (top->type != UCIS_EXPR && top->type != UCIS_COND)) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "expression bin outside an expression scope", name);
    }
    /* Bins created in the metric scope belong to the expression that encloses
     * it, which uw_el_begin_virtual has left as the innermost real element. */
    UW_TRY(uw_el_begin(db, "bin", 0));
    if (data != NULL && (data->flags & UCIS_HAS_GOAL) && data->goal > 0) {
        uw_text_attr_i64(&db->buf, "coverageCountGoal", data->goal);
    }
    UW_TRY(uw_el_begin(db, "contents", 0));
    uw_text_attr(&db->buf, "nameComponent", name, &db->warnings);
    if (db->metric[0] != '\0') {
        /* The metric level is gone as structure; this is where it survives. */
        uw_text_attr(&db->buf, "typeComponent", db->metric, &db->warnings);
    }
    uw_text_attr_u64(&db->buf, "coverageCount",
                     data ? uw_cover_count(db, data) : 0u);
    UW_TRY(uw_el_end(db));
    return uw_el_end(db);
}

/* ---- branch ----------------------------------------------------------- */

UW_INTERNAL int uw_open_branch_statement(uw_db_t* db, const char* name,
                                         const ucisSourceInfoT* srcinfo)
{
    uint32_t fileid = 0;
    uint32_t line   = 0;

    UW_TRY(uw_kind_open(db, UW_KIND_BRANCH));
    UW_TRY(uw_el_begin(db, "statement", UCIS_BRANCH));
    /* @statementType is required and the API has no argument for it; "if" is
     * the overwhelmingly common case and UCIS_INT_BRANCH_ISCASE overrides it
     * while the start tag is still pending. */
    uw_text_attr(&db->buf, "statementType", "if", NULL);
    uw_emit_obj_attrs(db, name, NULL);

    if (srcinfo != NULL) {
        uw_file_t* f = (uw_file_t*)srcinfo->filehandle;
        if (f != NULL) {
            fileid = f->id;
        }
        line = srcinfo->line > 0 ? (uint32_t)srcinfo->line : 0u;
    }
    uw_el_set_id(db, fileid, line, 1u, 1);
    return db->buf.status;
}

UW_INTERNAL int uw_emit_branch(uw_db_t* db, const char* name,
                               const ucisCoverDataT* data,
                               const ucisSourceInfoT* srcinfo)
{
    uw_elem_t* top = uw_el_top(db);
    uint32_t   fileid = 0;
    uint32_t   line   = 0;

    if (top == NULL || top->type != UCIS_BRANCH) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "branch bin outside a branch scope", name);
    }
    /* BRANCH carries no attributes at all, so the arm's name ("if", "else",
     * a case label) has only one place to go: the bin's nameComponent. */
    UW_TRY(uw_el_begin(db, "branch", 0));
    if (srcinfo != NULL) {
        uw_file_t* f = (uw_file_t*)srcinfo->filehandle;
        if (f != NULL) {
            fileid = f->id;
        }
        line = srcinfo->line > 0 ? (uint32_t)srcinfo->line : 0u;
    }
    if (fileid == 0 || line == 0) {
        /* Fall back to the enclosing statement's location rather than to 1:1,
         * which would point at the wrong file entirely. */
        fileid = top->id_file ? top->id_file : fileid;
        line   = top->id_line ? top->id_line : line;
    }
    uw_el_set_id(db, fileid, line, 1u, 0);

    UW_TRY(uw_emit_bin(db, "branchBin", name, data));
    return uw_el_end(db);                     /* </branch> */
}

/* ---- toggle ----------------------------------------------------------- */

static const char* uw_toggle_type_name(ucisToggleTypeT t)
{
    switch (t) {
        case UCIS_TOGGLE_TYPE_NET: return "net";
        case UCIS_TOGGLE_TYPE_REG: return "reg";
        default:                   return NULL;
    }
}

static const char* uw_toggle_dir_name(ucisToggleDirT d)
{
    switch (d) {
        case UCIS_TOGGLE_DIR_INTERNAL: return "internal";
        case UCIS_TOGGLE_DIR_IN:       return "in";
        case UCIS_TOGGLE_DIR_OUT:      return "out";
        case UCIS_TOGGLE_DIR_INOUT:    return "inout";
        default:                       return NULL;
    }
}

/* True if `s` is all decimal digits, in which case it is a bit index and
 * TOGGLE_BIT can carry an <index> element as well as a name. */
static int uw_all_digits(const char* s, uint64_t* out)
{
    uint64_t v = 0;
    size_t   i;
    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    for (i = 0; s[i] != '\0'; ++i) {
        if (s[i] < '0' || s[i] > '9' || i >= 19) {
            return 0;
        }
        v = v * 10u + (uint64_t)(s[i] - '0');
    }
    *out = v;
    return 1;
}

static int uw_open_toggle_bit(uw_db_t* db, const char* name)
{
    uint64_t index;

    UW_TRY(uw_el_begin(db, "toggleBit", UCIS_TOGGLE));
    uw_text_attr(&db->buf, "name", name, &db->warnings);
    uw_text_attr(&db->buf, "key", name, &db->warnings);

    if (uw_all_digits(name, &index)) {
        UW_TRY(uw_el_begin(db, "index", 0));
        uw_el_commit(db);
        uw_buf_u64(&db->buf, index);
        return uw_el_end(db);
    }
    return db->buf.status;
}

UW_INTERNAL int uw_open_toggle(uw_db_t* db, const char* name,
                               const char* canonical_name,
                               ucisToggleTypeT type, ucisToggleDirT dir)
{
    uw_elem_t* top = uw_el_top(db);

    /* UCIS nests a toggle scope per bit inside the object's toggle scope
     * (spec 6.7: "/4:top/0:w/0:1"). A toggle scope opened while one is already
     * open is therefore a bit, not another object. */
    if (top != NULL && top->type == UCIS_TOGGLE &&
        strcmp(top->tag, "toggleObject") == 0) {
        return uw_open_toggle_bit(db, name);
    }

    UW_TRY(uw_kind_open(db, UW_KIND_TOGGLE));
    UW_TRY(uw_el_begin(db, "toggleObject", UCIS_TOGGLE));
    uw_text_attr(&db->buf, "name", name, &db->warnings);
    uw_text_attr(&db->buf, "key", canonical_name ? canonical_name : name,
                 &db->warnings);
    uw_text_attr(&db->buf, "type", uw_toggle_type_name(type), NULL);
    uw_text_attr(&db->buf, "portDirection", uw_toggle_dir_name(dir), NULL);

    /* TOGGLE_OBJECT requires an <id>, but ucis_CreateToggle takes no source
     * info, so there is nothing to put in it. Not a warning: the API simply
     * does not carry the data. */
    uw_el_set_id(db, 0, 0, 1u, 0);
    return db->buf.status;
}

UW_INTERNAL int uw_emit_toggle_bin(uw_db_t* db, const char* name,
                                   const ucisCoverDataT* data)
{
    uw_elem_t*  top = uw_el_top(db);
    const char* arrow;
    const char* from;
    size_t      fromlen;
    const char* to;

    if (top == NULL || top->type != UCIS_TOGGLE) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "toggle bin outside a toggle scope", name);
    }
    if (strcmp(top->tag, "toggleObject") == 0) {
        /* Bins arriving straight into the object mean a scalar: TOGGLE_OBJECT
         * still requires toggleBit+, so synthesise the one bit. Its name is
         * the index within the object; the signal's name is on the object. */
        UW_TRY(uw_open_toggle_bit(db, "0"));
        top = uw_el_top(db);
        top->owned = 1;    /* ours, so ucis_WriteStreamScope closes through it */
    }

    /* Bin names are "0->1", "1 -> 0" and so on (spec 6.7.1). TOGGLE requires
     * both @from and @to, so a name that is not a transition -- an enum toggle
     * names its bins after the values -- becomes an arrival at a state from an
     * unstated one. The name survives verbatim in nameComponent either way. */
    arrow = NULL;
    if (name != NULL) {
        const char* p;
        for (p = name; p[0] != '\0' && p[1] != '\0'; ++p) {
            if (p[0] == '-' && p[1] == '>') {
                arrow = p;
                break;
            }
        }
    }
    if (arrow != NULL) {
        from    = name;
        fromlen = (size_t)(arrow - name);
        while (fromlen > 0 && from[fromlen - 1] == ' ') {
            fromlen--;
        }
        to = arrow + 2;
        while (*to == ' ') {
            to++;
        }
    } else {
        from    = "";
        fromlen = 0;
        to      = name ? name : "";
    }

    UW_TRY(uw_el_begin(db, "toggle", 0));
    uw_text_attr_n(&db->buf, "from", from, fromlen, &db->warnings);
    uw_text_attr(&db->buf, "to", to, &db->warnings);

    UW_TRY(uw_emit_bin(db, "bin", name, data));
    return uw_el_end(db);                     /* </toggle> */
}
