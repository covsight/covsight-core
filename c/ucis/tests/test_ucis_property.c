/*
 * Phase 2.5 acceptance test: typed property API.
 *
 * Coverage:
 *  - DB-level vendor / standard strings (Set + Get round-trip)
 *  - Scope int/string properties: weight, goal, source-type, toggle metric/
 *    type/dir, DU signature, toggle canonical name, coverpoint expr terms
 *  - Coveritem int properties: goal, weight, limit
 *  - Coveritem string property: COMMENT
 *  - History-node properties: int (test_status, compulsory),
 *    string (cmdline, runcwd, username, seed, toolcategory, date,
 *            simargs, timeunit, comment), real (cputime, simtime, cost)
 *  - Handle property: INSTANCE_DU set after-the-fact
 *  - Read-only properties return -1 from setters
 *  - Long-tail unmodeled properties survive via attribute fallback
 *    (UCIS_INT_STMT_INDEX, UCIS_STR_TEST_HOSTNAME)
 */

#include "ucis.h"
#include "ncdb/ncdb.h"
#include "ncdb_impl.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

extern ncdbT ucis_internal_get_ncdb(ucisT);

static void test_db_level(void)
{
    ucisT db = ucis_Open(NULL);
    assert(ucis_SetStringProperty(db, NULL, -1, UCIS_STR_VER_VENDOR_ID,        "COVSIGHT")  == 0);
    assert(ucis_SetStringProperty(db, NULL, -1, UCIS_STR_VER_VENDOR_TOOL,      "ncdb-ucis") == 0);
    assert(ucis_SetStringProperty(db, NULL, -1, UCIS_STR_VER_VENDOR_VERSION,   "0.1")       == 0);
    assert(ucis_SetStringProperty(db, NULL, -1, UCIS_STR_VER_STANDARD_VERSION, "1.0")       == 0);

    assert(strcmp(ucis_GetStringProperty(db, NULL, -1, UCIS_STR_VER_VENDOR_ID),      "COVSIGHT")  == 0);
    assert(strcmp(ucis_GetStringProperty(db, NULL, -1, UCIS_STR_VER_VENDOR_TOOL),    "ncdb-ucis") == 0);
    assert(strcmp(ucis_GetStringProperty(db, NULL, -1, UCIS_STR_VER_VENDOR_VERSION), "0.1")       == 0);
    assert(strcmp(ucis_GetStringProperty(db, NULL, -1, UCIS_STR_VER_STANDARD),       "UCIS")      == 0);

    /* UCIS_INT_NUM_TESTS reads back to 0 with no history nodes. */
    assert(ucis_GetIntProperty(db, NULL, -1, UCIS_INT_NUM_TESTS) == 0);
    /* Setter for that read-only property is an error. */
    assert(ucis_SetIntProperty(db, NULL, -1, UCIS_INT_NUM_TESTS, 99) == -1);
    ucis_Close(db);
}

static void test_scope_props(void)
{
    ucisT db = ucis_Open(NULL);
    ucisScopeT du   = ucis_CreateScope(db, NULL, "lib.m", NULL, -1, UCIS_VLOG, UCIS_DU_MODULE, 0);
    ucisScopeT inst = ucis_CreateInstance(db, NULL, "tb.dut", NULL, -1, UCIS_VLOG, UCIS_INSTANCE, du, 0);

    assert(ucis_SetIntProperty   (db, inst, -1, UCIS_INT_SCOPE_WEIGHT, 7)  == 0);
    assert(ucis_SetIntProperty   (db, inst, -1, UCIS_INT_SCOPE_GOAL,   55) == 0);
    assert(ucis_SetStringProperty(db, du,   -1, UCIS_STR_DU_SIGNATURE, "abc123") == 0);
    assert(ucis_GetIntProperty   (db, inst, -1, UCIS_INT_SCOPE_WEIGHT) == 7);
    assert(ucis_GetIntProperty   (db, inst, -1, UCIS_INT_SCOPE_GOAL)   == 55);
    assert(strcmp(ucis_GetStringProperty(db, du, -1, UCIS_STR_DU_SIGNATURE), "abc123") == 0);

    /* SCOPE_IS_UNDER_DU is read-only and computed from the DU link. */
    assert(ucis_GetIntProperty(db, inst, -1, UCIS_INT_SCOPE_IS_UNDER_DU) == 1);

    /* INSTANCE_DU_NAME read returns the DU scope's name. */
    assert(strcmp(ucis_GetStringProperty(db, inst, -1, UCIS_STR_INSTANCE_DU_NAME), "lib.m") == 0);

    /* Toggle metric/type/dir on a freshly-created toggle scope. */
    ucisScopeT tg = ucis_CreateScope(db, du, "clk", NULL, -1, UCIS_VLOG, UCIS_TOGGLE, 0);
    assert(ucis_SetIntProperty(db, tg, -1, UCIS_INT_TOGGLE_METRIC, UCIS_TOGGLE_METRIC_2STOGGLE) == 0);
    assert(ucis_SetIntProperty(db, tg, -1, UCIS_INT_TOGGLE_TYPE,   UCIS_TOGGLE_TYPE_REG)        == 0);
    assert(ucis_SetIntProperty(db, tg, -1, UCIS_INT_TOGGLE_DIR,    UCIS_TOGGLE_DIR_OUT)         == 0);
    assert(ucis_SetStringProperty(db, tg, -1, UCIS_STR_TOGGLE_CANON_NAME, "top.clk") == 0);
    assert(ucis_GetIntProperty(db, tg, -1, UCIS_INT_TOGGLE_METRIC) == UCIS_TOGGLE_METRIC_2STOGGLE);
    assert(ucis_GetIntProperty(db, tg, -1, UCIS_INT_TOGGLE_DIR)    == UCIS_TOGGLE_DIR_OUT);
    assert(strcmp(ucis_GetStringProperty(db, tg, -1, UCIS_STR_TOGGLE_CANON_NAME), "top.clk") == 0);

    /* Coverpoint expression terms */
    ucisScopeT cp = ucis_CreateScope(db, du, "cp", NULL, -1, UCIS_SV, UCIS_COVERPOINT, 0);
    assert(ucis_SetStringProperty(db, cp, -1, UCIS_STR_EXPR_TERMS, "a#b#c") == 0);
    assert(strcmp(ucis_GetStringProperty(db, cp, -1, UCIS_STR_EXPR_TERMS), "a#b#c") == 0);

    /* Long-tail integer property → attr fallback. */
    assert(ucis_SetIntProperty(db, cp, -1, UCIS_INT_CVG_STROBE, 1) == 0);
    ncdbAttrValue av;
    assert(ncdb_ScopeGetAttr(ucis_internal_get_ncdb(db), (ncdbScopeT)cp, "UCIS_INT_CVG_STROBE", &av) == 0);
    assert(av.type == NCDB_ATTR_INT64 && av.u.i64 == 1);

    ucis_Close(db);
}

static void test_cover_props(void)
{
    ucisT db = ucis_Open(NULL);
    ucisScopeT du   = ucis_CreateScope(db, NULL, "lib.m", NULL, -1, UCIS_VLOG, UCIS_DU_MODULE, 0);
    ucisScopeT inst = ucis_CreateInstance(db, NULL, "tb.dut", NULL, -1, UCIS_VLOG, UCIS_INSTANCE, du, 0);
    ucisCoverDataT d;
    memset(&d, 0, sizeof(d));
    d.type  = UCIS_STMTBIN;
    d.flags = UCIS_IS_64BIT | UCIS_HAS_COUNT;
    int idx = ucis_CreateNextCover(db, inst, "ln1", &d, NULL);

    assert(ucis_SetIntProperty   (db, inst, idx, UCIS_INT_COVER_GOAL,    99) == 0);
    assert(ucis_SetIntProperty   (db, inst, idx, UCIS_INT_COVER_WEIGHT,  4)  == 0);
    assert(ucis_SetIntProperty   (db, inst, idx, UCIS_INT_COVER_LIMIT,   77) == 0);
    assert(ucis_SetStringProperty(db, inst, idx, UCIS_STR_COMMENT, "hello") == 0);

    assert(ucis_GetIntProperty   (db, inst, idx, UCIS_INT_COVER_GOAL)   == 99);
    assert(ucis_GetIntProperty   (db, inst, idx, UCIS_INT_COVER_WEIGHT) == 4);
    assert(ucis_GetIntProperty   (db, inst, idx, UCIS_INT_COVER_LIMIT)  == 77);
    assert(strcmp(ucis_GetStringProperty(db, inst, idx, UCIS_STR_COMMENT), "hello") == 0);

    /* Long-tail property on a cover → attr fallback. */
    assert(ucis_SetIntProperty(db, inst, idx, UCIS_INT_STMT_INDEX, 5) == 0);
    ncdbAttrValue av;
    assert(ncdb_CoverGetAttr(ucis_internal_get_ncdb(db),
                             ((ncdbScopeT)inst)->covers[idx], "UCIS_INT_STMT_INDEX", &av) == 0);
    assert(av.u.i64 == 5);

    /* Out-of-range cover index errors out. */
    assert(ucis_SetIntProperty(db, inst, 99, UCIS_INT_COVER_GOAL, 1) == -1);

    ucis_Close(db);
}

static void test_history_props(void)
{
    ucisT db = ucis_Open(NULL);
    ucisHistoryNodeT h = ucis_CreateHistoryNode(db, NULL, (char*)"run", NULL,
                                                UCIS_HISTORYNODE_TEST);
    /* Now NUM_TESTS reads back to 1. */
    assert(ucis_GetIntProperty(db, NULL, -1, UCIS_INT_NUM_TESTS) == 1);

    assert(ucis_SetIntProperty   (db, h, 0, UCIS_INT_TEST_STATUS,     UCIS_TESTSTATUS_OK) == 0);
    assert(ucis_SetIntProperty   (db, h, 0, UCIS_INT_TEST_COMPULSORY, 1)                  == 0);
    assert(ucis_SetStringProperty(db, h, 0, UCIS_STR_HIST_CMDLINE,    "vsim -coverage")   == 0);
    assert(ucis_SetStringProperty(db, h, 0, UCIS_STR_HIST_RUNCWD,     "/sim/run1")        == 0);
    assert(ucis_SetStringProperty(db, h, 0, UCIS_STR_TEST_USERNAME,   "alice")            == 0);
    assert(ucis_SetStringProperty(db, h, 0, UCIS_STR_TEST_SEED,       "12345")            == 0);
    assert(ucis_SetStringProperty(db, h, 0, UCIS_STR_HIST_TOOLCATEGORY,UCIS_SIM_TOOL)     == 0);
    assert(ucis_SetStringProperty(db, h, 0, UCIS_STR_TEST_DATE,       "2026-05-23")       == 0);
    assert(ucis_SetStringProperty(db, h, 0, UCIS_STR_TEST_SIMARGS,    "+UVM_VERBOSITY=HIGH") == 0);
    assert(ucis_SetStringProperty(db, h, 0, UCIS_STR_TEST_TIMEUNIT,   "ns")               == 0);
    assert(ucis_SetStringProperty(db, h, 0, UCIS_STR_COMMENT,         "smoke")            == 0);

    assert(ucis_SetRealProperty  (db, h, 0, UCIS_REAL_HIST_CPUTIME, 12.5) == 0);
    assert(ucis_SetRealProperty  (db, h, 0, UCIS_REAL_TEST_SIMTIME, 1.5e3) == 0);
    assert(ucis_SetRealProperty  (db, h, 0, UCIS_REAL_TEST_COST,    0.25) == 0);

    assert(ucis_GetIntProperty   (db, h, 0, UCIS_INT_TEST_STATUS)     == UCIS_TESTSTATUS_OK);
    assert(ucis_GetIntProperty   (db, h, 0, UCIS_INT_TEST_COMPULSORY) == 1);
    assert(strcmp(ucis_GetStringProperty(db, h, 0, UCIS_STR_HIST_CMDLINE),  "vsim -coverage") == 0);
    assert(strcmp(ucis_GetStringProperty(db, h, 0, UCIS_STR_TEST_USERNAME), "alice")          == 0);
    assert(strcmp(ucis_GetStringProperty(db, h, 0, UCIS_STR_TEST_SEED),     "12345")          == 0);
    assert(strcmp(ucis_GetStringProperty(db, h, 0, UCIS_STR_TEST_TIMEUNIT), "ns")             == 0);
    assert(fabs(ucis_GetRealProperty(db, h, 0, UCIS_REAL_HIST_CPUTIME) - 12.5)  < 1e-9);
    assert(fabs(ucis_GetRealProperty(db, h, 0, UCIS_REAL_TEST_SIMTIME) - 1500.) < 1e-9);
    assert(fabs(ucis_GetRealProperty(db, h, 0, UCIS_REAL_TEST_COST)    - 0.25)  < 1e-9);

    /* Hostname is long-tail → attr fallback */
    assert(ucis_SetStringProperty(db, h, 0, UCIS_STR_TEST_HOSTNAME, "build-01") == 0);
    ncdbAttrValue av;
    assert(ncdb_HistoryGetAttr(ucis_internal_get_ncdb(db), (ncdbHistoryNodeT)h,
                               "UCIS_STR_TEST_HOSTNAME", &av) == 0);
    assert(av.type == NCDB_ATTR_STRING && strcmp(av.u.str.s, "build-01") == 0);

    ucis_Close(db);
}

static void test_handle_setter(void)
{
    ucisT db = ucis_Open(NULL);
    ucisScopeT du   = ucis_CreateScope(db, NULL, "lib.m",  NULL, -1, UCIS_VLOG, UCIS_DU_MODULE, 0);
    ucisScopeT inst = ucis_CreateScope(db, NULL, "tb.dut", NULL, -1, UCIS_VLOG, UCIS_INSTANCE, 0);

    /* Wire up DU after the fact via the handle setter. */
    assert(ucis_SetHandleProperty(db, inst, UCIS_HANDLE_INSTANCE_DU, du) == 0);
    assert(ncdb_GetScopeDU(ucis_internal_get_ncdb(db), (ncdbScopeT)inst) == (ncdbScopeT)du);

    /* History parent re-parenting. */
    ucisHistoryNodeT a = ucis_CreateHistoryNode(db, NULL, (char*)"a", NULL, UCIS_HISTORYNODE_MERGE);
    ucisHistoryNodeT b = ucis_CreateHistoryNode(db, NULL, (char*)"b", NULL, UCIS_HISTORYNODE_TEST);
    assert(ucis_SetHandleProperty(db, b, UCIS_HANDLE_HIST_NODE_PARENT, a) == 0);
    assert(ncdb_GetHistoryParent(ucis_internal_get_ncdb(db),
                                 (ncdbHistoryNodeT)b) == (ncdbHistoryNodeT)a);

    /* Read-only handle prop rejects setter. */
    assert(ucis_SetHandleProperty(db, inst, UCIS_HANDLE_SCOPE_PARENT, du) == -1);

    ucis_Close(db);
}

int main(void)
{
    test_db_level();
    test_scope_props();
    test_cover_props();
    test_history_props();
    test_handle_setter();
    printf("ucis property: int/string/real/handle Set+Get + attr fallback OK\n");
    return 0;
}
