/*
 * ncdb_formal.c — formal-verification metadata (Phase 4.5 / M5).
 *
 * Stores per-scope formal status / radius / witness for assertion-style
 * scopes that came from a formal tool. UCIS §8.19 specifies a broader
 * surface (envs, assumptions, formally-unreachable cover, coverage
 * context, per-cover variants); this first slice covers scope-level
 * status + radius + witness, which is what UCIS_FORMAL_TOOL
 * consumers most commonly emit.
 *
 * On-disk layout (NFRM v1):
 *
 *   magic[4]     "NFRM"
 *   version: u8  = 1
 *   reserved: u8 = 0
 *   n_records:   varint
 *   for i in 0..n_records:
 *     scope_dfs_idx: varint
 *     presence: u8
 *       bit0 = status   (u8 follows)
 *       bit1 = radius   (zigzag varint follows)
 *       bit2 = witness  (length-prefixed inline string follows)
 *     ...optional fields encoded in bit order...
 *
 * Strings are inline (length-prefixed) rather than going through
 * strings.bin because witness counts are typically tiny (50-100
 * records on a formal-heavy DB) and the extra strings.bin coupling
 * isn't worth the per-call complexity at this size.
 *
 * Records are sorted by scope_dfs_idx on write so the reader can
 * bsearch them. Not yet used (we keep a linear table in memory),
 * but the layout is bsearch-ready.
 */

#include "ncdb_impl.h"
#include "ncdb_manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NFRM_MAGIC      "NFRM"
#define NFRM_MAGIC_SZ   4U
#define NFRM_VERSION    1U

#define FORMAL_BIT_STATUS   0x01U
#define FORMAL_BIT_RADIUS   0x02U
#define FORMAL_BIT_WITNESS  0x04U

/* ====================================================================== */
/*  In-memory storage                                                     */
/* ====================================================================== */

static struct ncdb_formal_record_s *find_record(ncdbT db, ncdbScopeT s) {
    size_t i;
    if (!db || !s) return NULL;
    for (i = 0; i < db->formal_record_count; i++) {
        if (db->formal_records[i].scope == s) return &db->formal_records[i];
    }
    return NULL;
}

static struct ncdb_formal_record_s *get_or_create(ncdbT db, ncdbScopeT s) {
    struct ncdb_formal_record_s *r = find_record(db, s);
    if (r) return r;
    if (db->formal_record_count >= db->formal_record_cap) {
        size_t nc = db->formal_record_cap ? db->formal_record_cap * 2 : 8;
        struct ncdb_formal_record_s *na = (struct ncdb_formal_record_s *)
            realloc(db->formal_records, nc * sizeof(*na));
        if (!na) return NULL;
        db->formal_records = na;
        db->formal_record_cap = nc;
    }
    r = &db->formal_records[db->formal_record_count++];
    memset(r, 0, sizeof(*r));
    r->scope  = s;
    r->radius = -1;
    return r;
}

void ncdb_formal_free(ncdbT db) {
    size_t i;
    if (!db) return;
    for (i = 0; i < db->formal_record_count; i++) {
        free(db->formal_records[i].witness);
    }
    free(db->formal_records);
    db->formal_records = NULL;
    db->formal_record_count = db->formal_record_cap = 0;
}

/* ====================================================================== */
/*  Public API                                                            */
/* ====================================================================== */

int ncdb_formal_set_status(ncdbT db, ncdbScopeT s, uint8_t status) {
    struct ncdb_formal_record_s *r = get_or_create(db, s);
    if (!r) return -1;
    r->status   = status;
    r->presence |= FORMAL_BIT_STATUS;
    return 0;
}

int ncdb_formal_get_status(ncdbT db, ncdbScopeT s) {
    struct ncdb_formal_record_s *r = find_record(db, s);
    if (!r || !(r->presence & FORMAL_BIT_STATUS)) return -1;
    return (int)r->status;
}

int ncdb_formal_set_radius(ncdbT db, ncdbScopeT s, int64_t radius) {
    struct ncdb_formal_record_s *r = get_or_create(db, s);
    if (!r) return -1;
    r->radius    = radius;
    r->presence |= FORMAL_BIT_RADIUS;
    return 0;
}

int64_t ncdb_formal_get_radius(ncdbT db, ncdbScopeT s) {
    struct ncdb_formal_record_s *r = find_record(db, s);
    if (!r || !(r->presence & FORMAL_BIT_RADIUS)) return INT64_MIN;
    return r->radius;
}

int ncdb_formal_set_witness(ncdbT db, ncdbScopeT s, const char *witness) {
    struct ncdb_formal_record_s *r = get_or_create(db, s);
    char *copy;
    if (!r) return -1;
    copy = ncdb_impl_strdup(witness ? witness : "");
    if (!copy) return -1;
    free(r->witness);
    r->witness   = copy;
    r->presence |= FORMAL_BIT_WITNESS;
    return 0;
}

const char *ncdb_formal_get_witness(ncdbT db, ncdbScopeT s) {
    struct ncdb_formal_record_s *r = find_record(db, s);
    if (!r || !(r->presence & FORMAL_BIT_WITNESS)) return NULL;
    return r->witness;
}

/* ====================================================================== */
/*  Serialize / deserialize                                               */
/* ====================================================================== */

static uint64_t zz_encode_n(int64_t v) {
    return (uint64_t)((v << 1) ^ (v >> 63));
}
static int64_t zz_decode_n(uint64_t v) {
    return (int64_t)((v >> 1) ^ -(int64_t)(v & 1));
}

static int write_lp_str_n(ncdbBuf *b, const char *s) {
    size_t len = s ? strlen(s) : 0;
    if (ncdb_varint_encode_uint64((uint64_t)len, b) != 0) return -1;
    if (len > 0 && ncdb_impl_buf_append(b, s, len) != 0) return -1;
    return 0;
}

static int read_lp_str_n(const uint8_t *data, size_t size, size_t *off, char **out) {
    uint64_t len;
    char *buf;
    if (ncdb_varint_decode_uint64(data, size, off, &len) != 0) return -1;
    if (*off + len > size) return -1;
    buf = (char *)malloc((size_t)len + 1);
    if (!buf) return -1;
    if (len > 0) memcpy(buf, data + *off, (size_t)len);
    buf[len] = '\0';
    *off += (size_t)len;
    free(*out);
    *out = buf;
    return 0;
}

/* Materialize a flat (scope_dfs_idx → record) sort key. Used to emit
 * records in sorted order so a future bsearch reader can find them. */
typedef struct {
    uint64_t dfs_idx;
    const struct ncdb_formal_record_s *rec;
} sort_entry_t;

static int compare_sort_entry(const void *a, const void *b) {
    uint64_t aa = ((const sort_entry_t *)a)->dfs_idx;
    uint64_t bb = ((const sort_entry_t *)b)->dfs_idx;
    if (aa < bb) return -1;
    if (aa > bb) return 1;
    return 0;
}

int ncdb_formal_serialize(ncdbT db, ncdbBuf *out) {
    sort_entry_t *entries = NULL;
    size_t i, n;
    uint8_t version = NFRM_VERSION, reserved = 0;
    int rc = -1;

    if (!db || !out) return -1;
    if (db->formal_record_count == 0) { return 0; }  /* skip entirely */

    n = db->formal_record_count;
    entries = (sort_entry_t *)malloc(n * sizeof(*entries));
    if (!entries) return -1;
    for (i = 0; i < n; i++) {
        size_t dfs;
        if (ncdb_impl_scope_dfs_index(db, db->formal_records[i].scope, &dfs) != 0) goto cleanup;
        entries[i].dfs_idx = (uint64_t)dfs;
        entries[i].rec     = &db->formal_records[i];
    }
    qsort(entries, n, sizeof(*entries), compare_sort_entry);

    if (ncdb_impl_buf_append(out, NFRM_MAGIC, NFRM_MAGIC_SZ) != 0) goto cleanup;
    if (ncdb_impl_buf_append(out, &version, 1) != 0) goto cleanup;
    if (ncdb_impl_buf_append(out, &reserved, 1) != 0) goto cleanup;
    if (ncdb_varint_encode_uint64((uint64_t)n, out) != 0) goto cleanup;
    for (i = 0; i < n; i++) {
        const struct ncdb_formal_record_s *r = entries[i].rec;
        uint8_t p = (uint8_t)r->presence;
        if (ncdb_varint_encode_uint64(entries[i].dfs_idx, out) != 0) goto cleanup;
        if (ncdb_impl_buf_append(out, &p, 1) != 0) goto cleanup;
        if (p & FORMAL_BIT_STATUS) {
            if (ncdb_impl_buf_append(out, &r->status, 1) != 0) goto cleanup;
        }
        if (p & FORMAL_BIT_RADIUS) {
            if (ncdb_varint_encode_uint64(zz_encode_n(r->radius), out) != 0) goto cleanup;
        }
        if (p & FORMAL_BIT_WITNESS) {
            if (write_lp_str_n(out, r->witness) != 0) goto cleanup;
        }
    }
    db->feature_flags |= NCDB_FEATURE_FORMAL;
    rc = 0;

cleanup:
    free(entries);
    return rc;
}

int ncdb_formal_deserialize(ncdbT db, const uint8_t *data, size_t size,
                            char *errbuf, size_t errbuf_sz)
{
    size_t off;
    uint8_t version;
    uint64_t n, i;

    if (!db || !data || size < NFRM_MAGIC_SZ + 2) {
        if (errbuf) snprintf(errbuf, errbuf_sz, "%s", "NFRM too small");
        return -1;
    }
    if (memcmp(data, NFRM_MAGIC, NFRM_MAGIC_SZ) != 0) {
        if (errbuf) snprintf(errbuf, errbuf_sz, "%s", "bad NFRM magic");
        return -1;
    }
    off = NFRM_MAGIC_SZ;
    version = data[off++];
    off++;  /* skip reserved */
    if (version != NFRM_VERSION) {
        if (errbuf) snprintf(errbuf, errbuf_sz, "unknown NFRM version %u", (unsigned)version);
        return -1;
    }
    if (ncdb_varint_decode_uint64(data, size, &off, &n) != 0) goto bad;
    for (i = 0; i < n; i++) {
        uint64_t dfs;
        uint8_t p;
        ncdbScopeT s;
        struct ncdb_formal_record_s *r;
        if (ncdb_varint_decode_uint64(data, size, &off, &dfs) != 0) goto bad;
        if (off + 1 > size) goto bad;
        p = data[off++];
        s = ncdb_impl_scope_by_dfs_index(db, (size_t)dfs);
        if (!s) {
            /* Scope was removed between NFRM write and our read. Skip
             * the record's fields to keep parsing the rest. */
            if (p & FORMAL_BIT_STATUS)  { if (off + 1 > size) goto bad; off++; }
            if (p & FORMAL_BIT_RADIUS)  { uint64_t tmp; if (ncdb_varint_decode_uint64(data, size, &off, &tmp) != 0) goto bad; }
            if (p & FORMAL_BIT_WITNESS) {
                uint64_t len;
                if (ncdb_varint_decode_uint64(data, size, &off, &len) != 0) goto bad;
                if (off + len > size) goto bad;
                off += (size_t)len;
            }
            continue;
        }
        r = get_or_create(db, s);
        if (!r) goto bad;
        if (p & FORMAL_BIT_STATUS) {
            if (off + 1 > size) goto bad;
            r->status    = data[off++];
            r->presence |= FORMAL_BIT_STATUS;
        }
        if (p & FORMAL_BIT_RADIUS) {
            uint64_t enc;
            if (ncdb_varint_decode_uint64(data, size, &off, &enc) != 0) goto bad;
            r->radius    = zz_decode_n(enc);
            r->presence |= FORMAL_BIT_RADIUS;
        }
        if (p & FORMAL_BIT_WITNESS) {
            char *tmp = NULL;
            if (read_lp_str_n(data, size, &off, &tmp) != 0) goto bad;
            free(r->witness);
            r->witness   = tmp;
            r->presence |= FORMAL_BIT_WITNESS;
        }
    }
    return 0;
bad:
    if (errbuf) snprintf(errbuf, errbuf_sz, "%s", "truncated NFRM");
    return -1;
}
