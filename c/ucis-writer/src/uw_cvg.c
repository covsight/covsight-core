/* uw_cvg.c - functional coverage: covergroups, coverpoints, crosses.
 * SPDX-License-Identifier: Apache-2.0 */

#include "uw_cvg.h"
#include "uw_stack.h"
#include "uw_text.h"
#include "uw_xml.h"

#include <stdlib.h>
#include <string.h>

/* ---- option staging --------------------------------------------------- */

UW_INTERNAL void uw_opts_reset(uw_db_t* db)
{
    memset(&db->opts, 0, sizeof(db->opts));
}

UW_INTERNAL int uw_opts_set(uw_db_t* db, unsigned variant,
                            ucisIntPropertyEnumT property, int value)
{
    uw_opts_t* o = &db->opts;

    /* Common to all three option types. */
    switch (property) {
        case UCIS_INT_SCOPE_WEIGHT:
            o->weight = value; o->set |= UW_OPTSET_WEIGHT; return 1;
        case UCIS_INT_SCOPE_GOAL:
            o->goal = value; o->set |= UW_OPTSET_GOAL; return 1;
        case UCIS_INT_CVG_ATLEAST:
            o->at_least = value; o->set |= UW_OPTSET_AT_LEAST; return 1;
        default: break;
    }
    if (variant == UW_OPT_CGINST || variant == UW_OPT_COVERPOINT) {
        switch (property) {
            case UCIS_INT_CVG_AUTOBINMAX:
                o->auto_bin_max = value; o->set |= UW_OPTSET_AUTO_BIN_MAX; return 1;
            case UCIS_INT_CVG_DETECTOVERLAP:
                o->detect_overlap = value; o->set |= UW_OPTSET_DETECT_OVERLAP; return 1;
            default: break;
        }
    }
    if (variant == UW_OPT_CGINST || variant == UW_OPT_CROSS) {
        if (property == UCIS_INT_CVG_NUMPRINTMISSING) {
            o->num_print_missing = value; o->set |= UW_OPTSET_PRINT_MISSING;
            return 1;
        }
    }
    if (variant == UW_OPT_CGINST) {
        switch (property) {
            case UCIS_INT_CVG_PERINSTANCE:
                o->per_instance = value; o->set |= UW_OPTSET_PER_INSTANCE; return 1;
            case UCIS_INT_CVG_MERGEINSTANCES:
                o->merge_instances = value; o->set |= UW_OPTSET_MERGE_INST; return 1;
            default: break;
        }
    }
    return 0;
}

/* xsd:nonNegativeInteger: a negative weight or goal is not representable, and
 * clamping it silently would turn a caller's sign error into a plausible
 * number. Clamp and count. */
static void uw_opt_attr_nn(uw_db_t* db, const char* name, int32_t value)
{
    if (value < 0) {
        db->warnings++;
        value = 0;
    }
    uw_text_attr_u64(&db->buf, name, (uint64_t)value);
}

UW_INTERNAL int uw_emit_options(uw_db_t* db, unsigned variant)
{
    uw_opts_t* o = &db->opts;
    unsigned   i;

    UW_TRY(uw_el_begin(db, "options", 0));
    if (o->set & UW_OPTSET_WEIGHT) {
        uw_opt_attr_nn(db, "weight", o->weight);
    }
    if (o->set & UW_OPTSET_GOAL) {
        uw_opt_attr_nn(db, "goal", o->goal);
    }
    if (o->set & UW_OPTSET_AT_LEAST) {
        uw_opt_attr_nn(db, "at_least", o->at_least);
    }
    if ((o->set & UW_OPTSET_DETECT_OVERLAP) && variant != UW_OPT_CROSS) {
        uw_text_attr(&db->buf, "detect_overlap",
                     o->detect_overlap ? "true" : "false", NULL);
    }
    if ((o->set & UW_OPTSET_AUTO_BIN_MAX) && variant != UW_OPT_CROSS) {
        uw_opt_attr_nn(db, "auto_bin_max", o->auto_bin_max);
    }
    if ((o->set & UW_OPTSET_PRINT_MISSING) && variant != UW_OPT_COVERPOINT) {
        uw_opt_attr_nn(db, "cross_num_print_missing", o->num_print_missing);
    }
    if (variant == UW_OPT_CGINST) {
        if (o->set & UW_OPTSET_PER_INSTANCE) {
            uw_text_attr(&db->buf, "per_instance",
                         o->per_instance ? "true" : "false", NULL);
        }
        if (o->set & UW_OPTSET_MERGE_INST) {
            uw_text_attr(&db->buf, "merge_instances",
                         o->merge_instances ? "true" : "false", NULL);
        }
    }
    UW_TRY(uw_el_end(db));

    if (variant != UW_OPT_CROSS) {
        return db->buf.status;
    }
    /* CROSS puts crossExpr* directly after options, and the crossed coverpoint
     * names arrived as properties on the pending element, so they go out with
     * the options rather than waiting for a child that may never come. */
    for (i = 0; i < db->cross_n; ++i) {
        UW_TRY(uw_el_begin(db, "crossExpr", 0));
        UW_TRY(uw_el_commit(db));
        uw_text_escape_cstr(&db->buf, db->cross_names[i], &db->warnings);
        UW_TRY(uw_el_end(db));
    }
    return db->buf.status;
}

/* ---- cgId ------------------------------------------------------------- */

static int uw_emit_stmt_id(uw_db_t* db, const char* tag, uint32_t file,
                           uint32_t line)
{
    UW_TRY(uw_el_begin(db, tag, 0));
    /* All three components are xsd:positiveInteger; the clamp is the same one
     * uw_el_set_id applies, and for a covergroup it is routine -- the C API
     * carries no source info for a covergroup created by name alone. */
    uw_text_attr_u64(&db->buf, "file", file ? file : 1u);
    uw_text_attr_u64(&db->buf, "line", line ? line : 1u);
    uw_text_attr_u64(&db->buf, "inlineCount", 1u);
    return uw_el_end(db);
}

UW_INTERNAL int uw_emit_cgid(uw_db_t* db, uw_elem_t* el)
{
    UW_TRY(uw_el_begin(db, "cgId", 0));
    /* Both required. @cgName is where the covergroup type's identity lands --
     * it is the only trace of the type scope in the document. */
    uw_text_attr(&db->buf, "cgName",
                 db->cg_name[0] ? db->cg_name : "(unnamed)", &db->warnings);
    uw_text_attr(&db->buf, "moduleName",
                 db->cg_module[0] ? db->cg_module : "(unknown)", &db->warnings);
    UW_TRY(uw_emit_stmt_id(db, "cginstSourceId", el->id_file, el->id_line));
    UW_TRY(uw_emit_stmt_id(db, "cgSourceId", db->cg_file, db->cg_line));
    return uw_el_end(db);
}

/* ---- coverpoint bin ordinals ------------------------------------------ */

UW_INTERNAL void uw_cvptab_reset(uw_cvptab_t* t)
{
    free(t->slots);
    t->slots     = NULL;
    t->cap       = 0;
    t->used      = 0;
    t->cvp_count = 0;
    t->bin_count = 0;
}

/* Coverpoint names live in the same table as bin names, distinguished by an
 * ordinal no coverpoint can have. */
#define UW_CVP_NAMESPACE 0xFFFFFFFFu

static uint64_t uw_cvp_hash(uint32_t cvp, const char* name)
{
    uint64_t h = 1469598103934665603ULL;   /* FNV-1a 64 */
    int      i;
    for (i = 0; i < 4; ++i) {
        h ^= (uint64_t)((cvp >> (i * 8)) & 0xFFu);
        h *= 1099511628211ULL;
    }
    if (name != NULL) {
        while (*name != '\0') {
            h ^= (uint64_t)(unsigned char)*name++;
            h *= 1099511628211ULL;
        }
    }
    return h ? h : 1u;   /* 0 marks an empty slot */
}

static int uw_cvptab_grow(uw_db_t* db, uw_cvptab_t* t)
{
    size_t        newcap = t->cap ? t->cap * 2u : 64u;
    uw_cvpslot_t* ns     = (uw_cvpslot_t*)calloc(newcap, sizeof(uw_cvpslot_t));
    size_t        i;

    if (ns == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_ALLOC, "coverpoint bin table", NULL);
    }
    for (i = 0; i < t->cap; ++i) {
        size_t j;
        if (t->slots[i].hash == 0) {
            continue;
        }
        j = (size_t)t->slots[i].hash & (newcap - 1u);
        while (ns[j].hash != 0) {
            j = (j + 1u) & (newcap - 1u);
        }
        ns[j] = t->slots[i];
    }
    free(t->slots);
    t->slots = ns;
    t->cap   = newcap;
    return UCIS_WRITER_OK;
}

/* First writer wins: a coverpoint with two bins of the same name has already
 * lost the ability to distinguish them, and the earlier index is the one a
 * reader would guess. */
static int uw_cvptab_put(uw_db_t* db, uint32_t cvp, const char* name,
                         uint32_t index)
{
    uw_cvptab_t* t = &db->cvpbins;
    uint64_t     h;
    size_t       j;

    if (t->used * 4u >= t->cap * 3u) {
        UW_TRY(uw_cvptab_grow(db, t));
    }
    h = uw_cvp_hash(cvp, name);
    j = (size_t)h & (t->cap - 1u);
    while (t->slots[j].hash != 0) {
        if (t->slots[j].hash == h) {
            return UCIS_WRITER_OK;
        }
        j = (j + 1u) & (t->cap - 1u);
    }
    t->slots[j].hash  = h;
    t->slots[j].index = index;
    t->used++;
    return UCIS_WRITER_OK;
}

/* -1 when the name was never recorded. */
static int64_t uw_cvptab_get(uw_db_t* db, uint32_t cvp, const char* name,
                             size_t len)
{
    uw_cvptab_t* t = &db->cvpbins;
    char         tmp[128];
    uint64_t     h;
    size_t       j;

    if (t->cap == 0) {
        return -1;
    }
    if (len >= sizeof(tmp)) {
        return -1;
    }
    memcpy(tmp, name, len);
    tmp[len] = '\0';

    h = uw_cvp_hash(cvp, tmp);
    j = (size_t)h & (t->cap - 1u);
    while (t->slots[j].hash != 0) {
        if (t->slots[j].hash == h) {
            return (int64_t)t->slots[j].index;
        }
        j = (j + 1u) & (t->cap - 1u);
    }
    return -1;
}

/* ---- scopes ----------------------------------------------------------- */

UW_INTERNAL int uw_open_covergroup(uw_db_t* db, const char* name,
                                   const ucisSourceInfoT* srcinfo)
{
    UW_TRY(uw_kind_open(db, UW_KIND_CVG));

    uw_copy_bounded(db, db->cg_name, sizeof(db->cg_name), name);
    db->cg_file = 0;
    db->cg_line = 0;
    if (srcinfo != NULL) {
        uw_file_t* f = (uw_file_t*)srcinfo->filehandle;
        if (f != NULL) {
            db->cg_file = f->id;
        }
        db->cg_line = srcinfo->line > 0 ? (uint32_t)srcinfo->line : 0u;
    }
    /* The type scope itself emits nothing: UCIS-XML has no element for it, and
     * its name and location reappear inside each cgInstance's cgId. */
    return uw_el_begin_virtual(db, UCIS_COVERGROUP);
}

static int uw_open_cgi(uw_db_t* db, const char* name,
                       const ucisSourceInfoT* srcinfo, int owned)
{
    uw_elem_t* el;

    UW_TRY(uw_el_begin(db, "cgInstance", UCIS_COVERINSTANCE));
    uw_text_attr(&db->buf, "name", name ? name : "(unnamed)", &db->warnings);
    uw_text_attr(&db->buf, "key", name ? name : "(unnamed)", &db->warnings);

    el = uw_el_top(db);
    el->owned         = (unsigned char)(owned ? 1 : 0);
    el->needs_options = UW_OPT_CGINST;
    el->needs_cgid    = 1;
    /* Not needs_id: cgInstance has no <id> child. These carry cginstSourceId,
     * which uw_emit_cgid writes inside the cgId. */
    if (srcinfo != NULL) {
        uw_file_t* f = (uw_file_t*)srcinfo->filehandle;
        el->id_file = f != NULL ? f->id : 0u;
        el->id_line = srcinfo->line > 0 ? (uint32_t)srcinfo->line : 0u;
    } else {
        el->id_file = db->cg_file;
        el->id_line = db->cg_line;
    }
    uw_opts_reset(db);
    uw_cvptab_reset(&db->cvpbins);
    return db->buf.status;
}

UW_INTERNAL int uw_open_coverinstance(uw_db_t* db, const char* name,
                                      const ucisSourceInfoT* srcinfo)
{
    uw_elem_t* top = uw_el_top(db);

    /* Siblings: a second coverinstance closes the first. */
    if (top != NULL && top->type == UCIS_COVERINSTANCE) {
        UW_TRY(uw_el_end(db));
        top = uw_el_top(db);
    }
    if (top == NULL || top->type != UCIS_COVERGROUP) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "coverinstance outside a covergroup", name);
    }
    return uw_open_cgi(db, name, srcinfo, 0);
}

/* Make a cgInstance the innermost element, synthesising one for a covergroup
 * whose coverpoints hang off the type scope directly -- which is what
 * type-only coverage looks like, and the only shape UCIS-XML can express. */
static int uw_cgi_ensure(uw_db_t* db)
{
    uw_elem_t* top = uw_el_top(db);

    if (top != NULL && top->type == UCIS_COVERINSTANCE) {
        return UCIS_WRITER_OK;
    }
    /* A coverpoint or cross left open by the caller: close it and start the
     * next as a sibling, the same way a repeated coverage kind does. */
    if (top != NULL && (top->type == UCIS_COVERPOINT || top->type == UCIS_CROSS)) {
        UW_TRY(uw_el_end(db));
        top = uw_el_top(db);
        if (top != NULL && top->type == UCIS_COVERINSTANCE) {
            return UCIS_WRITER_OK;
        }
    }
    if (top != NULL && top->type == UCIS_COVERGROUP) {
        return uw_open_cgi(db, db->cg_name, NULL, 1);
    }
    return uw_fail(db, UCIS_WRITER_ERR_STATE,
                   "coverpoint or cross outside a covergroup", NULL);
}

UW_INTERNAL int uw_open_coverpoint(uw_db_t* db, const char* name)
{
    uw_elem_t* el;

    UW_TRY(uw_cgi_ensure(db));
    UW_TRY(uw_stage(db, UW_CGSTAGE_COVERPOINT, "coverpoint"));

    /* Record the coverpoint's ordinal before opening it, so that a cross can
     * resolve UCIS_STR_ITH_CROSSED_CVP_NAME to a position. */
    UW_TRY(uw_cvptab_put(db, UW_CVP_NAMESPACE, name ? name : "",
                         db->cvpbins.cvp_count));
    db->cvpbins.cvp_count++;
    db->cvpbins.bin_count = 0;

    UW_TRY(uw_el_begin(db, "coverpoint", UCIS_COVERPOINT));
    uw_text_attr(&db->buf, "name", name ? name : "(unnamed)", &db->warnings);
    uw_text_attr(&db->buf, "key", name ? name : "(unnamed)", &db->warnings);

    el = uw_el_top(db);
    el->needs_options = UW_OPT_COVERPOINT;
    uw_opts_reset(db);
    return db->buf.status;
}

UW_INTERNAL int uw_open_cross(uw_db_t* db, const char* name)
{
    uw_elem_t* el;

    UW_TRY(uw_cgi_ensure(db));
    UW_TRY(uw_stage(db, UW_CGSTAGE_CROSS, "cross"));

    UW_TRY(uw_el_begin(db, "cross", UCIS_CROSS));
    uw_text_attr(&db->buf, "name", name ? name : "(unnamed)", &db->warnings);
    uw_text_attr(&db->buf, "key", name ? name : "(unnamed)", &db->warnings);

    el = uw_el_top(db);
    el->needs_options = UW_OPT_CROSS;
    uw_opts_reset(db);
    db->cross_n = 0;
    return db->buf.status;
}

UW_INTERNAL int uw_cross_add_cvp(uw_db_t* db, const char* name)
{
    int64_t ord;

    if (db->cross_n >= UW_MAX_CROSSED) {
        db->warnings++;
        return UCIS_WRITER_OK;
    }
    ord = uw_cvptab_get(db, UW_CVP_NAMESPACE, name ? name : "",
                        name ? strlen(name) : 0u);
    if (ord < 0) {
        /* Crossing a coverpoint that has not been written yet. The schema
         * ordering makes that impossible to satisfy -- every coverpoint
         * precedes every cross -- so it is a caller error, not a gap we can
         * paper over. */
        return uw_fail(db, UCIS_WRITER_ERR_ORDER,
                       "cross names a coverpoint that was not written first",
                       name);
    }
    db->cross_cvp[db->cross_n] = (uint32_t)ord;
    uw_copy_bounded(db, db->cross_names[db->cross_n],
                    sizeof(db->cross_names[0]), name);
    db->cross_n++;
    return UCIS_WRITER_OK;
}

/* ---- bins ------------------------------------------------------------- */

static const char* uw_cvgbin_type(ucisCoverTypeT type)
{
    if (type & UCIS_IGNOREBIN)  return "ignore";
    if (type & UCIS_ILLEGALBIN) return "illegal";
    if (type & UCIS_DEFAULTBIN) return "default";
    return "bins";
}

/* Read a decimal integer, returning the first byte after it. */
static const char* uw_scan_int(const char* p, const char* end, int64_t* out,
                               int* ok)
{
    int64_t v   = 0;
    int     neg = 0;
    int     any = 0;

    if (p < end && (*p == '-' || *p == '+')) {
        neg = (*p == '-');
        p++;
    }
    while (p < end && *p >= '0' && *p <= '9') {
        if (v > (int64_t)1 << 55) {   /* far past anything a bin value holds */
            *ok = 0;
            return p;
        }
        v = v * 10 + (*p - '0');
        p++;
        any = 1;
    }
    *ok  = any;
    *out = neg ? -v : v;
    return p;
}

/* Recover the value range a bin covers from its name.
 *
 * Bin names carry the values in the forms tools actually produce: "7",
 * "[7]", "auto[7]", "3:9", "auto[3:9]". When one of those parses, the range
 * we emit is the real thing. When it does not -- a named bin like "small" --
 * there is no value to emit and range/@from and @to are required, so the bin's
 * ordinal within its coverpoint stands in and the caller is warned. Nothing is
 * lost: the true name is on @name and on contents/@nameComponent. See D13. */
static int uw_parse_bin_range(const char* name, int64_t* from, int64_t* to)
{
    const char* p;
    const char* end;
    const char* lb;
    int         ok = 0;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    end = name + strlen(name);
    lb  = strchr(name, '[');
    p   = lb != NULL ? lb + 1 : name;

    p = uw_scan_int(p, end, from, &ok);
    if (!ok) {
        return 0;
    }
    *to = *from;
    if (p < end && *p == ':') {
        p = uw_scan_int(p + 1, end, to, &ok);
        if (!ok) {
            return 0;
        }
    }
    /* Anything but the closing bracket (or end of string) means the name only
     * happened to start with digits; do not pretend it was a range. */
    if (p < end && *p == ']') {
        p++;
    }
    return p == end;
}

static int uw_emit_coverpoint_bin(uw_db_t* db, const char* name,
                                  const ucisCoverDataT* data)
{
    uint32_t index = db->cvpbins.bin_count;
    int64_t  from  = 0;
    int64_t  to    = 0;

    UW_TRY(uw_cvptab_put(db, db->cvpbins.cvp_count - 1u, name ? name : "",
                         index));
    db->cvpbins.bin_count++;

    UW_TRY(uw_el_begin(db, "coverpointBin", 0));
    uw_text_attr(&db->buf, "name", name ? name : "(unnamed)", &db->warnings);
    uw_text_attr(&db->buf, "key", name ? name : "(unnamed)", &db->warnings);
    uw_text_attr(&db->buf, "type", uw_cvgbin_type(data->type), NULL);
    if (data->flags & UCIS_EXCLUDED) {
        uw_text_attr(&db->buf, "alias", "excluded", NULL);
    }

    if (!uw_parse_bin_range(name, &from, &to)) {
        from = to = (int64_t)index;
        db->warnings++;
    }
    UW_TRY(uw_el_begin(db, "range", 0));
    uw_text_attr_i64(&db->buf, "from", from);
    uw_text_attr_i64(&db->buf, "to", to);
    UW_TRY(uw_el_begin(db, "contents", 0));
    uw_text_attr(&db->buf, "nameComponent", name, &db->warnings);
    uw_text_attr_u64(&db->buf, "coverageCount", uw_cover_count(db, data));
    UW_TRY(uw_el_end(db));                  /* </contents> */
    UW_TRY(uw_el_end(db));                  /* </range> */
    return uw_el_end(db);                   /* </coverpointBin> */
}

/* One <index> per crossed coverpoint, resolved from the corresponding
 * component of the bin's name. */
static int uw_emit_cross_indices(uw_db_t* db, const char* name)
{
    const char* p = name != NULL ? name : "";
    unsigned    i = 0;

    do {
        const char* start = p;
        int64_t     ord;
        uint32_t    cvp;

        while (*p != '\0' && *p != ',') {
            p++;
        }
        cvp = (i < db->cross_n) ? db->cross_cvp[i] : i;
        ord = uw_cvptab_get(db, cvp, start, (size_t)(p - start));
        if (ord < 0) {
            /* The component does not name a bin of that coverpoint, so the
             * position it refers to is genuinely unknown. -1 says so rather
             * than pointing at an unrelated bin. */
            ord = -1;
            db->warnings++;
        }
        UW_TRY(uw_el_begin(db, "index", 0));
        UW_TRY(uw_el_commit(db));
        uw_buf_i64(&db->buf, ord);
        UW_TRY(uw_el_end(db));
        i++;
        if (*p == ',') {
            p++;
        } else {
            break;
        }
    } while (i < UW_MAX_CROSSED);

    /* index+ is minOccurs="1"; the loop above always runs at least once. */
    return db->buf.status;
}

static int uw_emit_cross_bin(uw_db_t* db, const char* name,
                             const ucisCoverDataT* data)
{
    UW_TRY(uw_el_begin(db, "crossBin", 0));
    uw_text_attr(&db->buf, "name", name ? name : "(unnamed)", &db->warnings);
    uw_text_attr(&db->buf, "key", name ? name : "(unnamed)", &db->warnings);
    uw_text_attr(&db->buf, "type", uw_cvgbin_type(data->type), NULL);

    UW_TRY(uw_emit_cross_indices(db, name));
    UW_TRY(uw_el_begin(db, "contents", 0));
    uw_text_attr(&db->buf, "nameComponent", name, &db->warnings);
    uw_text_attr_u64(&db->buf, "coverageCount", uw_cover_count(db, data));
    UW_TRY(uw_el_end(db));                  /* </contents> */
    return uw_el_end(db);                   /* </crossBin> */
}

UW_INTERNAL int uw_emit_cvg_bin(uw_db_t* db, const char* name,
                                const ucisCoverDataT* data)
{
    uw_elem_t* top = uw_el_top(db);

    if (top == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "covergroup bin outside any scope", name);
    }
    if (top->type == UCIS_COVERPOINT) {
        return uw_emit_coverpoint_bin(db, name, data);
    }
    if (top->type == UCIS_CROSS) {
        return uw_emit_cross_bin(db, name, data);
    }
    return uw_fail(db, UCIS_WRITER_ERR_STATE,
                   "covergroup bin outside a coverpoint or cross", name);
}
