/*
 * Phase 2.8 acceptance test: covergroup family.
 *
 * Plain UCIS_COVERGROUP / UCIS_COVERPOINT scopes go through CreateScope
 * already (verified end-to-end here as a sanity check). The new code path
 * exercised is the cross specialization: CreateCross + CreateCrossByName
 * must wire the coverpoint arity and persist it across write/reopen.
 */

#include "ucis.h"
#include "ncdb/ncdb.h"
#include "ncdb_impl.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern ncdbT ucis_internal_get_ncdb(ucisT);

static const char *TMP = "/tmp/test_ucis_covergroup.ncdb";

static ucisScopeT make_cvg_with_cps(ucisT db, const char *cvg_name,
                                    int n_points,
                                    ucisScopeT *out_cps)
{
    ucisScopeT du  = ucis_CreateScope(db, NULL, "lib.m", NULL, -1, UCIS_SV, UCIS_DU_MODULE, 0);
    /* Covergroup scope: a plain UCIS_COVERGROUP scope under the DU. */
    ucisScopeT cvg = ucis_CreateScope(db, du, cvg_name, NULL, -1, UCIS_SV, UCIS_COVERGROUP, 0);
    assert(cvg && ucis_GetScopeType(db, cvg) == UCIS_COVERGROUP);
    for (int i = 0; i < n_points; i++) {
        char nm[16];
        snprintf(nm, sizeof(nm), "cp%d", i);
        out_cps[i] = ucis_CreateScope(db, cvg, nm, NULL, -1, UCIS_SV, UCIS_COVERPOINT, 0);
        assert(out_cps[i] && ucis_GetScopeType(db, out_cps[i]) == UCIS_COVERPOINT);
    }
    return cvg;
}

static void test_create_cross_by_handle(void)
{
    ucisT db = ucis_Open(NULL);
    ucisScopeT cps[3];
    ucisScopeT cvg = make_cvg_with_cps(db, "cg", 3, cps);

    ucisScopeT x = ucis_CreateCross(db, cvg, "x_a_b_c", NULL, -1, UCIS_SV,
                                    /*n*/3, cps);
    assert(x);
    assert(ucis_GetScopeType(db, x) == UCIS_CROSS);
    assert((ucis_GetScopeFlags(db, x) & UCIS_SCOPE_SPECIALIZED) != 0);

    ncdbT core = ucis_internal_get_ncdb(db);
    assert(ncdb_GetCrossArity(core, (ncdbScopeT)x) == 3);
    assert(ncdb_GetCrossPoint(core, (ncdbScopeT)x, 0) == (ncdbScopeT)cps[0]);
    assert(ncdb_GetCrossPoint(core, (ncdbScopeT)x, 1) == (ncdbScopeT)cps[1]);
    assert(ncdb_GetCrossPoint(core, (ncdbScopeT)x, 2) == (ncdbScopeT)cps[2]);

    /* Error paths: zero points, NULL element. */
    assert(ucis_CreateCross(db, cvg, "bad0", NULL, -1, UCIS_SV, 0, cps) == NULL);
    ucisScopeT bad[2] = { cps[0], NULL };
    assert(ucis_CreateCross(db, cvg, "bad1", NULL, -1, UCIS_SV, 2, bad) == NULL);

    ucis_Close(db);
}

static void test_create_cross_by_name(void)
{
    ucisT db = ucis_Open(NULL);
    ucisScopeT cps[2];
    ucisScopeT cvg = make_cvg_with_cps(db, "cg", 2, cps);

    char *names[2] = { (char*)"cp0", (char*)"cp1" };
    ucisScopeT x = ucis_CreateCrossByName(db, cvg, "x_ab", NULL, -1, UCIS_SV,
                                          2, names);
    assert(x);
    ncdbT core = ucis_internal_get_ncdb(db);
    assert(ncdb_GetCrossArity(core, (ncdbScopeT)x) == 2);
    assert(ncdb_GetCrossPoint(core, (ncdbScopeT)x, 0) == (ncdbScopeT)cps[0]);
    assert(ncdb_GetCrossPoint(core, (ncdbScopeT)x, 1) == (ncdbScopeT)cps[1]);

    /* Unknown coverpoint name → NULL, no partial mutation. */
    char *bad[2] = { (char*)"cp0", (char*)"nope" };
    assert(ucis_CreateCrossByName(db, cvg, "x_bad", NULL, -1, UCIS_SV, 2, bad) == NULL);
    /* No phantom cross got created. */
    size_t n_crosses_under_cvg = 0;
    ncdbScopeT ncvg = (ncdbScopeT)cvg;
    for (size_t i = 0; i < ncvg->child_count; i++) {
        if (ncvg->children[i]->type == UCIS_CROSS) n_crosses_under_cvg++;
    }
    assert(n_crosses_under_cvg == 1);

    ucis_Close(db);
}

static void test_coverinstance_and_roundtrip(void)
{
    unlink(TMP);
    {
        ucisT db = ucis_Open(NULL);
        ucisScopeT du = ucis_CreateScope(db, NULL, "lib.m", NULL, -1, UCIS_SV, UCIS_DU_MODULE, 0);
        ucisScopeT cvg = ucis_CreateScope(db, du, "cg", NULL, -1, UCIS_SV, UCIS_COVERGROUP, 0);
        /* Cover instance: per UCIS spec, CreateInstance(UCIS_COVERINSTANCE)
         * with du_scope=cvg threads the link correctly. */
        ucisScopeT ci = ucis_CreateInstance(db, du, "cg_inst", NULL, -1,
                                            UCIS_SV, UCIS_COVERINSTANCE, cvg, 0);
        assert(ci);
        ncdbT core = ucis_internal_get_ncdb(db);
        assert(ncdb_GetScopeDU(core, (ncdbScopeT)ci) == (ncdbScopeT)cvg);

        ucisScopeT cps[2] = {
            ucis_CreateScope(db, cvg, "cp0", NULL, -1, UCIS_SV, UCIS_COVERPOINT, 0),
            ucis_CreateScope(db, cvg, "cp1", NULL, -1, UCIS_SV, UCIS_COVERPOINT, 0),
        };
        ucisScopeT x = ucis_CreateCross(db, cvg, "x", NULL, -1, UCIS_SV, 2, cps);
        assert(x);
        assert(ucis_Write(db, TMP, NULL, 1, -1) == 0);
        ucis_Close(db);
    }
    {
        /* After reopen: the cross arity should persist (the cross.bin member
         * records the linkage, restored at read time). */
        ucisT db = ucis_Open(TMP);
        ncdbT core = ucis_internal_get_ncdb(db);
        ncdbScopeT cross = NULL;
        /* Recursive search. */
        ncdbScopeT stk[32]; int sp = 0;
        for (size_t i = 0; i < core->root_count; i++) stk[sp++] = core->roots[i];
        while (sp > 0 && !cross) {
            ncdbScopeT s = stk[--sp];
            if (s->type == UCIS_CROSS) { cross = s; break; }
            for (size_t i = 0; i < s->child_count && sp < 32; i++)
                stk[sp++] = s->children[i];
        }
        assert(cross);
        assert(ncdb_GetCrossArity(core, cross) == 2);
        assert(ncdb_GetCrossPoint(core, cross, 0) != NULL);
        assert(ncdb_GetCrossPoint(core, cross, 1) != NULL);
        ucis_Close(db);
    }
    unlink(TMP);
}

int main(void)
{
    test_create_cross_by_handle();
    test_create_cross_by_name();
    test_coverinstance_and_roundtrip();
    printf("ucis covergroup: cross handle/name + COVERINSTANCE + roundtrip OK\n");
    return 0;
}
