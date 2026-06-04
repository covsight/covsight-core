/*
 * ncdb_unique_id.c — UCIS unique-ID computation, xxh64, and the NUID
 * index member (Phase 4.3 / M3).
 *
 * UCIS §5.4 specifies that a scope's unique ID is constructed on demand
 * from its hierarchical path. Each component is `<type_log2>:<name>`,
 * separated by the database's path separator. The leading separator
 * marks an absolute UID; type_log2 is the bit-position of the scope
 * type's one-hot bit (e.g. UCIS_INSTANCE=0x10 → "4").
 *
 * NUID layout (this file's `serialize`):
 *
 *   magic[4]       "NUID"
 *   version: u8    = 1
 *   flags:   u8    = bit0 case_aware; bit1 alias_table_present
 *   n_scopes: varint
 *   for i in 0..n_scopes:                       (sorted by uid_hash)
 *     uid_hash: u64 LE
 *     dfs_idx:  varint
 *   if alias_table_present:
 *     n_aliases: varint
 *     for j in 0..n_aliases:
 *       alias_hash: u64 LE
 *       dfs_idx:    varint
 *
 * Aliases (UCIS_STR_UNIQUE_ID_ALIAS) are not yet populated by the
 * writer surface; the alias-table-present flag stays 0 in v4.0 and the
 * alias section is omitted.
 *
 * Hash function: xxh64, a 64-bit non-cryptographic hash with good
 * distribution. ~150 LOC inline; we don't vendor xxhash's single-header
 * (~3 KLOC) because we hash on the order of `n_scopes` per write, not
 * millions per second. See ADR-0002 / D8.
 */

#include "ncdb_impl.h"
#include "ncdb_manifest.h"  /* NCDB_FEATURE_UNIQUE_ID_INDEX */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ====================================================================== */
/*  xxh64                                                                 */
/* ====================================================================== */

#define XXH_PRIME64_1  0x9E3779B185EBCA87ULL
#define XXH_PRIME64_2  0xC2B2AE3D27D4EB4FULL
#define XXH_PRIME64_3  0x165667B19E3779F9ULL
#define XXH_PRIME64_4  0x85EBCA77C2B2AE63ULL
#define XXH_PRIME64_5  0x27D4EB2F165667C5ULL

static uint64_t xxh_rotl64(uint64_t v, int n) {
    return (v << n) | (v >> (64 - n));
}

static uint64_t xxh_read64le(const uint8_t *p) {
    uint64_t v = 0; int i;
    for (i = 0; i < 8; i++) v |= ((uint64_t)p[i]) << (i * 8);
    return v;
}

static uint32_t xxh_read32le(const uint8_t *p) {
    uint32_t v = 0; int i;
    for (i = 0; i < 4; i++) v |= ((uint32_t)p[i]) << (i * 8);
    return v;
}

static uint64_t xxh_round(uint64_t acc, uint64_t input) {
    acc += input * XXH_PRIME64_2;
    acc  = xxh_rotl64(acc, 31);
    acc *= XXH_PRIME64_1;
    return acc;
}

static uint64_t xxh_merge_round(uint64_t acc, uint64_t val) {
    val  = xxh_round(0, val);
    acc ^= val;
    acc  = acc * XXH_PRIME64_1 + XXH_PRIME64_4;
    return acc;
}

uint64_t ncdb_xxh64(const void *input, size_t len, uint64_t seed) {
    const uint8_t *p   = (const uint8_t *)input;
    const uint8_t *end = p + len;
    uint64_t h64;

    if (len >= 32) {
        const uint8_t *limit = end - 32;
        uint64_t v1 = seed + XXH_PRIME64_1 + XXH_PRIME64_2;
        uint64_t v2 = seed + XXH_PRIME64_2;
        uint64_t v3 = seed + 0;
        uint64_t v4 = seed - XXH_PRIME64_1;
        do {
            v1 = xxh_round(v1, xxh_read64le(p)); p += 8;
            v2 = xxh_round(v2, xxh_read64le(p)); p += 8;
            v3 = xxh_round(v3, xxh_read64le(p)); p += 8;
            v4 = xxh_round(v4, xxh_read64le(p)); p += 8;
        } while (p <= limit);
        h64 = xxh_rotl64(v1, 1) + xxh_rotl64(v2, 7)
            + xxh_rotl64(v3, 12) + xxh_rotl64(v4, 18);
        h64 = xxh_merge_round(h64, v1);
        h64 = xxh_merge_round(h64, v2);
        h64 = xxh_merge_round(h64, v3);
        h64 = xxh_merge_round(h64, v4);
    } else {
        h64 = seed + XXH_PRIME64_5;
    }
    h64 += (uint64_t)len;

    while (p + 8 <= end) {
        uint64_t k1 = xxh_round(0, xxh_read64le(p));
        h64 ^= k1;
        h64  = xxh_rotl64(h64, 27) * XXH_PRIME64_1 + XXH_PRIME64_4;
        p += 8;
    }
    if (p + 4 <= end) {
        h64 ^= (uint64_t)xxh_read32le(p) * XXH_PRIME64_1;
        h64  = xxh_rotl64(h64, 23) * XXH_PRIME64_2 + XXH_PRIME64_3;
        p += 4;
    }
    while (p < end) {
        h64 ^= (uint64_t)(*p) * XXH_PRIME64_5;
        h64  = xxh_rotl64(h64, 11) * XXH_PRIME64_1;
        p++;
    }
    h64 ^= h64 >> 33;
    h64 *= XXH_PRIME64_2;
    h64 ^= h64 >> 29;
    h64 *= XXH_PRIME64_3;
    h64 ^= h64 >> 32;
    return h64;
}

/* ====================================================================== */
/*  UCIS §5.4 — Unique-ID string construction                             */
/* ====================================================================== */

/* Return the bit position of `type`'s single set bit, or 0 if the type
 * isn't a clean one-hot (defensive — UCIS scope types are one-hot by
 * spec). */
static int type_log2(uint64_t type) {
    int n = 0;
    if (type == 0) return 0;
    while ((type & 1ULL) == 0 && n < 63) { type >>= 1; n++; }
    return n;
}

/* Reverse a chain of scope pointers from `scope` up to root into a
 * top-down array. Returns the depth, or -1 on overflow. */
static int collect_ancestors(ncdbScopeT scope,
                             ncdbScopeT *out, int cap)
{
    int d = 0;
    ncdbScopeT s = scope;
    while (s) {
        if (d >= cap) return -1;
        out[d++] = s;
        s = s->parent;
    }
    /* Reverse in place so root is first. */
    {
        int i, j;
        for (i = 0, j = d - 1; i < j; i++, j--) {
            ncdbScopeT t = out[i]; out[i] = out[j]; out[j] = t;
        }
    }
    return d;
}

/* Build the UCIS UID string for `scope` into `buf`. Returns the length
 * of the UID (excluding the trailing NUL), or -1 on overflow.
 *
 * Format: `<sep><log2(type)>:<name><sep><log2(type)>:<name>...`
 * Example with sep='/': `/4:tb/4:dut/6:always_blk`. */
int ncdb_compute_unique_id(ncdbT db, ncdbScopeT scope,
                           char *buf, size_t bufsz)
{
    ncdbScopeT chain[64];
    int depth, i;
    size_t pos = 0;
    char sep;
    if (!db || !scope || !buf || bufsz == 0) return -1;
    depth = collect_ancestors(scope, chain, (int)(sizeof(chain) / sizeof(chain[0])));
    if (depth < 0) return -1;
    sep = (db->path_separator && db->path_separator[0]) ? db->path_separator[0] : '/';
    for (i = 0; i < depth; i++) {
        ncdbScopeT s = chain[i];
        int tlog = type_log2(s->type);
        int written = snprintf(buf + pos, bufsz - pos,
                               "%c%d:%s", sep, tlog,
                               s->name ? s->name : "");
        if (written < 0 || (size_t)written >= bufsz - pos) return -1;
        pos += (size_t)written;
    }
    return (int)pos;
}

/* ====================================================================== */
/*  NUID member — serialize / deserialize                                 */
/* ====================================================================== */

#define NCDB_NUID_MAGIC      "NUID"
#define NCDB_NUID_MAGIC_SZ   4U
#define NCDB_NUID_VERSION    1U
#define NCDB_NUID_FLAG_CASE_AWARE    0x01U
#define NCDB_NUID_FLAG_HAS_ALIASES   0x02U

typedef struct {
    uint64_t hash;
    uint64_t dfs_idx;
} nuid_entry_t;

static int compare_nuid(const void *a, const void *b) {
    uint64_t ha = ((const nuid_entry_t *)a)->hash;
    uint64_t hb = ((const nuid_entry_t *)b)->hash;
    if (ha < hb) return -1;
    if (ha > hb) return 1;
    return 0;
}

static int write_u64le_n(ncdbBuf *b, uint64_t v) {
    uint8_t bytes[8]; int i;
    for (i = 0; i < 8; ++i) bytes[i] = (uint8_t)(v >> (i * 8));
    return ncdb_impl_buf_append(b, bytes, 8);
}

static int read_u64le_n(const uint8_t *data, size_t size, size_t *off, uint64_t *out) {
    int i; uint64_t v = 0;
    if (*off + 8 > size) return -1;
    for (i = 0; i < 8; ++i) v |= ((uint64_t)data[*off + i]) << (i * 8);
    *off += 8; *out = v;
    return 0;
}

int ncdb_unique_id_index_serialize(ncdbT db, ncdbBuf *out)
{
    nuid_entry_t *entries = NULL;
    ncdbScopeT *flat = NULL;
    size_t flat_count = 0;
    char uid_buf[1024];
    uint8_t version = NCDB_NUID_VERSION;
    uint8_t flags = 0;
    size_t i;
    int rc = -1;

    if (!db || !out) return -1;

    if (ncdb_impl_dfs_flatten(db, &flat, &flat_count) != 0) return -1;
    if (flat_count == 0) { rc = 0; goto cleanup; } /* nothing to emit */

    entries = (nuid_entry_t *)calloc(flat_count, sizeof(*entries));
    if (!entries) goto cleanup;

    for (i = 0; i < flat_count; i++) {
        int n = ncdb_compute_unique_id(db, flat[i], uid_buf, sizeof(uid_buf));
        if (n < 0) goto cleanup;
        entries[i].hash    = ncdb_xxh64(uid_buf, (size_t)n, 0);
        entries[i].dfs_idx = (uint64_t)i;
    }
    qsort(entries, flat_count, sizeof(*entries), compare_nuid);

    if (ncdb_impl_buf_append(out, NCDB_NUID_MAGIC, NCDB_NUID_MAGIC_SZ) != 0) goto cleanup;
    if (ncdb_impl_buf_append(out, &version, 1) != 0) goto cleanup;
    if (ncdb_impl_buf_append(out, &flags, 1) != 0) goto cleanup;
    if (ncdb_varint_encode_uint64((uint64_t)flat_count, out) != 0) goto cleanup;
    for (i = 0; i < flat_count; i++) {
        if (write_u64le_n(out, entries[i].hash) != 0) goto cleanup;
        if (ncdb_varint_encode_uint64(entries[i].dfs_idx, out) != 0) goto cleanup;
    }
    db->feature_flags |= NCDB_FEATURE_UNIQUE_ID_INDEX;
    rc = 0;

cleanup:
    free(entries);
    free(flat);
    return rc;
}

/* In-memory loaded NUID index. Owned by the db so it survives across
 * lookups. */
struct ncdb_nuid_index_s {
    nuid_entry_t *entries;
    size_t        count;
};

void ncdb_unique_id_index_free(ncdb_nuid_index_t *idx) {
    if (!idx) return;
    free(idx->entries);
    free(idx);
}

int ncdb_unique_id_index_deserialize(const uint8_t *data, size_t size,
                                     ncdb_nuid_index_t **out_idx,
                                     char *errbuf, size_t errbuf_sz)
{
    ncdb_nuid_index_t *idx = NULL;
    size_t off;
    uint8_t version, flags;
    uint64_t n;
    size_t i;
    if (!data || size < NCDB_NUID_MAGIC_SZ + 3) {
        if (errbuf) snprintf(errbuf, errbuf_sz, "%s", "NUID member too small");
        return -1;
    }
    if (memcmp(data, NCDB_NUID_MAGIC, NCDB_NUID_MAGIC_SZ) != 0) {
        if (errbuf) snprintf(errbuf, errbuf_sz, "%s", "bad NUID magic");
        return -1;
    }
    off = NCDB_NUID_MAGIC_SZ;
    version = data[off++];
    flags   = data[off++];
    (void)flags;
    if (version != NCDB_NUID_VERSION) {
        if (errbuf) snprintf(errbuf, errbuf_sz, "unknown NUID version %u", (unsigned)version);
        return -1;
    }
    if (ncdb_varint_decode_uint64(data, size, &off, &n) != 0) goto bad;
    idx = (ncdb_nuid_index_t *)calloc(1, sizeof(*idx));
    if (!idx) goto bad;
    if (n > 0) {
        idx->entries = (nuid_entry_t *)calloc((size_t)n, sizeof(*idx->entries));
        if (!idx->entries) goto bad;
    }
    idx->count = (size_t)n;
    for (i = 0; i < idx->count; i++) {
        uint64_t dfs;
        if (read_u64le_n(data, size, &off, &idx->entries[i].hash) != 0) goto bad;
        if (ncdb_varint_decode_uint64(data, size, &off, &dfs) != 0) goto bad;
        idx->entries[i].dfs_idx = dfs;
    }
    *out_idx = idx;
    return 0;
bad:
    if (errbuf) snprintf(errbuf, errbuf_sz, "%s", "truncated NUID member");
    ncdb_unique_id_index_free(idx);
    return -1;
}

/* Binary-search the index for `hash`. Returns the DFS index of the
 * first match, or SIZE_MAX if not found. */
size_t ncdb_unique_id_index_lookup(const ncdb_nuid_index_t *idx, uint64_t hash) {
    size_t lo, hi;
    if (!idx || idx->count == 0) return (size_t)-1;
    lo = 0; hi = idx->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (idx->entries[mid].hash < hash) lo = mid + 1;
        else hi = mid;
    }
    if (lo >= idx->count || idx->entries[lo].hash != hash) return (size_t)-1;
    return (size_t)idx->entries[lo].dfs_idx;
}
