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
        cJSON_AddNumberToObject(obj, "test_status", 0);
        cJSON_AddNullToObject(obj, "sim_time");
        cJSON_AddNullToObject(obj, "time_unit");
        cJSON_AddNullToObject(obj, "run_cwd");
        cJSON_AddNullToObject(obj, "cpu_time");
        cJSON_AddNullToObject(obj, "seed");
        cJSON_AddNullToObject(obj, "cmd");
        cJSON_AddNullToObject(obj, "args");
        cJSON_AddNullToObject(obj, "compulsory");
        cJSON_AddNullToObject(obj, "date");
        cJSON_AddNullToObject(obj, "user_name");
        cJSON_AddNullToObject(obj, "cost");
        cJSON_AddNullToObject(obj, "tool_category");
        cJSON_AddNullToObject(obj, "ucis_version");
        cJSON_AddNullToObject(obj, "vendor_id");
        cJSON_AddNullToObject(obj, "vendor_tool");
        cJSON_AddNullToObject(obj, "vendor_tool_version");
        cJSON_AddNullToObject(obj, "same_tests");
        cJSON_AddNullToObject(obj, "comment");
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
        cJSON *kind = cJSON_GetObjectItemCaseSensitive(it, "kind");
        const char *physical_s = (physical && cJSON_IsString(physical)) ? physical->valuestring : NULL;
        const char *kind_s = (kind && cJSON_IsString(kind)) ? kind->valuestring : "TEST";
        /* fall back to "test_name" for compatibility with older TypeScript-written files */
        if (!logical || !cJSON_IsString(logical)) {
            logical = cJSON_GetObjectItemCaseSensitive(it, "test_name");
        }
        if (logical && cJSON_IsString(logical) && logical->valuestring) {
            if (!ncdb_impl_create_history(db, kind_from_str(kind_s), logical->valuestring, physical_s)) {
                snprintf(errbuf, errbuf_sz, "%s", "out of memory");
                cJSON_Delete(arr);
                return -1;
            }
        }
    }
    cJSON_Delete(arr);
    return 0;
}
