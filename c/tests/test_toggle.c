#include "ncdb/ncdb.h"
#include "ncdb_impl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *PATH = "toggle_roundtrip.cdb";

int main(void) {
    ncdbT db = ncdb_Open(NULL);
    ncdbScopeT inst, t1, t2;
    int rc = 1;

    inst = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "top");
    t1   = ncdb_impl_create_scope(db, inst, NCDB_SCOPE_TOGGLE, "sig_a");
    t2   = ncdb_impl_create_scope(db, inst, NCDB_SCOPE_TOGGLE, "sig_b");

    ncdb_SetToggleCanonicalName(db, t1, "top.module.sig_a[3:0]");
    ncdb_SetToggleMetric(db, t1, NCDB_TOGGLE_METRIC_ZTOGGLE);
    ncdb_SetToggleType  (db, t1, NCDB_TOGGLE_TYPE_REG);
    ncdb_SetToggleDir   (db, t1, NCDB_TOGGLE_DIR_OUT);
    /* t2 stays all-default: should NOT produce a record */

    if (ncdb_Write(db, PATH) != 0) { printf("write failed: %s\n", ncdb_GetLastError(db)); return 2; }
    ncdb_Close(db);

    db = ncdb_Open(PATH);
    if (!db || db->root_count != 1) { rc = 3; goto out; }
    inst = db->roots[0];
    if (inst->child_count != 2) { rc = 4; goto out; }
    {
        ncdbScopeT r1 = inst->children[0];
        ncdbScopeT r2 = inst->children[1];
        const char *can = ncdb_GetToggleCanonicalName(db, r1);
        if (!can || strcmp(can, "top.module.sig_a[3:0]") != 0) { rc = 5; goto out; }
        if (ncdb_GetToggleMetric(db, r1) != NCDB_TOGGLE_METRIC_ZTOGGLE) { rc = 6; goto out; }
        if (ncdb_GetToggleType  (db, r1) != NCDB_TOGGLE_TYPE_REG)       { rc = 7; goto out; }
        if (ncdb_GetToggleDir   (db, r1) != NCDB_TOGGLE_DIR_OUT)        { rc = 8; goto out; }
        /* t2 has no record; getters return 0 (defaults) */
        if (ncdb_GetToggleCanonicalName(db, r2) != NULL) { rc = 9; goto out; }
        if (ncdb_GetToggleMetric(db, r2) != 0) { rc = 10; goto out; }
    }
    rc = 0;
out:
    ncdb_Close(db);
    remove(PATH);
    if (rc) printf("toggle roundtrip failed: %d\n", rc);
    else    printf("toggle roundtrip ok\n");
    return rc;
}
