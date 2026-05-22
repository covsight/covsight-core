/*
 * sources.json — flat source-path list (binary v1, magic "NSRC").
 *
 * Format:
 *   magic[4]    "NSRC"
 *   version: u8 = 1
 *   count: varint
 *   for each: len-prefixed UTF-8 path
 *
 * Reader falls back to legacy JSON arrays for backward compat with existing
 * fixtures. NOTE: this table is global; Phase 1.8 will move source paths to
 * per-DU tables, at which point this member becomes deprecated.
 */

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

#include "ncdb_sources.h"

#define NCDB_SRC_MAGIC    "NSRC"
#define NCDB_SRC_MAGIC_SZ 4U
#define NCDB_SRC_VERSION  1U

int ncdb_sources_serialize(ncdbT db, ncdbBuf *out) {
    uint8_t version = NCDB_SRC_VERSION;
    size_t i;
    if (ncdb_impl_buf_append(out, NCDB_SRC_MAGIC, NCDB_SRC_MAGIC_SZ) != 0) return -1;
    if (ncdb_impl_buf_append(out, &version, 1) != 0) return -1;
    if (ncdb_varint_encode_uint64((uint64_t)db->source_count, out) != 0) return -1;
    for (i = 0; i < db->source_count; ++i) {
        const char *s = db->sources[i] ? db->sources[i] : "";
        size_t len = strlen(s);
        if (ncdb_varint_encode_uint64((uint64_t)len, out) != 0) return -1;
        if (len > 0 && ncdb_impl_buf_append(out, s, len) != 0) return -1;
    }
    return 0;
}

static int deserialize_json_fallback(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz) {
    cJSON *arr = cJSON_ParseWithLength((const char *)data, size);
    cJSON *it;
    if (!arr || !cJSON_IsArray(arr)) {
        snprintf(errbuf, errbuf_sz, "%s", "invalid sources json");
        cJSON_Delete(arr);
        return -1;
    }
    cJSON_ArrayForEach(it, arr) {
        if (cJSON_IsString(it) && it->valuestring) {
            if (ncdb_impl_add_source(db, it->valuestring) < 0) {
                snprintf(errbuf, errbuf_sz, "%s", "out of memory");
                cJSON_Delete(arr); return -1;
            }
        }
    }
    cJSON_Delete(arr);
    return 0;
}

int ncdb_sources_deserialize(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz) {
    size_t off, i;
    uint8_t version;
    uint64_t n;
    if (!db) return -1;
    if (size < NCDB_SRC_MAGIC_SZ + 1 || memcmp(data, NCDB_SRC_MAGIC, NCDB_SRC_MAGIC_SZ) != 0) {
        return deserialize_json_fallback(db, data, size, errbuf, errbuf_sz);
    }
    off = NCDB_SRC_MAGIC_SZ;
    version = data[off++];
    if (version != NCDB_SRC_VERSION) {
        snprintf(errbuf, errbuf_sz, "unknown sources binary version %u", (unsigned)version);
        return -1;
    }
    if (ncdb_varint_decode_uint64(data, size, &off, &n) != 0) goto bad;
    for (i = 0; i < n; ++i) {
        uint64_t len;
        char *buf;
        if (ncdb_varint_decode_uint64(data, size, &off, &len) != 0) goto bad;
        if (off + len > size) goto bad;
        buf = (char *)malloc((size_t)len + 1);
        if (!buf) { snprintf(errbuf, errbuf_sz, "%s", "out of memory"); return -1; }
        if (len > 0) memcpy(buf, data + off, (size_t)len);
        buf[len] = '\0';
        off += (size_t)len;
        if (ncdb_impl_add_source(db, buf) < 0) {
            free(buf); snprintf(errbuf, errbuf_sz, "%s", "out of memory"); return -1;
        }
        free(buf);
    }
    return 0;
bad:
    snprintf(errbuf, errbuf_sz, "%s", "truncated sources binary");
    return -1;
}
