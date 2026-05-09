#include <stdio.h>

#include "cJSON.h"

#include "ncdb_history.h"

static const char *kind_to_str(uint32_t kind) {
    switch (kind) {
        case NCDB_HISTORY_MERGE: return "MERGE";
        case NCDB_HISTORY_TEST:
        default: return "TEST";
    }
}

static uint32_t kind_from_str(const char *s) {
    if (s && strcmp(s, "MERGE") == 0) {
        return NCDB_HISTORY_MERGE;
    }
    return NCDB_HISTORY_TEST;
}

int ncdb_history_serialize(ncdbT db, ncdbBuf *out) {
    cJSON *arr = cJSON_CreateArray();
    char *text;
    size_t i;
    if (!arr) {
        return -1;
    }
    for (i = 0; i < db->history_count; i++) {
        ncdbHistoryNodeT n = db->history_nodes[i];
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "logical_name", n->logical_name ? n->logical_name : "");
        if (n->physical_name) {
            cJSON_AddStringToObject(obj, "physical_name", n->physical_name);
        } else {
            cJSON_AddNullToObject(obj, "physical_name");
        }
        cJSON_AddStringToObject(obj, "kind", kind_to_str(n->kind));
        cJSON_AddNumberToObject(obj, "test_status", (double)n->test_status);
        cJSON_AddNumberToObject(obj, "sim_time", n->sim_time);
        if (n->time_unit) {
            cJSON_AddStringToObject(obj, "time_unit", n->time_unit);
        } else {
            cJSON_AddNullToObject(obj, "time_unit");
        }
        if (n->run_cwd) {
            cJSON_AddStringToObject(obj, "run_cwd", n->run_cwd);
        } else {
            cJSON_AddNullToObject(obj, "run_cwd");
        }
        cJSON_AddNumberToObject(obj, "cpu_time", n->cpu_time);
        if (n->seed) {
            cJSON_AddStringToObject(obj, "seed", n->seed);
        } else {
            cJSON_AddNullToObject(obj, "seed");
        }
        if (n->cmd) {
            cJSON_AddStringToObject(obj, "cmd", n->cmd);
        } else {
            cJSON_AddNullToObject(obj, "cmd");
        }
        if (n->args) {
            cJSON_AddStringToObject(obj, "args", n->args);
        } else {
            cJSON_AddNullToObject(obj, "args");
        }
        cJSON_AddNumberToObject(obj, "compulsory", (double)n->compulsory);
        cJSON_AddNullToObject(obj, "date");  /* date not stored in C impl */
        if (n->user_name) {
            cJSON_AddStringToObject(obj, "user_name", n->user_name);
        } else {
            cJSON_AddNullToObject(obj, "user_name");
        }
        cJSON_AddNumberToObject(obj, "cost", n->cost);
        if (n->tool_category) {
            cJSON_AddStringToObject(obj, "tool_category", n->tool_category);
        } else {
            cJSON_AddNullToObject(obj, "tool_category");
        }
        cJSON_AddNullToObject(obj, "ucis_version");
        if (n->vendor_id) {
            cJSON_AddStringToObject(obj, "vendor_id", n->vendor_id);
        } else {
            cJSON_AddNullToObject(obj, "vendor_id");
        }
        if (n->vendor_tool) {
            cJSON_AddStringToObject(obj, "vendor_tool", n->vendor_tool);
        } else {
            cJSON_AddNullToObject(obj, "vendor_tool");
        }
        if (n->vendor_tool_version) {
            cJSON_AddStringToObject(obj, "vendor_tool_version", n->vendor_tool_version);
        } else {
            cJSON_AddNullToObject(obj, "vendor_tool_version");
        }
        if (n->same_tests) {
            cJSON_AddStringToObject(obj, "same_tests", n->same_tests);
        } else {
            cJSON_AddNullToObject(obj, "same_tests");
        }
        if (n->comment) {
            cJSON_AddStringToObject(obj, "comment", n->comment);
        } else {
            cJSON_AddNullToObject(obj, "comment");
        }
        cJSON_AddItemToArray(arr, obj);
    }
    text = cJSON_Print(arr);
    cJSON_Delete(arr);
    if (!text) {
        return -1;
    }
    if (ncdb_impl_buf_append(out, text, strlen(text)) != 0) {
        cJSON_free(text);
        return -1;
    }
    cJSON_free(text);
    return 0;
}

int ncdb_history_deserialize(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz) {
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
        /* fall back to "test_name" for compatibility with older TypeScript-written files */
        if (!logical || !cJSON_IsString(logical)) {
            logical = cJSON_GetObjectItemCaseSensitive(it, "test_name");
        }
        if (!logical || !cJSON_IsString(logical) || !logical->valuestring) continue;
        node = ncdb_impl_create_history(db, kind_from_str(kind_s), logical->valuestring, physical_s);
        if (!node) {
            snprintf(errbuf, errbuf_sz, "%s", "out of memory");
            cJSON_Delete(arr);
            return -1;
        }
        {
            cJSON *ts = cJSON_GetObjectItemCaseSensitive(it, "test_status");
            if (ts && cJSON_IsNumber(ts)) node->test_status = (uint32_t)ts->valuedouble;
        }
        {
            cJSON *v = cJSON_GetObjectItemCaseSensitive(it, "sim_time");
            if (v && cJSON_IsNumber(v)) node->sim_time = v->valuedouble;
        }
        {
            cJSON *v = cJSON_GetObjectItemCaseSensitive(it, "cpu_time");
            if (v && cJSON_IsNumber(v)) node->cpu_time = v->valuedouble;
        }
        {
            cJSON *v = cJSON_GetObjectItemCaseSensitive(it, "cost");
            if (v && cJSON_IsNumber(v)) node->cost = v->valuedouble;
        }
        {
            cJSON *v = cJSON_GetObjectItemCaseSensitive(it, "compulsory");
            if (v && cJSON_IsNumber(v)) node->compulsory = (int)v->valuedouble;
        }
        {
#define READ_STR_FIELD(json_key, struct_field) \
            cJSON *v = cJSON_GetObjectItemCaseSensitive(it, json_key); \
            if (v && cJSON_IsString(v)) { \
                free(node->struct_field); \
                node->struct_field = ncdb_impl_strdup(v->valuestring); \
            }
            { READ_STR_FIELD("user_name", user_name) }
            { READ_STR_FIELD("seed", seed) }
            { READ_STR_FIELD("tool_category", tool_category) }
            { READ_STR_FIELD("comment", comment) }
            { READ_STR_FIELD("run_cwd", run_cwd) }
            { READ_STR_FIELD("cmd", cmd) }
            { READ_STR_FIELD("args", args) }
            { READ_STR_FIELD("time_unit", time_unit) }
            { READ_STR_FIELD("vendor_id", vendor_id) }
            { READ_STR_FIELD("vendor_tool", vendor_tool) }
            { READ_STR_FIELD("vendor_tool_version", vendor_tool_version) }
            { READ_STR_FIELD("same_tests", same_tests) }
#undef READ_STR_FIELD
        }
    }
    cJSON_Delete(arr);
    return 0;
}
