#include <stdio.h>

#include "ncdb_scope_tree.h"

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
        /* Phase 1.5: extended cover fields needed when any cover has source/weight/goal/comment/per-bin at_least. */
        for (i = 0; i < scope->cover_count; i++) {
            ncdbCoverT cv = scope->covers[i];
            if (cv->source_path || cv->weight != 0 || cv->goal != -1 ||
                cv->comment ||
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
                }
            }
        }
        for (i = 0; i < scope->child_count; i++) {
            if (write_scope(db, scope->children[i], strings, tree_out, counts_tmp) != 0) return -1;
        }
    }
    return 0;
}

int ncdb_scope_tree_serialize(ncdbT db, ncdbStringTable *strings, ncdbBuf *tree_out, ncdbBuf *counts_tmp) {
    size_t i;
    for (i = 0; i < db->root_count; i++) {
        if (write_scope(db, db->roots[i], strings, tree_out, counts_tmp) != 0) {
            return -1;
        }
    }
    return 0;
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
        if (ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &num_children) != 0 ||
            ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &num_coveritems) != 0) return -1;
        if (num_coveritems > 0 && ncdb_varint_decode_uint64(ctx->data, ctx->size, offset, &child_cover_type) != 0) return -1;
        scope = ncdb_impl_create_scope(ctx->db, parent, scope_type, ncdb_strings_get(ctx->strings, name_ref));
        if (!scope) return -1;
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
    while (off < size) {
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
    rc = 0;
cleanup:
    free(ctx.pending);
    free(ctx.pending_du_files);
    return rc;
}
