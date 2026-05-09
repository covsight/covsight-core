#include <stdio.h>
#include "unity.h"
#include "ncdb_impl.h"

static int first_scope_cb(ncdbT db, ncdbScopeT scope, void *ud) { (void)db; *(ncdbScopeT *)ud = scope; return 1; }

typedef struct { ncdbCoverT covers[4]; int idx; } cover_collect_t;
static int collect_cover_cb(ncdbT db, ncdbCoverT cover, void *ud) { (void)db; cover_collect_t *c = (cover_collect_t *)ud; c->covers[c->idx++] = cover; return 0; }

typedef struct { ncdbScopeT scopes[4]; int idx; } scope_collect_t;
static int collect_scope_cb(ncdbT db, ncdbScopeT scope, void *ud) { (void)db; scope_collect_t *c = (scope_collect_t *)ud; c->scopes[c->idx++] = scope; return 0; }

static void test_db_roundtrip(void) {
    ncdbT db = ncdb_Open(NULL);
    ncdbT rd;
    ncdbScopeT top, stmt, branch;
    scope_collect_t children = {{0}, 0};
    cover_collect_t covers = {{0}, 0};
    TEST_ASSERT(db != NULL);
    top = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "top");
    stmt = ncdb_impl_create_scope(db, top, NCDB_SCOPE_BLOCK, "stmt_scope");
    branch = ncdb_impl_create_scope(db, top, NCDB_SCOPE_BRANCH, "branch_scope");
    TEST_ASSERT(top && stmt && branch);
    TEST_ASSERT(ncdb_impl_create_cover(db, stmt, NCDB_COVER_STMTBIN, "stmt-bin", 7) != NULL);
    TEST_ASSERT(ncdb_impl_create_cover(db, branch, NCDB_COVER_TOGGLEBIN, "0 -> 1", 3) != NULL);
    TEST_ASSERT(ncdb_impl_create_cover(db, branch, NCDB_COVER_TOGGLEBIN, "1 -> 0", 4) != NULL);
    TEST_ASSERT(ncdb_impl_create_history(db, NCDB_HISTORY_TEST, "smoke", NULL) != NULL);
    TEST_ASSERT(ncdb_Write(db, "roundtrip.cdb") == 0);
    ncdb_Close(db);

    rd = ncdb_Open("roundtrip.cdb");
    TEST_ASSERT(rd != NULL);
    TEST_ASSERT_EQUAL_STRING("", ncdb_GetLastError(rd));
    TEST_ASSERT(ncdb_ScopeIterate(rd, NULL, 0xFFFFFFFFU, collect_scope_cb, &children) == 0);
    TEST_ASSERT_EQUAL_UINT64(1, children.idx);
    TEST_ASSERT_EQUAL_STRING("top", ncdb_GetScopeName(rd, children.scopes[0]));

    children.idx = 0;
    TEST_ASSERT(ncdb_ScopeIterate(rd, children.scopes[0], 0xFFFFFFFFU, collect_scope_cb, &children) == 0);
    TEST_ASSERT_EQUAL_UINT64(2, children.idx);
    TEST_ASSERT_EQUAL_STRING("stmt_scope", ncdb_GetScopeName(rd, children.scopes[0]));
    TEST_ASSERT_EQUAL_STRING("branch_scope", ncdb_GetScopeName(rd, children.scopes[1]));

    TEST_ASSERT(ncdb_CoverIterate(rd, children.scopes[0], collect_cover_cb, &covers) == 0);
    TEST_ASSERT_EQUAL_UINT64(1, covers.idx);
    TEST_ASSERT_EQUAL_STRING("stmt-bin", ncdb_GetCoverName(rd, covers.covers[0]));
    TEST_ASSERT_EQUAL_UINT64(7, ncdb_GetCoverCount(rd, covers.covers[0]));

    covers.idx = 0;
    TEST_ASSERT(ncdb_CoverIterate(rd, children.scopes[1], collect_cover_cb, &covers) == 0);
    TEST_ASSERT_EQUAL_UINT64(2, covers.idx);
    TEST_ASSERT_EQUAL_UINT64(3, ncdb_GetCoverCount(rd, covers.covers[0]));
    TEST_ASSERT_EQUAL_UINT64(4, ncdb_GetCoverCount(rd, covers.covers[1]));
    ncdb_Close(rd);
}

int main(void) {
    RUN_TEST(test_db_roundtrip);
    return 0;
}
