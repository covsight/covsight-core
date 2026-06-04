/*
 * ncdb_metric.c — metric definitions table (Phase 4.6 / M6).
 *
 * Stores UCIS metric *identifiers* (URL-style name, mode, applicable
 * scope-type mask) — NOT computed metric values. Per ADR-0002 / D7
 * we deliberately avoid persisting denormalized aggregates: they're
 * trivially recomputed from counts.bin and persisting them risks
 * staleness on partial updates.
 *
 * On-disk layout (NMTR v1):
 *
 *   magic[4]       "NMTR"
 *   version: u8    = 1
 *   reserved: u8   = 0
 *   n_definitions: varint
 *   for i in 0..n_definitions:
 *     metric_id:        varint
 *     mode:             u8
 *     target_type_mask: u64 LE
 *     name:             length-prefixed inline string
 *
 * Names are inline (not via strings.bin) — typical n is ~50 so the
 * dedupe win doesn't justify the cross-member coupling.
 */

#include "ncdb_impl.h"
#include "ncdb_manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NMTR_MAGIC      "NMTR"
#define NMTR_MAGIC_SZ   4U
#define NMTR_VERSION    1U

int ncdb_metric_add(ncdbT db, uint32_t id, const char *name,
                    uint8_t mode, uint64_t target_type_mask)
{
    struct ncdb_metric_def_s *m;
    char *copy;
    if (!db) return -1;
    copy = ncdb_impl_strdup(name ? name : "");
    if (!copy) return -1;
    if (db->metric_count >= db->metric_cap) {
        size_t nc = db->metric_cap ? db->metric_cap * 2 : 8;
        struct ncdb_metric_def_s *na = (struct ncdb_metric_def_s *)
            realloc(db->metrics, nc * sizeof(*na));
        if (!na) { free(copy); return -1; }
        db->metrics = na;
        db->metric_cap = nc;
    }
    m = &db->metrics[db->metric_count++];
    m->metric_id        = id;
    m->name             = copy;
    m->mode             = mode;
    m->target_type_mask = target_type_mask;
    return 0;
}

size_t ncdb_metric_count(ncdbT db) { return db ? db->metric_count : 0; }

const struct ncdb_metric_def_s *ncdb_metric_get(ncdbT db, size_t idx) {
    if (!db || idx >= db->metric_count) return NULL;
    return &db->metrics[idx];
}

void ncdb_metric_free(ncdbT db) {
    size_t i;
    if (!db) return;
    for (i = 0; i < db->metric_count; i++) free(db->metrics[i].name);
    free(db->metrics);
    db->metrics = NULL;
    db->metric_count = db->metric_cap = 0;
}

static int write_u64le_m(ncdbBuf *b, uint64_t v) {
    uint8_t bytes[8]; int i;
    for (i = 0; i < 8; ++i) bytes[i] = (uint8_t)(v >> (i * 8));
    return ncdb_impl_buf_append(b, bytes, 8);
}
static int read_u64le_m(const uint8_t *data, size_t size, size_t *off, uint64_t *out) {
    int i; uint64_t v = 0;
    if (*off + 8 > size) return -1;
    for (i = 0; i < 8; ++i) v |= ((uint64_t)data[*off + i]) << (i * 8);
    *off += 8; *out = v;
    return 0;
}

int ncdb_metric_serialize(ncdbT db, ncdbBuf *out) {
    uint8_t version = NMTR_VERSION, reserved = 0;
    size_t i;
    if (!db || !out) return -1;
    if (db->metric_count == 0) return 0;   /* skip member entirely */

    if (ncdb_impl_buf_append(out, NMTR_MAGIC, NMTR_MAGIC_SZ) != 0) return -1;
    if (ncdb_impl_buf_append(out, &version, 1) != 0) return -1;
    if (ncdb_impl_buf_append(out, &reserved, 1) != 0) return -1;
    if (ncdb_varint_encode_uint64((uint64_t)db->metric_count, out) != 0) return -1;
    for (i = 0; i < db->metric_count; i++) {
        const struct ncdb_metric_def_s *m = &db->metrics[i];
        size_t nlen = m->name ? strlen(m->name) : 0;
        if (ncdb_varint_encode_uint64((uint64_t)m->metric_id, out) != 0) return -1;
        if (ncdb_impl_buf_append(out, &m->mode, 1) != 0) return -1;
        if (write_u64le_m(out, m->target_type_mask) != 0) return -1;
        if (ncdb_varint_encode_uint64((uint64_t)nlen, out) != 0) return -1;
        if (nlen > 0 && ncdb_impl_buf_append(out, m->name, nlen) != 0) return -1;
    }
    db->feature_flags |= NCDB_FEATURE_METRICS;
    return 0;
}

int ncdb_metric_deserialize(ncdbT db, const uint8_t *data, size_t size,
                            char *errbuf, size_t errbuf_sz)
{
    size_t off;
    uint8_t version;
    uint64_t n, i;

    if (!db || !data || size < NMTR_MAGIC_SZ + 2) {
        if (errbuf) snprintf(errbuf, errbuf_sz, "%s", "NMTR too small");
        return -1;
    }
    if (memcmp(data, NMTR_MAGIC, NMTR_MAGIC_SZ) != 0) {
        if (errbuf) snprintf(errbuf, errbuf_sz, "%s", "bad NMTR magic");
        return -1;
    }
    off = NMTR_MAGIC_SZ;
    version = data[off++];
    off++; /* reserved */
    if (version != NMTR_VERSION) {
        if (errbuf) snprintf(errbuf, errbuf_sz, "unknown NMTR version %u", (unsigned)version);
        return -1;
    }
    if (ncdb_varint_decode_uint64(data, size, &off, &n) != 0) goto bad;
    for (i = 0; i < n; i++) {
        uint64_t id, mask, nlen;
        uint8_t mode;
        char *name;
        if (ncdb_varint_decode_uint64(data, size, &off, &id) != 0) goto bad;
        if (off + 1 > size) goto bad;
        mode = data[off++];
        if (read_u64le_m(data, size, &off, &mask) != 0) goto bad;
        if (ncdb_varint_decode_uint64(data, size, &off, &nlen) != 0) goto bad;
        if (off + nlen > size) goto bad;
        name = (char *)malloc((size_t)nlen + 1);
        if (!name) goto bad;
        if (nlen > 0) memcpy(name, data + off, (size_t)nlen);
        name[nlen] = '\0';
        off += (size_t)nlen;
        if (ncdb_metric_add(db, (uint32_t)id, name, mode, mask) != 0) {
            free(name); goto bad;
        }
        free(name); /* ncdb_metric_add duplicates */
    }
    return 0;
bad:
    if (errbuf) snprintf(errbuf, errbuf_sz, "%s", "truncated NMTR");
    return -1;
}
