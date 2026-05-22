#include "ncdb/ncdb.h"
#include "ncdb_impl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *PATH = "covergroup.cdb";

int main(void) {
    ncdbT db = ncdb_Open(NULL);
    ncdbScopeT cg, cp1, cp2, cross;
    ncdbCoverT b1, b2;
    int rc = 1;

    cg    = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_COVERGROUP,  "cg_alu");
    cp1   = ncdb_impl_create_scope(db, cg,   NCDB_SCOPE_COVERPOINT,  "op");
    cp2   = ncdb_impl_create_scope(db, cg,   NCDB_SCOPE_COVERPOINT,  "result");
    cross = ncdb_impl_create_scope(db, cg,   NCDB_SCOPE_CROSS,       "op_x_result");

    ncdb_SetCoverpointExprTerms(db, cp1, "instr.op");
    ncdb_SetCoverpointExprTerms(db, cp2, "alu_out[15:0]");

    /* CVGBIN bins under coverpoint */
    b1 = ncdb_impl_create_cover(db, cp1, NCDB_COVER_CVGBIN, "[0:7]",   100);
    b2 = ncdb_impl_create_cover(db, cp1, NCDB_COVER_CVGBIN, "[8:15]",  50);
    ncdb_SetCoverComment(db, b1, "low half");
    ncdb_SetCoverComment(db, b2, "high half");

    /* Cross references */
    if (ncdb_impl_add_cross_point(cross, cp1) != 0) return 2;
    if (ncdb_impl_add_cross_point(cross, cp2) != 0) return 3;

    if (ncdb_Write(db, PATH) != 0) { printf("write: %s\n", ncdb_GetLastError(db)); return 4; }
    ncdb_Close(db);

    db = ncdb_Open(PATH);
    if (!db) return 5;
    cg = db->roots[0];
    if (cg->type != NCDB_SCOPE_COVERGROUP) { rc = 6; goto out; }
    if (cg->child_count != 3) { rc = 7; goto out; }
    cp1   = cg->children[0];
    cp2   = cg->children[1];
    cross = cg->children[2];

    if (!ncdb_GetCoverpointExprTerms(db, cp1) || strcmp(ncdb_GetCoverpointExprTerms(db, cp1), "instr.op") != 0) { rc = 8; goto out; }
    if (!ncdb_GetCoverpointExprTerms(db, cp2) || strcmp(ncdb_GetCoverpointExprTerms(db, cp2), "alu_out[15:0]") != 0) { rc = 9; goto out; }

    if (cp1->cover_count != 2) { rc = 10; goto out; }
    b1 = cp1->covers[0]; b2 = cp1->covers[1];
    if (b1->type != NCDB_COVER_CVGBIN || strcmp(b1->name, "[0:7]") != 0 || b1->count != 100) { rc = 11; goto out; }
    if (b2->type != NCDB_COVER_CVGBIN || strcmp(b2->name, "[8:15]") != 0 || b2->count != 50) { rc = 12; goto out; }
    if (!ncdb_GetCoverComment(db, b1) || strcmp(ncdb_GetCoverComment(db, b1), "low half") != 0) { rc = 13; goto out; }

    if (ncdb_GetCrossArity(db, cross) != 2) { rc = 14; goto out; }
    if (ncdb_GetCrossPoint(db, cross, 0) != cp1) { rc = 15; goto out; }
    if (ncdb_GetCrossPoint(db, cross, 1) != cp2) { rc = 16; goto out; }

    rc = 0;
out:
    ncdb_Close(db);
    remove(PATH);
    if (rc) printf("covergroup failed: %d\n", rc);
    else    printf("covergroup ok\n");
    return rc;
}
