/*
 * tags.bin — sparse per-scope tag set (binary v1, magic "NTAG").
 *
 * Format:
 *   magic[4]                  "NTAG"
 *   version: u8               = 1
 *   record_count: varint
 *     for each:
 *       scope_dfs_idx: varint
 *       tag_count:    varint
 *         tag: len-prefixed string
 */

#include <stdio.h>
#include <string.h>

#include "ncdb_impl.h"

#define NCDB_TAGS_MAGIC    "NTAG"
#define NCDB_TAGS_MAGIC_SZ 4U
#define NCDB_TAGS_VERSION  1U

static int write_lp(ncdbBuf *b, const char *s) {
    size_t len = s ? strlen(s) : 0;
    if (ncdb_varint_encode_uint64((uint64_t)len, b) != 0) return -1;
    if (len > 0 && ncdb_impl_buf_append(b, s, len) != 0) return -1;
    return 0;
}

int ncdb_tags_serialize(ncdbT db, ncdbBuf *out) {
    ncdbScopeT *flat = NULL;
    size_t total = 0, i, j, records = 0;
    uint8_t version = NCDB_TAGS_VERSION;
    int rc = -1;

    if (!db || !out) return -1;
    if (ncdb_impl_dfs_flatten(db, &flat, &total) != 0) return -1;

    for (i = 0; i < total; ++i)
        if (flat[i]->tag_count > 0) records++;
    if (records == 0) { rc = 0; goto cleanup; }

    if (ncdb_impl_buf_append(out, NCDB_TAGS_MAGIC, NCDB_TAGS_MAGIC_SZ) != 0) goto cleanup;
    if (ncdb_impl_buf_append(out, &version, 1) != 0) goto cleanup;
    if (ncdb_varint_encode_uint64((uint64_t)records, out) != 0) goto cleanup;

    for (i = 0; i < total; ++i) {
        if (flat[i]->tag_count == 0) continue;
        if (ncdb_varint_encode_uint64((uint64_t)i, out) != 0) goto cleanup;
        if (ncdb_varint_encode_uint64((uint64_t)flat[i]->tag_count, out) != 0) goto cleanup;
        for (j = 0; j < flat[i]->tag_count; ++j)
            if (write_lp(out, flat[i]->tags[j]) != 0) goto cleanup;
    }
    rc = 0;
cleanup:
    free(flat);
    return rc;
}

int ncdb_tags_deserialize(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz) {
    size_t off = 0, total = 0, i, j;
    uint8_t version;
    uint64_t n, k, tc, len;
    ncdbScopeT *flat = NULL;
    int rc = -1;
    (void)errbuf; (void)errbuf_sz;

    if (!db) return -1;
    if (size < NCDB_TAGS_MAGIC_SZ + 1) return 0;
    if (memcmp(data, NCDB_TAGS_MAGIC, NCDB_TAGS_MAGIC_SZ) != 0) return 0;
    off = NCDB_TAGS_MAGIC_SZ;
    if (off >= size) return -1;
    version = data[off++];
    if (version != NCDB_TAGS_VERSION) return 0;

    if (ncdb_impl_dfs_flatten(db, &flat, &total) != 0) return -1;
    if (ncdb_varint_decode_uint64(data, size, &off, &n) != 0) goto cleanup;

    for (i = 0; i < n; ++i) {
        ncdbScopeT s;
        if (ncdb_varint_decode_uint64(data, size, &off, &k) != 0) goto cleanup;
        if (k >= total) goto cleanup;
        s = flat[k];
        if (ncdb_varint_decode_uint64(data, size, &off, &tc) != 0) goto cleanup;
        for (j = 0; j < tc; ++j) {
            char *tag;
            if (ncdb_varint_decode_uint64(data, size, &off, &len) != 0) goto cleanup;
            if (off + len > size) goto cleanup;
            tag = (char *)malloc((size_t)len + 1);
            if (!tag) goto cleanup;
            if (len > 0) memcpy(tag, data + off, (size_t)len);
            tag[len] = '\0';
            off += (size_t)len;
            if (s->tag_count == s->tag_cap) {
                size_t ncap = s->tag_cap ? s->tag_cap * 2U : 4U;
                char **na = (char **)realloc(s->tags, ncap * sizeof(*na));
                if (!na) { free(tag); goto cleanup; }
                s->tags = na;
                s->tag_cap = ncap;
            }
            s->tags[s->tag_count++] = tag;
        }
    }
    rc = 0;
cleanup:
    free(flat);
    return rc;
}
