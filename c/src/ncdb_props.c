/*
 * ncdb_props.c — typed-property table (Phase 4.2 / M2).
 *
 * Numeric-keyed per-scope / per-cover property storage. See
 * `c/include/ncdb/ncdb_props.h` for the ID registry and range policy.
 *
 * Serialized format (no magic — gated by NCDB_PRESENCE_TYPED_PROPS on
 * the scope or NCDB_COVER_PRESENCE_TYPED_PROPS on the cover):
 *
 *   count: varint
 *   for i in 0..count:
 *     prop_id:  varint  (semantically u16 but encoded as varint for
 *                        compactness — IDs <= 127 cost 1 byte each)
 *     type_tag: u8      (1=INT32, 2=INT64, 3=DOUBLE, 4=STRING)
 *     value:    depends on type_tag
 *       INT32:  sint32 (zigzag varint)
 *       INT64:  sint64 (zigzag varint)
 *       DOUBLE: 8 bytes LE
 *       STRING: varint string-table index
 *
 * Unknown prop_id values are read but kept opaquely so a v4 reader on a
 * newer-than-itself fixture round-trips without dropping data.
 */

#include "ncdb_impl.h"
#include "ncdb/ncdb_props.h"

#include <stdlib.h>
#include <string.h>

void ncdb_prop_table_init(ncdb_prop_table_t *t) {
    if (t) memset(t, 0, sizeof(*t));
}

void ncdb_prop_table_free(ncdb_prop_table_t *t) {
    size_t i;
    if (!t) return;
    for (i = 0; i < t->count; i++) {
        if (t->entries[i].type_tag == NCDB_PROP_TYPE_STRING) {
            free(t->entries[i].v.str);
        }
    }
    free(t->entries);
    memset(t, 0, sizeof(*t));
}

static ncdb_prop_entry_t *find_entry(ncdb_prop_table_t *t, uint16_t id) {
    size_t i;
    for (i = 0; i < t->count; i++) {
        if (t->entries[i].prop_id == id) return &t->entries[i];
    }
    return NULL;
}

static const ncdb_prop_entry_t *find_entry_c(const ncdb_prop_table_t *t, uint16_t id) {
    size_t i;
    for (i = 0; i < t->count; i++) {
        if (t->entries[i].prop_id == id) return &t->entries[i];
    }
    return NULL;
}

/* Reserve room for a new entry; returns a pointer to it (uninitialized
 * beyond prop_id). Caller fills type_tag + value. */
static ncdb_prop_entry_t *reserve_entry(ncdb_prop_table_t *t, uint16_t id) {
    ncdb_prop_entry_t *e = find_entry(t, id);
    if (e) {
        if (e->type_tag == NCDB_PROP_TYPE_STRING) {
            free(e->v.str);
            e->v.str = NULL;
        }
        return e;
    }
    if (t->count >= t->cap) {
        size_t new_cap = t->cap ? t->cap * 2U : 4U;
        ncdb_prop_entry_t *na = (ncdb_prop_entry_t *)realloc(
            t->entries, new_cap * sizeof(*na));
        if (!na) return NULL;
        t->entries = na;
        t->cap = new_cap;
    }
    e = &t->entries[t->count++];
    memset(e, 0, sizeof(*e));
    e->prop_id = id;
    return e;
}

int ncdb_prop_table_set_int32(ncdb_prop_table_t *t, uint16_t id, int32_t v) {
    ncdb_prop_entry_t *e;
    if (!t) return -1;
    e = reserve_entry(t, id);
    if (!e) return -1;
    e->type_tag = NCDB_PROP_TYPE_INT32;
    e->v.i32 = v;
    return 0;
}

int ncdb_prop_table_set_int64(ncdb_prop_table_t *t, uint16_t id, int64_t v) {
    ncdb_prop_entry_t *e;
    if (!t) return -1;
    e = reserve_entry(t, id);
    if (!e) return -1;
    e->type_tag = NCDB_PROP_TYPE_INT64;
    e->v.i64 = v;
    return 0;
}

int ncdb_prop_table_set_double(ncdb_prop_table_t *t, uint16_t id, double v) {
    ncdb_prop_entry_t *e;
    if (!t) return -1;
    e = reserve_entry(t, id);
    if (!e) return -1;
    e->type_tag = NCDB_PROP_TYPE_DOUBLE;
    e->v.d = v;
    return 0;
}

int ncdb_prop_table_set_string(ncdb_prop_table_t *t, uint16_t id, const char *s) {
    ncdb_prop_entry_t *e;
    char *copy;
    if (!t) return -1;
    copy = ncdb_impl_strdup(s ? s : "");
    if (!copy) return -1;
    e = reserve_entry(t, id);
    if (!e) { free(copy); return -1; }
    e->type_tag = NCDB_PROP_TYPE_STRING;
    e->v.str = copy;
    return 0;
}

int ncdb_prop_table_get_int(const ncdb_prop_table_t *t, uint16_t id, int64_t *out) {
    const ncdb_prop_entry_t *e;
    if (!t || !out) return -1;
    e = find_entry_c(t, id);
    if (!e) return -1;
    switch (e->type_tag) {
    case NCDB_PROP_TYPE_INT32: *out = (int64_t)e->v.i32; return 0;
    case NCDB_PROP_TYPE_INT64: *out = e->v.i64;          return 0;
    default: return -1;
    }
}

int ncdb_prop_table_get_double(const ncdb_prop_table_t *t, uint16_t id, double *out) {
    const ncdb_prop_entry_t *e;
    if (!t || !out) return -1;
    e = find_entry_c(t, id);
    if (!e || e->type_tag != NCDB_PROP_TYPE_DOUBLE) return -1;
    *out = e->v.d;
    return 0;
}

const char *ncdb_prop_table_get_string(const ncdb_prop_table_t *t, uint16_t id) {
    const ncdb_prop_entry_t *e;
    if (!t) return NULL;
    e = find_entry_c(t, id);
    if (!e || e->type_tag != NCDB_PROP_TYPE_STRING) return NULL;
    return e->v.str;
}

int ncdb_prop_table_remove(ncdb_prop_table_t *t, uint16_t id) {
    size_t i;
    if (!t) return -1;
    for (i = 0; i < t->count; i++) {
        if (t->entries[i].prop_id == id) {
            if (t->entries[i].type_tag == NCDB_PROP_TYPE_STRING) {
                free(t->entries[i].v.str);
            }
            if (i + 1 < t->count) {
                memmove(&t->entries[i], &t->entries[i + 1],
                        (t->count - i - 1) * sizeof(t->entries[0]));
            }
            t->count--;
            return 0;
        }
    }
    return -1;
}

/* --- Serialize / deserialize --------------------------------------- */

/* Zigzag encoding for signed varints. */
static uint64_t zz_encode64(int64_t v) {
    return (uint64_t)((v << 1) ^ (v >> 63));
}
static int64_t zz_decode64(uint64_t v) {
    return (int64_t)((v >> 1) ^ -(int64_t)(v & 1));
}

static int write_u64le_p(ncdbBuf *b, uint64_t v) {
    uint8_t bytes[8]; int i;
    for (i = 0; i < 8; ++i) bytes[i] = (uint8_t)(v >> (i * 8));
    return ncdb_impl_buf_append(b, bytes, 8);
}
static int read_u64le_p(const uint8_t *data, size_t size, size_t *off, uint64_t *out) {
    int i; uint64_t v = 0;
    if (*off + 8 > size) return -1;
    for (i = 0; i < 8; ++i) v |= ((uint64_t)data[*off + i]) << (i * 8);
    *off += 8; *out = v;
    return 0;
}

int ncdb_prop_table_serialize(const ncdb_prop_table_t *t,
                              ncdbStringTable *strings, ncdbBuf *out)
{
    size_t i;
    if (!t || !out) return -1;
    if (ncdb_varint_encode_uint64((uint64_t)t->count, out) != 0) return -1;
    for (i = 0; i < t->count; i++) {
        const ncdb_prop_entry_t *e = &t->entries[i];
        uint8_t tag = e->type_tag;
        if (ncdb_varint_encode_uint64((uint64_t)e->prop_id, out) != 0) return -1;
        if (ncdb_impl_buf_append(out, &tag, 1) != 0) return -1;
        switch (e->type_tag) {
        case NCDB_PROP_TYPE_INT32:
            if (ncdb_varint_encode_uint64(zz_encode64((int64_t)e->v.i32), out) != 0) return -1;
            break;
        case NCDB_PROP_TYPE_INT64:
            if (ncdb_varint_encode_uint64(zz_encode64(e->v.i64), out) != 0) return -1;
            break;
        case NCDB_PROP_TYPE_DOUBLE: {
            union { double d; uint64_t u; } cu;
            cu.d = e->v.d;
            if (write_u64le_p(out, cu.u) != 0) return -1;
            break;
        }
        case NCDB_PROP_TYPE_STRING: {
            int idx;
            if (!strings) return -1;
            idx = ncdb_strings_add(strings, e->v.str ? e->v.str : "");
            if (idx < 0) return -1;
            if (ncdb_varint_encode_uint64((uint64_t)idx, out) != 0) return -1;
            break;
        }
        default:
            /* Unknown tag — should not be reachable from setters, but
             * round-trip via the opaque encoding would still work if we
             * stored raw bytes. For now reject. */
            return -1;
        }
    }
    return 0;
}

int ncdb_prop_table_deserialize(ncdb_prop_table_t *t,
                                const uint8_t *data, size_t size, size_t *off,
                                const ncdbStringTable *strings)
{
    uint64_t count, i;
    if (!t || !data || !off) return -1;
    if (ncdb_varint_decode_uint64(data, size, off, &count) != 0) return -1;
    for (i = 0; i < count; i++) {
        uint64_t id;
        uint8_t tag;
        if (ncdb_varint_decode_uint64(data, size, off, &id) != 0) return -1;
        if (*off + 1 > size) return -1;
        tag = data[(*off)++];
        switch (tag) {
        case NCDB_PROP_TYPE_INT32: {
            uint64_t enc;
            if (ncdb_varint_decode_uint64(data, size, off, &enc) != 0) return -1;
            if (ncdb_prop_table_set_int32(t, (uint16_t)id, (int32_t)zz_decode64(enc)) != 0) return -1;
            break;
        }
        case NCDB_PROP_TYPE_INT64: {
            uint64_t enc;
            if (ncdb_varint_decode_uint64(data, size, off, &enc) != 0) return -1;
            if (ncdb_prop_table_set_int64(t, (uint16_t)id, zz_decode64(enc)) != 0) return -1;
            break;
        }
        case NCDB_PROP_TYPE_DOUBLE: {
            uint64_t bits;
            union { double d; uint64_t u; } cu;
            if (read_u64le_p(data, size, off, &bits) != 0) return -1;
            cu.u = bits;
            if (ncdb_prop_table_set_double(t, (uint16_t)id, cu.d) != 0) return -1;
            break;
        }
        case NCDB_PROP_TYPE_STRING: {
            uint64_t sidx;
            const char *s;
            if (ncdb_varint_decode_uint64(data, size, off, &sidx) != 0) return -1;
            s = ncdb_strings_get(strings, sidx);
            if (ncdb_prop_table_set_string(t, (uint16_t)id, s ? s : "") != 0) return -1;
            break;
        }
        default:
            /* Unknown tag from a newer writer: bail. Future revs could
             * add a "skip-bytes" varint between tag and payload to make
             * unknown entries truly opaque. */
            return -1;
        }
    }
    return 0;
}
