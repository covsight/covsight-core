#include <stdio.h>

#include "ncdb_counts.h"

static size_t varint_size(uint64_t v) {
    size_t n = 1;
    while (v >= 0x80U) {
        v >>= 7;
        n++;
    }
    return n;
}

int ncdb_counts_serialize(const uint64_t *counts, size_t count, ncdbBuf *out) {
    size_t i;
    size_t vbytes = 0;
    size_t fixed = count * 4U;
    int has_large = 0;
    uint8_t mode;

    for (i = 0; i < count; i++) {
        vbytes += varint_size(counts[i]);
        if (counts[i] > 0xFFFFFFFFULL) {
            has_large = 1;
        }
    }
    mode = (has_large || vbytes < fixed) ? NCDB_COUNTS_MODE_VARINT : NCDB_COUNTS_MODE_UINT32;
    if (ncdb_impl_buf_append(out, &mode, 1) != 0 || ncdb_varint_encode_uint64(count, out) != 0) {
        return -1;
    }
    if (mode == NCDB_COUNTS_MODE_VARINT) {
        return ncdb_varint_encode_many(counts, count, out);
    }
    for (i = 0; i < count; i++) {
        uint32_t v = (uint32_t)counts[i];
        uint8_t bytes[4];
        bytes[0] = (uint8_t)(v & 0xFFU);
        bytes[1] = (uint8_t)((v >> 8) & 0xFFU);
        bytes[2] = (uint8_t)((v >> 16) & 0xFFU);
        bytes[3] = (uint8_t)((v >> 24) & 0xFFU);
        if (ncdb_impl_buf_append(out, bytes, 4) != 0) {
            return -1;
        }
    }
    return 0;
}

int ncdb_counts_deserialize(const uint8_t *data, size_t size, uint64_t **counts, size_t *count, char *errbuf, size_t errbuf_sz) {
    size_t off = 0;
    uint8_t mode;
    uint64_t n64 = 0;
    uint64_t *vals;
    size_t i;
    if (size == 0) {
        *counts = NULL;
        *count = 0;
        return 0;
    }
    mode = data[off++];
    if (ncdb_varint_decode_uint64(data, size, &off, &n64) != 0) {
        snprintf(errbuf, errbuf_sz, "%s", "invalid counts header");
        return -1;
    }
    vals = (uint64_t *)calloc((size_t)n64 ? (size_t)n64 : 1U, sizeof(uint64_t));
    if (!vals) {
        snprintf(errbuf, errbuf_sz, "%s", "out of memory");
        return -1;
    }
    if (mode == NCDB_COUNTS_MODE_VARINT) {
        if (ncdb_varint_decode_many(data, size, &off, (size_t)n64, vals) != 0) {
            free(vals);
            snprintf(errbuf, errbuf_sz, "%s", "invalid varint counts payload");
            return -1;
        }
    } else if (mode == NCDB_COUNTS_MODE_UINT32) {
        if (off + ((size_t)n64 * 4U) > size) {
            free(vals);
            snprintf(errbuf, errbuf_sz, "%s", "invalid uint32 counts payload");
            return -1;
        }
        for (i = 0; i < (size_t)n64; i++) {
            vals[i] = (uint64_t)data[off] |
                ((uint64_t)data[off + 1U] << 8) |
                ((uint64_t)data[off + 2U] << 16) |
                ((uint64_t)data[off + 3U] << 24);
            off += 4;
        }
    } else {
        free(vals);
        snprintf(errbuf, errbuf_sz, "%s", "unknown counts mode");
        return -1;
    }
    *counts = vals;
    *count = (size_t)n64;
    return 0;
}
