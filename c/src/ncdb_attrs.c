/*
 * attrs.bin — typed attribute serialization (binary v3).
 *
 * Format:
 *   magic[4]               "NATR"
 *   version: u8            = 1
 *   db_attr_count: varint  followed by N entries
 *   scope_count:   varint  followed by N records (dfs_idx, attr_count, entries)
 *   cover_count:   varint  followed by N records (dfs_scope_idx, cover_idx, attr_count, entries)
 *   history_count: varint  followed by N records (hist_idx, attr_count, entries)
 *
 * Entry encoding:
 *   key_len: varint, key bytes (no NUL)
 *   type:    u8
 *   payload:
 *     INT32:  zigzag varint
 *     INT64:  zigzag varint
 *     FLOAT:  4 bytes little-endian
 *     DOUBLE: 8 bytes little-endian
 *     STRING: len varint, bytes (no NUL)
 *     BYTES:  len varint, bytes
 *
 * Reader silently skips payloads lacking the magic (forward-compat: legacy
 * placeholder JSON from prior writers is treated as "no attrs").
 */

#include <stdio.h>
#include <string.h>

#include "ncdb_attrs.h"

#define NCDB_ATTRS_MAGIC    "NATR"
#define NCDB_ATTRS_MAGIC_SZ 4U
#define NCDB_ATTRS_VERSION  1U

/* ── attr_table_t helpers ──────────────────────────────────────────────── */

void ncdb_attr_table_init(ncdb_attr_table_t *t) {
    if (!t) return;
    t->entries = NULL;
    t->count = 0;
    t->cap = 0;
}

void ncdb_attr_table_free(ncdb_attr_table_t *t) {
    size_t i;
    if (!t || !t->entries) { if (t) { t->count = t->cap = 0; t->entries = NULL; } return; }
    for (i = 0; i < t->count; ++i) {
        free(t->entries[i].key);
        free(t->entries[i].str);
        free(t->entries[i].blob);
    }
    free(t->entries);
    t->entries = NULL;
    t->count = t->cap = 0;
}

static ncdb_attr_entry_t *attr_table_find(const ncdb_attr_table_t *t, const char *key) {
    size_t i;
    if (!t || !key) return NULL;
    for (i = 0; i < t->count; ++i) {
        if (t->entries[i].key && strcmp(t->entries[i].key, key) == 0)
            return &t->entries[i];
    }
    return NULL;
}

static int attr_entry_reset_payload(ncdb_attr_entry_t *e) {
    free(e->str); e->str = NULL;
    free(e->blob); e->blob = NULL;
    e->blob_size = 0;
    e->i32 = 0; e->i64 = 0; e->f32 = 0.0f; e->f64 = 0.0;
    return 0;
}

static int attr_entry_store(ncdb_attr_entry_t *e, const ncdbAttrValue *v) {
    attr_entry_reset_payload(e);
    e->type = v->type;
    switch (v->type) {
    case NCDB_ATTR_INT32:  e->i32 = v->u.i32; break;
    case NCDB_ATTR_INT64:  e->i64 = v->u.i64; break;
    case NCDB_ATTR_FLOAT:  e->f32 = v->u.f32; break;
    case NCDB_ATTR_DOUBLE: e->f64 = v->u.f64; break;
    case NCDB_ATTR_STRING:
        e->str = ncdb_impl_strdup(v->u.str.s ? v->u.str.s : "");
        if (!e->str) return -1;
        break;
    case NCDB_ATTR_BYTES:
        if (v->u.bytes.size > 0 && v->u.bytes.data) {
            e->blob = (uint8_t *)malloc(v->u.bytes.size);
            if (!e->blob) return -1;
            memcpy(e->blob, v->u.bytes.data, v->u.bytes.size);
        }
        e->blob_size = v->u.bytes.size;
        break;
    default:
        return -1;
    }
    return 0;
}

int ncdb_attr_table_set(ncdb_attr_table_t *t, const char *key, const ncdbAttrValue *v) {
    ncdb_attr_entry_t *e;
    if (!t || !key || !v) return -1;
    e = attr_table_find(t, key);
    if (e) return attr_entry_store(e, v);
    if (t->count == t->cap) {
        size_t ncap = t->cap ? t->cap * 2U : 4U;
        ncdb_attr_entry_t *na = (ncdb_attr_entry_t *)realloc(t->entries, ncap * sizeof(*na));
        if (!na) return -1;
        memset(na + t->cap, 0, (ncap - t->cap) * sizeof(*na));
        t->entries = na;
        t->cap = ncap;
    }
    e = &t->entries[t->count];
    e->key = ncdb_impl_strdup(key);
    if (!e->key) return -1;
    if (attr_entry_store(e, v) != 0) { free(e->key); e->key = NULL; return -1; }
    t->count++;
    return 0;
}

static void attr_entry_view(const ncdb_attr_entry_t *e, ncdbAttrValue *out) {
    out->type = e->type;
    switch (e->type) {
    case NCDB_ATTR_INT32:  out->u.i32 = e->i32; break;
    case NCDB_ATTR_INT64:  out->u.i64 = e->i64; break;
    case NCDB_ATTR_FLOAT:  out->u.f32 = e->f32; break;
    case NCDB_ATTR_DOUBLE: out->u.f64 = e->f64; break;
    case NCDB_ATTR_STRING: out->u.str.s = e->str ? e->str : ""; break;
    case NCDB_ATTR_BYTES:  out->u.bytes.data = e->blob; out->u.bytes.size = e->blob_size; break;
    }
}

int ncdb_attr_table_get(const ncdb_attr_table_t *t, const char *key, ncdbAttrValue *out) {
    ncdb_attr_entry_t *e = attr_table_find(t, key);
    if (!e) return -1;
    if (out) attr_entry_view(e, out);
    return 0;
}

int ncdb_attr_table_iterate(const ncdb_attr_table_t *t,
                            int (*cb)(const char *, const ncdbAttrValue *, void *),
                            void *ud) {
    size_t i;
    int visited = 0;
    if (!t || !cb) return 0;
    for (i = 0; i < t->count; ++i) {
        ncdbAttrValue v;
        attr_entry_view(&t->entries[i], &v);
        visited++;
        if (cb(t->entries[i].key, &v, ud) != 0) break;
    }
    return visited;
}

/* ── DFS scope indexing ────────────────────────────────────────────────── */

static int dfs_visit_find(ncdbScopeT s, ncdbScopeT target, size_t *idx, size_t *out) {
    size_t i;
    if (!s) return 0;
    if (s == target) { *out = *idx; return 1; }
    (*idx)++;
    for (i = 0; i < s->child_count; ++i)
        if (dfs_visit_find(s->children[i], target, idx, out)) return 1;
    return 0;
}

static int dfs_visit_at(ncdbScopeT s, size_t want, size_t *idx, ncdbScopeT *out) {
    size_t i;
    if (!s) return 0;
    if (*idx == want) { *out = s; return 1; }
    (*idx)++;
    for (i = 0; i < s->child_count; ++i)
        if (dfs_visit_at(s->children[i], want, idx, out)) return 1;
    return 0;
}

int ncdb_impl_scope_dfs_index(ncdbT db, ncdbScopeT target, size_t *out_idx) {
    size_t idx = 0, i;
    if (!db || !target || !out_idx) return -1;
    for (i = 0; i < db->root_count; ++i)
        if (dfs_visit_find(db->roots[i], target, &idx, out_idx)) return 0;
    return -1;
}

ncdbScopeT ncdb_impl_scope_by_dfs_index(ncdbT db, size_t want) {
    size_t idx = 0, i;
    ncdbScopeT out = NULL;
    if (!db) return NULL;
    for (i = 0; i < db->root_count; ++i)
        if (dfs_visit_at(db->roots[i], want, &idx, &out)) return out;
    return NULL;
}

int ncdb_impl_history_index(ncdbT db, ncdbHistoryNodeT h, size_t *out_idx) {
    size_t i;
    if (!db || !h || !out_idx) return -1;
    for (i = 0; i < db->history_count; ++i)
        if (db->history_nodes[i] == h) { *out_idx = i; return 0; }
    return -1;
}

/* ── Binary I/O primitives ─────────────────────────────────────────────── */

static int write_u8(ncdbBuf *b, uint8_t v) {
    return ncdb_impl_buf_append(b, &v, 1);
}

static int read_u8(const uint8_t *data, size_t size, size_t *off, uint8_t *out) {
    if (*off + 1 > size) return -1;
    *out = data[(*off)++];
    return 0;
}

static int write_u32le(ncdbBuf *b, uint32_t v) {
    uint8_t bytes[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    return ncdb_impl_buf_append(b, bytes, 4);
}

static int read_u32le(const uint8_t *data, size_t size, size_t *off, uint32_t *out) {
    if (*off + 4 > size) return -1;
    *out = (uint32_t)data[*off] | ((uint32_t)data[*off + 1] << 8) |
           ((uint32_t)data[*off + 2] << 16) | ((uint32_t)data[*off + 3] << 24);
    *off += 4;
    return 0;
}

static int write_u64le(ncdbBuf *b, uint64_t v) {
    uint8_t bytes[8];
    int i;
    for (i = 0; i < 8; ++i) bytes[i] = (uint8_t)(v >> (i * 8));
    return ncdb_impl_buf_append(b, bytes, 8);
}

static int read_u64le(const uint8_t *data, size_t size, size_t *off, uint64_t *out) {
    int i;
    uint64_t v = 0;
    if (*off + 8 > size) return -1;
    for (i = 0; i < 8; ++i) v |= ((uint64_t)data[*off + i]) << (i * 8);
    *off += 8;
    *out = v;
    return 0;
}

static uint64_t zigzag_encode_i64(int64_t v) { return (uint64_t)((v << 1) ^ (v >> 63)); }
static int64_t  zigzag_decode_i64(uint64_t v) { return (int64_t)((v >> 1) ^ -(int64_t)(v & 1)); }

static int write_string(ncdbBuf *b, const char *s) {
    size_t len = s ? strlen(s) : 0;
    if (ncdb_varint_encode_uint64((uint64_t)len, b) != 0) return -1;
    if (len > 0 && ncdb_impl_buf_append(b, s, len) != 0) return -1;
    return 0;
}

static int read_lenprefixed(const uint8_t *data, size_t size, size_t *off,
                            const uint8_t **out_ptr, size_t *out_len) {
    uint64_t len;
    if (ncdb_varint_decode_uint64(data, size, off, &len) != 0) return -1;
    if (*off + len > size) return -1;
    *out_ptr = data + *off;
    *out_len = (size_t)len;
    *off += len;
    return 0;
}

/* ── Entry encode/decode ───────────────────────────────────────────────── */

static int write_entry(ncdbBuf *b, const ncdb_attr_entry_t *e) {
    union { float f; uint32_t u; } cf;
    union { double d; uint64_t u; } cd;
    if (write_string(b, e->key) != 0) return -1;
    if (write_u8(b, (uint8_t)e->type) != 0) return -1;
    switch (e->type) {
    case NCDB_ATTR_INT32:
        return ncdb_varint_encode_uint64(zigzag_encode_i64((int64_t)e->i32), b);
    case NCDB_ATTR_INT64:
        return ncdb_varint_encode_uint64(zigzag_encode_i64(e->i64), b);
    case NCDB_ATTR_FLOAT:
        cf.f = e->f32; return write_u32le(b, cf.u);
    case NCDB_ATTR_DOUBLE:
        cd.d = e->f64; return write_u64le(b, cd.u);
    case NCDB_ATTR_STRING:
        return write_string(b, e->str ? e->str : "");
    case NCDB_ATTR_BYTES:
        if (ncdb_varint_encode_uint64((uint64_t)e->blob_size, b) != 0) return -1;
        if (e->blob_size > 0 && ncdb_impl_buf_append(b, e->blob, e->blob_size) != 0) return -1;
        return 0;
    }
    return -1;
}

static int read_entry_into(const uint8_t *data, size_t size, size_t *off, ncdb_attr_table_t *t) {
    const uint8_t *kp; size_t klen;
    uint8_t type;
    char keybuf_stack[128];
    char *keybuf = keybuf_stack;
    int rc = -1;
    ncdbAttrValue v;
    memset(&v, 0, sizeof(v));

    if (read_lenprefixed(data, size, off, &kp, &klen) != 0) return -1;
    if (klen + 1 > sizeof(keybuf_stack)) {
        keybuf = (char *)malloc(klen + 1);
        if (!keybuf) return -1;
    }
    memcpy(keybuf, kp, klen);
    keybuf[klen] = '\0';
    if (read_u8(data, size, off, &type) != 0) goto done;
    v.type = (ncdbAttrType)type;
    switch (v.type) {
    case NCDB_ATTR_INT32: {
        uint64_t z;
        if (ncdb_varint_decode_uint64(data, size, off, &z) != 0) goto done;
        v.u.i32 = (int32_t)zigzag_decode_i64(z);
        break;
    }
    case NCDB_ATTR_INT64: {
        uint64_t z;
        if (ncdb_varint_decode_uint64(data, size, off, &z) != 0) goto done;
        v.u.i64 = zigzag_decode_i64(z);
        break;
    }
    case NCDB_ATTR_FLOAT: {
        union { float f; uint32_t u; } c;
        if (read_u32le(data, size, off, &c.u) != 0) goto done;
        v.u.f32 = c.f;
        break;
    }
    case NCDB_ATTR_DOUBLE: {
        union { double d; uint64_t u; } c;
        if (read_u64le(data, size, off, &c.u) != 0) goto done;
        v.u.f64 = c.d;
        break;
    }
    case NCDB_ATTR_STRING: {
        const uint8_t *sp; size_t slen;
        char *sbuf;
        if (read_lenprefixed(data, size, off, &sp, &slen) != 0) goto done;
        sbuf = (char *)malloc(slen + 1);
        if (!sbuf) goto done;
        memcpy(sbuf, sp, slen);
        sbuf[slen] = '\0';
        v.u.str.s = sbuf;
        rc = ncdb_attr_table_set(t, keybuf, &v);
        free(sbuf);
        goto done;
    }
    case NCDB_ATTR_BYTES: {
        const uint8_t *bp; size_t blen;
        if (read_lenprefixed(data, size, off, &bp, &blen) != 0) goto done;
        v.u.bytes.data = bp;
        v.u.bytes.size = blen;
        rc = ncdb_attr_table_set(t, keybuf, &v);
        goto done;
    }
    default:
        goto done;
    }
    rc = ncdb_attr_table_set(t, keybuf, &v);
done:
    if (keybuf != keybuf_stack) free(keybuf);
    return rc;
}

static int write_table(ncdbBuf *b, const ncdb_attr_table_t *t) {
    size_t i;
    if (ncdb_varint_encode_uint64((uint64_t)t->count, b) != 0) return -1;
    for (i = 0; i < t->count; ++i)
        if (write_entry(b, &t->entries[i]) != 0) return -1;
    return 0;
}

static int read_table_into(const uint8_t *data, size_t size, size_t *off, ncdb_attr_table_t *t) {
    uint64_t n, i;
    if (ncdb_varint_decode_uint64(data, size, off, &n) != 0) return -1;
    for (i = 0; i < n; ++i)
        if (read_entry_into(data, size, off, t) != 0) return -1;
    return 0;
}

/* ── DFS scope walk (snapshot to flat array) ───────────────────────────── */

static void dfs_collect(ncdbScopeT s, ncdbScopeT *arr, size_t cap, size_t *idx) {
    size_t i;
    if (!s) return;
    if (*idx < cap) arr[(*idx)++] = s;
    for (i = 0; i < s->child_count; ++i)
        dfs_collect(s->children[i], arr, cap, idx);
}

static void dfs_count(ncdbScopeT s, size_t *n) {
    size_t i;
    if (!s) return;
    (*n)++;
    for (i = 0; i < s->child_count; ++i) dfs_count(s->children[i], n);
}

/* ── Public serialize / deserialize ────────────────────────────────────── */

int ncdb_attrs_serialize(ncdbT db, ncdbBuf *out) {
    size_t total = 0, i, j, idx;
    ncdbScopeT *flat = NULL;
    size_t scope_records = 0;
    size_t cover_records = 0;
    size_t history_records = 0;
    int rc = -1;

    if (!db || !out) return -1;

    if (ncdb_impl_buf_append(out, NCDB_ATTRS_MAGIC, NCDB_ATTRS_MAGIC_SZ) != 0) return -1;
    if (write_u8(out, NCDB_ATTRS_VERSION) != 0) return -1;

    /* DB-level table */
    if (write_table(out, &db->attrs) != 0) return -1;

    /* Walk scopes once to flatten */
    for (i = 0; i < db->root_count; ++i) dfs_count(db->roots[i], &total);
    if (total > 0) {
        flat = (ncdbScopeT *)malloc(total * sizeof(*flat));
        if (!flat) return -1;
        idx = 0;
        for (i = 0; i < db->root_count; ++i) dfs_collect(db->roots[i], flat, total, &idx);
    }

    /* Count scope records with non-empty attrs */
    for (i = 0; i < total; ++i)
        if (flat[i]->attrs.count > 0) scope_records++;
    if (ncdb_varint_encode_uint64((uint64_t)scope_records, out) != 0) goto cleanup;
    for (i = 0; i < total; ++i) {
        if (flat[i]->attrs.count == 0) continue;
        if (ncdb_varint_encode_uint64((uint64_t)i, out) != 0) goto cleanup;
        if (write_table(out, &flat[i]->attrs) != 0) goto cleanup;
    }

    /* Cover records */
    for (i = 0; i < total; ++i)
        for (j = 0; j < flat[i]->cover_count; ++j)
            if (flat[i]->covers[j]->attrs.count > 0) cover_records++;
    if (ncdb_varint_encode_uint64((uint64_t)cover_records, out) != 0) goto cleanup;
    for (i = 0; i < total; ++i) {
        for (j = 0; j < flat[i]->cover_count; ++j) {
            if (flat[i]->covers[j]->attrs.count == 0) continue;
            if (ncdb_varint_encode_uint64((uint64_t)i, out) != 0) goto cleanup;
            if (ncdb_varint_encode_uint64((uint64_t)j, out) != 0) goto cleanup;
            if (write_table(out, &flat[i]->covers[j]->attrs) != 0) goto cleanup;
        }
    }

    /* History records */
    for (i = 0; i < db->history_count; ++i)
        if (db->history_nodes[i]->attrs.count > 0) history_records++;
    if (ncdb_varint_encode_uint64((uint64_t)history_records, out) != 0) goto cleanup;
    for (i = 0; i < db->history_count; ++i) {
        if (db->history_nodes[i]->attrs.count == 0) continue;
        if (ncdb_varint_encode_uint64((uint64_t)i, out) != 0) goto cleanup;
        if (write_table(out, &db->history_nodes[i]->attrs) != 0) goto cleanup;
    }

    rc = 0;
cleanup:
    free(flat);
    return rc;
}

int ncdb_attrs_deserialize(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz) {
    size_t off = 0;
    uint8_t version;
    uint64_t n, k;
    size_t total = 0, i, idx;
    ncdbScopeT *flat = NULL;
    int rc = -1;
    (void)errbuf; (void)errbuf_sz;

    if (!db) return -1;
    if (size < NCDB_ATTRS_MAGIC_SZ + 1) return 0;  /* legacy/empty payload */
    if (memcmp(data, NCDB_ATTRS_MAGIC, NCDB_ATTRS_MAGIC_SZ) != 0) return 0;  /* not binary; skip */
    off = NCDB_ATTRS_MAGIC_SZ;
    if (read_u8(data, size, &off, &version) != 0) return -1;
    if (version != NCDB_ATTRS_VERSION) return 0;  /* unknown — skip safely */

    if (read_table_into(data, size, &off, &db->attrs) != 0) return -1;

    /* Flatten scopes for index lookups */
    for (i = 0; i < db->root_count; ++i) dfs_count(db->roots[i], &total);
    if (total > 0) {
        flat = (ncdbScopeT *)malloc(total * sizeof(*flat));
        if (!flat) return -1;
        idx = 0;
        for (i = 0; i < db->root_count; ++i) dfs_collect(db->roots[i], flat, total, &idx);
    }

    /* Scope attrs */
    if (ncdb_varint_decode_uint64(data, size, &off, &n) != 0) goto cleanup;
    for (i = 0; i < n; ++i) {
        if (ncdb_varint_decode_uint64(data, size, &off, &k) != 0) goto cleanup;
        if (k >= total) goto cleanup;
        if (read_table_into(data, size, &off, &flat[k]->attrs) != 0) goto cleanup;
    }

    /* Cover attrs */
    if (ncdb_varint_decode_uint64(data, size, &off, &n) != 0) goto cleanup;
    for (i = 0; i < n; ++i) {
        uint64_t s_idx, c_idx;
        if (ncdb_varint_decode_uint64(data, size, &off, &s_idx) != 0) goto cleanup;
        if (ncdb_varint_decode_uint64(data, size, &off, &c_idx) != 0) goto cleanup;
        if (s_idx >= total) goto cleanup;
        if (c_idx >= flat[s_idx]->cover_count) goto cleanup;
        if (read_table_into(data, size, &off, &flat[s_idx]->covers[c_idx]->attrs) != 0) goto cleanup;
    }

    /* History attrs */
    if (ncdb_varint_decode_uint64(data, size, &off, &n) != 0) goto cleanup;
    for (i = 0; i < n; ++i) {
        if (ncdb_varint_decode_uint64(data, size, &off, &k) != 0) goto cleanup;
        if (k >= db->history_count) goto cleanup;
        if (read_table_into(data, size, &off, &db->history_nodes[k]->attrs) != 0) goto cleanup;
    }

    rc = 0;
cleanup:
    free(flat);
    return rc;
}
