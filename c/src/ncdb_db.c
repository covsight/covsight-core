#include <stdio.h>
#include <stdarg.h>

#include "ncdb_impl.h"
#include "ncdb_cross.h"

static void free_scope(ncdbScopeT scope) {
    size_t i;
    if (!scope) return;
    for (i = 0; i < scope->cover_count; i++) {
        free(scope->covers[i]->name);
        free(scope->covers[i]);
    }
    for (i = 0; i < scope->child_count; i++) {
        free_scope(scope->children[i]);
    }
    free(scope->covers);
    free(scope->children);
    free(scope->crossed_points);  /* array only — pointed-to scopes are owned elsewhere */
    free(scope->name);
    free(scope->source_path);
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

ncdbCoverT ncdb_impl_create_cover(ncdbT db, ncdbScopeT scope, uint64_t type, const char *name, uint64_t count) {
    ncdbCoverT c = (ncdbCoverT)calloc(1, sizeof(struct ncdb_cover_s));
    (void)db;
    if (!c) return NULL;
    c->parent = scope;
    c->type = type;
    c->name = ncdb_impl_strdup(name ? name : "");
    c->count = count;
    c->flags = ncdb_cover_default_flags(type);
    c->at_least = ncdb_cover_default_at_least(type);
    if (!c->name) { free(c); return NULL; }
    if (append_ptr((void ***)&scope->covers, &scope->cover_count, &scope->cover_cap, c) != 0) {
        free(c->name); free(c); return NULL;
    }
    return c;
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
    free(db);
}

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
    ncdbBuf tree_b, counts_tmp, counts_b, strings_b, manifest_b, history_b, sources_b, attrs_b, cross_b;
    ncdbManifest manifest;
    ncdbZipMember members[8];
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
        ncdb_attrs_serialize_empty(&attrs_b) != 0 ||
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
    if (cross_rc > 0)  /* cross.bin present */
        members[member_count++] = (ncdbZipMember){NCDB_MEMBER_CROSS, cross_b.data, cross_b.size, 0};
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

size_t     ncdb_GetCrossPointCount(ncdbT db, ncdbScopeT scope) { (void)db; return scope ? scope->crossed_count : 0; }
ncdbScopeT ncdb_GetCrossPoint(ncdbT db, ncdbScopeT scope, size_t idx) { (void)db; return (scope && idx < scope->crossed_count) ? scope->crossed_points[idx] : NULL; }

int ncdb_impl_add_cross_point(ncdbScopeT cross_scope, ncdbScopeT cp) {
    return append_ptr((void ***)&cross_scope->crossed_points, &cross_scope->crossed_count, &cross_scope->crossed_cap, cp);
}

const char *ncdb_GetLastError(ncdbT db) { return (db && db->last_error) ? db->last_error : ""; }
