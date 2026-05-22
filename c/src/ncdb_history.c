/*
 * history.json — UCIS history-node tree (binary v1, magic "NHIS").
 *
 * Format:
 *   magic[4]      "NHIS"
 *   version: u8   = 1
 *   node_count: varint
 *   for each node:
 *     kind: u8                            (1 = TEST, 2 = MERGE)
 *     parent_plus_1: varint               (0 = no parent; else 1-based parent idx)
 *     test_status: varint
 *     compulsory:  u8
 *     sim_time:    8 bytes LE (double bitpattern)
 *     cpu_time:    8 bytes LE (double bitpattern)
 *     cost:        8 bytes LE (double bitpattern)
 *     15 length-prefixed strings (varint length, raw bytes):
 *       logical_name, physical_name, user_name, seed, tool_category,
 *       comment, date, run_cwd, cmd, args, time_unit,
 *       vendor_id, vendor_tool, vendor_tool_version, same_tests
 *
 * Reader auto-detects legacy JSON arrays (first byte '[') for back-compat
 * with existing fixtures and Python-written archives.
 */

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

#include "ncdb_history.h"

#define NCDB_HIST_MAGIC    "NHIS"
#define NCDB_HIST_MAGIC_SZ 4U
#define NCDB_HIST_VERSION  1U

static int write_u8(ncdbBuf *b, uint8_t v) { return ncdb_impl_buf_append(b, &v, 1); }

static int write_u64le(ncdbBuf *b, uint64_t v) {
    uint8_t bytes[8]; int i;
    for (i = 0; i < 8; ++i) bytes[i] = (uint8_t)(v >> (i * 8));
    return ncdb_impl_buf_append(b, bytes, 8);
}

static int read_u64le(const uint8_t *data, size_t size, size_t *off, uint64_t *out) {
    int i; uint64_t v = 0;
    if (*off + 8 > size) return -1;
    for (i = 0; i < 8; ++i) v |= ((uint64_t)data[*off + i]) << (i * 8);
    *off += 8; *out = v;
    return 0;
}

static int write_lp(ncdbBuf *b, const char *s) {
    size_t len = s ? strlen(s) : 0;
    if (ncdb_varint_encode_uint64((uint64_t)len, b) != 0) return -1;
    if (len > 0 && ncdb_impl_buf_append(b, s, len) != 0) return -1;
    return 0;
}

static int read_lp_dup(const uint8_t *data, size_t size, size_t *off, char **out) {
    uint64_t len; char *buf;
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

int ncdb_history_serialize(ncdbT db, ncdbBuf *out) {
    uint8_t version = NCDB_HIST_VERSION;
    size_t i;
    union { double d; uint64_t u; } cu;

    if (!db || !out) return -1;
    if (ncdb_impl_buf_append(out, NCDB_HIST_MAGIC, NCDB_HIST_MAGIC_SZ) != 0) return -1;
    if (write_u8(out, version) != 0) return -1;
    if (ncdb_varint_encode_uint64((uint64_t)db->history_count, out) != 0) return -1;

    for (i = 0; i < db->history_count; ++i) {
        ncdbHistoryNodeT n = db->history_nodes[i];
        uint64_t parent_plus_1 = (n->parent_idx == (size_t)-1) ? 0U : (uint64_t)(n->parent_idx + 1U);
        if (write_u8(out, (uint8_t)n->kind) != 0) return -1;
        if (ncdb_varint_encode_uint64(parent_plus_1, out) != 0) return -1;
        if (ncdb_varint_encode_uint64((uint64_t)n->test_status, out) != 0) return -1;
        if (write_u8(out, (uint8_t)(n->compulsory ? 1 : 0)) != 0) return -1;
        cu.d = n->sim_time; if (write_u64le(out, cu.u) != 0) return -1;
        cu.d = n->cpu_time; if (write_u64le(out, cu.u) != 0) return -1;
        cu.d = n->cost;     if (write_u64le(out, cu.u) != 0) return -1;
        if (write_lp(out, n->logical_name) != 0) return -1;
        if (write_lp(out, n->physical_name) != 0) return -1;
        if (write_lp(out, n->user_name) != 0) return -1;
        if (write_lp(out, n->seed) != 0) return -1;
        if (write_lp(out, n->tool_category) != 0) return -1;
        if (write_lp(out, n->comment) != 0) return -1;
        if (write_lp(out, n->date) != 0) return -1;
        if (write_lp(out, n->run_cwd) != 0) return -1;
        if (write_lp(out, n->cmd) != 0) return -1;
        if (write_lp(out, n->args) != 0) return -1;
        if (write_lp(out, n->time_unit) != 0) return -1;
        if (write_lp(out, n->vendor_id) != 0) return -1;
        if (write_lp(out, n->vendor_tool) != 0) return -1;
        if (write_lp(out, n->vendor_tool_version) != 0) return -1;
        if (write_lp(out, n->same_tests) != 0) return -1;
    }
    return 0;
}

static uint32_t kind_from_str(const char *s) {
    if (s && strcmp(s, "MERGE") == 0) return NCDB_HISTORY_MERGE;
    return NCDB_HISTORY_TEST;
}

static int deserialize_json_fallback(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz) {
    cJSON *arr = cJSON_ParseWithLength((const char *)data, size);
    cJSON *it;
    if (!arr || !cJSON_IsArray(arr)) {
        snprintf(errbuf, errbuf_sz, "%s", "invalid history json");
        cJSON_Delete(arr);
        return -1;
    }
    cJSON_ArrayForEach(it, arr) {
        cJSON *logical = cJSON_GetObjectItemCaseSensitive(it, "logical_name");
        cJSON *physical = cJSON_GetObjectItemCaseSensitive(it, "physical_name");
        cJSON *kind_j = cJSON_GetObjectItemCaseSensitive(it, "kind");
        const char *physical_s = (physical && cJSON_IsString(physical)) ? physical->valuestring : NULL;
        const char *kind_s = (kind_j && cJSON_IsString(kind_j)) ? kind_j->valuestring : "TEST";
        ncdbHistoryNodeT node;
        if (!logical || !cJSON_IsString(logical)) {
            logical = cJSON_GetObjectItemCaseSensitive(it, "test_name");
        }
        if (!logical || !cJSON_IsString(logical) || !logical->valuestring) continue;
        node = ncdb_impl_create_history(db, kind_from_str(kind_s), logical->valuestring, physical_s);
        if (!node) { snprintf(errbuf, errbuf_sz, "%s", "out of memory"); cJSON_Delete(arr); return -1; }
        { cJSON *v = cJSON_GetObjectItemCaseSensitive(it, "test_status"); if (v && cJSON_IsNumber(v)) node->test_status = (uint32_t)v->valuedouble; }
        { cJSON *v = cJSON_GetObjectItemCaseSensitive(it, "sim_time");    if (v && cJSON_IsNumber(v)) node->sim_time    = v->valuedouble; }
        { cJSON *v = cJSON_GetObjectItemCaseSensitive(it, "cpu_time");    if (v && cJSON_IsNumber(v)) node->cpu_time    = v->valuedouble; }
        { cJSON *v = cJSON_GetObjectItemCaseSensitive(it, "cost");        if (v && cJSON_IsNumber(v)) node->cost        = v->valuedouble; }
        { cJSON *v = cJSON_GetObjectItemCaseSensitive(it, "compulsory");  if (v && cJSON_IsNumber(v)) node->compulsory  = (int)v->valuedouble; }
#define READ_STR(json_key, field) do { \
        cJSON *v = cJSON_GetObjectItemCaseSensitive(it, json_key); \
        if (v && cJSON_IsString(v)) { free(node->field); node->field = ncdb_impl_strdup(v->valuestring); } \
    } while (0)
        READ_STR("user_name", user_name);
        READ_STR("seed", seed);
        READ_STR("tool_category", tool_category);
        READ_STR("comment", comment);
        READ_STR("run_cwd", run_cwd);
        READ_STR("cmd", cmd);
        READ_STR("args", args);
        READ_STR("time_unit", time_unit);
        READ_STR("vendor_id", vendor_id);
        READ_STR("vendor_tool", vendor_tool);
        READ_STR("vendor_tool_version", vendor_tool_version);
        READ_STR("same_tests", same_tests);
        READ_STR("date", date);
#undef READ_STR
    }
    cJSON_Delete(arr);
    return 0;
}

int ncdb_history_deserialize(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz) {
    size_t off, i;
    uint8_t version;
    uint64_t n, kind, status, parent_p1;
    union { double d; uint64_t u; } cu;

    if (!db) return -1;
    if (size < NCDB_HIST_MAGIC_SZ + 1 ||
        memcmp(data, NCDB_HIST_MAGIC, NCDB_HIST_MAGIC_SZ) != 0) {
        return deserialize_json_fallback(db, data, size, errbuf, errbuf_sz);
    }
    off = NCDB_HIST_MAGIC_SZ;
    version = data[off++];
    if (version != NCDB_HIST_VERSION) {
        snprintf(errbuf, errbuf_sz, "unknown history binary version %u", (unsigned)version);
        return -1;
    }
    if (ncdb_varint_decode_uint64(data, size, &off, &n) != 0) goto bad;
    for (i = 0; i < n; ++i) {
        ncdbHistoryNodeT node;
        uint8_t kbyte;
        if (off + 1 > size) goto bad;
        kbyte = data[off++]; kind = kbyte;
        if (ncdb_varint_decode_uint64(data, size, &off, &parent_p1) != 0) goto bad;
        if (ncdb_varint_decode_uint64(data, size, &off, &status) != 0) goto bad;
        if (off + 1 > size) goto bad;
        node = ncdb_impl_create_history(db, (uint32_t)kind, "", NULL);
        if (!node) { snprintf(errbuf, errbuf_sz, "%s", "out of memory"); return -1; }
        node->test_status = (uint32_t)status;
        node->parent_idx = parent_p1 ? (size_t)(parent_p1 - 1U) : (size_t)-1;
        node->compulsory = data[off++];
        if (read_u64le(data, size, &off, &cu.u) != 0) goto bad; node->sim_time = cu.d;
        if (read_u64le(data, size, &off, &cu.u) != 0) goto bad; node->cpu_time = cu.d;
        if (read_u64le(data, size, &off, &cu.u) != 0) goto bad; node->cost     = cu.d;
        if (read_lp_dup(data, size, &off, &node->logical_name)        != 0) goto bad;
        if (read_lp_dup(data, size, &off, &node->physical_name)       != 0) goto bad;
        if (read_lp_dup(data, size, &off, &node->user_name)           != 0) goto bad;
        if (read_lp_dup(data, size, &off, &node->seed)                != 0) goto bad;
        if (read_lp_dup(data, size, &off, &node->tool_category)       != 0) goto bad;
        if (read_lp_dup(data, size, &off, &node->comment)             != 0) goto bad;
        if (read_lp_dup(data, size, &off, &node->date)                != 0) goto bad;
        if (read_lp_dup(data, size, &off, &node->run_cwd)             != 0) goto bad;
        if (read_lp_dup(data, size, &off, &node->cmd)                 != 0) goto bad;
        if (read_lp_dup(data, size, &off, &node->args)                != 0) goto bad;
        if (read_lp_dup(data, size, &off, &node->time_unit)           != 0) goto bad;
        if (read_lp_dup(data, size, &off, &node->vendor_id)           != 0) goto bad;
        if (read_lp_dup(data, size, &off, &node->vendor_tool)         != 0) goto bad;
        if (read_lp_dup(data, size, &off, &node->vendor_tool_version) != 0) goto bad;
        if (read_lp_dup(data, size, &off, &node->same_tests)          != 0) goto bad;
    }
    return 0;
bad:
    snprintf(errbuf, errbuf_sz, "%s", "truncated history binary");
    return -1;
}
