/*
 * test_tests_assoc — Phase 4.4 / M4: NTAS tests↔coveritem association.
 *
 * Cases:
 *   1. In-memory: AssocCoverHistory records associations; MERGE nodes
 *      are rejected; idempotent.
 *   2. End-to-end round-trip with multiple tier shapes:
 *        - cover hit by no tests → tier NEVER (no on-disk payload)
 *        - cover hit by every test → tier ALL with empty exception list
 *        - cover hit by every test but one → tier ALL with exception
 *        - cover hit by ~half tests → tier SPARSE
 *   3. NCDB_FEATURE_TESTS_ASSOC set in feature_flags after write.
 *   4. Member is absent when no associations are recorded.
 *   5. MERGE history nodes never appear in the encoded payload (slot
 *      mapping skips them).
 */

#include "ncdb/ncdb.h"
#include "ncdb_impl.h"
#include "ncdb_manifest.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *PATH = "/tmp/test_tests_assoc.cdb";

static void test_in_memory_basic(void)
{
    ncdbT db = ncdb_Open(NULL);
    ncdbScopeT s   = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "top");
    ncdbCoverT c   = ncdb_impl_create_cover(db, s, NCDB_COVER_STMTBIN, "ln1", 1);
    ncdbHistoryNodeT t1 = ncdb_impl_create_history(db, NCDB_HISTORY_TEST, "test_1", "/tmp/t1");
    ncdbHistoryNodeT t2 = ncdb_impl_create_history(db, NCDB_HISTORY_TEST, "test_2", "/tmp/t2");
    ncdbHistoryNodeT m  = ncdb_impl_create_history(db, NCDB_HISTORY_MERGE, "merge", "");

    assert(ncdb_AssocCoverHistory(db, c, t1) == 0);
    assert(ncdb_AssocCoverHistory(db, c, t2) == 0);
    assert(ncdb_AssocCoverHistory(db, c, t1) == 0);   /* idempotent */
    assert(ncdb_AssocCoverHistory(db, c, m)  != 0);   /* MERGE rejected */

    assert(ncdb_CoverHistoryCount(db, c) == 2);
    /* In-memory order is sorted-slot-asc; t1 was created first so its
     * slot is 0, t2 slot 1 (m doesn't get a slot). */
    assert(ncdb_CoverHistoryAt(db, c, 0) == t1);
    assert(ncdb_CoverHistoryAt(db, c, 1) == t2);
    assert(ncdb_CoverHistoryAt(db, c, 2) == NULL);

    ncdb_Close(db);
}

static void test_roundtrip_three_tiers(void)
{
    unlink(PATH);

    ncdbT db = ncdb_Open(NULL);
    ncdbScopeT s = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "top");
    ncdbCoverT c_never  = ncdb_impl_create_cover(db, s, NCDB_COVER_STMTBIN, "never",  0);
    ncdbCoverT c_all    = ncdb_impl_create_cover(db, s, NCDB_COVER_STMTBIN, "all",    10);
    ncdbCoverT c_allexc = ncdb_impl_create_cover(db, s, NCDB_COVER_STMTBIN, "allexc", 9);
    ncdbCoverT c_sparse = ncdb_impl_create_cover(db, s, NCDB_COVER_STMTBIN, "sparse", 5);

    /* 25 test nodes plus 1 merge — the merge is filtered out. ALL_WITH_EXC
     * needs us to exceed the 95% threshold (>23.75 hits → ≥24 covers
     * needed for n=25 to qualify as ALL with exceptions). */
    ncdbHistoryNodeT tests[25];
    int i;
    for (i = 0; i < 25; i++) {
        char nm[16]; snprintf(nm, sizeof(nm), "t%d", i);
        tests[i] = ncdb_impl_create_history(db, NCDB_HISTORY_TEST, nm, NULL);
    }
    (void)ncdb_impl_create_history(db, NCDB_HISTORY_MERGE, "merge", "");

    /* c_never: no associations.
     * c_all: every test.
     * c_allexc: every test except #7.
     * c_sparse: tests 1, 3, 5, 7, 9 (5 of 25 = 20%, well below ALL threshold). */
    for (i = 0; i < 25; i++) ncdb_AssocCoverHistory(db, c_all, tests[i]);
    for (i = 0; i < 25; i++) if (i != 7) ncdb_AssocCoverHistory(db, c_allexc, tests[i]);
    for (i = 1; i < 10; i += 2) ncdb_AssocCoverHistory(db, c_sparse, tests[i]);

    if (ncdb_Write(db, PATH) != 0) {
        printf("write: %s\n", ncdb_GetLastError(db)); exit(1);
    }
    ncdb_Close(db);

    /* Re-open. */
    db = ncdb_Open(PATH);
    assert(db);
    assert((db->feature_flags & NCDB_FEATURE_TESTS_ASSOC) != 0);

    /* Refetch covers from the rebuilt tree. */
    ncdbScopeT s2 = db->roots[0];
    assert(s2->cover_count == 4);
    ncdbCoverT covers[4];
    for (i = 0; i < 4; i++) covers[i] = s2->covers[i];
    /* Insertion order on the writer side was never/all/allexc/sparse;
     * scope_tree preserves cover order. */
    assert(strcmp(covers[0]->name, "never")  == 0);
    assert(strcmp(covers[1]->name, "all")    == 0);
    assert(strcmp(covers[2]->name, "allexc") == 0);
    assert(strcmp(covers[3]->name, "sparse") == 0);

    assert(ncdb_CoverHistoryCount(db, covers[0]) == 0);
    assert(ncdb_CoverHistoryCount(db, covers[1]) == 25);
    assert(ncdb_CoverHistoryCount(db, covers[2]) == 24);
    assert(ncdb_CoverHistoryCount(db, covers[3]) == 5);

    /* Spot-check that the sparse cover's history nodes match what we
     * recorded. The logical names should be t1, t3, t5, t7, t9. */
    {
        const char *expected[5] = {"t1", "t3", "t5", "t7", "t9"};
        for (i = 0; i < 5; i++) {
            ncdbHistoryNodeT h = ncdb_CoverHistoryAt(db, covers[3], (size_t)i);
            assert(h);
            assert(strcmp(ncdb_GetHistoryLogicalName(db, h), expected[i]) == 0);
        }
    }
    /* The ALL_WITH_EXC cover should NOT include test #7. */
    {
        size_t n = ncdb_CoverHistoryCount(db, covers[2]);
        size_t j;
        for (j = 0; j < n; j++) {
            ncdbHistoryNodeT h = ncdb_CoverHistoryAt(db, covers[2], j);
            assert(strcmp(ncdb_GetHistoryLogicalName(db, h), "t7") != 0);
        }
    }

    ncdb_Close(db);
    unlink(PATH);
}

static void test_no_associations_no_member(void)
{
    /* When no cover has an association, NTAS isn't written at all
     * and feature_flags stays clear. */
    unlink(PATH);
    ncdbT db = ncdb_Open(NULL);
    ncdbScopeT s = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "top");
    (void)ncdb_impl_create_cover(db, s, NCDB_COVER_STMTBIN, "ln1", 1);
    (void)ncdb_impl_create_history(db, NCDB_HISTORY_TEST, "test", NULL);
    if (ncdb_Write(db, PATH) != 0) exit(1);
    ncdb_Close(db);

    db = ncdb_Open(PATH);
    assert((db->feature_flags & NCDB_FEATURE_TESTS_ASSOC) == 0);
    ncdb_Close(db);
    unlink(PATH);
}

int main(void)
{
    test_in_memory_basic();
    test_roundtrip_three_tiers();
    test_no_associations_no_member();
    printf("tests_assoc M4 ok\n");
    return 0;
}
