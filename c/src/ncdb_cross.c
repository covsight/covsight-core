#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "ncdb_cross.h"
#include "ncdb_scope_tree.h"

#define CROSS_VERSION 1

/* -------------------------------------------------------------------------
 * DFS scope list — matches scope_tree.bin encoding order.
 * Toggle-pair BRANCH scopes are included; their children are skipped.
 * ---------------------------------------------------------------------- */

static int is_toggle_pair_x(ncdbScopeT scope) {
    size_t i;
    int have01 = 0, have10 = 0;
    if (!scope || scope->type != NCDB_SCOPE_BRANCH || scope->child_count != 0 || scope->cover_count != 2) return 0;
    for (i = 0; i < scope->cover_count; i++) {
        if (scope->covers[i]->type != NCDB_COVER_TOGGLEBIN) return 0;
        if (strcmp(scope->covers[i]->name, NCDB_TOGGLE_BIN_0_TO_1) == 0) have01 = 1;
        if (strcmp(scope->covers[i]->name, NCDB_TOGGLE_BIN_1_TO_0) == 0) have10 = 1;
    }
    return have01 && have10;
}

/* Build a flat DFS-ordered array of scopes. Returns count; caller frees *out. */
static int build_dfs_index(ncdbT db, ncdbScopeT **out, size_t *count) {
    /* Use a simple expandable stack for iterative DFS */
    ncdbScopeT *stack = NULL;
    size_t stack_top = 0, stack_cap = 0;
    ncdbScopeT *result = NULL;
    size_t result_count = 0, result_cap = 0;
    size_t i;

#define PUSH(s) do { \
    if (stack_top >= stack_cap) { \
        size_t nc = stack_cap ? stack_cap * 2U : 64U; \
        ncdbScopeT *tmp = (ncdbScopeT*)realloc(stack, nc * sizeof(ncdbScopeT)); \
        if (!tmp) { free(stack); free(result); return -1; } \
        stack = tmp; stack_cap = nc; \
    } \
    stack[stack_top++] = (s); \
} while (0)

#define PUSH_RESULT(s) do { \
    if (result_count >= result_cap) { \
        size_t nc = result_cap ? result_cap * 2U : 64U; \
        ncdbScopeT *tmp = (ncdbScopeT*)realloc(result, nc * sizeof(ncdbScopeT)); \
        if (!tmp) { free(stack); free(result); return -1; } \
        result = tmp; result_cap = nc; \
    } \
    result[result_count++] = (s); \
} while (0)

    /* Push top-level scopes in reverse order so we pop them in forward order */
    for (i = db->root_count; i > 0; i--) PUSH(db->roots[i - 1]);

    while (stack_top > 0) {
        ncdbScopeT scope = stack[--stack_top];
        PUSH_RESULT(scope);
        if (!is_toggle_pair_x(scope)) {
            for (i = scope->child_count; i > 0; i--) PUSH(scope->children[i - 1]);
        }
    }

#undef PUSH
#undef PUSH_RESULT

    free(stack);
    *out = result;
    *count = result_count;
    return 0;
}

/* -------------------------------------------------------------------------
 * Serializer
 * ---------------------------------------------------------------------- */

int ncdb_cross_serialize(ncdbT db, ncdbBuf *out) {
    ncdbScopeT *scopes = NULL;
    size_t scope_count = 0, i, j;
    cJSON *arr = NULL;
    char *text = NULL;
    cJSON *payload;
    int has_entries = 0;

    if (build_dfs_index(db, &scopes, &scope_count) != 0) return -1;

    arr = cJSON_CreateArray();
    if (!arr) { free(scopes); return -1; }

    for (i = 0; i < scope_count; i++) {
        ncdbScopeT scope = scopes[i];
        cJSON *entry, *crossed;
        if (scope->type != NCDB_SCOPE_CROSS || scope->crossed_count == 0) continue;
        entry = cJSON_CreateObject();
        crossed = cJSON_CreateArray();
        cJSON_AddNumberToObject(entry, "idx", (double)i);
        for (j = 0; j < scope->crossed_count; j++) {
            cJSON_AddItemToArray(crossed, cJSON_CreateString(scopes[i]->crossed_points[j]->name));
        }
        cJSON_AddItemToObject(entry, "crossed", crossed);
        cJSON_AddItemToArray(arr, entry);
        has_entries = 1;
    }
    free(scopes);

    if (!has_entries) {
        cJSON_Delete(arr);
        return 0;  /* empty — caller skips adding cross.bin member */
    }

    payload = cJSON_CreateObject();
    cJSON_AddNumberToObject(payload, "version", CROSS_VERSION);
    cJSON_AddItemToObject(payload, "entries", arr);
    text = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    if (!text) return -1;
    if (ncdb_impl_buf_append(out, text, strlen(text)) != 0) { cJSON_free(text); return -1; }
    cJSON_free(text);
    return 1;  /* 1 = entries were written */
}

/* -------------------------------------------------------------------------
 * Deserializer
 * ---------------------------------------------------------------------- */

int ncdb_cross_deserialize(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz) {
    ncdbScopeT *scopes = NULL;
    size_t scope_count = 0;
    cJSON *payload, *entries_j, *entry;
    int version;

    if (!data || size == 0) return 0;

    payload = cJSON_ParseWithLength((const char *)data, size);
    if (!payload) {
        snprintf(errbuf, errbuf_sz, "invalid cross.bin JSON");
        return -1;
    }

    {
        cJSON *v = cJSON_GetObjectItemCaseSensitive(payload, "version");
        version = (v && cJSON_IsNumber(v)) ? (int)v->valuedouble : 0;
    }
    if (version != CROSS_VERSION) {
        snprintf(errbuf, errbuf_sz, "unsupported cross.bin version %d", version);
        cJSON_Delete(payload);
        return -1;
    }

    if (build_dfs_index(db, &scopes, &scope_count) != 0) {
        cJSON_Delete(payload);
        return -1;
    }

    entries_j = cJSON_GetObjectItemCaseSensitive(payload, "entries");
    cJSON_ArrayForEach(entry, entries_j) {
        cJSON *idx_j = cJSON_GetObjectItemCaseSensitive(entry, "idx");
        cJSON *crossed_j = cJSON_GetObjectItemCaseSensitive(entry, "crossed");
        cJSON *name_j;
        size_t idx;
        ncdbScopeT cross_scope;
        ncdbScopeT parent;
        size_t p_i;

        if (!idx_j || !cJSON_IsNumber(idx_j)) continue;
        idx = (size_t)idx_j->valuedouble;
        if (idx >= scope_count) continue;
        cross_scope = scopes[idx];
        if (cross_scope->type != NCDB_SCOPE_CROSS) continue;

        parent = cross_scope->parent;
        if (!parent) continue;

        cJSON_ArrayForEach(name_j, crossed_j) {
            const char *name;
            if (!cJSON_IsString(name_j)) continue;
            name = name_j->valuestring;
            /* Find sibling with this name */
            for (p_i = 0; p_i < parent->child_count; p_i++) {
                ncdbScopeT sib = parent->children[p_i];
                if (sib->type == NCDB_SCOPE_COVERPOINT && strcmp(sib->name, name) == 0) {
                    ncdb_impl_add_cross_point(cross_scope, sib);
                    break;
                }
            }
        }
    }

    free(scopes);
    cJSON_Delete(payload);
    return 0;
}
