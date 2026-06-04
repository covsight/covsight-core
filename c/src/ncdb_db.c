#include <stdio.h>
#include <stdarg.h>

#include "ncdb_impl.h"
#include "ncdb_cross.h"
#include "ncdb_issues.h"

static int set_owned_str(char **slot, const char *v);

static void free_scope(ncdbScopeT scope) {
    size_t i;
    if (!scope) return;
    for (i = 0; i < scope->cover_count; i++) {
        ncdb_attr_table_free(&scope->covers[i]->attrs);
        ncdb_prop_table_free(&scope->covers[i]->props);
        ncdb_assoc_free(scope->covers[i]->assoc);
        free(scope->covers[i]->name);
        free(scope->covers[i]->source_path);
        free(scope->covers[i]->comment);
        free(scope->covers[i]);
    }
    for (i = 0; i < scope->child_count; i++) {
        free_scope(scope->children[i]);
    }
    ncdb_attr_table_free(&scope->attrs);
    ncdb_prop_table_free(&scope->props);
    free(scope->covers);
    free(scope->children);
    free(scope->crossed_points);  /* array only — pointed-to scopes are owned elsewhere */
    free(scope->name);
    free(scope->source_path);
    free(scope->du_signature);
    free(scope->expr_terms);
    free(scope->toggle_canonical);
    for (i = 0; i < scope->fsm_state_count; i++) free(scope->fsm_states[i].name);
    free(scope->fsm_states);
    for (i = 0; i < scope->tag_count; i++) free(scope->tags[i]);
    free(scope->tags);
    free(scope->du_files);
    free(scope);
}

static size_t count_scope_tree(ncdbScopeT scope) {
    size_t i;
    size_t count = 1;
    for (i = 0; i < scope->child_count; i++) count += count_scope_tree(scope->children[i]);
    return count;
}

static size_t count_cover_tree(ncdbScopeT scope) {
    size_t i;
    size_t count = scope->cover_count;
    for (i = 0; i < scope->child_count; i++) count += count_cover_tree(scope->children[i]);
    return count;
}

char *ncdb_impl_strdup(const char *s) {
    size_t len;
    char *ret;
    if (!s) return NULL;
    len = strlen(s);
    ret = (char *)malloc(len + 1U);
    if (!ret) return NULL;
    memcpy(ret, s, len + 1U);
    return ret;
}

void ncdb_impl_set_error(ncdbT db, const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    if (!db) return;
    free(db->last_error);
    db->last_error = NULL;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    db->last_error = ncdb_impl_strdup(tmp);
}

void ncdb_impl_buf_init(ncdbBuf *buf) { memset(buf, 0, sizeof(*buf)); }

int ncdb_impl_buf_reserve(ncdbBuf *buf, size_t addl) {
    size_t need = buf->size + addl;
    uint8_t *tmp;
    size_t new_cap = buf->cap ? buf->cap : 64U;
    while (new_cap < need) new_cap *= 2U;
    if (need <= buf->cap) return 0;
    tmp = (uint8_t *)realloc(buf->data, new_cap);
    if (!tmp) return -1;
    buf->data = tmp;
    buf->cap = new_cap;
    return 0;
}

int ncdb_impl_buf_append(ncdbBuf *buf, const void *data, size_t len) {
    if (ncdb_impl_buf_reserve(buf, len) != 0) return -1;
    memcpy(buf->data + buf->size, data, len);
    buf->size += len;
    return 0;
}

void ncdb_impl_buf_free(ncdbBuf *buf) {
    free(buf->data);
    memset(buf, 0, sizeof(*buf));
}

void ncdb_impl_reset_db(ncdbT db) {
    size_t i;
    if (!db) return;
    for (i = 0; i < db->root_count; i++) free_scope(db->roots[i]);
    for (i = 0; i < db->history_count; i++) {
        ncdbHistoryNodeT n = db->history_nodes[i];
        ncdb_attr_table_free(&n->attrs);
        free(n->logical_name);
        free(n->physical_name);
        free(n->user_name); free(n->seed); free(n->tool_category); free(n->comment);
        free(n->date); free(n->run_cwd); free(n->cmd); free(n->args);
        free(n->time_unit); free(n->vendor_id); free(n->vendor_tool);
        free(n->vendor_tool_version); free(n->same_tests);
        free(n);
    }
    for (i = 0; i < db->source_count; i++) free(db->sources[i]);
    free(db->roots); db->roots = NULL; db->root_count = db->root_cap = 0;
    free(db->history_nodes); db->history_nodes = NULL; db->history_count = db->history_cap = 0;
    free(db->sources); db->sources = NULL; db->source_count = db->source_cap = 0;
    ncdb_issues_free(db->issues); db->issues = NULL;
    free(db->issues_hist_data); db->issues_hist_data = NULL; db->issues_hist_len = 0;
    ncdb_attr_table_free(&db->attrs);
    ncdb_unique_id_index_free(db->nuid_idx);
    db->nuid_idx = NULL;
    ncdb_formal_free(db);
    ncdb_metric_free(db);
    /* Phase 4.7 / M7 — free DU-name lookup loaded from scope-tree trailer. */
    if (db->du_lookup) {
        size_t k;
        for (k = 0; k < db->du_lookup_count; k++) free(db->du_lookup[k].name);
        free(db->du_lookup);
        db->du_lookup = NULL;
        db->du_lookup_count = 0;
    }
    db->feature_flags = 0;
}

static int append_ptr(void ***arr, size_t *count, size_t *cap, void *item) {
    void **tmp;
    if (*count >= *cap) {
        size_t new_cap = *cap ? (*cap * 2U) : 8U;
        tmp = (void **)realloc(*arr, new_cap * sizeof(void *));
        if (!tmp) return -1;
        *arr = tmp;
        *cap = new_cap;
    }
    (*arr)[(*count)++] = item;
    return 0;
}

ncdbScopeT ncdb_impl_create_scope(ncdbT db, ncdbScopeT parent, uint64_t type, const char *name) {
    ncdbScopeT s = (ncdbScopeT)calloc(1, sizeof(struct ncdb_scope_s));
    if (!s) return NULL;
    s->parent = parent;
    s->type = type;
    s->name = ncdb_impl_strdup(name ? name : "");
    s->weight = 1;
    s->goal = -1;
    s->source_type = NCDB_SOURCE_NONE;
    if (!s->name) { free(s); return NULL; }
    if (parent) {
        if (append_ptr((void ***)&parent->children, &parent->child_count, &parent->child_cap, s) != 0) { free_scope(s); return NULL; }
    } else if (append_ptr((void ***)&db->roots, &db->root_count, &db->root_cap, s) != 0) { free_scope(s); return NULL; }
    return s;
}

/* Phase 4.1 / M1 — short human-friendly names for R7 synthetic child
 * scopes. Return NULL for unknown types; caller falls back to a
 * `<bintype_0xNN>` form. Names are stable, never reassigned. */
static const char *r7_bintype_short_name(uint64_t type) {
    switch (type) {
    case NCDB_COVER_PASSBIN:       return "<pass>";
    case NCDB_COVER_FAILBIN:       return "<fail>";
    case NCDB_COVER_ATTEMPTBIN:    return "<attempt>";
    case NCDB_COVER_ACTIVEBIN:     return "<active>";
    case NCDB_COVER_PEAKACTIVEBIN: return "<peakactive>";
    case NCDB_COVER_VACUOUSBIN:    return "<vacuous>";
    case NCDB_COVER_DISABLEDBIN:   return "<disabled>";
    case NCDB_COVER_COVERBIN:      return "<cover>";
    case NCDB_COVER_ASSERTBIN:     return "<assert>";
    case NCDB_COVER_STMTBIN:       return "<stmt>";
    case NCDB_COVER_BRANCHBIN:     return "<branch>";
    case NCDB_COVER_EXPRBIN:       return "<expr>";
    case NCDB_COVER_CONDBIN:       return "<cond>";
    case NCDB_COVER_TOGGLEBIN:     return "<toggle>";
    case NCDB_COVER_FSMBIN:        return "<fsm>";
    case NCDB_COVER_USERBIN:       return "<user>";
    case NCDB_COVER_CVGBIN:        return "<cvg>";
    case NCDB_COVER_IGNOREBIN:     return "<ignore>";
    case NCDB_COVER_ILLEGALBIN:    return "<illegal>";
    case NCDB_COVER_DEFAULTBIN:    return "<default>";
    case NCDB_COVER_BLOCKBIN:      return "<block>";
    default:                       return NULL;
    }
}

/* Find an existing R7 synthetic child of `scope` holding covers of
 * `type`. Synthetics are distinguished by NCDB_SCOPE_FLAG_R7_SYNTHETIC
 * and identified by their first cover's type. */
static ncdbScopeT find_r7_synthetic(ncdbScopeT scope, uint64_t type) {
    size_t i;
    for (i = 0; i < scope->child_count; i++) {
        ncdbScopeT c = scope->children[i];
        if (!(c->flags & NCDB_SCOPE_FLAG_R7_SYNTHETIC)) continue;
        if (c->cover_count > 0 && c->covers[0]->type == type) return c;
    }
    return NULL;
}

/* Create a fresh R7 synthetic child of `scope` for `type`. Synthetic
 * inherits the parent's scope type (preserves type-mask iteration) but
 * carries the UCIS_SCOPE_INTERNAL flag (so spec-compliant readers
 * filter it) and our R7-specific marker bit. */
static ncdbScopeT create_r7_synthetic(ncdbT db, ncdbScopeT scope, uint64_t type) {
    char namebuf[64];
    const char *short_name = r7_bintype_short_name(type);
    ncdbScopeT s;
    if (short_name) {
        snprintf(namebuf, sizeof(namebuf), "%s", short_name);
    } else {
        snprintf(namebuf, sizeof(namebuf),
                 "<bintype_0x%llx>", (unsigned long long)type);
    }
    s = ncdb_impl_create_scope(db, scope, scope->type, namebuf);
    if (!s) return NULL;
    s->flags |= (uint64_t)(NCDB_SCOPE_FLAG_INTERNAL | NCDB_SCOPE_FLAG_R7_SYNTHETIC);
    return s;
}

ncdbCoverT ncdb_impl_create_cover(ncdbT db, ncdbScopeT scope, uint64_t type, const char *name, uint64_t count) {
    ncdbCoverT c;
    if (!scope) return NULL;

    /* Phase 4.1 / M1: enforce the single-child_cover_type invariant that
     * scope_tree.bin assumes. If the parent already holds covers of a
     * different type, reparent the new cover into a synthetic child.
     * Synthetics themselves never recurse — they hold one type by
     * construction. */
    if (scope->cover_count > 0 &&
        scope->covers[0]->type != type &&
        !(scope->flags & NCDB_SCOPE_FLAG_R7_SYNTHETIC)) {
        ncdbScopeT synth = find_r7_synthetic(scope, type);
        if (!synth) synth = create_r7_synthetic(db, scope, type);
        if (!synth) return NULL;
        return ncdb_impl_create_cover(db, synth, type, name, count);
    }

    c = (ncdbCoverT)calloc(1, sizeof(struct ncdb_cover_s));
    if (!c) return NULL;
    c->parent = scope;
    c->type = type;
    c->name = ncdb_impl_strdup(name ? name : "");
    c->count = count;
    c->flags = ncdb_cover_default_flags(type);
    c->at_least = ncdb_cover_default_at_least(type);
    c->weight = 0;
    c->goal   = -1;
    if (!c->name) { free(c); return NULL; }
    if (append_ptr((void ***)&scope->covers, &scope->cover_count, &scope->cover_cap, c) != 0) {
        free(c->name); free(c); return NULL;
    }
    return c;
}

/* Phase 4.1 / M1 — logical cover view across a parent and its R7
 * synthetic children. UCIS callers see one flat sequence of covers per
 * scope, regardless of how many synthetic groups the writer created. */

size_t ncdb_scope_logical_cover_count(ncdbScopeT scope) {
    size_t total, i;
    if (!scope) return 0;
    total = scope->cover_count;
    for (i = 0; i < scope->child_count; i++) {
        if (scope->children[i]->flags & NCDB_SCOPE_FLAG_R7_SYNTHETIC) {
            total += scope->children[i]->cover_count;
        }
    }
    return total;
}

ncdbCoverT ncdb_scope_logical_cover_at(ncdbScopeT scope, size_t idx) {
    size_t i;
    if (!scope) return NULL;
    if (idx < scope->cover_count) return scope->covers[idx];
    idx -= scope->cover_count;
    for (i = 0; i < scope->child_count; i++) {
        ncdbScopeT child = scope->children[i];
        if (!(child->flags & NCDB_SCOPE_FLAG_R7_SYNTHETIC)) continue;
        if (idx < child->cover_count) return child->covers[idx];
        idx -= child->cover_count;
    }
    return NULL;
}

ncdbHistoryNodeT ncdb_impl_create_history(ncdbT db, uint32_t kind, const char *logical_name, const char *physical_name) {
    ncdbHistoryNodeT h = (ncdbHistoryNodeT)calloc(1, sizeof(struct ncdb_history_node_s));
    if (!h) return NULL;
    h->kind = kind;
    h->logical_name = ncdb_impl_strdup(logical_name ? logical_name : "");
    h->physical_name = ncdb_impl_strdup(physical_name);
    if (!h->logical_name) { free(h->physical_name); free(h); return NULL; }
    h->test_status = 0;  /* OK */
    h->compulsory  = 0;
    h->sim_time    = -1.0;
    h->cpu_time    = -1.0;
    h->cost        = -1.0;
    h->parent_idx  = (size_t)-1;
    if (append_ptr((void ***)&db->history_nodes, &db->history_count, &db->history_cap, h) != 0) {
        free(h->logical_name); free(h->physical_name); free(h); return NULL;
    }
    return h;
}

int ncdb_impl_add_source(ncdbT db, const char *path) {
    char *copy;
    if (append_ptr((void ***)&db->sources, &db->source_count, &db->source_cap, NULL) != 0) return -1;
    copy = ncdb_impl_strdup(path ? path : "");
    if (!copy) return -1;
    db->sources[db->source_count - 1U] = copy;
    return (int)(db->source_count - 1U);
}

int ncdb_impl_get_source_id(ncdbT db, const char *path, int create) {
    size_t i;
    if (!path) return 0;
    for (i = 0; i < db->source_count; i++) {
        if (strcmp(db->sources[i], path) == 0) return (int)i;
    }
    return create ? ncdb_impl_add_source(db, path) : -1;
}

size_t ncdb_impl_count_scopes(ncdbT db) {
    size_t i, count = 0;
    for (i = 0; i < db->root_count; i++) count += count_scope_tree(db->roots[i]);
    return count;
}

size_t ncdb_impl_count_covers(ncdbT db) {
    size_t i, count = 0;
    for (i = 0; i < db->root_count; i++) count += count_cover_tree(db->roots[i]);
    return count;
}

uint32_t ncdb_cover_default_flags(uint64_t t) {
    switch (t) {
        case NCDB_COVER_CVGBIN: return 0x19U;
        case NCDB_COVER_TOGGLEBIN:
        case NCDB_COVER_STMTBIN:
        case NCDB_COVER_BRANCHBIN:
        case NCDB_COVER_CONDBIN:
        case NCDB_COVER_EXPRBIN:
        case NCDB_COVER_FSMBIN:
        case NCDB_COVER_DEFAULTBIN:
        case NCDB_COVER_IGNOREBIN:
        case NCDB_COVER_ILLEGALBIN:
        case NCDB_COVER_BLOCKBIN:
        case NCDB_COVER_COVERBIN:
        case NCDB_COVER_ASSERTBIN:
        case NCDB_COVER_PASSBIN:
        case NCDB_COVER_FAILBIN:
        default: return 0x01U;
    }
}

uint64_t ncdb_cover_default_at_least(uint64_t t) {
    return (t == NCDB_COVER_CVGBIN) ? 1U : 0U;
}

ncdbT ncdb_Open(const char *path) {
    ncdbT db = (ncdbT)calloc(1, sizeof(struct ncdb_s));
    if (!db) return NULL;
    db->path_separator = ncdb_impl_strdup("/");
    if (path) {
        if (ncdb_Read(db, path) != 0) {
            return db;
        }
    }
    return db;
}

void ncdb_Close(ncdbT db) {
    if (!db) return;
    ncdb_impl_reset_db(db);
    free(db->path);
    free(db->path_separator);
    free(db->last_error);
    free(db->vendor_id);
    free(db->vendor_tool);
    free(db->vendor_tool_version);
    free(db->ucis_standard);
    free(db);
}

static int set_owned_str(char **slot, const char *v) {
    char *copy = ncdb_impl_strdup(v ? v : "");
    if (!copy) return -1;
    free(*slot); *slot = copy;
    return 0;
}

int ncdb_SetVendorId         (ncdbT db, const char *v) { return db ? set_owned_str(&db->vendor_id,           v) : -1; }
int ncdb_SetVendorTool       (ncdbT db, const char *v) { return db ? set_owned_str(&db->vendor_tool,         v) : -1; }
int ncdb_SetVendorToolVersion(ncdbT db, const char *v) { return db ? set_owned_str(&db->vendor_tool_version, v) : -1; }
int ncdb_SetUcisStandard     (ncdbT db, const char *v) { return db ? set_owned_str(&db->ucis_standard,       v) : -1; }
const char *ncdb_GetVendorId         (ncdbT db) { return db ? db->vendor_id           : NULL; }
const char *ncdb_GetVendorTool       (ncdbT db) { return db ? db->vendor_tool         : NULL; }
const char *ncdb_GetVendorToolVersion(ncdbT db) { return db ? db->vendor_tool_version : NULL; }
const char *ncdb_GetUcisStandard     (ncdbT db) { return db ? db->ucis_standard       : NULL; }

int ncdb_Read(ncdbT db, const char *path) {
    uint8_t *manifest_b = NULL, *strings_b = NULL, *tree_b = NULL, *counts_b = NULL, *history_b = NULL, *sources_b = NULL;
    uint8_t *attrs_b = NULL, *toggle_b = NULL, *cross_b = NULL, *fsm_b = NULL;
    size_t manifest_sz = 0, strings_sz = 0, tree_sz = 0, counts_sz = 0, history_sz = 0, sources_sz = 0;
    size_t attrs_sz = 0, toggle_sz = 0, cross_sz = 0, fsm_sz = 0;
    char err[256] = {0};
    ncdbManifest manifest;
    ncdbStringTable strings;
    uint64_t *counts = NULL;
    size_t count_count = 0;
    int rc = -1;

    ncdb_impl_reset_db(db);
    free(db->path);
    db->path = ncdb_impl_strdup(path);
    if (ncdb_zip_read_member(path, NCDB_MEMBER_MANIFEST, &manifest_b, &manifest_sz, err, sizeof(err)) != 0 ||
        ncdb_zip_read_member(path, NCDB_MEMBER_STRINGS, &strings_b, &strings_sz, err, sizeof(err)) != 0 ||
        ncdb_zip_read_member(path, NCDB_MEMBER_SCOPE_TREE, &tree_b, &tree_sz, err, sizeof(err)) != 0 ||
        ncdb_zip_read_member(path, NCDB_MEMBER_COUNTS, &counts_b, &counts_sz, err, sizeof(err)) != 0 ||
        ncdb_zip_read_member(path, NCDB_MEMBER_HISTORY, &history_b, &history_sz, err, sizeof(err)) != 0 ||
        ncdb_zip_read_member(path, NCDB_MEMBER_SOURCES, &sources_b, &sources_sz, err, sizeof(err)) != 0) {
        ncdb_impl_set_error(db, "%s", err);
        goto done;
    }
    ncdb_manifest_init(&manifest);
    if (ncdb_manifest_deserialize(manifest_b, manifest_sz, &manifest, err, sizeof(err)) != 0) {
        ncdb_impl_set_error(db, "%s", err); goto done;
    }
    if (!manifest.format || strcmp(manifest.format, NCDB_FORMAT) != 0) {
        ncdb_impl_set_error(db, "invalid manifest format"); goto done;
    }
    free(db->path_separator);
    db->path_separator = ncdb_impl_strdup(manifest.path_separator ? manifest.path_separator : "/");
    if (manifest.vendor_id && manifest.vendor_id[0])                     set_owned_str(&db->vendor_id, manifest.vendor_id);
    if (manifest.vendor_tool && manifest.vendor_tool[0])                 set_owned_str(&db->vendor_tool, manifest.vendor_tool);
    if (manifest.vendor_tool_version && manifest.vendor_tool_version[0]) set_owned_str(&db->vendor_tool_version, manifest.vendor_tool_version);
    if (manifest.ucis_standard && manifest.ucis_standard[0])             set_owned_str(&db->ucis_standard, manifest.ucis_standard);
    /* Phase 4.8 / M8: propagate the feature_flags bitset off disk so
     * callers can ask "is the M-foo feature present?" via db->feature_flags. */
    db->feature_flags = manifest.feature_flags;
    ncdb_strings_init(&strings);
    if (ncdb_strings_deserialize(strings_b, strings_sz, &strings, err, sizeof(err)) != 0 ||
        ncdb_counts_deserialize(counts_b, counts_sz, &counts, &count_count, err, sizeof(err)) != 0 ||
        ncdb_sources_deserialize(db, sources_b, sources_sz, err, sizeof(err)) != 0 ||
        ncdb_scope_tree_deserialize(db, tree_b, tree_sz, &strings, counts, count_count, err, sizeof(err)) != 0 ||
        ncdb_history_deserialize(db, history_b, history_sz, err, sizeof(err)) != 0) {
        ncdb_impl_set_error(db, "%s", err); goto cleanup_manifest;
    }
    if (ncdb_zip_read_member(path, NCDB_MEMBER_ATTRS, &attrs_b, &attrs_sz, err, sizeof(err)) == 0) ncdb_attrs_deserialize(db, attrs_b, attrs_sz, err, sizeof(err));
    if (ncdb_zip_read_member(path, NCDB_MEMBER_TOGGLE, &toggle_b, &toggle_sz, err, sizeof(err)) == 0) ncdb_toggle_deserialize(db, toggle_b, toggle_sz, err, sizeof(err));
    if (ncdb_zip_read_member(path, NCDB_MEMBER_CROSS, &cross_b, &cross_sz, err, sizeof(err)) == 0) ncdb_cross_deserialize(db, cross_b, cross_sz, err, sizeof(err));
    if (ncdb_zip_read_member(path, NCDB_MEMBER_FSM, &fsm_b, &fsm_sz, err, sizeof(err)) == 0) ncdb_fsm_deserialize(db, fsm_b, fsm_sz, err, sizeof(err));
    {
        uint8_t *tags_b = NULL; size_t tags_sz = 0;
        if (ncdb_zip_read_member(path, NCDB_MEMBER_TAGS, &tags_b, &tags_sz, err, sizeof(err)) == 0)
            ncdb_tags_deserialize(db, tags_b, tags_sz, err, sizeof(err));
        free(tags_b);
    }
    /* Phase 4.3 / M3: optional NUID member. Absent on v3 fixtures; v4
     * writers emit it when scope_count > 0. Failure to parse is
     * non-fatal — callers that ask for MatchScopeByUniqueID fall back
     * to O(n) traversal until a rewrite produces a fresh index. */
    {
        uint8_t *nuid_b = NULL; size_t nuid_sz = 0;
        ncdb_unique_id_index_free(db->nuid_idx);
        db->nuid_idx = NULL;
        if (ncdb_zip_read_member(path, NCDB_MEMBER_UNIQUE_ID, &nuid_b, &nuid_sz, err, sizeof(err)) == 0) {
            ncdb_unique_id_index_deserialize(nuid_b, nuid_sz, &db->nuid_idx, err, sizeof(err));
        }
        free(nuid_b);
    }
    /* Phase 4.4 / M4 — optional NTAS member. Absent on v3 fixtures and on
     * v4 fixtures that recorded no associations. Failure to parse is
     * non-fatal here; associations stay empty if NTAS is malformed. */
    {
        uint8_t *ntas_b = NULL; size_t ntas_sz = 0;
        if (ncdb_zip_read_member(path, NCDB_MEMBER_TESTS_ASSOC, &ntas_b, &ntas_sz, err, sizeof(err)) == 0) {
            ncdb_tests_assoc_deserialize(db, ntas_b, ntas_sz, err, sizeof(err));
        }
        free(ntas_b);
    }
    /* Phase 4.5 / M5 — optional NFRM member. */
    {
        uint8_t *nfrm_b = NULL; size_t nfrm_sz = 0;
        if (ncdb_zip_read_member(path, NCDB_MEMBER_FORMAL, &nfrm_b, &nfrm_sz, err, sizeof(err)) == 0) {
            ncdb_formal_deserialize(db, nfrm_b, nfrm_sz, err, sizeof(err));
        }
        free(nfrm_b);
    }
    /* Phase 4.6 / M6 — optional NMTR member. */
    {
        uint8_t *nmtr_b = NULL; size_t nmtr_sz = 0;
        if (ncdb_zip_read_member(path, NCDB_MEMBER_METRICS, &nmtr_b, &nmtr_sz, err, sizeof(err)) == 0) {
            ncdb_metric_deserialize(db, nmtr_b, nmtr_sz, err, sizeof(err));
        }
        free(nmtr_b);
    }
    {
        uint8_t *issues_b = NULL;
        size_t issues_sz = 0;
        if (ncdb_zip_read_member(path, NCDB_MEMBER_ISSUES, &issues_b, &issues_sz, err, sizeof(err)) == 0) {
            ncdb_issues_free(db->issues);
            db->issues = NULL;
            if (ncdb_issues_parse(&db->issues, issues_b, issues_sz, err, sizeof(err)) != 0) {
                db->issues = NULL;
            }
            free(issues_b);
        }
        free(db->issues_hist_data);
        db->issues_hist_data = NULL;
        db->issues_hist_len = 0;
        {
            uint8_t *hist_b = NULL;
            size_t hist_sz = 0;
            if (ncdb_zip_read_member(path, NCDB_MEMBER_ISSUES_HISTORY, &hist_b, &hist_sz, err, sizeof(err)) == 0) {
                db->issues_hist_data = hist_b;
                db->issues_hist_len = hist_sz;
            }
        }
    }
    rc = 0;
cleanup_manifest:
    ncdb_strings_free(&strings);
    ncdb_manifest_free(&manifest);
done:
    free(manifest_b); free(strings_b); free(tree_b); free(counts_b); free(history_b); free(sources_b);
    free(attrs_b); free(toggle_b); free(cross_b); free(fsm_b); free(counts);
    return rc;
}

int ncdb_Write(ncdbT db, const char *path) {
    ncdbStringTable strings;
    ncdbBuf tree_b, counts_tmp, counts_b, strings_b, manifest_b, history_b, sources_b, attrs_b, cross_b, toggle_b, fsm_b, tags_b, nuid_b, ntas_b, nfrm_b, nmtr_b;
    ncdbManifest manifest;
    ncdbZipMember members[20];
    size_t member_count = 0;
    uint64_t *counts = NULL;
    size_t count_count = 0;
    char err[256] = {0};
    int rc = -1;
    int cross_rc;

    ncdb_strings_init(&strings);
    ncdb_impl_buf_init(&tree_b); ncdb_impl_buf_init(&counts_tmp); ncdb_impl_buf_init(&counts_b);
    ncdb_impl_buf_init(&strings_b); ncdb_impl_buf_init(&manifest_b); ncdb_impl_buf_init(&history_b);
    ncdb_impl_buf_init(&sources_b); ncdb_impl_buf_init(&attrs_b); ncdb_impl_buf_init(&cross_b);
    ncdb_impl_buf_init(&toggle_b);
    ncdb_impl_buf_init(&fsm_b);
    ncdb_impl_buf_init(&tags_b);
    ncdb_impl_buf_init(&nuid_b);
    ncdb_impl_buf_init(&ntas_b);
    ncdb_impl_buf_init(&nfrm_b);
    ncdb_impl_buf_init(&nmtr_b);
    ncdb_manifest_init(&manifest);

    if (ncdb_strings_add(&strings, "") < 0 ||
        ncdb_scope_tree_serialize(db, &strings, &tree_b, &counts_tmp) != 0) {
        ncdb_impl_set_error(db, "%s", "failed to build scope tree");
        goto done;
    }
    counts = (uint64_t *)counts_tmp.data;
    count_count = counts_tmp.size / sizeof(uint64_t);
    cross_rc = ncdb_cross_serialize(db, &cross_b);
    if (cross_rc < 0 ||
        ncdb_counts_serialize(counts, count_count, &counts_b) != 0 ||
        ncdb_strings_serialize(&strings, &strings_b) != 0 ||
        ncdb_history_serialize(db, &history_b) != 0 ||
        ncdb_sources_serialize(db, &sources_b) != 0 ||
        ncdb_attrs_serialize(db, &attrs_b) != 0 ||
        ncdb_toggle_serialize(db, &toggle_b) != 0 ||
        ncdb_fsm_serialize(db, &fsm_b) != 0 ||
        ncdb_tags_serialize(db, &tags_b) != 0 ||
        ncdb_unique_id_index_serialize(db, &nuid_b) != 0 ||
        ncdb_tests_assoc_serialize(db, &ntas_b) != 0 ||
        ncdb_formal_serialize(db, &nfrm_b) != 0 ||
        ncdb_metric_serialize(db, &nmtr_b) != 0 ||
        ncdb_manifest_build(db, tree_b.data, tree_b.size, counts, count_count, &manifest) != 0 ||
        ncdb_manifest_serialize(&manifest, &manifest_b) != 0) {
        ncdb_impl_set_error(db, "%s", "failed to serialize database");
        goto done;
    }
    members[member_count++] = (ncdbZipMember){NCDB_MEMBER_MANIFEST, manifest_b.data, manifest_b.size, 0};
    members[member_count++] = (ncdbZipMember){NCDB_MEMBER_STRINGS, strings_b.data, strings_b.size, 0};
    members[member_count++] = (ncdbZipMember){NCDB_MEMBER_SCOPE_TREE, tree_b.data, tree_b.size, 0};
    members[member_count++] = (ncdbZipMember){NCDB_MEMBER_COUNTS, counts_b.data, counts_b.size, 0};
    members[member_count++] = (ncdbZipMember){NCDB_MEMBER_HISTORY, history_b.data, history_b.size, 0};
    members[member_count++] = (ncdbZipMember){NCDB_MEMBER_SOURCES, sources_b.data, sources_b.size, 0};
    members[member_count++] = (ncdbZipMember){NCDB_MEMBER_ATTRS, attrs_b.data, attrs_b.size, 0};
    if (cross_rc > 0)
        members[member_count++] = (ncdbZipMember){NCDB_MEMBER_CROSS, cross_b.data, cross_b.size, 0};
    if (toggle_b.size > 0)
        members[member_count++] = (ncdbZipMember){NCDB_MEMBER_TOGGLE, toggle_b.data, toggle_b.size, 0};
    if (fsm_b.size > 0)
        members[member_count++] = (ncdbZipMember){NCDB_MEMBER_FSM, fsm_b.data, fsm_b.size, 0};
    if (tags_b.size > 0)
        members[member_count++] = (ncdbZipMember){NCDB_MEMBER_TAGS, tags_b.data, tags_b.size, 0};
    if (nuid_b.size > 0)
        members[member_count++] = (ncdbZipMember){NCDB_MEMBER_UNIQUE_ID, nuid_b.data, nuid_b.size, 0};
    if (ntas_b.size > 0)
        members[member_count++] = (ncdbZipMember){NCDB_MEMBER_TESTS_ASSOC, ntas_b.data, ntas_b.size, 0};
    if (nfrm_b.size > 0)
        members[member_count++] = (ncdbZipMember){NCDB_MEMBER_FORMAL, nfrm_b.data, nfrm_b.size, 0};
    if (nmtr_b.size > 0)
        members[member_count++] = (ncdbZipMember){NCDB_MEMBER_METRICS, nmtr_b.data, nmtr_b.size, 0};
    if (ncdb_zip_write_archive(path, members, member_count, err, sizeof(err)) != 0) {
        ncdb_impl_set_error(db, "%s", err); goto done;
    }
    free(db->path);
    db->path = ncdb_impl_strdup(path);
    rc = 0;
done:
    ncdb_manifest_free(&manifest);
    ncdb_strings_free(&strings);
    ncdb_impl_buf_free(&tree_b);
    ncdb_impl_buf_free(&counts_tmp);
    ncdb_impl_buf_free(&counts_b);
    ncdb_impl_buf_free(&strings_b);
    ncdb_impl_buf_free(&manifest_b);
    ncdb_impl_buf_free(&history_b);
    ncdb_impl_buf_free(&sources_b);
    ncdb_impl_buf_free(&attrs_b);
    ncdb_impl_buf_free(&cross_b);
    ncdb_impl_buf_free(&toggle_b);
    ncdb_impl_buf_free(&fsm_b);
    ncdb_impl_buf_free(&tags_b);
    ncdb_impl_buf_free(&nuid_b);
    ncdb_impl_buf_free(&ntas_b);
    ncdb_impl_buf_free(&nfrm_b);
    ncdb_impl_buf_free(&nmtr_b);
    return rc;
}

int ncdb_ScopeIterate(ncdbT db, ncdbScopeT parent, uint32_t type_mask, int (*cb)(ncdbT, ncdbScopeT, void *), void *ud) {
    size_t i, count;
    ncdbScopeT *arr;
    if (!db || !cb) return -1;
    arr = parent ? parent->children : db->roots;
    count = parent ? parent->child_count : db->root_count;
    for (i = 0; i < count; i++) {
        if (type_mask == 0U || type_mask == 0xFFFFFFFFU || (((uint64_t)type_mask) & arr[i]->type) != 0U || (uint64_t)type_mask == arr[i]->type) {
            int stop = cb(db, arr[i], ud);
            if (stop) return stop;
        }
    }
    return 0;
}

const char *ncdb_GetScopeName(ncdbT db, ncdbScopeT scope) { (void)db; return scope ? scope->name : NULL; }
uint32_t ncdb_GetScopeType(ncdbT db, ncdbScopeT scope) { (void)db; return scope ? (uint32_t)scope->type : 0; }
ncdbScopeT ncdb_GetParent(ncdbT db, ncdbScopeT scope) { (void)db; return scope ? scope->parent : NULL; }
uint64_t ncdb_GetScopeWeight(ncdbT db, ncdbScopeT scope) { (void)db; return scope ? scope->weight : 1; }
int64_t ncdb_GetScopeGoal(ncdbT db, ncdbScopeT scope) { (void)db; return scope ? scope->goal : -1; }
void ncdb_SetScopeWeight(ncdbT db, ncdbScopeT scope, uint64_t weight) { (void)db; if (scope) scope->weight = weight; }
void ncdb_SetScopeGoal(ncdbT db, ncdbScopeT scope, int64_t goal) { (void)db; if (scope) scope->goal = goal; }

const char *ncdb_GetScopeSourcePath(ncdbT db, ncdbScopeT scope) { (void)db; return scope ? scope->source_path : NULL; }
uint64_t ncdb_GetScopeSourceLine(ncdbT db, ncdbScopeT scope) { (void)db; return scope ? scope->source_line : 0; }
uint64_t ncdb_GetScopeSourceToken(ncdbT db, ncdbScopeT scope) { (void)db; return scope ? scope->source_token : 0; }

int ncdb_SetScopeSourceInfo(ncdbT db, ncdbScopeT scope, const char *path, uint64_t line, uint64_t token) {
    char *copy;
    if (!scope || !path) return -1;
    (void)db;
    copy = ncdb_impl_strdup(path);
    if (!copy) return -1;
    free(scope->source_path);
    scope->source_path = copy;
    scope->source_line  = line;
    scope->source_token = token;
    return 0;
}

int ncdb_CoverIterate(ncdbT db, ncdbScopeT scope, int (*cb)(ncdbT, ncdbCoverT, void *), void *ud) {
    size_t i;
    if (!db || !scope || !cb) return -1;
    for (i = 0; i < scope->cover_count; i++) {
        int stop = cb(db, scope->covers[i], ud);
        if (stop) return stop;
    }
    return 0;
}

const char *ncdb_GetCoverName(ncdbT db, ncdbCoverT cover) { (void)db; return cover ? cover->name : NULL; }
uint32_t ncdb_GetCoverType(ncdbT db, ncdbCoverT cover) { (void)db; return cover ? (uint32_t)cover->type : 0; }
uint64_t ncdb_GetCoverCount(ncdbT db, ncdbCoverT cover) { (void)db; return cover ? cover->count : 0; }

int ncdb_HistoryIterate(ncdbT db, uint32_t kind_mask, int (*cb)(ncdbT, ncdbHistoryNodeT, void *), void *ud) {
    size_t i;
    if (!db || !cb) return -1;
    for (i = 0; i < db->history_count; i++) {
        uint32_t k = db->history_nodes[i]->kind;
        if (kind_mask == 0U || kind_mask == 0xFFFFFFFFU || kind_mask == k || (kind_mask & (1U << k))) {
            int stop = cb(db, db->history_nodes[i], ud);
            if (stop) return stop;
        }
    }
    return 0;
}

const char *ncdb_GetHistoryLogicalName(ncdbT db, ncdbHistoryNodeT node) { (void)db; return node ? node->logical_name : NULL; }
const char *ncdb_GetHistoryPhysicalName(ncdbT db, ncdbHistoryNodeT node) { (void)db; return node ? node->physical_name : NULL; }
uint32_t    ncdb_GetHistoryKind(ncdbT db, ncdbHistoryNodeT node)        { (void)db; return node ? node->kind : 0; }
const char *ncdb_GetHistoryUserName(ncdbT db, ncdbHistoryNodeT node)    { (void)db; return node ? node->user_name : NULL; }
const char *ncdb_GetHistorySeed(ncdbT db, ncdbHistoryNodeT node)        { (void)db; return node ? node->seed : NULL; }
const char *ncdb_GetHistoryToolCategory(ncdbT db, ncdbHistoryNodeT node){ (void)db; return node ? node->tool_category : NULL; }
const char *ncdb_GetHistoryComment(ncdbT db, ncdbHistoryNodeT node)     { (void)db; return node ? node->comment : NULL; }
uint32_t    ncdb_GetHistoryTestStatus(ncdbT db, ncdbHistoryNodeT node)  { (void)db; return node ? node->test_status : 0; }

static void ncdb_impl_set_history_str(char **field, const char *val) {
    free(*field);
    *field = ncdb_impl_strdup(val);
}

void ncdb_SetHistoryUserName(ncdbT db, ncdbHistoryNodeT node, const char *v)    { (void)db; if (node) ncdb_impl_set_history_str(&node->user_name, v); }
void ncdb_SetHistorySeed(ncdbT db, ncdbHistoryNodeT node, const char *v)        { (void)db; if (node) ncdb_impl_set_history_str(&node->seed, v); }
void ncdb_SetHistoryToolCategory(ncdbT db, ncdbHistoryNodeT node, const char *v){ (void)db; if (node) ncdb_impl_set_history_str(&node->tool_category, v); }
void ncdb_SetHistoryComment(ncdbT db, ncdbHistoryNodeT node, const char *v)     { (void)db; if (node) ncdb_impl_set_history_str(&node->comment, v); }
void ncdb_SetHistoryTestStatus(ncdbT db, ncdbHistoryNodeT node, uint32_t v)     { (void)db; if (node) node->test_status = v; }
int ncdb_SetHistoryParent(ncdbT db, ncdbHistoryNodeT node, ncdbHistoryNodeT parent) {
    size_t i;
    if (!db || !node) return -1;
    if (!parent) { node->parent_idx = (size_t)-1; return 0; }
    for (i = 0; i < db->history_count; ++i)
        if (db->history_nodes[i] == parent) { node->parent_idx = i; return 0; }
    return -1;
}
ncdbHistoryNodeT ncdb_GetHistoryParent(ncdbT db, ncdbHistoryNodeT node) {
    if (!db || !node || node->parent_idx == (size_t)-1 ||
        node->parent_idx >= db->history_count) return NULL;
    return db->history_nodes[node->parent_idx];
}

size_t     ncdb_GetCrossPointCount(ncdbT db, ncdbScopeT scope) { (void)db; return scope ? scope->crossed_count : 0; }
ncdbScopeT ncdb_GetCrossPoint(ncdbT db, ncdbScopeT scope, size_t idx) { (void)db; return (scope && idx < scope->crossed_count) ? scope->crossed_points[idx] : NULL; }

int ncdb_impl_add_cross_point(ncdbScopeT cross_scope, ncdbScopeT cp) {
    return append_ptr((void ***)&cross_scope->crossed_points, &cross_scope->crossed_count, &cross_scope->crossed_cap, cp);
}

static void dfs_count_(ncdbScopeT s, size_t *n) {
    size_t i;
    if (!s) return;
    (*n)++;
    for (i = 0; i < s->child_count; ++i) dfs_count_(s->children[i], n);
}
static void dfs_collect_(ncdbScopeT s, ncdbScopeT *arr, size_t *idx) {
    size_t i;
    if (!s) return;
    arr[(*idx)++] = s;
    for (i = 0; i < s->child_count; ++i) dfs_collect_(s->children[i], arr, idx);
}
int ncdb_impl_dfs_flatten(ncdbT db, ncdbScopeT **out, size_t *out_count) {
    size_t total = 0, idx = 0, i;
    if (!db || !out || !out_count) return -1;
    for (i = 0; i < db->root_count; ++i) dfs_count_(db->roots[i], &total);
    *out_count = total;
    *out = NULL;
    if (total == 0) return 0;
    *out = (ncdbScopeT *)malloc(total * sizeof(**out));
    if (!*out) return -1;
    for (i = 0; i < db->root_count; ++i) dfs_collect_(db->roots[i], *out, &idx);
    return 0;
}

const char *ncdb_GetLastError(ncdbT db) { return (db && db->last_error) ? db->last_error : ""; }

/* ── TOGGLE metadata accessors ─────────────────────────────────────────── */
const char *ncdb_GetToggleCanonicalName(ncdbT db, ncdbScopeT s) { (void)db; return s ? s->toggle_canonical : NULL; }
uint8_t     ncdb_GetToggleMetric(ncdbT db, ncdbScopeT s)        { (void)db; return s ? s->toggle_metric : 0; }
uint8_t     ncdb_GetToggleType(ncdbT db, ncdbScopeT s)          { (void)db; return s ? s->toggle_type : 0; }
uint8_t     ncdb_GetToggleDir(ncdbT db, ncdbScopeT s)           { (void)db; return s ? s->toggle_dir : 0; }
int ncdb_SetToggleCanonicalName(ncdbT db, ncdbScopeT s, const char *name) {
    char *copy;
    (void)db;
    if (!s) return -1;
    copy = ncdb_impl_strdup(name ? name : "");
    if (!copy) return -1;
    free(s->toggle_canonical);
    s->toggle_canonical = copy;
    return 0;
}
void ncdb_SetToggleMetric(ncdbT db, ncdbScopeT s, uint8_t v) { (void)db; if (s) s->toggle_metric = v; }
void ncdb_SetToggleType  (ncdbT db, ncdbScopeT s, uint8_t v) { (void)db; if (s) s->toggle_type   = v; }
void ncdb_SetToggleDir   (ncdbT db, ncdbScopeT s, uint8_t v) { (void)db; if (s) s->toggle_dir    = v; }

/* ── FSM metadata accessors ────────────────────────────────────────────── */
/* ── Coverpoint / Cross UCIS metadata ──────────────────────────────────── */
int ncdb_SetCoverpointExprTerms(ncdbT db, ncdbScopeT s, const char *expr) {
    char *copy;
    (void)db;
    if (!s) return -1;
    copy = ncdb_impl_strdup(expr ? expr : "");
    if (!copy) return -1;
    free(s->expr_terms); s->expr_terms = copy;
    return 0;
}
const char *ncdb_GetCoverpointExprTerms(ncdbT db, ncdbScopeT s) {
    (void)db; return s ? s->expr_terms : NULL;
}
size_t ncdb_GetCrossArity(ncdbT db, ncdbScopeT cross) {
    (void)db; return cross ? cross->crossed_count : 0;
}

/* ── Per-cover UCIS metadata ───────────────────────────────────────────── */
int ncdb_SetCoverSource(ncdbT db, ncdbCoverT c, const char *path, uint64_t line, uint64_t token) {
    char *copy;
    (void)db;
    if (!c || !path) return -1;
    copy = ncdb_impl_strdup(path);
    if (!copy) return -1;
    free(c->source_path); c->source_path = copy;
    c->source_line = line; c->source_token = token;
    return 0;
}
int ncdb_SetCoverWeight (ncdbT db, ncdbCoverT c, uint64_t w) { (void)db; if (!c) return -1; c->weight = w; return 0; }
int ncdb_SetCoverGoal   (ncdbT db, ncdbCoverT c, int64_t g)  { (void)db; if (!c) return -1; c->goal = g; return 0; }
int ncdb_SetCoverComment(ncdbT db, ncdbCoverT c, const char *cm) {
    char *copy;
    (void)db;
    if (!c) return -1;
    copy = ncdb_impl_strdup(cm ? cm : "");
    if (!copy) return -1;
    free(c->comment); c->comment = copy;
    return 0;
}
int ncdb_SetCoverAtLeast(ncdbT db, ncdbCoverT c, uint64_t al) { (void)db; if (!c) return -1; c->at_least = al; return 0; }
const char *ncdb_GetCoverSourcePath (ncdbT db, ncdbCoverT c) { (void)db; return c ? c->source_path : NULL; }
uint64_t    ncdb_GetCoverSourceLine (ncdbT db, ncdbCoverT c) { (void)db; return c ? c->source_line : 0; }
uint64_t    ncdb_GetCoverSourceToken(ncdbT db, ncdbCoverT c) { (void)db; return c ? c->source_token : 0; }
uint64_t    ncdb_GetCoverWeight (ncdbT db, ncdbCoverT c)     { (void)db; return c ? c->weight : 0; }
int64_t     ncdb_GetCoverGoal   (ncdbT db, ncdbCoverT c)     { (void)db; return c ? c->goal : -1; }
const char *ncdb_GetCoverComment(ncdbT db, ncdbCoverT c)     { (void)db; return c ? c->comment : NULL; }
uint64_t    ncdb_GetCoverAtLeast(ncdbT db, ncdbCoverT c)     { (void)db; return c ? c->at_least : 0; }

/* ── DU↔instance linking ───────────────────────────────────────────────── */
int ncdb_SetScopeDU(ncdbT db, ncdbScopeT instance, ncdbScopeT du) {
    (void)db;
    if (!instance) return -1;
    instance->du_scope = du;  /* not owned; du lives in same tree */
    return 0;
}
ncdbScopeT ncdb_GetScopeDU(ncdbT db, ncdbScopeT instance) {
    (void)db; return instance ? instance->du_scope : NULL;
}
int ncdb_SetDUSignature(ncdbT db, ncdbScopeT du, const char *sig) {
    char *copy;
    (void)db;
    if (!du) return -1;
    copy = ncdb_impl_strdup(sig ? sig : "");
    if (!copy) return -1;
    free(du->du_signature); du->du_signature = copy;
    return 0;
}
const char *ncdb_GetDUSignature(ncdbT db, ncdbScopeT du) {
    (void)db; return du ? du->du_signature : NULL;
}

int ncdb_DuAddFile(ncdbT db, ncdbScopeT du, const char *path) {
    int global_id;
    if (!db || !du || !path) return -1;
    global_id = ncdb_impl_get_source_id(db, path, 1);
    if (global_id < 0) return -1;
    if (du->du_file_count == du->du_file_cap) {
        size_t ncap = du->du_file_cap ? du->du_file_cap * 2U : 4U;
        uint32_t *na = (uint32_t *)realloc(du->du_files, ncap * sizeof(*na));
        if (!na) return -1;
        du->du_files = na; du->du_file_cap = ncap;
    }
    du->du_files[du->du_file_count] = (uint32_t)global_id;
    return (int)du->du_file_count++;
}
size_t ncdb_DuFileCount(ncdbT db, ncdbScopeT du) { (void)db; return du ? du->du_file_count : 0; }
const char *ncdb_DuGetFile(ncdbT db, ncdbScopeT du, size_t idx) {
    if (!db || !du || idx >= du->du_file_count) return NULL;
    {
        uint32_t gid = du->du_files[idx];
        return (gid < db->source_count) ? db->sources[gid] : NULL;
    }
}

/* ── Scope tags ────────────────────────────────────────────────────────── */
int ncdb_ScopeAddTag(ncdbT db, ncdbScopeT s, const char *tag) {
    char *copy;
    (void)db;
    if (!s || !tag) return -1;
    if (s->tag_count == s->tag_cap) {
        size_t ncap = s->tag_cap ? s->tag_cap * 2U : 4U;
        char **na = (char **)realloc(s->tags, ncap * sizeof(*na));
        if (!na) return -1;
        s->tags = na; s->tag_cap = ncap;
    }
    copy = ncdb_impl_strdup(tag);
    if (!copy) return -1;
    s->tags[s->tag_count++] = copy;
    return 0;
}
size_t ncdb_ScopeTagCount(ncdbT db, ncdbScopeT s) { (void)db; return s ? s->tag_count : 0; }
const char *ncdb_ScopeGetTag(ncdbT db, ncdbScopeT s, size_t idx) {
    (void)db; return (s && idx < s->tag_count) ? s->tags[idx] : NULL;
}

int ncdb_FsmAddState(ncdbT db, ncdbScopeT fsm, const char *name, uint32_t value) {
    char *copy;
    (void)db;
    if (!fsm || !name) return -1;
    if (fsm->fsm_state_count == fsm->fsm_state_cap) {
        size_t ncap = fsm->fsm_state_cap ? fsm->fsm_state_cap * 2U : 4U;
        struct ncdb_fsm_state_s *na =
            (struct ncdb_fsm_state_s *)realloc(fsm->fsm_states, ncap * sizeof(*na));
        if (!na) return -1;
        fsm->fsm_states = na;
        fsm->fsm_state_cap = ncap;
    }
    copy = ncdb_impl_strdup(name);
    if (!copy) return -1;
    fsm->fsm_states[fsm->fsm_state_count].name  = copy;
    fsm->fsm_states[fsm->fsm_state_count].value = value;
    fsm->fsm_state_count++;
    return 0;
}
size_t ncdb_FsmStateCount(ncdbT db, ncdbScopeT fsm) {
    (void)db; return fsm ? fsm->fsm_state_count : 0;
}
int ncdb_FsmGetState(ncdbT db, ncdbScopeT fsm, size_t idx, const char **name, uint32_t *value) {
    (void)db;
    if (!fsm || idx >= fsm->fsm_state_count) return -1;
    if (name)  *name  = fsm->fsm_states[idx].name;
    if (value) *value = fsm->fsm_states[idx].value;
    return 0;
}

/* ── Typed attribute public API ────────────────────────────────────────── */

int ncdb_DbSetAttr(ncdbT db, const char *key, const ncdbAttrValue *v) {
    return (db && key && v) ? ncdb_attr_table_set(&db->attrs, key, v) : -1;
}
int ncdb_ScopeSetAttr(ncdbT db, ncdbScopeT s, const char *key, const ncdbAttrValue *v) {
    (void)db; return (s && key && v) ? ncdb_attr_table_set(&s->attrs, key, v) : -1;
}
int ncdb_CoverSetAttr(ncdbT db, ncdbCoverT c, const char *key, const ncdbAttrValue *v) {
    (void)db; return (c && key && v) ? ncdb_attr_table_set(&c->attrs, key, v) : -1;
}
int ncdb_HistorySetAttr(ncdbT db, ncdbHistoryNodeT h, const char *key, const ncdbAttrValue *v) {
    (void)db; return (h && key && v) ? ncdb_attr_table_set(&h->attrs, key, v) : -1;
}

int ncdb_DbGetAttr(ncdbT db, const char *key, ncdbAttrValue *out) {
    return (db && key) ? ncdb_attr_table_get(&db->attrs, key, out) : -1;
}
int ncdb_ScopeGetAttr(ncdbT db, ncdbScopeT s, const char *key, ncdbAttrValue *out) {
    (void)db; return (s && key) ? ncdb_attr_table_get(&s->attrs, key, out) : -1;
}
int ncdb_CoverGetAttr(ncdbT db, ncdbCoverT c, const char *key, ncdbAttrValue *out) {
    (void)db; return (c && key) ? ncdb_attr_table_get(&c->attrs, key, out) : -1;
}
int ncdb_HistoryGetAttr(ncdbT db, ncdbHistoryNodeT h, const char *key, ncdbAttrValue *out) {
    (void)db; return (h && key) ? ncdb_attr_table_get(&h->attrs, key, out) : -1;
}

int ncdb_DbAttrIterate(ncdbT db, int (*cb)(const char *, const ncdbAttrValue *, void *), void *ud) {
    return db ? ncdb_attr_table_iterate(&db->attrs, cb, ud) : 0;
}
int ncdb_ScopeAttrIterate(ncdbT db, ncdbScopeT s, int (*cb)(const char *, const ncdbAttrValue *, void *), void *ud) {
    (void)db; return s ? ncdb_attr_table_iterate(&s->attrs, cb, ud) : 0;
}
int ncdb_CoverAttrIterate(ncdbT db, ncdbCoverT c, int (*cb)(const char *, const ncdbAttrValue *, void *), void *ud) {
    (void)db; return c ? ncdb_attr_table_iterate(&c->attrs, cb, ud) : 0;
}
int ncdb_HistoryAttrIterate(ncdbT db, ncdbHistoryNodeT h, int (*cb)(const char *, const ncdbAttrValue *, void *), void *ud) {
    (void)db; return h ? ncdb_attr_table_iterate(&h->attrs, cb, ud) : 0;
}
