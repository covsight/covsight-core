/*
 * toggle.bin — TOGGLE-scope metadata (binary v1).
 *
 * Format:
 *   magic[4]                  "NTGL"
 *   version: u8               = 1
 *   record_count: varint
 *     for each:
 *       scope_dfs_idx: varint
 *       presence: u8          bit0=canonical, bit1=metric, bit2=type, bit3=dir
 *       [canonical: len-prefixed string]
 *       [metric: u8]
 *       [type:   u8]
 *       [dir:    u8]
 *
 * Sparse: only TOGGLE scopes with at least one non-default value are emitted.
 * Reader silently skips payloads without the magic (forward-compat for legacy
 * JSON content written by older Python writers).
 */

#include <stdio.h>
#include <string.h>

#include "ncdb_toggle.h"

#define NCDB_TOGGLE_MAGIC    "NTGL"
#define NCDB_TOGGLE_MAGIC_SZ 4U
#define NCDB_TOGGLE_VERSION  1U

#define BIT_CANON  0x01U
#define BIT_METRIC 0x02U
#define BIT_TYPE   0x04U
#define BIT_DIR    0x08U

static int write_lenprefixed_str(ncdbBuf *b, const char *s) {
    size_t len = s ? strlen(s) : 0;
    if (ncdb_varint_encode_uint64((uint64_t)len, b) != 0) return -1;
    if (len > 0 && ncdb_impl_buf_append(b, s, len) != 0) return -1;
    return 0;
}

static int read_lenprefixed_str(const uint8_t *data, size_t size, size_t *off, char **out_str) {
    uint64_t len;
    char *buf;
    if (ncdb_varint_decode_uint64(data, size, off, &len) != 0) return -1;
    if (*off + len > size) return -1;
    buf = (char *)malloc((size_t)len + 1U);
    if (!buf) return -1;
    if (len > 0) memcpy(buf, data + *off, (size_t)len);
    buf[len] = '\0';
    *off += (size_t)len;
    free(*out_str);
    *out_str = buf;
    return 0;
}

static uint8_t build_presence(ncdbScopeT s) {
    uint8_t p = 0;
    if (s->toggle_canonical && s->toggle_canonical[0] != '\0' &&
        (s->name == NULL || strcmp(s->toggle_canonical, s->name) != 0))
        p |= BIT_CANON;
    if (s->toggle_metric != 0) p |= BIT_METRIC;
    if (s->toggle_type   != 0) p |= BIT_TYPE;
    if (s->toggle_dir    != 0) p |= BIT_DIR;
    return p;
}

int ncdb_toggle_serialize(ncdbT db, ncdbBuf *out) {
    ncdbScopeT *flat = NULL;
    size_t total = 0, i, records = 0;
    uint8_t version = NCDB_TOGGLE_VERSION;
    int rc = -1;

    if (!db || !out) return -1;

    if (ncdb_impl_dfs_flatten(db, &flat, &total) != 0) return -1;

    for (i = 0; i < total; ++i) {
        if (flat[i]->type != NCDB_SCOPE_TOGGLE) continue;
        if (build_presence(flat[i]) != 0) records++;
    }
    if (records == 0) { rc = 0; goto cleanup; }

    if (ncdb_impl_buf_append(out, NCDB_TOGGLE_MAGIC, NCDB_TOGGLE_MAGIC_SZ) != 0) goto cleanup;
    if (ncdb_impl_buf_append(out, &version, 1) != 0) goto cleanup;
    if (ncdb_varint_encode_uint64((uint64_t)records, out) != 0) goto cleanup;

    for (i = 0; i < total; ++i) {
        uint8_t p;
        if (flat[i]->type != NCDB_SCOPE_TOGGLE) continue;
        p = build_presence(flat[i]);
        if (p == 0) continue;
        if (ncdb_varint_encode_uint64((uint64_t)i, out) != 0) goto cleanup;
        if (ncdb_impl_buf_append(out, &p, 1) != 0) goto cleanup;
        if ((p & BIT_CANON)  && write_lenprefixed_str(out, flat[i]->toggle_canonical) != 0) goto cleanup;
        if ((p & BIT_METRIC) && ncdb_impl_buf_append(out, &flat[i]->toggle_metric, 1) != 0) goto cleanup;
        if ((p & BIT_TYPE)   && ncdb_impl_buf_append(out, &flat[i]->toggle_type,   1) != 0) goto cleanup;
        if ((p & BIT_DIR)    && ncdb_impl_buf_append(out, &flat[i]->toggle_dir,    1) != 0) goto cleanup;
    }
    rc = 0;
cleanup:
    free(flat);
    return rc;
}

int ncdb_toggle_deserialize(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz) {
    size_t off = 0, total = 0, i;
    uint8_t version;
    uint64_t n, k;
    ncdbScopeT *flat = NULL;
    int rc = -1;
    (void)errbuf; (void)errbuf_sz;

    if (!db) return -1;
    if (size < NCDB_TOGGLE_MAGIC_SZ + 1) return 0;
    if (memcmp(data, NCDB_TOGGLE_MAGIC, NCDB_TOGGLE_MAGIC_SZ) != 0) return 0;
    off = NCDB_TOGGLE_MAGIC_SZ;
    if (off >= size) return -1;
    version = data[off++];
    if (version != NCDB_TOGGLE_VERSION) return 0;

    if (ncdb_impl_dfs_flatten(db, &flat, &total) != 0) return -1;
    if (ncdb_varint_decode_uint64(data, size, &off, &n) != 0) goto cleanup;

    for (i = 0; i < n; ++i) {
        uint8_t p;
        ncdbScopeT s;
        if (ncdb_varint_decode_uint64(data, size, &off, &k) != 0) goto cleanup;
        if (off + 1 > size) goto cleanup;
        p = data[off++];
        if (k >= total) goto cleanup;
        s = flat[k];
        if (p & BIT_CANON) {
            if (read_lenprefixed_str(data, size, &off, &s->toggle_canonical) != 0) goto cleanup;
        }
        if (p & BIT_METRIC) { if (off + 1 > size) goto cleanup; s->toggle_metric = data[off++]; }
        if (p & BIT_TYPE)   { if (off + 1 > size) goto cleanup; s->toggle_type   = data[off++]; }
        if (p & BIT_DIR)    { if (off + 1 > size) goto cleanup; s->toggle_dir    = data[off++]; }
    }
    rc = 0;
cleanup:
    free(flat);
    return rc;
}
