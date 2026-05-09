#include <stdio.h>

#include "cJSON.h"

#include "ncdb_sources.h"

int ncdb_sources_serialize(ncdbT db, ncdbBuf *out) {
    cJSON *arr = cJSON_CreateArray();
    char *text;
    size_t i;
    if (!arr) {
        return -1;
    }
    for (i = 0; i < db->source_count; i++) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(db->sources[i]));
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

int ncdb_sources_deserialize(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz) {
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
                cJSON_Delete(arr);
                return -1;
            }
        }
    }
    cJSON_Delete(arr);
    return 0;
}
