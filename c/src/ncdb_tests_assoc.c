/*
 * ncdb_tests_assoc.c — test↔coveritem association (Phase 4.4 / M4).
 *
 * UCIS allows a coveritem to be linked to a *set* of history nodes
 * (which tests contributed to its count). v3 had no place to store
 * this; M4 adds the `tests_assoc.bin` member ("NTAS").
 *
 * **Design — transposed + tiered.** See ADR-0002 / D7 and the M4
 * section of docs/ncdb-format-v4-plan.md for the reasoning. Briefly:
 * associations are stored per-cover (not per-test bitmap) and
 * classified into three tiers by hit cardinality:
 *
 *   0 NEVER  — zero hits; no payload at all
 *   1 ALL    — hit by ~every real test; payload is a (usually short)
 *              exception list of tests that *didn't* hit
 *   2 SPARSE — hit by a small fraction of tests; payload is a
 *              delta-encoded sorted list of test slot indices
 *
 * "Real tests" = history nodes excluding MERGE-kind (whose
 * associations are derivable from their children's at read time).
 * Slot indices are positions in the per-DB real_tests vector, so MERGE
 * nodes never appear in the encoded payloads.
 *
 * On-disk layout (NTAS v1):
 *
 *   magic[4]                "NTAS"
 *   version: u8             = 1
 *   reserved: u8            = 0   (room for future flags / 2nd section)
 *   n_real_tests: varint
 *   n_covers:     varint
 *   real_test_hist_idxs:    delta-varint × n_real_tests
 *                           (translates slot idx → global history idx)
 *   tier_bits:              ceil(n_covers / 4) bytes, 4 covers per byte,
 *                           2 bits each, LSB-first within byte
 *   payload:                packed per-cover records in DFS order:
 *                             tier 0:  (nothing)
 *                             tier 1:  varint n_exc + delta-varint × n_exc
 *                             tier 2:  varint n     + delta-varint × n
 *   n_skip:       varint
 *   skip[]:       (cover_idx varint, payload_byte_offset varint) × n_skip
 *
 * The skip table makes lookup of cover N's payload O(log n_skip) +
 * O(linear-scan-within-bucket).
 *
 * Tier classification thresholds at write time are tunable (see
 * `classify_tier`). They currently favor the NEVER and ALL tiers (zero-
 * or near-zero payload bytes) over SPARSE when in doubt.
 */

#include "ncdb_impl.h"
#include "ncdb_manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ====================================================================== */
/*  In-memory assoc list                                                  */
/* ====================================================================== */

void ncdb_assoc_free(struct ncdb_assoc_s *a) {
    if (!a) return;
    free(a->slots);
    free(a);
}

/* Insert `slot` into `a` keeping the array sorted. No-op if already
 * present. */
static int assoc_insert_sorted(struct ncdb_assoc_s *a, uint32_t slot) {
    size_t lo = 0, hi = a->count, mid;
    while (lo < hi) {
        mid = lo + (hi - lo) / 2;
        if (a->slots[mid] < slot) lo = mid + 1;
        else if (a->slots[mid] > slot) hi = mid;
        else return 0; /* already present */
    }
    if (a->count >= a->cap) {
        size_t nc = a->cap ? a->cap * 2 : 4;
        uint32_t *na = (uint32_t *)realloc(a->slots, nc * sizeof(uint32_t));
        if (!na) return -1;
        a->slots = na;
        a->cap = nc;
    }
    if (lo < a->count) {
        memmove(&a->slots[lo + 1], &a->slots[lo],
                (a->count - lo) * sizeof(uint32_t));
    }
    a->slots[lo] = slot;
    a->count++;
    return 0;
}

/* ====================================================================== */
/*  Real-test slot mapping                                                */
/* ====================================================================== */

/* Build a translation table: db->history_nodes index → real-test slot
 * index (or SIZE_MAX if the node is MERGE). Caller frees the returned
 * arrays. */
static int build_real_test_map(ncdbT db,
                               size_t **out_hist_to_slot,
                               size_t  *out_n_hist,
                               size_t **out_slot_to_hist,
                               size_t  *out_n_real)
{
    size_t i, n = db->history_count;
    size_t slot = 0;
    size_t *h2s = (size_t *)malloc(n * sizeof(size_t));
    size_t *s2h = (size_t *)malloc(n * sizeof(size_t));   /* worst case */
    if ((n > 0 && (!h2s || !s2h))) { free(h2s); free(s2h); return -1; }
    for (i = 0; i < n; i++) {
        if (db->history_nodes[i]->kind == NCDB_HISTORY_MERGE) {
            h2s[i] = (size_t)-1;
        } else {
            h2s[i] = slot;
            s2h[slot++] = i;
        }
    }
    *out_hist_to_slot = h2s;
    *out_n_hist       = n;
    *out_slot_to_hist = s2h;
    *out_n_real       = slot;
    return 0;
}

/* ====================================================================== */
/*  Public API                                                            */
/* ====================================================================== */

int ncdb_AssocCoverHistory(ncdbT db, ncdbCoverT cover, ncdbHistoryNodeT h) {
    size_t hist_idx;
    size_t *h2s = NULL, *s2h = NULL;
    size_t n_hist, n_real;
    int rc = -1;
    if (!db || !cover || !h) return -1;
    if (h->kind == NCDB_HISTORY_MERGE) return -1;   /* derivable, never stored */
    if (ncdb_impl_history_index(db, h, &hist_idx) != 0) return -1;

    /* Translate to slot-idx via a fresh map. This is O(n_hist) per call
     * which is fine for the writer-side rate (assoc API is called once
     * per (cover, test) hit, not per call to IncrementCover). Future
     * optimization: cache the map on db and invalidate when history
     * nodes are added. */
    if (build_real_test_map(db, &h2s, &n_hist, &s2h, &n_real) != 0) goto out;
    if (hist_idx >= n_hist || h2s[hist_idx] == (size_t)-1) goto out;

    if (!cover->assoc) {
        cover->assoc = (struct ncdb_assoc_s *)calloc(1, sizeof(*cover->assoc));
        if (!cover->assoc) goto out;
    }
    rc = assoc_insert_sorted(cover->assoc, (uint32_t)h2s[hist_idx]);
out:
    free(h2s); free(s2h);
    return rc;
}

size_t ncdb_CoverHistoryCount(ncdbT db, ncdbCoverT cover) {
    (void)db;
    if (!cover || !cover->assoc) return 0;
    return cover->assoc->count;
}

ncdbHistoryNodeT ncdb_CoverHistoryAt(ncdbT db, ncdbCoverT cover, size_t idx) {
    size_t *h2s = NULL, *s2h = NULL;
    size_t n_hist, n_real;
    ncdbHistoryNodeT result = NULL;
    if (!db || !cover || !cover->assoc) return NULL;
    if (idx >= cover->assoc->count) return NULL;
    if (build_real_test_map(db, &h2s, &n_hist, &s2h, &n_real) != 0) return NULL;
    {
        uint32_t slot = cover->assoc->slots[idx];
        if (slot < n_real) {
            result = db->history_nodes[s2h[slot]];
        }
    }
    free(h2s); free(s2h);
    return result;
}

/* ====================================================================== */
/*  Serialize / deserialize                                               */
/* ====================================================================== */

#define NTAS_MAGIC      "NTAS"
#define NTAS_MAGIC_SZ   4U
#define NTAS_VERSION    1U

#define TIER_NEVER  0
#define TIER_ALL    1
#define TIER_SPARSE 2

#define SKIP_STRIDE 1024U   /* one skip entry every 1024 non-NEVER covers */

/* Flat cover enumeration in DFS order. The caller frees `out`. */
static int flatten_covers(ncdbT db, ncdbCoverT **out, size_t *out_count);

static void flatten_walk(ncdbScopeT s, ncdbCoverT **arr, size_t *idx) {
    size_t i;
    for (i = 0; i < s->cover_count; i++) (*arr)[(*idx)++] = s->covers[i];
    for (i = 0; i < s->child_count; i++) flatten_walk(s->children[i], arr, idx);
}

static int flatten_covers(ncdbT db, ncdbCoverT **out, size_t *out_count) {
    size_t total = ncdb_impl_count_covers(db);
    size_t idx = 0, i;
    ncdbCoverT *arr;
    if (total == 0) { *out = NULL; *out_count = 0; return 0; }
    arr = (ncdbCoverT *)malloc(total * sizeof(*arr));
    if (!arr) return -1;
    for (i = 0; i < db->root_count; i++) flatten_walk(db->roots[i], &arr, &idx);
    *out = arr;
    *out_count = idx;
    return 0;
}

/* Classify a cover's hit-set into a tier and (for ALL) compute the
 * exception list. exceptions[] is written sorted ascending. Returns
 * the number of exception slots (0 for SPARSE, n_exc for ALL). */
static int classify_tier(const struct ncdb_assoc_s *a, size_t n_real,
                         uint8_t *tier_out,
                         uint32_t *exceptions, size_t exc_cap, size_t *n_exc_out)
{
    size_t hit = a ? a->count : 0;
    *n_exc_out = 0;
    if (hit == 0) { *tier_out = TIER_NEVER; return 0; }
    /* ALL_NO_EXC: every real test hit. */
    if (hit == n_real) { *tier_out = TIER_ALL; return 0; }
    /* ALL_WITH_EXC threshold: if fewer than ~5% of tests missed, encode
     * as ALL + exception list. The crossover where exceptions become
     * shorter than the hit list is ~50%, but we bias toward NEVER/ALL
     * to keep the tier_bits more predictable. */
    if (hit > (n_real - n_real / 20) && n_real - hit <= exc_cap) {
        /* Build sorted exception list: slots in [0,n_real) NOT in a->slots. */
        size_t ai = 0, slot, n_exc = 0;
        for (slot = 0; slot < n_real; slot++) {
            if (ai < a->count && a->slots[ai] == slot) { ai++; continue; }
            exceptions[n_exc++] = (uint32_t)slot;
        }
        *tier_out = TIER_ALL;
        *n_exc_out = n_exc;
        return 0;
    }
    *tier_out = TIER_SPARSE;
    return 0;
}

/* Write a delta-encoded list of sorted u32s as varints. */
static int write_delta_list(ncdbBuf *out, const uint32_t *vals, size_t n) {
    size_t i;
    uint32_t prev = 0;
    if (ncdb_varint_encode_uint64((uint64_t)n, out) != 0) return -1;
    for (i = 0; i < n; i++) {
        uint32_t delta = (i == 0) ? vals[i] : vals[i] - prev;
        if (ncdb_varint_encode_uint64((uint64_t)delta, out) != 0) return -1;
        prev = vals[i];
    }
    return 0;
}

static int read_delta_list(const uint8_t *data, size_t size, size_t *off,
                           uint32_t **out_vals, size_t *out_n)
{
    uint64_t n, i, delta;
    uint32_t cur = 0;
    uint32_t *vals;
    if (ncdb_varint_decode_uint64(data, size, off, &n) != 0) return -1;
    vals = (n > 0) ? (uint32_t *)malloc((size_t)n * sizeof(uint32_t)) : NULL;
    if (n > 0 && !vals) return -1;
    for (i = 0; i < n; i++) {
        if (ncdb_varint_decode_uint64(data, size, off, &delta) != 0) {
            free(vals); return -1;
        }
        cur += (uint32_t)delta;
        vals[i] = cur;
    }
    *out_vals = vals;
    *out_n = (size_t)n;
    return 0;
}

int ncdb_tests_assoc_serialize(ncdbT db, ncdbBuf *out) {
    size_t *h2s = NULL, *s2h = NULL, n_hist = 0, n_real = 0;
    ncdbCoverT *covers = NULL;
    size_t n_covers = 0, i;
    uint8_t  *tier_bits = NULL;
    size_t    tier_bytes;
    ncdbBuf   payload;
    uint64_t *skip_cover = NULL, *skip_off = NULL;
    size_t    skip_count = 0, skip_cap = 0;
    size_t    nonempty_count = 0;
    uint32_t *exc_buf = NULL;
    size_t    exc_cap = 256;  /* max exceptions per cover we'll encode */
    int       rc = -1;
    int       any_assoc = 0;
    uint8_t   version = NTAS_VERSION, reserved = 0;

    if (!db || !out) return -1;
    ncdb_impl_buf_init(&payload);

    if (build_real_test_map(db, &h2s, &n_hist, &s2h, &n_real) != 0) goto cleanup;
    if (flatten_covers(db, &covers, &n_covers) != 0) goto cleanup;

    /* If no cover has any associations, skip the member entirely. */
    for (i = 0; i < n_covers; i++) {
        if (covers[i]->assoc && covers[i]->assoc->count > 0) { any_assoc = 1; break; }
    }
    if (!any_assoc) { rc = 0; goto cleanup; }

    tier_bytes = (n_covers + 3) / 4;
    tier_bits = (uint8_t *)calloc(tier_bytes ? tier_bytes : 1, 1);
    exc_buf   = (uint32_t *)malloc(exc_cap * sizeof(uint32_t));
    if ((tier_bytes > 0 && !tier_bits) || !exc_buf) goto cleanup;

    /* Pass 1: classify each cover, emit payload, build skip table. */
    for (i = 0; i < n_covers; i++) {
        uint8_t tier;
        size_t n_exc = 0;
        struct ncdb_assoc_s *a = covers[i]->assoc;
        classify_tier(a, n_real, &tier, exc_buf, exc_cap, &n_exc);
        /* Pack tier (2 bits) into tier_bits[i/4]. */
        tier_bits[i / 4] |= (uint8_t)((tier & 0x3) << ((i % 4) * 2));

        if (tier == TIER_NEVER) continue;

        /* Add skip entry every SKIP_STRIDE non-empty covers, recording
         * the byte offset at which this cover's payload starts. */
        if ((nonempty_count % SKIP_STRIDE) == 0) {
            if (skip_count >= skip_cap) {
                size_t nc = skip_cap ? skip_cap * 2 : 8;
                uint64_t *nco = (uint64_t *)realloc(skip_cover, nc * sizeof(*nco));
                uint64_t *nof = (uint64_t *)realloc(skip_off,   nc * sizeof(*nof));
                if (!nco || !nof) { free(nco); free(nof); goto cleanup; }
                skip_cover = nco; skip_off = nof; skip_cap = nc;
            }
            skip_cover[skip_count] = (uint64_t)i;
            skip_off[skip_count]   = (uint64_t)payload.size;
            skip_count++;
        }
        nonempty_count++;

        if (tier == TIER_ALL) {
            if (write_delta_list(&payload, exc_buf, n_exc) != 0) goto cleanup;
        } else { /* TIER_SPARSE */
            if (write_delta_list(&payload, a->slots, a->count) != 0) goto cleanup;
        }
    }

    /* Emit header. */
    if (ncdb_impl_buf_append(out, NTAS_MAGIC, NTAS_MAGIC_SZ) != 0) goto cleanup;
    if (ncdb_impl_buf_append(out, &version, 1) != 0) goto cleanup;
    if (ncdb_impl_buf_append(out, &reserved, 1) != 0) goto cleanup;
    if (ncdb_varint_encode_uint64((uint64_t)n_real, out) != 0) goto cleanup;
    if (ncdb_varint_encode_uint64((uint64_t)n_covers, out) != 0) goto cleanup;
    /* Real-test slot → global history-node-idx mapping, delta-encoded. */
    {
        size_t prev = 0;
        size_t slot;
        for (slot = 0; slot < n_real; slot++) {
            uint64_t delta = (uint64_t)(s2h[slot] - prev);
            if (ncdb_varint_encode_uint64(delta, out) != 0) goto cleanup;
            prev = s2h[slot];
        }
    }
    if (ncdb_impl_buf_append(out, tier_bits, tier_bytes) != 0) goto cleanup;
    if (ncdb_impl_buf_append(out, payload.data, payload.size) != 0) goto cleanup;
    if (ncdb_varint_encode_uint64((uint64_t)skip_count, out) != 0) goto cleanup;
    for (i = 0; i < skip_count; i++) {
        if (ncdb_varint_encode_uint64(skip_cover[i], out) != 0) goto cleanup;
        if (ncdb_varint_encode_uint64(skip_off[i],   out) != 0) goto cleanup;
    }
    db->feature_flags |= NCDB_FEATURE_TESTS_ASSOC;
    rc = 0;

cleanup:
    free(h2s); free(s2h); free(covers); free(tier_bits); free(exc_buf);
    free(skip_cover); free(skip_off);
    ncdb_impl_buf_free(&payload);
    return rc;
}

int ncdb_tests_assoc_deserialize(ncdbT db, const uint8_t *data, size_t size,
                                 char *errbuf, size_t errbuf_sz)
{
    size_t *h2s = NULL, *s2h_disk = NULL;
    size_t n_hist_local, n_real_local;
    ncdbCoverT *covers = NULL;
    size_t n_covers = 0, i;
    size_t off;
    uint8_t version;
    uint64_t n_real, n_covers_disk;
    uint32_t *test_slot_to_hist = NULL;   /* from on-disk mapping */
    int rc = -1;

    if (!db || !data || size < NTAS_MAGIC_SZ + 2) {
        if (errbuf) snprintf(errbuf, errbuf_sz, "%s", "NTAS too small");
        return -1;
    }
    if (memcmp(data, NTAS_MAGIC, NTAS_MAGIC_SZ) != 0) {
        if (errbuf) snprintf(errbuf, errbuf_sz, "%s", "bad NTAS magic");
        return -1;
    }
    off = NTAS_MAGIC_SZ;
    version = data[off++];
    /* skip reserved byte */ off++;
    if (version != NTAS_VERSION) {
        if (errbuf) snprintf(errbuf, errbuf_sz, "unknown NTAS version %u", (unsigned)version);
        return -1;
    }

    if (ncdb_varint_decode_uint64(data, size, &off, &n_real) != 0) goto bad;
    if (ncdb_varint_decode_uint64(data, size, &off, &n_covers_disk) != 0) goto bad;

    /* Real-test slot → global history index. */
    if (n_real > 0) {
        test_slot_to_hist = (uint32_t *)malloc((size_t)n_real * sizeof(uint32_t));
        if (!test_slot_to_hist) goto bad;
    }
    {
        uint64_t delta;
        uint64_t prev = 0, j;
        for (j = 0; j < n_real; j++) {
            if (ncdb_varint_decode_uint64(data, size, &off, &delta) != 0) goto bad;
            prev += delta;
            test_slot_to_hist[j] = (uint32_t)prev;
        }
    }

    /* Flatten covers in DFS order so we can match cover_idx → ncdbCoverT. */
    if (flatten_covers(db, &covers, &n_covers) != 0) goto bad;
    if (n_covers != (size_t)n_covers_disk) {
        if (errbuf) snprintf(errbuf, errbuf_sz, "%s",
                             "NTAS cover count mismatch — scope_tree differs from when NTAS was written");
        goto bad;
    }

    /* Tier-bits block. */
    {
        size_t tier_bytes = (n_covers + 3) / 4;
        const uint8_t *tier_bits;
        if (off + tier_bytes > size) goto bad;
        tier_bits = data + off;
        off += tier_bytes;

        /* Payload: per non-NEVER cover, in DFS order. */
        for (i = 0; i < n_covers; i++) {
            uint8_t tier = (uint8_t)((tier_bits[i / 4] >> ((i % 4) * 2)) & 0x3);
            uint32_t *list = NULL;
            size_t n_list = 0;
            if (tier == TIER_NEVER) continue;
            if (read_delta_list(data, size, &off, &list, &n_list) != 0) goto bad;
            if (tier == TIER_ALL) {
                /* ALL: list is exceptions. Materialize the hit-set
                 * as everything-except-exceptions. */
                size_t slot, ex_i = 0;
                covers[i]->assoc = (struct ncdb_assoc_s *)calloc(1, sizeof(*covers[i]->assoc));
                if (!covers[i]->assoc) { free(list); goto bad; }
                for (slot = 0; slot < n_real; slot++) {
                    if (ex_i < n_list && list[ex_i] == slot) { ex_i++; continue; }
                    if (assoc_insert_sorted(covers[i]->assoc, (uint32_t)slot) != 0) {
                        free(list); goto bad;
                    }
                }
            } else { /* TIER_SPARSE */
                size_t k;
                covers[i]->assoc = (struct ncdb_assoc_s *)calloc(1, sizeof(*covers[i]->assoc));
                if (!covers[i]->assoc) { free(list); goto bad; }
                for (k = 0; k < n_list; k++) {
                    if (assoc_insert_sorted(covers[i]->assoc, list[k]) != 0) {
                        free(list); goto bad;
                    }
                }
            }
            free(list);
        }
    }

    /* Skip table — currently parsed but not used; the deserializer
     * walks the payload linearly anyway. Reader can consult it for
     * random-access cover-N lookup once the query API needs it. */
    {
        uint64_t n_skip, j;
        if (ncdb_varint_decode_uint64(data, size, &off, &n_skip) != 0) goto bad;
        for (j = 0; j < n_skip; j++) {
            uint64_t a, b;
            if (ncdb_varint_decode_uint64(data, size, &off, &a) != 0) goto bad;
            if (ncdb_varint_decode_uint64(data, size, &off, &b) != 0) goto bad;
            (void)a; (void)b;
        }
    }

    /* Validate slot→hist mapping matches our local view of real-test
     * positions. If they diverge, history.bin was modified post-NTAS-
     * write and the assoc data is no longer trustworthy. */
    if (build_real_test_map(db, &h2s, &n_hist_local, &s2h_disk, &n_real_local) != 0)
        goto bad;
    if (n_real_local != (size_t)n_real) {
        if (errbuf) snprintf(errbuf, errbuf_sz, "%s",
                             "NTAS real-test count mismatch with history.bin");
        goto bad;
    }
    {
        size_t j;
        for (j = 0; j < (size_t)n_real; j++) {
            if (s2h_disk[j] != test_slot_to_hist[j]) {
                if (errbuf) snprintf(errbuf, errbuf_sz,
                                     "NTAS slot %zu maps to hist %u but local map says %zu",
                                     j, (unsigned)test_slot_to_hist[j], s2h_disk[j]);
                goto bad;
            }
        }
    }
    rc = 0;

bad:
    free(test_slot_to_hist);
    free(h2s); free(s2h_disk); free(covers);
    return rc;
}
