#include "ncdb/ncdb.h"
#include "ncdb_impl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *PATH = "manifest_vendor.cdb";

int main(void) {
    ncdbT db = ncdb_Open(NULL);
    int rc = 1;
    ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "top");
    ncdb_SetVendorId         (db, "covsight");
    ncdb_SetVendorTool       (db, "ncdb-c");
    ncdb_SetVendorToolVersion(db, "0.1.0");
    ncdb_SetUcisStandard     (db, "UCIS_1.0");
    if (ncdb_Write(db, PATH) != 0) { printf("write: %s\n", ncdb_GetLastError(db)); return 2; }
    ncdb_Close(db);

    db = ncdb_Open(PATH);
    if (!db) return 3;
    if (!ncdb_GetVendorId(db)          || strcmp(ncdb_GetVendorId(db),          "covsight") != 0) { rc = 4; goto out; }
    if (!ncdb_GetVendorTool(db)        || strcmp(ncdb_GetVendorTool(db),        "ncdb-c")   != 0) { rc = 5; goto out; }
    if (!ncdb_GetVendorToolVersion(db) || strcmp(ncdb_GetVendorToolVersion(db), "0.1.0")    != 0) { rc = 6; goto out; }
    if (!ncdb_GetUcisStandard(db)      || strcmp(ncdb_GetUcisStandard(db),      "UCIS_1.0") != 0) { rc = 7; goto out; }
    rc = 0;
out:
    ncdb_Close(db);
    remove(PATH);
    if (rc) printf("manifest_vendor failed: %d\n", rc);
    else    printf("manifest_vendor ok\n");
    return rc;
}
