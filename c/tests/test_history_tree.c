#include "ncdb/ncdb.h"
#include "ncdb_impl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *PATH = "history_tree.cdb";

int main(void) {
    ncdbT db = ncdb_Open(NULL);
    ncdbHistoryNodeT root, child1, child2, grandchild;
    int rc = 1;

    ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "top");
    root       = ncdb_impl_create_history(db, NCDB_HISTORY_MERGE, "merge_root", NULL);
    child1     = ncdb_impl_create_history(db, NCDB_HISTORY_TEST,  "run_a", "/runs/a");
    child2     = ncdb_impl_create_history(db, NCDB_HISTORY_TEST,  "run_b", "/runs/b");
    grandchild = ncdb_impl_create_history(db, NCDB_HISTORY_TEST,  "child_of_a", NULL);

    ncdb_SetHistorySeed(db, child1, "0xDEADBEEF");
    ncdb_SetHistoryUserName(db, child1, "matt");
    child1->sim_time = 1234.5;
    child1->cpu_time = 78.9;

    if (ncdb_SetHistoryParent(db, child1, root) != 0) return 2;
    if (ncdb_SetHistoryParent(db, child2, root) != 0) return 3;
    if (ncdb_SetHistoryParent(db, grandchild, child1) != 0) return 4;

    if (ncdb_Write(db, PATH) != 0) { printf("write: %s\n", ncdb_GetLastError(db)); return 5; }
    ncdb_Close(db);

    db = ncdb_Open(PATH);
    if (!db || db->history_count != 4) { rc = 6; goto out; }
    root       = db->history_nodes[0];
    child1     = db->history_nodes[1];
    child2     = db->history_nodes[2];
    grandchild = db->history_nodes[3];

    if (ncdb_GetHistoryParent(db, root) != NULL) { rc = 7; goto out; }
    if (ncdb_GetHistoryParent(db, child1) != root) { rc = 8; goto out; }
    if (ncdb_GetHistoryParent(db, child2) != root) { rc = 9; goto out; }
    if (ncdb_GetHistoryParent(db, grandchild) != child1) { rc = 10; goto out; }
    if (strcmp(ncdb_GetHistorySeed(db, child1), "0xDEADBEEF") != 0) { rc = 11; goto out; }
    if (child1->sim_time != 1234.5) { rc = 12; goto out; }
    if (child1->cpu_time != 78.9)   { rc = 13; goto out; }
    if (ncdb_GetHistoryKind(db, root) != NCDB_HISTORY_MERGE) { rc = 14; goto out; }
    if (ncdb_GetHistoryKind(db, child1) != NCDB_HISTORY_TEST) { rc = 15; goto out; }

    rc = 0;
out:
    ncdb_Close(db);
    remove(PATH);
    if (rc) printf("history_tree failed: %d\n", rc);
    else    printf("history_tree ok\n");
    return rc;
}
