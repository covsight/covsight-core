#include <stdio.h>

#include "ncdb_scope_tree.h"
#include "ncdb_manifest.h"  /* NCDB_FEATURE_TYPED_PROPS */

static int is_toggle_pair(ncdbScopeT scope) {
    int have01 = 0, have10 = 0;
    size_t i;
    if (!scope || scope->type != NCDB_SCOPE_BRANCH || scope->child_count != 0 || scope->cover_count != 2) {
        return 0;
    }
    for (i = 0; i < scope->cover_count; i++) {
        if (scope->covers[i]->type != NCDB_COVER_TOGGLEBIN) {
            return 0;
        }
        if (strcmp(scope->covers[i]->name, NCDB_TOGGLE_BIN_0_TO_1) == 0) have01 = 1;
        if (strcmp(scope->covers[i]->name, NCDB_TOGGLE_BIN_1_TO_0) == 0) have10 = 1;
    }
    return have01 && have10;
}

static int append_count(ncdbBuf *counts_tmp, uint64_t value) {
    return ncdb_impl_buf_append(counts_tmp, &value, sizeof(value));
}

static int write_scope(ncdbT db, ncdbScopeT scope, ncdbStringTable *strings, ncdbBuf *tree_out, ncdbBuf *counts_tmp) {
    size_t i;
    int name_ref;
    (void)db;
    if (is_toggle_pair(scope)) {
        uint8_t marker = NCDB_SCOPE_MARKER_TOGGLE_PAIR;
        uint64_t c01 = 0, c10 = 0;
        if (ncdb_impl_buf_append(tree_out, &marker, 1) != 0) return -1;
        name_ref = ncdb_strings_add(strings, scope->name);
        if (name_ref < 0 || ncdb_varint_encode_uint64((uint64_t)name_ref, tree_out) != 0) return -1;
        for (i = 0; i < scope->cover_count; i++) {
            if (strcmp(scope->covers[i]->name, NCDB_TOGGLE_BIN_0_TO_1) == 0) c01 = scope->covers[i]->count;
            else if (strcmp(scope->covers[i]->name, NCDB_TOGGLE_BIN_1_TO_0) == 0) c10 = scope->covers[i]->count;
        }
        return append_count(counts_tmp, c01) || append_count(counts_tmp, c10);
    } else {
        uint8_t marker = NCDB_SCOPE_MARKER_REGULAR;
        uint64_t presence = 0;
        uint64_t child_cover_type = 0;
        uint64_t at_least_override = 0;
        int has_at_least = 0;
        if (scope->flags != 0) presence |= NCDB_PRESENCE_FLAGS;
        if (scope->source_path) presence |= NCDB_PRESENCE_SOURCE;
        if (scope->weight != 1) presence |= NCDB_PRESENCE_WEIGHT;
        if (scope->goal != -1) presence |= NCDB_PRESENCE_GOAL;
        if (scope->source_type != NCDB_SOURCE_NONE) presence |= NCDB_PRESENCE_SOURCE_TYPE;
        if (scope->du_scope) presence |= NCDB_PRESENCE_DU_LINK;
        if (scope->du_signature && scope->du_signature[0]) presence |= NCDB_PRESENCE_DU_SIG;
        if (scope->expr_terms && scope->expr_terms[0]) presence |= NCDB_PRESENCE_EXPR_TERMS;
        if (scope->du_file_count > 0) presence |= NCDB_PRESENCE_DU_FILES;
        if (scope->props.count > 0) presence |= NCDB_PRESENCE_TYPED_PROPS;
        /* Phase 1.5: extended cover fields needed when any cover has source/weight/goal/comment/per-bin at_least. */
        for (i = 0; i < scope->cover_count; i++) {
            ncdbCoverT cv = scope->covers[i];
            if (cv->source_path || cv->weight != 0 || cv->goal != -1 ||
                cv->comment || cv->props.count > 0 ||
                (cv->at_least != ncdb_cover_default_at_least(cv->type) && !has_at_least)) {
                presence |= NCDB_PRESENCE_EXT_COVERS;
                break;
            }
        }
        if (scope->cover_count > 0) {
            child_cover_type = scope->covers[0]->type;
            at_least_override = scope->covers[0]->at_least;
            if (at_least_override != ncdb_cover_default_at_least(child_cover_type)) {
                presence |= NCDB_PRESENCE_AT_LEAST;
                has_at_least = 1;
            }
        }
        if (ncdb_impl_buf_append(tree_out, &marker, 1) != 0) return -1;
        name_ref = ncdb_strings_add(strings, scope->name);
        if (name_ref < 0) return -1;
        if (ncdb_varint_encode_uint64(scope->type, tree_out) != 0 ||
            ncdb_varint_encode_uint64((uint64_t)name_ref, tree_out) != 0 ||
            ncdb_varint_encode_uint64(presence, tree_out) != 0) return -1;
        if (presence & NCDB_PRESENCE_FLAGS) if (ncdb_varint_encode_uint64(scope->flags, tree_out) != 0) return -1;
        if (presence & NCDB_PRESENCE_SOURCE) {
            int sid = ncdb_impl_get_source_id(db, scope->source_path, 1);
            if (sid < 0) return -1;
            if (ncdb_varint_encode_uint64((uint64_t)sid, tree_out) != 0 ||
                ncdb_varint_encode_uint64(scope->source_line, tree_out) != 0 ||
                ncdb_varint_encode_uint64(scope->source_token, tree_out) != 0) return -1;
        }
        if (presence & NCDB_PRESENCE_WEIGHT) if (ncdb_varint_encode_uint64(scope->weight, tree_out) != 0) return -1;
        if (has_at_least) if (ncdb_varint_encode_uint64(at_least_override, tree_out) != 0) return -1;
        if (presence & NCDB_PRESENCE_GOAL) if (ncdb_varint_encode_uint64((uint64_t)scope->goal, tree_out) != 0) return -1;
        if (presence & NCDB_PRESENCE_SOURCE_TYPE) if (ncdb_varint_encode_uint64((uint64_t)scope->source_type, tree_out) != 0) return -1;
        if (presence & NCDB_PRESENCE_DU_LINK) {
            size_t du_idx;
            if (ncdb_impl_scope_dfs_index(db, scope->du_scope, &du_idx) != 0) return -1;
            if (ncdb_varint_encode_uint64((uint64_t)du_idx, tree_out) != 0) return -1;
        }
        if (presence & NCDB_PRESENCE_DU_SIG) {
            int sig_ref = ncdb_strings_add(strings, scope->du_signature);
            if (sig_ref < 0 || ncdb_varint_encode_uint64((uint64_t)sig_ref, tree_out) != 0) return -1;
        }
        if (presence & NCDB_PRESENCE_EXPR_TERMS) {
            int ref = ncdb_strings_add(strings, scope->expr_terms);
            if (ref < 0 || ncdb_varint_encode_uint64((uint64_t)ref, tree_out) != 0) return -1;
        }
        if (presence & NCDB_PRESENCE_DU_FILES) {
            size_t fi;
            if (ncdb_varint_encode_uint64((uint64_t)scope->du_file_count, tree_out) != 0) return -1;
            for (fi = 0; fi < scope->du_file_count; ++fi)
                if (ncdb_varint_encode_uint64((uint64_t)scope->du_files[fi], tree_out) != 0) return -1;
        }
        if (presence & NCDB_PRESENCE_TYPED_PROPS) {
            if (ncdb_prop_table_serialize(&scope->props, strings, tree_out) != 0) return -1;
            db->feature_flags |= NCDB_FEATURE_TYPED_PROPS;
        }
        if (ncdb_varint_encode_uint64(scope->child_count, tree_out) != 0 || ncdb_varint_encode_uint64(scope->cover_count, tree_out) != 0) return -1;
        if (scope->cover_count > 0) {
            int ext = (presence & NCDB_PRESENCE_EXT_COVERS) != 0;
            if (ncdb_varint_encode_uint64(child_cover_type, tree_out) != 0) return -1;
            for (i = 0; i < scope->cover_count; i++) {
                ncdbCoverT cv = scope->covers[i];
                int ci_name = ncdb_strings_add(strings, cv->name);
                if (ci_name < 0 || ncdb_varint_encode_uint64((uint64_t)ci_name, tree_out) != 0) return -1;
                if (append_count(counts_tmp, cv->count) != 0) return -1;
                if (ext) {
                    uint8_t cp = 0;
                    if (cv->source_path)              cp |= NCDB_COVER_PRESENCE_SOURCE;
                    if (cv->weight != 0)              cp |= NCDB_COVER_PRESENCE_WEIGHT;
                    if (cv->goal != -1)               cp |= NCDB_COVER_PRESENCE_GOAL;
                    if (cv->comment && cv->comment[0]) cp |= NCDB_COVER_PRESENCE_COMMENT;
                    if (cv->at_least != ncdb_cover_default_at_least(cv->type))
                                                       cp |= NCDB_COVER_PRESENCE_AT_LEAST;
                    if (cv->props.count > 0)          cp |= NCDB_COVER_PRESENCE_TYPED_PROPS;
                    if (ncdb_impl_buf_append(tree_out, &cp, 1) != 0) return -1;
                    if (cp & NCDB_COVER_PRESENCE_SOURCE) {
                        int sid = ncdb_impl_get_source_id(db, cv->source_path, 1);
                        if (sid < 0) return -1;
                        if (ncdb_varint_encode_uint64((uint64_t)sid, tree_out) != 0 ||
                            ncdb_varint_encode_uint64(cv->source_line, tree_out) != 0 ||
                            ncdb_varint_encode_uint64(cv->source_token, tree_out) != 0) return -1;
                    }
                    if (cp & NCDB_COVER_PRESENCE_WEIGHT)
                        if (ncdb_varint_encode_uint64(cv->weight, tree_out) != 0) return -1;
                    if (cp & NCDB_COVER_PRESENCE_GOAL)
                        if (ncdb_varint_encode_uint64((uint64_t)cv->goal, tree_out) != 0) return -1;
                    if (cp & NCDB_COVER_PRESENCE_COMMENT) {
                        int cr = ncdb_strings_add(strings, cv->comment);
                        if (cr < 0 || ncdb_varint_encode_uint64((uint64_t)cr, tree_out) != 0) return -1;
                    }
                    if (cp & NCDB_COVER_PRESENCE_AT_LEAST)
                        if (ncdb_varint_encode_uint64(cv->at_least, tree_out) != 0) return -1;
                    if (cp & NCDB_COVER_PRESENCE_TYPED_PROPS) {
                        if (ncdb_prop_table_serialize(&cv->props, strings, tree_out) != 0) return -1;
                        db->feature_flags |= NCDB_FEATURE_TYPED_PROPS;
                    }
                }
            }
        }
        for (i = 0; i < scope->child_count; i++) {
            if (write_scope(db, scope->children[i], strings, tree_out, counts_tmp) != 0) return -1;
        }
    }
    return 0;
}

/* Phase 4.7 / M7 — scope_tree.bin trailer with DU-name → DFS-idx
 * sorted index. The trailer is appended after the main DFS-record
 * stream, followed by an 8-byte LE trailer-start offset and a final
 * "NTRL" magic. Readers detect the trailer by checking the last 4
 * bytes for the magic. */

#define NCDB_SCOPE_TRAILER_MAGIC      "NTRL"
#define NCDB_SCOPE_TRAILER_MAGIC_SZ   4U
#define NCDB_SCOPE_TRAILER_VERSION    1U

#define NCDB_DU_TYPE_MASK ( \
    NCDB_SCOPE_DU_MODULE | NCDB_SCOPE_DU_ARCH | NCDB_SCOPE_DU_PACKAGE | \
    NCDB_SCOPE_DU_PROGRAM | NCDB_SCOPE_DU_INTERFACE)

typedef struct {
    uint64_t name_str_idx;
    uint64_t dfs_idx;
    ncdbScopeT scope;            /* not serialized; resolution helper */
    const char *name_for_sort;   /* not serialized; just for qsort */
} du_index_entry_t;

static int compare_du_entry(const void *a, const void *b) {
    const du_index_entry_t *aa = (const du_index_entry_t *)a;
    const du_index_entry_t *bb = (const du_index_entry_t *)b;
    return strcmp(aa->name_for_sort, bb->name_for_sort);
}

static void collect_dus(ncdbScopeT s, du_index_entry_t **arr,
                        size_t *count, size_t *cap)
{
    size_t i;
    if (!s) return;
    if (s->type & NCDB_DU_TYPE_MASK) {
        if (*count >= *cap) {
            size_t nc = *cap ? *cap * 2 : 8;
            du_index_entry_t *na = (du_index_entry_t *)realloc(*arr, nc * sizeof(*na));
            if (!na) return;
            *arr = na;
            *cap = nc;
        }
        (*arr)[*count].scope         = s;
        (*arr)[*count].name_for_sort = s->name ? s->name : "";
        (*arr)[(*count)++].dfs_idx   = 0;  /* filled below */
    }
    for (i = 0; i < s->child_count; i++) collect_dus(s->children[i], arr, count, cap);
}

static int write_scope_trailer(ncdbT db, ncdbStringTable *strings, ncdbBuf *tree_out) {
    du_index_entry_t *entries = NULL;
    size_t cnt = 0, cap = 0, i;
    size_t trailer_offset;
    uint8_t version = NCDB_SCOPE_TRAILER_VERSION, reserved = 0;
    uint8_t footer_bytes[12];
    int rc = -1;

    for (i = 0; i < db->root_count; i++) collect_dus(db->roots[i], &entries, &cnt, &cap);
    if (cnt == 0) { free(entries); return 0; }  /* no DUs → no trailer */

    for (i = 0; i < cnt; i++) {
        size_t dfs;
        if (ncdb_impl_scope_dfs_index(db, entries[i].scope, &dfs) != 0) {
            free(entries); return -1;
        }
        entries[i].dfs_idx      = (uint64_t)dfs;
        entries[i].name_str_idx = (uint64_t)ncdb_strings_add(strings,
                                   entries[i].name_for_sort);
    }
    qsort(entries, cnt, sizeof(*entries), compare_du_entry);

    trailer_offset = tree_out->size;

    if (ncdb_impl_buf_append(tree_out, NCDB_SCOPE_TRAILER_MAGIC, NCDB_SCOPE_TRAILER_MAGIC_SZ) != 0) goto cleanup;
    if (ncdb_impl_buf_append(tree_out, &version, 1) != 0) goto cleanup;
    if (ncdb_impl_buf_append(tree_out, &reserved, 1) != 0) goto cleanup;
    if (ncdb_varint_encode_uint64((uint64_t)cnt, tree_out) != 0) goto cleanup;
    for (i = 0; i < cnt; i++) {
        if (ncdb_varint_encode_uint64(entries[i].name_str_idx, tree_out) != 0) goto cleanup;
        if (ncdb_varint_encode_uint64(entries[i].dfs_idx,      tree_out) != 0) goto cleanup;
    }
    /* Footer: 8-byte LE trailer-start offset + 4-byte magic. */
    {
        int j;
        for (j = 0; j < 8; j++) footer_bytes[j] = (uint8_t)(trailer_offset >> (j * 8));
        memcpy(footer_bytes + 8, NCDB_SCOPE_TRAILER_MAGIC, NCDB_SCOPE_TRAILER_MAGIC_SZ);
    }
    if (ncdb_impl_buf_append(tree_out, footer_bytes, sizeof(footer_bytes)) != 0) goto cleanup;
    db->feature_flags |= NCDB_FEATURE_SCOPE_TRAILER;
    rc = 0;

cleanup:
    free(entries);
    return rc;
}

ncdbScopeT ncdb_match_du_by_name(ncdbT db, const char *name)
{
    size_t lo, hi, mid;
    if (!db || !name || db->du_lookup_count == 0) return NULL;
    lo = 0; hi = db->du_lookup_count;
    while (lo < hi) {
        int cmp;
        mid = lo + (hi - lo) / 2;
        cmp = strcmp(db->du_lookup[mid].name, name);
        if      (cmp < 0) lo = mid + 1;
        else if (cmp > 0) hi = mid;
        else              return db->du_lookup[mid].scope;
    }
    return NULL;
}

int ncdb_scope_tree_serialize(ncdbT db, ncdbStringTable *strings, ncdbBuf *tree_out, ncdbBuf *counts_tmp) {
    size_t i;
    for (i = 0; i < db->root_count; i++) {
        if (write_scope(db, db->roots[i], strings, tree_out, counts_tmp) != 0) {
            return -1;
        }
    }
    /* Phase 4.7 / M7 — append the optional DU trailer. */
    return write_scope_trailer(db, strings, tree_out);
}

typedef struct {
    ncdbScopeT scope;
    uint64_t   du_dfs_idx;
} pending_du_link_t;

typedef struct {
    ncdbT db;
    const uint8_t *data;
    size_t size;
    const ncdbStringTable *strings;
    const uint64_t *counts;
    size_t count_count;
    size_t count_idx;
    char *errbuf;
    size_t errbuf_sz;
    /* Deferred DU-link fixups (instances may reference DUs not yet read). */
    pending_du_link_t *pending;
    size_t pending_count;
    size_t pending_cap;
    /* Per-record scratch */
    int64_t pending_du_link;
    int64_t pending_du_sig;
    int64_t pending_expr_terms;
    uint32_t *pending_du_files;     /* malloc'd; transferred to scope */
    size_t   pending_du_file_count;
} read_ctx;

static uint64_t next_count(read_ctx *ctx) {
    if (ctx->count_idx >= ctx->count_count) {
        return 0;
    }
    return ctx->counts[ctx->count_idx++];
}

static int read_scope(read_ctx *ctx, size_t *offset, ncdbScopeT parent) {
    uint8_t marker;
    uint64_t name_ref;
    if (*offset >= ctx->size) return -1;
    marker = ctx->data[(*offset)++];
    if (marker == NCDB_SCOPE_MARKER_TOGGLE_PAIR) {
        ncdbScopeT scope;
        if (ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &name_ref) != 0) return -1;
        scope = ncdb_impl_create_scope(ctx->db, parent, NCDB_SCOPE_BRANCH, ncdb_strings_get(ctx->strings, name_ref));
        if (!scope) return -1;
        if (!ncdb_impl_create_cover(ctx->db, scope, NCDB_COVER_TOGGLEBIN, NCDB_TOGGLE_BIN_0_TO_1, next_count(ctx))) return -1;
        if (!ncdb_impl_create_cover(ctx->db, scope, NCDB_COVER_TOGGLEBIN, NCDB_TOGGLE_BIN_1_TO_0, next_count(ctx))) return -1;
        return 0;
    } else if (marker == NCDB_SCOPE_MARKER_REGULAR) {
        uint64_t scope_type, presence, num_children, num_coveritems;
        uint64_t flags = 0, file_id = 0, line = 0, token = 0, weight = 1, at_least = 0, goal = (uint64_t)-1, source_type = NCDB_SOURCE_NONE;
        uint64_t child_cover_type = 0;
        size_t i;
        ncdbScopeT scope;
        if (ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &scope_type) != 0 ||
            ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &name_ref) != 0 ||
            ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &presence) != 0) return -1;
        if (presence & NCDB_PRESENCE_FLAGS) if (ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &flags) != 0) return -1;
        if (presence & NCDB_PRESENCE_SOURCE) {
            if (ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &file_id) != 0 ||
                ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &line) != 0 ||
                ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &token) != 0) return -1;
        }
        if (presence & NCDB_PRESENCE_WEIGHT) if (ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &weight) != 0) return -1;
        if (presence & NCDB_PRESENCE_AT_LEAST) if (ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &at_least) != 0) return -1;
        if (presence & NCDB_PRESENCE_GOAL) if (ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &goal) != 0) return -1;
        if (presence & NCDB_PRESENCE_SOURCE_TYPE) if (ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &source_type) != 0) return -1;
        {
            uint64_t du_idx_v = 0, du_sig_ref = 0;
            int has_du_link = (presence & NCDB_PRESENCE_DU_LINK) != 0;
            int has_du_sig  = (presence & NCDB_PRESENCE_DU_SIG)  != 0;
            if (has_du_link) if (ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &du_idx_v)  != 0) return -1;
            if (has_du_sig)  if (ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &du_sig_ref) != 0) return -1;
            ctx->pending_du_link = has_du_link ? (int64_t)du_idx_v : -1;
            ctx->pending_du_sig  = has_du_sig  ? (int64_t)du_sig_ref : -1;
        }
        {
            uint64_t et_ref = 0;
            int has_et = (presence & NCDB_PRESENCE_EXPR_TERMS) != 0;
            if (has_et) if (ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &et_ref) != 0) return -1;
            ctx->pending_expr_terms = has_et ? (int64_t)et_ref : -1;
        }
        if (presence & NCDB_PRESENCE_DU_FILES) {
            uint64_t fc, fi, fid;
            if (ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &fc) != 0) return -1;
            free(ctx->pending_du_files);
            ctx->pending_du_files = (fc > 0) ? (uint32_t *)malloc((size_t)fc * sizeof(uint32_t)) : NULL;
            if (fc > 0 && !ctx->pending_du_files) return -1;
            ctx->pending_du_file_count = (size_t)fc;
            for (fi = 0; fi < fc; ++fi) {
                if (ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &fid) != 0) return -1;
                ctx->pending_du_files[fi] = (uint32_t)fid;
            }
        } else {
            free(ctx->pending_du_files);
            ctx->pending_du_files = NULL;
            ctx->pending_du_file_count = 0;
        }
        /* Phase 4.2 / M2: scope-level typed-property block lands here
         * (between DU_FILES and the child/cover counts), matching the
         * writer. Must be deserialized into a *temporary* table because
         * the scope it belongs to hasn't been created yet — transferred
         * into the new scope below. */
        ncdb_prop_table_t pending_scope_props;
        ncdb_prop_table_init(&pending_scope_props);
        if (presence & NCDB_PRESENCE_TYPED_PROPS) {
            if (ncdb_prop_table_deserialize(&pending_scope_props, ctx->data,
                                            ctx->size, offset, ctx->strings) != 0) {
                ncdb_prop_table_free(&pending_scope_props);
                return -1;
            }
        }
        if (ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &num_children) != 0 ||
            ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &num_coveritems) != 0) {
            ncdb_prop_table_free(&pending_scope_props);
            return -1;
        }
        if (num_coveritems > 0 && ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &child_cover_type) != 0) {
            ncdb_prop_table_free(&pending_scope_props);
            return -1;
        }
        scope = ncdb_impl_create_scope(ctx->db, parent, scope_type, ncdb_strings_get(ctx->strings, name_ref));
        if (!scope) { ncdb_prop_table_free(&pending_scope_props); return -1; }
        /* Transfer ownership of the deserialized props into the scope. */
        scope->props = pending_scope_props;
        scope->flags = flags;
        scope->weight = weight;
        scope->goal = (presence & NCDB_PRESENCE_GOAL) ? (int64_t)goal : -1;
        scope->source_type = (int)source_type;
        if ((presence & NCDB_PRESENCE_SOURCE) && file_id < ctx->db->source_count) {
            scope->source_path = ncdb_impl_strdup(ctx->db->sources[file_id]);
            scope->source_line = line;
            scope->source_token = token;
        }
        if (ctx->pending_du_sig >= 0) {
            const char *sig = ncdb_strings_get(ctx->strings, (uint64_t)ctx->pending_du_sig);
            if (sig) scope->du_signature = ncdb_impl_strdup(sig);
        }
        if (ctx->pending_expr_terms >= 0) {
            const char *et = ncdb_strings_get(ctx->strings, (uint64_t)ctx->pending_expr_terms);
            if (et) scope->expr_terms = ncdb_impl_strdup(et);
        }
        if (ctx->pending_du_files) {
            scope->du_files = ctx->pending_du_files;
            scope->du_file_count = ctx->pending_du_file_count;
            scope->du_file_cap   = ctx->pending_du_file_count;
            ctx->pending_du_files = NULL;
            ctx->pending_du_file_count = 0;
        }
        if (ctx->pending_du_link >= 0) {
            /* Defer until end of pass — DU may not yet be deserialized. */
            if (ctx->pending_count == ctx->pending_cap) {
                size_t nc = ctx->pending_cap ? ctx->pending_cap * 2U : 8U;
                pending_du_link_t *na = (pending_du_link_t *)realloc(ctx->pending, nc * sizeof(*na));
                if (!na) return -1;
                ctx->pending = na; ctx->pending_cap = nc;
            }
            ctx->pending[ctx->pending_count].scope = scope;
            ctx->pending[ctx->pending_count].du_dfs_idx = (uint64_t)ctx->pending_du_link;
            ctx->pending_count++;
        }
        ctx->pending_du_link = -1;
        ctx->pending_du_sig  = -1;
        ctx->pending_expr_terms = -1;
        for (i = 0; i < (size_t)num_coveritems; i++) {
            uint64_t ci_name_ref;
            ncdbCoverT c;
            if (ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &ci_name_ref) != 0) return -1;
            c = ncdb_impl_create_cover(ctx->db, scope, child_cover_type, ncdb_strings_get(ctx->strings, ci_name_ref), next_count(ctx));
            if (!c) return -1;
            c->flags = ncdb_cover_default_flags(child_cover_type);
            c->at_least = (presence & NCDB_PRESENCE_AT_LEAST) ? at_least : ncdb_cover_default_at_least(child_cover_type);
            if (presence & NCDB_PRESENCE_EXT_COVERS) {
                uint8_t cp;
                uint64_t sid, line2, token2, w, g, cref, al;
                if (*offset + 1 > ctx->size) return -1;
                cp = ctx->data[(*offset)++];
                if (cp & NCDB_COVER_PRESENCE_SOURCE) {
                    if (ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &sid) != 0 ||
                        ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &line2) != 0 ||
                        ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &token2) != 0) return -1;
                    if (sid < ctx->db->source_count) {
                        c->source_path = ncdb_impl_strdup(ctx->db->sources[sid]);
                        c->source_line = line2;
                        c->source_token = token2;
                    }
                }
                if (cp & NCDB_COVER_PRESENCE_WEIGHT) {
                    if (ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &w) != 0) return -1;
                    c->weight = w;
                }
                if (cp & NCDB_COVER_PRESENCE_GOAL) {
                    if (ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &g) != 0) return -1;
                    c->goal = (int64_t)g;
                }
                if (cp & NCDB_COVER_PRESENCE_COMMENT) {
                    const char *s;
                    if (ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &cref) != 0) return -1;
                    s = ncdb_strings_get(ctx->strings, cref);
                    if (s) c->comment = ncdb_impl_strdup(s);
                }
                if (cp & NCDB_COVER_PRESENCE_AT_LEAST) {
                    if (ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &al) != 0) return -1;
                    c->at_least = al;
                }
                if (cp & NCDB_COVER_PRESENCE_TYPED_PROPS) {
                    /* Phase 4.2 / M2 — per-cover typed-property block. */
                    if (ncdb_prop_table_deserialize(&c->props, ctx->data,
                                                    ctx->size, offset,
                                                    ctx->strings) != 0) return -1;
                }
            }
        }
        for (i = 0; i < (size_t)num_children; i++) {
            if (read_scope(ctx, offset, scope) != 0) return -1;
        }
        return 0;
    }
    snprintf(ctx->errbuf, ctx->errbuf_sz, "%s", "unknown scope marker");
    return -1;
}

int ncdb_scope_tree_deserialize(ncdbT db, const uint8_t *data, size_t size, const ncdbStringTable *strings, const uint64_t *counts, size_t count_count, char *errbuf, size_t errbuf_sz) {
    read_ctx ctx;
    size_t off = 0, i;
    size_t main_size = size;
    size_t trailer_off = 0;
    int    has_trailer = 0;
    int rc = -1;
    memset(&ctx, 0, sizeof(ctx));
    ctx.db = db;
    ctx.data = data;
    ctx.size = size;
    ctx.strings = strings;
    ctx.counts = counts;
    ctx.count_count = count_count;
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;
    ctx.pending_du_link = -1;
    ctx.pending_du_sig  = -1;
    ctx.pending_expr_terms = -1;

    /* Phase 4.7 / M7: detect the optional NTRL trailer by checking the
     * last 4 bytes for the magic. The 8 bytes immediately before are
     * the trailer-start offset (LE). */
    if (size >= 12 &&
        memcmp(data + size - 4, NCDB_SCOPE_TRAILER_MAGIC, NCDB_SCOPE_TRAILER_MAGIC_SZ) == 0)
    {
        int j;
        uint64_t off_le = 0;
        for (j = 0; j < 8; j++) off_le |= ((uint64_t)data[size - 12 + j]) << (j * 8);
        if (off_le < size - 12) {
            trailer_off = (size_t)off_le;
            has_trailer = 1;
            /* Cap the main read so it doesn't try to parse the trailer
             * bytes as scope records. */
            main_size = trailer_off;
            ctx.size  = main_size;
        }
    }

    while (off < main_size) {
        if (read_scope(&ctx, &off, NULL) != 0) {
            if (errbuf && errbuf_sz && !errbuf[0]) {
                snprintf(errbuf, errbuf_sz, "%s", "failed to parse scope tree");
            }
            goto cleanup;
        }
    }
    /* Resolve deferred DU links */
    for (i = 0; i < ctx.pending_count; ++i) {
        ncdbScopeT du = ncdb_impl_scope_by_dfs_index(db, (size_t)ctx.pending[i].du_dfs_idx);
        if (du) ctx.pending[i].scope->du_scope = du;
    }

    /* Parse the trailer's DU index (if present), populating db->du_lookup
     * for bsearch-able ucis_MatchDU. */
    if (has_trailer) {
        size_t toff = trailer_off;
        ctx.size = size;  /* restore full buffer for trailer reads */
        if (toff + NCDB_SCOPE_TRAILER_MAGIC_SZ + 2 > size - 12) goto trailer_skip;
        if (memcmp(data + toff, NCDB_SCOPE_TRAILER_MAGIC, NCDB_SCOPE_TRAILER_MAGIC_SZ) != 0) goto trailer_skip;
        toff += NCDB_SCOPE_TRAILER_MAGIC_SZ;
        if (data[toff++] != NCDB_SCOPE_TRAILER_VERSION) goto trailer_skip;
        toff++; /* reserved */
        {
            uint64_t n_du;
            if (ncdb_varint_decode_uint64(data, size - 12, &toff, &n_du) != 0) goto trailer_skip;
            if (n_du > 0) {
                free(db->du_lookup);
                db->du_lookup = (struct ncdb_du_lookup_s *)calloc((size_t)n_du, sizeof(*db->du_lookup));
                if (!db->du_lookup) goto trailer_skip;
                db->du_lookup_count = (size_t)n_du;
                for (i = 0; i < (size_t)n_du; i++) {
                    uint64_t name_idx, dfs;
                    const char *nm;
                    if (ncdb_varint_decode_uint64(data, size - 12, &toff, &name_idx) != 0) goto trailer_skip;
                    if (ncdb_varint_decode_uint64(data, size - 12, &toff, &dfs)       != 0) goto trailer_skip;
                    nm = ncdb_strings_get(strings, name_idx);
                    db->du_lookup[i].name  = ncdb_impl_strdup(nm ? nm : "");
                    db->du_lookup[i].scope = ncdb_impl_scope_by_dfs_index(db, (size_t)dfs);
                }
            }
        }
trailer_skip: ;
    }
    rc = 0;
cleanup:
    free(ctx.pending);
    free(ctx.pending_du_files);
    return rc;
}
