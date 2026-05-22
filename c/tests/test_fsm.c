#include "ncdb/ncdb.h"
#include "ncdb_impl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *PATH = "fsm_roundtrip.cdb";

int main(void) {
    ncdbT db = ncdb_Open(NULL);
    ncdbScopeT inst, fsm1, fsm2;
    int rc = 1;
    const char *n; uint32_t v;

    inst = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "top");
    fsm1 = ncdb_impl_create_scope(db, inst, NCDB_SCOPE_FSM, "ctrl_fsm");
    fsm2 = ncdb_impl_create_scope(db, inst, NCDB_SCOPE_FSM, "no_overrides_fsm");

    ncdb_FsmAddState(db, fsm1, "IDLE",   0x10);
    ncdb_FsmAddState(db, fsm1, "RUN",    0x20);
    ncdb_FsmAddState(db, fsm1, "DONE",   0x40);
    /* fsm2: no overrides — should produce no record */

    if (ncdb_Write(db, PATH) != 0) { printf("write failed: %s\n", ncdb_GetLastError(db)); return 2; }
    ncdb_Close(db);

    db = ncdb_Open(PATH);
    if (!db || db->root_count != 1) { rc = 3; goto out; }
    inst = db->roots[0];
    if (inst->child_count != 2) { rc = 4; goto out; }
    fsm1 = inst->children[0];
    fsm2 = inst->children[1];

    if (ncdb_FsmStateCount(db, fsm1) != 3) { rc = 5; goto out; }
    if (ncdb_FsmGetState(db, fsm1, 0, &n, &v) != 0 || strcmp(n, "IDLE") != 0 || v != 0x10) { rc = 6; goto out; }
    if (ncdb_FsmGetState(db, fsm1, 1, &n, &v) != 0 || strcmp(n, "RUN")  != 0 || v != 0x20) { rc = 7; goto out; }
    if (ncdb_FsmGetState(db, fsm1, 2, &n, &v) != 0 || strcmp(n, "DONE") != 0 || v != 0x40) { rc = 8; goto out; }
    if (ncdb_FsmStateCount(db, fsm2) != 0) { rc = 9; goto out; }

    rc = 0;
out:
    ncdb_Close(db);
    remove(PATH);
    if (rc) printf("fsm roundtrip failed: %d\n", rc);
    else    printf("fsm roundtrip ok\n");
    return rc;
}
