/*
 * fsm.bin — FSM-scope state-value overrides (binary v1).
 *
 * Format:
 *   magic[4]                  "NFSM"
 *   version: u8               = 1
 *   record_count: varint
 *     for each:
 *       scope_dfs_idx: varint
 *       state_count:   varint
 *         name: len-prefixed string
 *         value: varint
 *
 * State *names* and *hit counts* live in scope_tree.bin (FSMBIN cover items
 * under FSM_STATES). This module only persists the optional
 * UCIS_INT_FSM_STATEVAL numeric mapping (sparse). FSM scopes with all-default
 * sequential state values produce zero records, so the file is normally tiny
 * or absent.
 */

#include <stdio.h>
#include <string.h>

#include "ncdb_fsm.h"

#define NCDB_FSM_MAGIC    "NFSM"
#define NCDB_FSM_MAGIC_SZ 4U
#define NCDB_FSM_VERSION  1U

static int write_lenprefixed_str(ncdbBuf *b, const char *s) {
    size_t len = s ? strlen(s) : 0;
    if (ncdb_varint_encode_uint64((uint64_t)len, b) != 0) return -1;
    if (len > 0 && ncdb_impl_buf_append(b, s, len) != 0) return -1;
    return 0;
}

static int read_lenprefixed_strdup(const uint8_t *data, size_t size, size_t *off, char **out) {
    uint64_t len;
    char *buf;
    if (ncdb_varint_decode_uint64(data, size, off, &len) != 0) return -1;
    if (*off + len > size) return -1;
    buf = (char *)malloc((size_t)len + 1);
    if (!buf) return -1;
    if (len > 0) memcpy(buf, data + *off, (size_t)len);
    buf[len] = '\0';
    *off += (size_t)len;
    free(*out); *out = buf;
    return 0;
}

int ncdb_fsm_serialize(ncdbT db, ncdbBuf *out) {
    ncdbScopeT *flat = NULL;
    size_t total = 0, i, j, records = 0;
    uint8_t version = NCDB_FSM_VERSION;
    int rc = -1;

    if (!db || !out) return -1;
    if (ncdb_impl_dfs_flatten(db, &flat, &total) != 0) return -1;

    for (i = 0; i < total; ++i)
        if (flat[i]->type == NCDB_SCOPE_FSM && flat[i]->fsm_state_count > 0) records++;
    if (records == 0) { rc = 0; goto cleanup; }

    if (ncdb_impl_buf_append(out, NCDB_FSM_MAGIC, NCDB_FSM_MAGIC_SZ) != 0) goto cleanup;
    if (ncdb_impl_buf_append(out, &version, 1) != 0) goto cleanup;
    if (ncdb_varint_encode_uint64((uint64_t)records, out) != 0) goto cleanup;

    for (i = 0; i < total; ++i) {
        ncdbScopeT s = flat[i];
        if (s->type != NCDB_SCOPE_FSM || s->fsm_state_count == 0) continue;
        if (ncdb_varint_encode_uint64((uint64_t)i, out) != 0) goto cleanup;
        if (ncdb_varint_encode_uint64((uint64_t)s->fsm_state_count, out) != 0) goto cleanup;
        for (j = 0; j < s->fsm_state_count; ++j) {
            if (write_lenprefixed_str(out, s->fsm_states[j].name) != 0) goto cleanup;
            if (ncdb_varint_encode_uint64((uint64_t)s->fsm_states[j].value, out) != 0) goto cleanup;
        }
    }
    rc = 0;
cleanup:
    free(flat);
    return rc;
}

int ncdb_fsm_deserialize(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz) {
    size_t off = 0, total = 0, i, j;
    uint8_t version;
    uint64_t n, k, sc, val;
    ncdbScopeT *flat = NULL;
    int rc = -1;
    (void)errbuf; (void)errbuf_sz;

    if (!db) return -1;
    if (size < NCDB_FSM_MAGIC_SZ + 1) return 0;
    if (memcmp(data, NCDB_FSM_MAGIC, NCDB_FSM_MAGIC_SZ) != 0) return 0;
    off = NCDB_FSM_MAGIC_SZ;
    if (off >= size) return -1;
    version = data[off++];
    if (version != NCDB_FSM_VERSION) return 0;

    if (ncdb_impl_dfs_flatten(db, &flat, &total) != 0) return -1;
    if (ncdb_varint_decode_uint64(data, size, &off, &n) != 0) goto cleanup;

    for (i = 0; i < n; ++i) {
        ncdbScopeT s;
        if (ncdb_varint_decode_uint64(data, size, &off, &k) != 0) goto cleanup;
        if (k >= total) goto cleanup;
        s = flat[k];
        if (ncdb_varint_decode_uint64(data, size, &off, &sc) != 0) goto cleanup;
        /* Grow capacity */
        if (sc > 0) {
            struct ncdb_fsm_state_s *arr =
                (struct ncdb_fsm_state_s *)calloc((size_t)sc, sizeof(*arr));
            if (!arr) goto cleanup;
            for (j = 0; j < sc; ++j) {
                if (read_lenprefixed_strdup(data, size, &off, &arr[j].name) != 0) {
                    size_t r;
                    for (r = 0; r <= j; ++r) free(arr[r].name);
                    free(arr); goto cleanup;
                }
                if (ncdb_varint_decode_uint64(data, size, &off, &val) != 0) {
                    size_t r;
                    for (r = 0; r <= j; ++r) free(arr[r].name);
                    free(arr); goto cleanup;
                }
                arr[j].value = (uint32_t)val;
            }
            /* Free any prior overrides on this scope, then take ownership */
            { size_t r; for (r = 0; r < s->fsm_state_count; ++r) free(s->fsm_states[r].name); }
            free(s->fsm_states);
            s->fsm_states = arr;
            s->fsm_state_count = (size_t)sc;
            s->fsm_state_cap = (size_t)sc;
        }
    }
    rc = 0;
cleanup:
    free(flat);
    return rc;
}
