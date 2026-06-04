/*
 * test_ucis_unique_id — Phase 4.3 / M3: NUID member + UID computation
 * + ucis_MatchScopeByUniqueID.
 *
 * Cases:
 *   1. ncdb_compute_unique_id builds the expected UCIS-§5.4-style
 *      path for nested scopes.
 *   2. xxh64 is stable: same input → same hash across calls.
 *   3. End-to-end: build a tree, write, reopen, query by UID via the
 *      fast (index) path. Verify a non-existent UID returns NULL.
 *   4. Slow-path lookup works for an in-memory DB with no NUID index.
 *   5. Manifest carries NCDB_FEATURE_UNIQUE_ID_INDEX after write.
 */

#include "ncdb/ncdb.h"
#include "ncdb_impl.h"
#include "ncdb_manifest.h"
#include "ucis.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern ncdbT ucis_internal_get_ncdb(ucisT);

static const char *PATH = "/tmp/test_ucis_unique_id.cdb";

static void test_uid_string_format(void)
{
    ncdbT db = ncdb_Open(NULL);
    /* UCIS_DU_MODULE = 0x01000000 → log2 = 24
     * UCIS_INSTANCE  = 0x00000010 → log2 = 4
     * UCIS_BLOCK     = 0x00000040 → log2 = 6 */
    ncdbScopeT du   = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_DU_MODULE, "mylib.dut");
    ncdbScopeT inst = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "tb");
    ncdbScopeT sub  = ncdb_impl_create_scope(db, inst, NCDB_SCOPE_INSTANCE, "dut");
    ncdbScopeT blk  = ncdb_impl_create_scope(db, sub,  NCDB_SCOPE_BLOCK,    "blk1");
    (void)du;

    char buf[256];
    int n = ncdb_compute_unique_id(db, blk, buf, sizeof(buf));
    assert(n > 0);
    if (strcmp(buf, "/4:tb/4:dut/6:blk1") != 0) {
        printf("got UID '%s'\n", buf);
        assert(0);
    }
    ncdb_Close(db);
}

static void test_xxh64_stable(void)
{
    uint64_t a = ncdb_xxh64("hello world", 11, 0);
    uint64_t b = ncdb_xxh64("hello world", 11, 0);
    uint64_t c = ncdb_xxh64("hello worle", 11, 0);  /* one char diff */
    assert(a == b);
    assert(a != c);
}

static void test_e2e_index_lookup(void)
{
    unlink(PATH);
    ucisT db = ucis_Open(NULL);
    ucisScopeT du   = ucis_CreateScope(db, NULL, "lib.m", NULL, -1, UCIS_VLOG, UCIS_DU_MODULE, 0);
    ucisScopeT tb   = ucis_CreateInstance(db, NULL, "tb",  NULL, -1, UCIS_VLOG, UCIS_INSTANCE, du, 0);
    ucisScopeT dut  = ucis_CreateInstance(db, tb,   "dut", NULL, -1, UCIS_VLOG, UCIS_INSTANCE, du, 0);
    ucisScopeT blk  = ucis_CreateScope   (db, dut,  "blk1", NULL, -1, UCIS_VLOG, UCIS_BLOCK, 0);
    (void)blk;

    assert(ucis_Write(db, PATH, NULL, 1, -1) == 0);
    assert(ucis_Close(db) == 0);

    ucisT db2 = ucis_Open(PATH);
    assert(db2);
    ncdbT core2 = ucis_internal_get_ncdb(db2);
    assert(core2->nuid_idx != NULL);   /* index loaded from disk */
    assert((core2->feature_flags & NCDB_FEATURE_UNIQUE_ID_INDEX) != 0);

    /* Look up a known scope by UID. Note: instance scopes appear as
     * children of the root (not under a DU scope), so the UID for
     * /tb/dut/blk1 doesn't include the DU. */
    ucisScopeT found = ucis_MatchScopeByUniqueID(db2, NULL, "/4:tb/4:dut/6:blk1");
    assert(found);
    assert(strcmp(ncdb_GetScopeName(core2, (ncdbScopeT)found), "blk1") == 0);

    /* Intermediate scopes also resolve. */
    found = ucis_MatchScopeByUniqueID(db2, NULL, "/4:tb/4:dut");
    assert(found);
    assert(strcmp(ncdb_GetScopeName(core2, (ncdbScopeT)found), "dut") == 0);

    /* Non-existent UID. */
    assert(ucis_MatchScopeByUniqueID(db2, NULL, "/4:tb/4:nope") == NULL);
    /* Bad format / never-built UID — hash will miss the index. */
    assert(ucis_MatchScopeByUniqueID(db2, NULL, "garbage") == NULL);

    assert(ucis_Close(db2) == 0);
    unlink(PATH);
}

static void test_slow_path_in_memory(void)
{
    /* No write — no NUID index. The fallback DFS scan should still work. */
    ucisT db = ucis_Open(NULL);
    ucisScopeT du  = ucis_CreateScope(db, NULL, "lib.m", NULL, -1, UCIS_VLOG, UCIS_DU_MODULE, 0);
    ucisScopeT tb  = ucis_CreateInstance(db, NULL, "tb",  NULL, -1, UCIS_VLOG, UCIS_INSTANCE, du, 0);
    ncdbT core = ucis_internal_get_ncdb(db);
    assert(core->nuid_idx == NULL);

    ucisScopeT found = ucis_MatchScopeByUniqueID(db, NULL, "/4:tb");
    assert(found);
    assert(found == tb);

    assert(ucis_MatchScopeByUniqueID(db, NULL, "/4:nope") == NULL);
    ucis_Close(db);
}

int main(void)
{
    test_uid_string_format();
    test_xxh64_stable();
    test_e2e_index_lookup();
    test_slow_path_in_memory();
    printf("ucis unique_id M3 ok\n");
    return 0;
}
