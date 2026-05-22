#include "ncdb/ncdb.h"
#include "ncdb_impl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *PATH = "cover_fields.cdb";

int main(void) {
    ncdbT db = ncdb_Open(NULL);
    ncdbScopeT s;
    ncdbCoverT c1, c2, c3;
    int rc = 1;

    s  = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "u");
    c1 = ncdb_impl_create_cover(db, s, NCDB_COVER_STMTBIN, "line17", 42);
    c2 = ncdb_impl_create_cover(db, s, NCDB_COVER_STMTBIN, "line22", 0);
    c3 = ncdb_impl_create_cover(db, s, NCDB_COVER_STMTBIN, "plain", 7);

    ncdb_SetCoverSource (db, c1, "/src/alu.v", 17, 0);
    ncdb_SetCoverWeight (db, c1, 5);
    ncdb_SetCoverGoal   (db, c1, 1);
    ncdb_SetCoverComment(db, c1, "main path");
    ncdb_SetCoverAtLeast(db, c1, 3);

    ncdb_SetCoverSource (db, c2, "/src/alu.v", 22, 4);
    ncdb_SetCoverGoal   (db, c2, 10);
    /* c3 stays defaults — should serialize without per-cover ext byte */

    if (ncdb_Write(db, PATH) != 0) { printf("write: %s\n", ncdb_GetLastError(db)); return 2; }
    ncdb_Close(db);

    db = ncdb_Open(PATH);
    if (!db) return 3;
    s  = db->roots[0];
    c1 = s->covers[0]; c2 = s->covers[1]; c3 = s->covers[2];

    if (!ncdb_GetCoverSourcePath(db, c1) || strcmp(ncdb_GetCoverSourcePath(db, c1), "/src/alu.v") != 0) { rc = 4; goto out; }
    if (ncdb_GetCoverSourceLine(db, c1) != 17) { rc = 5; goto out; }
    if (ncdb_GetCoverWeight(db, c1) != 5) { rc = 6; goto out; }
    if (ncdb_GetCoverGoal(db, c1) != 1) { rc = 7; goto out; }
    if (!ncdb_GetCoverComment(db, c1) || strcmp(ncdb_GetCoverComment(db, c1), "main path") != 0) { rc = 8; goto out; }
    if (ncdb_GetCoverAtLeast(db, c1) != 3) { rc = 9; goto out; }

    if (!ncdb_GetCoverSourcePath(db, c2) || strcmp(ncdb_GetCoverSourcePath(db, c2), "/src/alu.v") != 0) { rc = 10; goto out; }
    if (ncdb_GetCoverSourceLine(db, c2) != 22) { rc = 11; goto out; }
    if (ncdb_GetCoverSourceToken(db, c2) != 4) { rc = 12; goto out; }
    if (ncdb_GetCoverGoal(db, c2) != 10) { rc = 13; goto out; }

    if (ncdb_GetCoverSourcePath(db, c3) != NULL) { rc = 14; goto out; }
    if (ncdb_GetCoverGoal(db, c3) != -1) { rc = 15; goto out; }

    rc = 0;
out:
    ncdb_Close(db);
    remove(PATH);
    if (rc) printf("cover_fields failed: %d\n", rc);
    else    printf("cover_fields ok\n");
    return rc;
}
