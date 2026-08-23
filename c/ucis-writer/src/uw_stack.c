/* uw_stack.c - XML element stack and the ordering stage machine.
 * SPDX-License-Identifier: Apache-2.0 */

#include "uw_stack.h"
#include "uw_cvg.h"
#include "uw_text.h"

static const char uw_indent_spaces[] =
    "                                                                ";
#define UW_INDENT_MAX (sizeof(uw_indent_spaces) - 1)

/* Indentation counts elements, not stack entries: virtual scopes take up a
 * stack slot but produce no markup to indent under. */
static int uw_real_depth(uw_db_t* db, int depth)
{
    int n = 0;
    int i;
    for (i = 0; i < depth && i < db->depth; ++i) {
        if (db->stack[i].tag != NULL) {
            n++;
        }
    }
    return n;
}

static int uw_newline(uw_db_t* db, int depth)
{
    size_t want;
    if (!db->pretty) {
        return db->buf.status;
    }
    if (uw_buf_putc(&db->buf, '\n') != UCIS_WRITER_OK) {
        return db->buf.status;
    }
    depth = uw_real_depth(db, depth);
    want = (size_t)(depth > 0 ? depth : 0) * 2u;
    if (want > UW_INDENT_MAX) {
        want = UW_INDENT_MAX;
    }
    return uw_buf_write(&db->buf, uw_indent_spaces, want);
}

UW_INTERNAL uw_elem_t* uw_el_top(uw_db_t* db)
{
    return db->depth > 0 ? &db->stack[db->depth - 1] : NULL;
}

UW_INTERNAL int uw_el_commit(uw_db_t* db)
{
    uw_elem_t* top = uw_el_top(db);
    if (top == NULL || !top->pending) {
        return db->buf.status;
    }
    top->pending = 0;
    if (top->needs_expr) {
        /* Written last among the attributes because UCIS_STR_EXPR_TERMS may
         * have arrived at any point since the scope was created. */
        uw_text_attr(&db->buf, "exprString", db->expr_terms, &db->warnings);
        uw_text_attr_u64(&db->buf, "index", top->expr_index);
        uw_text_attr_u64(&db->buf, "width", uw_expr_term_count(db));
    }
    if (uw_buf_putc(&db->buf, '>') != UCIS_WRITER_OK) {
        return db->buf.status;
    }
    /* Deferred children, in the order their content models require. Each flag
     * is cleared before its emitter runs, because the emitter opens elements
     * and so re-enters this function. */
    if (top->needs_options) {
        unsigned variant = top->needs_options;
        top->needs_options = UW_OPT_NONE;
        top->haskids       = 1;
        if (uw_emit_options(db, variant) != UCIS_WRITER_OK) {
            return db->buf.status;
        }
    }
    if (top->needs_cgid) {
        top->needs_cgid = 0;
        top->haskids    = 1;
        if (uw_emit_cgid(db, top) != UCIS_WRITER_OK) {
            return db->buf.status;
        }
    }
    if (top->needs_id) {
        top->needs_id = 0;
        top->haskids  = 1;
        if (uw_newline(db, db->depth) != UCIS_WRITER_OK) {
            return db->buf.status;
        }
        UW_LIT(&db->buf, "<id file=\"");
        uw_buf_u32(&db->buf, top->id_file);
        UW_LIT(&db->buf, "\" line=\"");
        uw_buf_u32(&db->buf, top->id_line);
        UW_LIT(&db->buf, "\" inlineCount=\"");
        uw_buf_u32(&db->buf, top->id_inline);
        UW_LIT(&db->buf, "\"/>");
    }
    if (top->needs_expr) {
        top->needs_expr = 0;
        top->haskids    = 1;
        uw_emit_sub_exprs(db);
    }
    return db->buf.status;
}

UW_INTERNAL uint64_t uw_expr_term_count(uw_db_t* db)
{
    uint64_t    n = 0;
    const char* p = db->expr_terms;

    if (!db->expr_terms_explicit) {
        return 1u;      /* the scope name is one opaque term, not a list */
    }
    while (*p != '\0') {
        while (*p == '#') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        n++;
        while (*p != '\0' && *p != '#') {
            p++;
        }
    }
    return n ? n : 1u;   /* subExpr+ means never zero */
}

/* EXPR requires subExpr+ before its bins. The operand list arrives as one
 * '#'-separated string (spec 6.5.4.1) and becomes one element per operand. */
UW_INTERNAL int uw_emit_sub_exprs(uw_db_t* db)
{
    const char* p = db->expr_terms;
    int         any = 0;

    if (!db->expr_terms_explicit) {
        UW_TRY(uw_el_begin(db, "subExpr", 0));
        uw_el_commit(db);
        uw_text_escape_cstr(&db->buf, db->expr_terms, &db->warnings);
        return uw_el_end(db);
    }
    while (*p != '\0') {
        const char* start;
        while (*p == '#') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        start = p;
        while (*p != '\0' && *p != '#') {
            p++;
        }
        UW_TRY(uw_el_begin(db, "subExpr", 0));
        uw_el_commit(db);
        uw_text_escape(&db->buf, start, (size_t)(p - start), &db->warnings);
        UW_TRY(uw_el_end(db));
        any = 1;
    }
    if (!any) {
        UW_TRY(uw_el_begin(db, "subExpr", 0));
        UW_TRY(uw_el_end(db));
    }
    return db->buf.status;
}

UW_INTERNAL void uw_el_set_id(uw_db_t* db, uint32_t file, uint32_t line,
                              uint32_t inlinecount, int warn_on_clamp)
{
    uw_elem_t* top = uw_el_top(db);
    if (top == NULL) {
        return;
    }
    /* All three are xsd:positiveInteger, so zero is not representable and is
     * clamped to 1. Whether that deserves a warning depends on the caller:
     * "I have no source information for this instance" is ordinary, while a
     * statement bin whose line number came out as 0 is a real loss. */
    if (warn_on_clamp && (file == 0 || line == 0 || inlinecount == 0)) {
        db->warnings++;
    }
    top->needs_id  = 1;
    top->id_file   = file ? file : 1u;
    top->id_line   = line ? line : 1u;
    top->id_inline = inlinecount ? inlinecount : 1u;
}

UW_INTERNAL int uw_el_begin(uw_db_t* db, const char* tag, ucisScopeTypeT type)
{
    uw_elem_t* top = uw_el_top(db);
    uw_elem_t* el;

    if (db->depth >= UCIS_WRITER_MAX_DEPTH) {
        return uw_fail(db, UCIS_WRITER_ERR_DEPTH, "scope nesting too deep", tag);
    }
    if (uw_el_commit(db) != UCIS_WRITER_OK) {
        return db->buf.status;
    }
    /* Virtual scopes are transparent: the element that will actually contain
     * this child is the nearest real ancestor. */
    {
        int i;
        for (i = db->depth - 1; i >= 0; --i) {
            db->stack[i].haskids = 1;
            if (db->stack[i].tag != NULL) {
                break;
            }
        }
    }
    (void)top;
    if (uw_newline(db, db->depth) != UCIS_WRITER_OK) {
        return db->buf.status;
    }
    if (uw_buf_putc(&db->buf, '<') != UCIS_WRITER_OK) {
        return db->buf.status;
    }
    if (uw_buf_puts(&db->buf, tag) != UCIS_WRITER_OK) {
        return db->buf.status;
    }

    el = &db->stack[db->depth++];
    el->tag       = tag;
    el->type      = type;
    el->stage     = 0;
    el->pending   = 1;
    el->haskids   = 0;
    el->owned     = (type == 0);   /* only caller scopes carry a UCIS type */
    el->needs_id  = 0;
    el->needs_expr = 0;
    el->expr_index = 0;
    el->needs_options = UW_OPT_NONE;
    el->needs_cgid    = 0;
    el->id_file   = 0;
    el->id_line   = 0;
    el->id_inline = 0;
    return db->buf.status;
}

UW_INTERNAL int uw_el_begin_virtual(uw_db_t* db, ucisScopeTypeT type)
{
    uw_elem_t* el;

    if (db->depth >= UCIS_WRITER_MAX_DEPTH) {
        return uw_fail(db, UCIS_WRITER_ERR_DEPTH, "scope nesting too deep",
                       "metric scope");
    }
    UW_TRY(uw_el_commit(db));

    el = &db->stack[db->depth++];
    el->tag        = NULL;      /* what makes it virtual */
    el->type       = type;
    el->stage      = 0;
    el->pending    = 0;
    el->haskids    = 0;
    el->owned      = 0;         /* the caller opened it and will close it */
    el->needs_id   = 0;
    el->needs_expr = 0;
    el->expr_index = 0;
    el->needs_options = UW_OPT_NONE;
    el->needs_cgid    = 0;
    el->id_file    = 0;
    el->id_line    = 0;
    el->id_inline  = 0;
    return db->buf.status;
}

UW_INTERNAL int uw_el_end(uw_db_t* db)
{
    uw_elem_t* el;

    if (db->depth <= 0) {
        return uw_fail(db, UCIS_WRITER_ERR_UNBALANCED,
                       "no scope is open", NULL);
    }
    el = &db->stack[db->depth - 1];
    if (el->needs_id || el->needs_options || el->needs_cgid) {
        /* Required children cannot be dropped just because nothing else was
         * written into the scope. An empty coverpoint still owes an <options>,
         * and an <id> is required whether or not the scope has bins. */
        if (uw_el_commit(db) != UCIS_WRITER_OK) {
            return db->buf.status;
        }
    }
    el = &db->stack[--db->depth];
    if (el->tag == NULL) {
        /* Virtual: nothing was emitted, so there is nothing to close. The
         * metric name dies with it. */
        db->metric[0] = '\0';
        return db->buf.status;
    }
    if (el->type == UCIS_INSTANCE) {
        /* The in-instance ordering state dies with the instance. */
        db->inst_depth = 0;
        db->cur_kind   = 0;
        db->block_mode = 0;
    }
    if (el->type == UCIS_COVERINSTANCE) {
        /* The bin-ordinal table is the one thing this library holds that grows
         * with content, and it exists only to index this instance's crosses.
         * It must not outlive the instance that justified it. */
        uw_cvptab_reset(&db->cvpbins);
    }

    if (el->pending) {
        /* Never acquired children: collapse to an empty element. This is what
         * keeps documents made mostly of leaf bins from paying for close tags. */
        return uw_buf_write(&db->buf, "/>", 2);
    }
    if (el->haskids && uw_newline(db, db->depth) != UCIS_WRITER_OK) {
        return db->buf.status;
    }
    if (uw_buf_write(&db->buf, "</", 2) != UCIS_WRITER_OK) {
        return db->buf.status;
    }
    if (uw_buf_puts(&db->buf, el->tag) != UCIS_WRITER_OK) {
        return db->buf.status;
    }
    return uw_buf_putc(&db->buf, '>');
}

UW_INTERNAL int uw_el_unwind(uw_db_t* db, int depth)
{
    while (db->depth > depth) {
        if (uw_el_end(db) != UCIS_WRITER_OK) {
            return db->buf.status;
        }
    }
    return db->buf.status;
}

UW_INTERNAL int uw_stage(uw_db_t* db, unsigned stage, const char* what)
{
    uw_elem_t* top = uw_el_top(db);
    if (top == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE, "no scope is open", what);
    }
    if (stage < top->stage) {
        return uw_fail(db, UCIS_WRITER_ERR_ORDER,
                       "out of order within the enclosing scope", what);
    }
    top->stage = stage;
    return UCIS_WRITER_OK;
}
