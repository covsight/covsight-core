#include "unity.h"
#include "ncdb/ncdb.h"
#include "ncdb_impl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *PATH = "attrs_roundtrip.cdb";

static ncdbScopeT first_root(ncdbT db) { return db->roots[0]; }
static ncdbCoverT first_cover(ncdbT db) { return db->roots[0]->covers[0]; }

static int build_db(void) {
    ncdbT db = ncdb_Open(NULL);
    ncdbScopeT s; ncdbCoverT c; ncdbHistoryNodeT h;
    ncdbAttrValue v;

    s = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "top");
    c = ncdb_impl_create_cover(db, s, NCDB_COVER_STMTBIN, "stmt1", 42);
    h = ncdb_impl_create_history(db, NCDB_HISTORY_TEST, "test_a", "phys_a");

    /* DB-level attrs */
    v.type = NCDB_ATTR_STRING; v.u.str.s = "covsight";
    if (ncdb_DbSetAttr(db, "tool", &v) != 0) return 1;
    v.type = NCDB_ATTR_INT32;  v.u.i32 = -7;
    if (ncdb_DbSetAttr(db, "answer", &v) != 0) return 2;

    /* Scope attrs */
    v.type = NCDB_ATTR_INT64; v.u.i64 = 0x7FFFFFFFFFLL;
    if (ncdb_ScopeSetAttr(db, s, "id64", &v) != 0) return 3;
    v.type = NCDB_ATTR_DOUBLE; v.u.f64 = 3.141592653589793;
    if (ncdb_ScopeSetAttr(db, s, "pi", &v) != 0) return 4;

    /* Cover attrs */
    v.type = NCDB_ATTR_FLOAT; v.u.f32 = 1.5f;
    if (ncdb_CoverSetAttr(db, c, "ratio", &v) != 0) return 5;
    { static const uint8_t blob[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x42};
      v.type = NCDB_ATTR_BYTES; v.u.bytes.data = blob; v.u.bytes.size = sizeof(blob);
      if (ncdb_CoverSetAttr(db, c, "raw", &v) != 0) return 6; }

    /* History attrs */
    v.type = NCDB_ATTR_STRING; v.u.str.s = "regression";
    if (ncdb_HistorySetAttr(db, h, "campaign", &v) != 0) return 7;

    /* Update an existing key (should overwrite, not duplicate) */
    v.type = NCDB_ATTR_INT32; v.u.i32 = 99;
    if (ncdb_DbSetAttr(db, "answer", &v) != 0) return 8;

    if (ncdb_Write(db, PATH) != 0) { fprintf(stderr, "write failed: %s\n", ncdb_GetLastError(db)); return 9; }
    ncdb_Close(db);
    return 0;
}

static int read_and_check(void) {
    ncdbT db = ncdb_Open(PATH);
    ncdbScopeT s; ncdbCoverT c; ncdbHistoryNodeT h = NULL;
    ncdbAttrValue v;
    int rc = 100;

    if (!db || db->root_count != 1) goto out;
    s = first_root(db);
    if (s->cover_count != 1) { rc = 101; goto out; }
    c = first_cover(db);
    if (db->history_count != 1) { rc = 102; goto out; }
    h = db->history_nodes[0];

    /* DB-level */
    if (ncdb_DbGetAttr(db, "tool", &v) != 0) { rc = 110; goto out; }
    if (v.type != NCDB_ATTR_STRING || strcmp(v.u.str.s, "covsight") != 0) { rc = 111; goto out; }
    if (ncdb_DbGetAttr(db, "answer", &v) != 0) { rc = 112; goto out; }
    if (v.type != NCDB_ATTR_INT32 || v.u.i32 != 99) { rc = 113; goto out; }

    /* Scope */
    if (ncdb_ScopeGetAttr(db, s, "id64", &v) != 0) { rc = 120; goto out; }
    if (v.type != NCDB_ATTR_INT64 || v.u.i64 != 0x7FFFFFFFFFLL) { rc = 121; goto out; }
    if (ncdb_ScopeGetAttr(db, s, "pi", &v) != 0) { rc = 122; goto out; }
    if (v.type != NCDB_ATTR_DOUBLE || v.u.f64 != 3.141592653589793) { rc = 123; goto out; }

    /* Cover */
    if (ncdb_CoverGetAttr(db, c, "ratio", &v) != 0) { rc = 130; goto out; }
    if (v.type != NCDB_ATTR_FLOAT || v.u.f32 != 1.5f) { rc = 131; goto out; }
    if (ncdb_CoverGetAttr(db, c, "raw", &v) != 0) { rc = 132; goto out; }
    if (v.type != NCDB_ATTR_BYTES || v.u.bytes.size != 6) { rc = 133; goto out; }
    if (((const uint8_t *)v.u.bytes.data)[3] != 0xEF) { rc = 134; goto out; }

    /* History */
    if (ncdb_HistoryGetAttr(db, h, "campaign", &v) != 0) { rc = 140; goto out; }
    if (v.type != NCDB_ATTR_STRING || strcmp(v.u.str.s, "regression") != 0) { rc = 141; goto out; }

    /* Negative lookup */
    if (ncdb_DbGetAttr(db, "missing", &v) == 0) { rc = 150; goto out; }

    rc = 0;
out:
    ncdb_Close(db);
    return rc;
}

static int count_cb(const char *k, const ncdbAttrValue *v, void *ud) {
    int *n = (int *)ud;
    (void)k; (void)v;
    (*n)++;
    return 0;
}

static int test_iterate(void) {
    ncdbT db = ncdb_Open(NULL);
    ncdbAttrValue v;
    int n = 0, rc;
    v.type = NCDB_ATTR_INT32; v.u.i32 = 1; ncdb_DbSetAttr(db, "a", &v);
    v.u.i32 = 2; ncdb_DbSetAttr(db, "b", &v);
    v.u.i32 = 3; ncdb_DbSetAttr(db, "c", &v);
    rc = ncdb_DbAttrIterate(db, count_cb, &n);
    ncdb_Close(db);
    return (rc == 3 && n == 3) ? 0 : 200;
}

int main(void) {
    int rc;
    rc = build_db();    if (rc) { printf("build_db failed: %d\n", rc); return rc; }
    rc = read_and_check(); if (rc) { printf("read failed: %d\n", rc); return rc; }
    rc = test_iterate(); if (rc) { printf("iterate failed: %d\n", rc); return rc; }
    remove(PATH);
    printf("attrs roundtrip ok\n");
    return 0;
}
