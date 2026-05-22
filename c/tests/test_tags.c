#include "ncdb/ncdb.h"
#include "ncdb_impl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *PATH = "tags_roundtrip.cdb";

int main(void) {
    ncdbT db = ncdb_Open(NULL);
    ncdbScopeT a, b, c;
    int rc = 1;
    a = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "with_tags");
    b = ncdb_impl_create_scope(db, a, NCDB_SCOPE_TOGGLE, "child_with_tag");
    c = ncdb_impl_create_scope(db, a, NCDB_SCOPE_TOGGLE, "no_tags");

    if (ncdb_ScopeAddTag(db, a, "release-blocker") != 0) return 2;
    if (ncdb_ScopeAddTag(db, a, "smoke")            != 0) return 3;
    if (ncdb_ScopeAddTag(db, b, "regression")       != 0) return 4;

    if (ncdb_Write(db, PATH) != 0) return 5;
    ncdb_Close(db);

    db = ncdb_Open(PATH);
    if (!db) return 6;
    a = db->roots[0];
    b = a->children[0];
    c = a->children[1];
    if (ncdb_ScopeTagCount(db, a) != 2) { rc = 7; goto out; }
    if (strcmp(ncdb_ScopeGetTag(db, a, 0), "release-blocker") != 0) { rc = 8; goto out; }
    if (strcmp(ncdb_ScopeGetTag(db, a, 1), "smoke") != 0) { rc = 9; goto out; }
    if (ncdb_ScopeTagCount(db, b) != 1) { rc = 10; goto out; }
    if (strcmp(ncdb_ScopeGetTag(db, b, 0), "regression") != 0) { rc = 11; goto out; }
    if (ncdb_ScopeTagCount(db, c) != 0) { rc = 12; goto out; }
    rc = 0;
out:
    ncdb_Close(db);
    remove(PATH);
    if (rc) printf("tags failed: %d\n", rc);
    else    printf("tags ok\n");
    return rc;
}
