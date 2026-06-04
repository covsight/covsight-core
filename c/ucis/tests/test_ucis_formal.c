/*
 * test_ucis_formal — Phase 4.5 / M5: formal-verification metadata.
 *
 * Cases:
 *   1. Set/Get status + radius + witness on a scope via the UCIS shim.
 *   2. End-to-end roundtrip: write, reopen, fields survive.
 *   3. NCDB_FEATURE_FORMAL set in feature_flags when used.
 *   4. Sparse representation: only scopes with formal data appear in
 *      the on-disk member.
 *   5. Unset getter returns the sentinel (UCIS_FORMAL_NONE / INT64_MIN /
 *      NULL).
 */

#include "ucis.h"
#include "ncdb/ncdb.h"
#include "ncdb_impl.h"
#include "ncdb_manifest.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern ncdbT ucis_internal_get_ncdb(ucisT);

static const char *PATH = "/tmp/test_ucis_formal.cdb";

static void test_in_memory(void)
{
    ucisT db = ucis_Open(NULL);
    ucisScopeT du = ucis_CreateScope(db, NULL, "lib.m", NULL, -1, UCIS_SV, UCIS_DU_MODULE, 0);
    ucisScopeT i  = ucis_CreateInstance(db, NULL, "tb.dut", NULL, -1, UCIS_SV, UCIS_INSTANCE, du, 0);
    ucisScopeT a  = ucis_CreateScope(db, i, "p_no_overflow", NULL, 1, UCIS_SV, UCIS_ASSERT, 0);
    (void)du;

    /* Unset → sentinels. */
    assert(ucis_GetFormalStatus(db, a) == UCIS_FORMAL_NONE);
    assert(ucis_GetFormalRadius(db, a) == INT64_MIN);
    assert(ucis_GetFormalWitness(db, a) == NULL);

    assert(ucis_SetFormalStatus (db, a, UCIS_FORMAL_PROOF) == 0);
    assert(ucis_SetFormalRadius (db, a, 17)                == 0);
    assert(ucis_SetFormalWitness(db, a, "counterexample.vcd") == 0);

    assert(ucis_GetFormalStatus(db, a) == UCIS_FORMAL_PROOF);
    assert(ucis_GetFormalRadius(db, a) == 17);
    {
        const char *w = ucis_GetFormalWitness(db, a);
        assert(w && strcmp(w, "counterexample.vcd") == 0);
    }

    ucis_Close(db);
}

static void test_roundtrip(void)
{
    unlink(PATH);

    ucisT db = ucis_Open(NULL);
    ucisScopeT du   = ucis_CreateScope(db, NULL, "lib.m", NULL, -1, UCIS_SV, UCIS_DU_MODULE, 0);
    ucisScopeT inst = ucis_CreateInstance(db, NULL, "tb", NULL, -1, UCIS_SV, UCIS_INSTANCE, du, 0);
    ucisScopeT a1   = ucis_CreateScope(db, inst, "p_a", NULL, 1, UCIS_SV, UCIS_ASSERT, 0);
    ucisScopeT a2   = ucis_CreateScope(db, inst, "p_b", NULL, 1, UCIS_SV, UCIS_ASSERT, 0);
    /* a3 has no formal data — verifies sparse storage. */
    ucisScopeT a3   = ucis_CreateScope(db, inst, "p_c", NULL, 1, UCIS_SV, UCIS_ASSERT, 0);
    (void)a3;

    ucis_SetFormalStatus (db, a1, UCIS_FORMAL_PROOF);
    ucis_SetFormalRadius (db, a1, 42);

    ucis_SetFormalStatus (db, a2, UCIS_FORMAL_FAILURE);
    ucis_SetFormalWitness(db, a2, "trace.vcd");

    assert(ucis_Write(db, PATH, NULL, 1, -1) == 0);
    ucis_Close(db);

    ucisT db2 = ucis_Open(PATH);
    assert(db2);
    ncdbT core2 = ucis_internal_get_ncdb(db2);
    assert((core2->feature_flags & NCDB_FEATURE_FORMAL) != 0);
    /* The NFRM member only has 2 records — one per scope with formal data. */
    assert(core2->formal_record_count == 2);

    /* Re-find the assertions in the rebuilt tree and re-query. */
    ncdbScopeT tb2 = NULL;
    for (size_t k = 0; k < core2->root_count; k++) {
        if (strcmp(core2->roots[k]->name, "tb") == 0) { tb2 = core2->roots[k]; break; }
    }
    assert(tb2);

    ncdbScopeT a1_2 = NULL, a2_2 = NULL, a3_2 = NULL;
    for (size_t k = 0; k < tb2->child_count; k++) {
        const char *n = tb2->children[k]->name;
        if      (strcmp(n, "p_a") == 0) a1_2 = tb2->children[k];
        else if (strcmp(n, "p_b") == 0) a2_2 = tb2->children[k];
        else if (strcmp(n, "p_c") == 0) a3_2 = tb2->children[k];
    }
    assert(a1_2 && a2_2 && a3_2);

    assert(ucis_GetFormalStatus(db2, (ucisScopeT)a1_2) == UCIS_FORMAL_PROOF);
    assert(ucis_GetFormalRadius(db2, (ucisScopeT)a1_2) == 42);
    /* Witness wasn't set on a1 → returns NULL. */
    assert(ucis_GetFormalWitness(db2, (ucisScopeT)a1_2) == NULL);

    assert(ucis_GetFormalStatus(db2, (ucisScopeT)a2_2) == UCIS_FORMAL_FAILURE);
    {
        const char *w = ucis_GetFormalWitness(db2, (ucisScopeT)a2_2);
        assert(w && strcmp(w, "trace.vcd") == 0);
    }
    /* Radius wasn't set on a2 → sentinel. */
    assert(ucis_GetFormalRadius(db2, (ucisScopeT)a2_2) == INT64_MIN);

    /* a3 was never touched — all getters return sentinels and no record
     * exists. */
    assert(ucis_GetFormalStatus (db2, (ucisScopeT)a3_2) == UCIS_FORMAL_NONE);
    assert(ucis_GetFormalRadius (db2, (ucisScopeT)a3_2) == INT64_MIN);
    assert(ucis_GetFormalWitness(db2, (ucisScopeT)a3_2) == NULL);

    ucis_Close(db2);
    unlink(PATH);
}

static void test_no_records_no_member(void)
{
    unlink(PATH);
    ucisT db = ucis_Open(NULL);
    ucis_CreateScope(db, NULL, "tb", NULL, -1, UCIS_SV, UCIS_INSTANCE, 0);
    assert(ucis_Write(db, PATH, NULL, 1, -1) == 0);
    ucis_Close(db);

    ucisT db2 = ucis_Open(PATH);
    ncdbT core = ucis_internal_get_ncdb(db2);
    assert((core->feature_flags & NCDB_FEATURE_FORMAL) == 0);
    assert(core->formal_record_count == 0);
    ucis_Close(db2);
    unlink(PATH);
}

int main(void)
{
    test_in_memory();
    test_roundtrip();
    test_no_records_no_member();
    printf("ucis formal M5 ok\n");
    return 0;
}
