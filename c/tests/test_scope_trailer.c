/*
 * test_scope_trailer — Phase 4.7 / M7: scope_tree.bin NTRL trailer with
 * DU-name index. The trailer enables O(log n) ucis_MatchDU lookup; the
 * fallback DFS scan remains as the non-trailer path.
 *
 * Cases:
 *   1. A DB with no DU scopes writes no trailer (small files stay small).
 *   2. A DB with several DUs round-trips; ncdb_match_du_by_name resolves
 *      each by name and returns the right scope.
 *   3. Unknown DU names return NULL.
 *   4. NCDB_FEATURE_SCOPE_TRAILER flag set when the trailer is emitted.
 *   5. Multi-byte-NTRL inside scope-tree main data doesn't get mistaken
 *      for a trailer (regression — the trailer-detection scheme is
 *      "last 4 bytes match magic").
 */

#include "ncdb/ncdb.h"
#include "ncdb_impl.h"
#include "ncdb_manifest.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *PATH = "/tmp/test_scope_trailer.cdb";

static void test_no_du_no_trailer(void)
{
    unlink(PATH);
    ncdbT db = ncdb_Open(NULL);
    ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "top");
    if (ncdb_Write(db, PATH) != 0) exit(1);
    ncdb_Close(db);

    db = ncdb_Open(PATH);
    assert((db->feature_flags & NCDB_FEATURE_SCOPE_TRAILER) == 0);
    assert(db->du_lookup_count == 0);
    ncdb_Close(db);
    unlink(PATH);
}

static void test_du_trailer_roundtrip(void)
{
    unlink(PATH);
    ncdbT db = ncdb_Open(NULL);
    ncdbScopeT du1 = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_DU_MODULE,  "lib.cpu");
    ncdbScopeT du2 = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_DU_MODULE,  "lib.mem");
    ncdbScopeT du3 = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_DU_PACKAGE, "lib.pkg");
    /* Add an instance to make the tree look real. */
    (void)ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "tb");
    (void)du1; (void)du2; (void)du3;
    if (ncdb_Write(db, PATH) != 0) {
        printf("write: %s\n", ncdb_GetLastError(db)); exit(1);
    }
    ncdb_Close(db);

    db = ncdb_Open(PATH);
    assert(db);
    assert((db->feature_flags & NCDB_FEATURE_SCOPE_TRAILER) != 0);
    assert(db->du_lookup_count == 3);

    /* DUs should be queryable by name in O(log n). The lookup is
     * sorted, so the in-memory order is alphabetical regardless of
     * insertion order. */
    assert(strcmp(db->du_lookup[0].name, "lib.cpu") == 0);
    assert(strcmp(db->du_lookup[1].name, "lib.mem") == 0);
    assert(strcmp(db->du_lookup[2].name, "lib.pkg") == 0);

    ncdbScopeT hit = ncdb_match_du_by_name(db, "lib.mem");
    assert(hit);
    assert(strcmp(ncdb_GetScopeName(db, hit), "lib.mem") == 0);
    assert((ncdb_GetScopeType(db, hit) & NCDB_SCOPE_DU_MODULE) != 0);

    hit = ncdb_match_du_by_name(db, "lib.pkg");
    assert(hit);
    assert((ncdb_GetScopeType(db, hit) & NCDB_SCOPE_DU_PACKAGE) != 0);

    /* Unknown DU. */
    assert(ncdb_match_du_by_name(db, "lib.ghost") == NULL);

    ncdb_Close(db);
    unlink(PATH);
}

/* Sanity check: a DB whose scope-tree main payload happens to end in
 * the byte sequence "NTRL" (e.g. a scope name) must NOT be mistaken
 * for a trailer. The footer's separate 8-byte offset prefix gives us
 * a 12-byte detection window; a stray "NTRL" alone wouldn't match
 * because the bytes before would not be a valid offset → trailer
 * detection requires that offset to be within the file. */
static void test_no_trailer_false_positive(void)
{
    unlink(PATH);
    ncdbT db = ncdb_Open(NULL);
    /* No DU → no trailer, regardless of scope names. */
    ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "NTRL");  /* try to confuse */
    if (ncdb_Write(db, PATH) != 0) exit(1);
    ncdb_Close(db);

    db = ncdb_Open(PATH);
    /* If we mistook scope-tree bytes for a trailer, du_lookup_count
     * could be garbage; assert it's zero (no false positive). */
    assert(db->du_lookup_count == 0);
    ncdb_Close(db);
    unlink(PATH);
}

int main(void)
{
    test_no_du_no_trailer();
    test_du_trailer_roundtrip();
    test_no_trailer_false_positive();
    printf("scope_trailer M7 ok\n");
    return 0;
}
