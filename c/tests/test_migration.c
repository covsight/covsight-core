/*
 * Migration smoke test: verify the new C reader still loads pre-3.0 golden
 * fixtures (JSON manifests/history/sources, no DU links, no tags, etc.).
 */
#include "ncdb/ncdb.h"
#include "ncdb_impl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *FIXTURES[] = {
    "py_minimal.cdb",
    "py_history.cdb",
    "py_cross.cdb",
    "py_weight_goal.cdb",
    "py_large_count.cdb",
    "py_unicode_names.cdb",
    "py_deep.cdb",
    "py_empty.cdb",
    "ts_minimal.cdb",
    "ts_full.cdb",
    NULL
};

int main(void) {
    const char *dir = getenv("NCDB_GOLDEN_DIR");
    const char *fallback = "../../tests/compat/golden";
    char path[1024];
    int i, failures = 0;

    if (!dir) dir = fallback;

    for (i = 0; FIXTURES[i]; ++i) {
        ncdbT db;
        snprintf(path, sizeof(path), "%s/%s", dir, FIXTURES[i]);
        db = ncdb_Open(path);
        if (!db) {
            printf("FAIL: could not open %s\n", path);
            failures++; continue;
        }
        /* Sanity: open should not error and last_error should be empty */
        if (ncdb_GetLastError(db)[0] != '\0') {
            printf("FAIL: %s reported error: %s\n", FIXTURES[i], ncdb_GetLastError(db));
            failures++;
        } else {
            printf("OK:   %s (scopes=%zu, history=%zu)\n",
                   FIXTURES[i], db->root_count, db->history_count);
        }
        ncdb_Close(db);
    }
    if (failures) {
        printf("migration: %d failures\n", failures);
        return 1;
    }
    printf("migration ok (%d fixtures)\n", i);
    return 0;
}
