#include "ncdb/ncdb.h"
#include "ncdb_impl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *PATH = "du_files.cdb";

int main(void) {
    ncdbT db = ncdb_Open(NULL);
    ncdbScopeT du_a, du_b;
    int rc = 1;
    int n;

    du_a = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_DU_MODULE, "alu");
    du_b = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_DU_MODULE, "regfile");

    /* DU file numbering is per-DU: each DU starts file_num=0 */
    n = ncdb_DuAddFile(db, du_a, "/src/alu.v");      if (n != 0) return 2;
    n = ncdb_DuAddFile(db, du_a, "/src/alu_pkg.sv"); if (n != 1) return 3;
    n = ncdb_DuAddFile(db, du_b, "/src/regfile.v"); if (n != 0) return 4;
    /* Shared global path — re-used as global ID; DU still owns own file_num */
    n = ncdb_DuAddFile(db, du_b, "/src/alu.v");      if (n != 1) return 5;

    if (ncdb_DuFileCount(db, du_a) != 2) return 6;
    if (ncdb_DuFileCount(db, du_b) != 2) return 7;

    if (ncdb_Write(db, PATH) != 0) { printf("write: %s\n", ncdb_GetLastError(db)); return 8; }
    ncdb_Close(db);

    db = ncdb_Open(PATH);
    if (!db) return 9;
    du_a = db->roots[0]; du_b = db->roots[1];

    if (ncdb_DuFileCount(db, du_a) != 2) { rc = 10; goto out; }
    if (ncdb_DuFileCount(db, du_b) != 2) { rc = 11; goto out; }
    if (strcmp(ncdb_DuGetFile(db, du_a, 0), "/src/alu.v")      != 0) { rc = 12; goto out; }
    if (strcmp(ncdb_DuGetFile(db, du_a, 1), "/src/alu_pkg.sv") != 0) { rc = 13; goto out; }
    if (strcmp(ncdb_DuGetFile(db, du_b, 0), "/src/regfile.v")  != 0) { rc = 14; goto out; }
    if (strcmp(ncdb_DuGetFile(db, du_b, 1), "/src/alu.v")      != 0) { rc = 15; goto out; }

    rc = 0;
out:
    ncdb_Close(db);
    remove(PATH);
    if (rc) printf("du_files failed: %d\n", rc);
    else    printf("du_files ok\n");
    return rc;
}
