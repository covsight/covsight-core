/*
 * Phase 2.3 acceptance test: scope/instance/toggle/history-node creation.
 */

#include "ucis.h"
#include "ncdb/ncdb.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

extern ncdbT ucis_internal_get_ncdb(ucisT);

static void test_create_scope_with_srcinfo(void)
{
    ucisT db = ucis_Open(NULL);
    assert(db);
    ncdbT core = ucis_internal_get_ncdb(db);

    ucisFileHandleT fh = ucis_CreateFileHandle(db, "/src/top.sv", NULL);
    assert(fh);

    ucisScopeT du = ucis_CreateScope(db, NULL, "top_pkg.top", NULL, /*w*/-1,
                                     UCIS_SV, UCIS_DU_MODULE,
                                     UCIS_ENABLED_STMT | UCIS_ENABLED_TOGGLE);
    assert(du);
    assert(ucis_GetScopeType(db, du) == UCIS_DU_MODULE);
    assert((ucis_GetScopeFlags(db, du) & UCIS_ENABLED_STMT) != 0);

    ucisSourceInfoT si = { fh, 42, 7 };
    ucisScopeT blk = ucis_CreateScope(db, du, "blk", &si, /*w*/3,
                                      UCIS_SV, UCIS_BLOCK, 0);
    assert(blk);
    assert(strcmp(ncdb_GetScopeSourcePath(core, (ncdbScopeT)blk), "/src/top.sv") == 0);
    assert(ncdb_GetScopeSourceLine(core, (ncdbScopeT)blk) == 42);
    assert(ncdb_GetScopeWeight(core, (ncdbScopeT)blk) == 3);

    /* Flag manipulators */
    ucis_SetScopeFlag(db, blk, UCIS_INST_ONCE, 1);
    assert((ucis_GetScopeFlags(db, blk) & UCIS_INST_ONCE) != 0);
    ucis_SetScopeFlag(db, blk, UCIS_INST_ONCE, 0);
    assert((ucis_GetScopeFlags(db, blk) & UCIS_INST_ONCE) == 0);
    ucis_SetScopeFlags(db, blk, UCIS_UOR_SAFE_SCOPE);
    assert(ucis_GetScopeFlags(db, blk) == UCIS_UOR_SAFE_SCOPE);

    assert(ucis_Close(db) == 0);
}

static void test_create_instance_with_du_link(void)
{
    ucisT db = ucis_Open(NULL);
    assert(db);
    ncdbT core = ucis_internal_get_ncdb(db);

    ucisScopeT du   = ucis_CreateScope(db, NULL, "lib.foo", NULL, -1,
                                       UCIS_VLOG, UCIS_DU_MODULE, 0);
    ucisScopeT inst = ucis_CreateInstance(db, NULL, "tb.u_foo", NULL, -1,
                                          UCIS_VLOG, UCIS_INSTANCE, du, 0);
    assert(du && inst && du != inst);
    assert(ncdb_GetScopeDU(core, (ncdbScopeT)inst) == (ncdbScopeT)du);
    /* CreateInstance MUST set the SPECIALIZED flag per UCIS spec. */
    assert((ucis_GetScopeFlags(db, inst) & UCIS_SCOPE_SPECIALIZED) != 0);

    /* By-name resolution should find the same DU. */
    ucisScopeT inst2 = ucis_CreateInstanceByName(db, NULL, "tb.u_foo2", NULL,
                                                 -1, UCIS_VLOG, UCIS_INSTANCE,
                                                 (char*)"lib.foo", 0);
    assert(inst2);
    assert(ncdb_GetScopeDU(core, (ncdbScopeT)inst2) == (ncdbScopeT)du);

    /* DU lookup miss returns NULL. */
    assert(ucis_CreateInstanceByName(db, NULL, "x", NULL, -1, UCIS_VLOG,
                                     UCIS_INSTANCE, (char*)"no.such", 0) == NULL);

    /* du_scope NULL is rejected. */
    assert(ucis_CreateInstance(db, NULL, "y", NULL, -1, UCIS_VLOG,
                               UCIS_INSTANCE, NULL, 0) == NULL);

    assert(ucis_Close(db) == 0);
}

static void test_create_toggle(void)
{
    ucisT db = ucis_Open(NULL);
    assert(db);
    ncdbT core = ucis_internal_get_ncdb(db);

    ucisScopeT du = ucis_CreateScope(db, NULL, "lib.m", NULL, -1, UCIS_VLOG,
                                     UCIS_DU_MODULE, 0);
    ucisScopeT tg = ucis_CreateToggle(db, du, "clk", "top.clk", 0,
                                      UCIS_TOGGLE_METRIC_2STOGGLE,
                                      UCIS_TOGGLE_TYPE_NET,
                                      UCIS_TOGGLE_DIR_IN);
    assert(tg);
    assert(ucis_GetScopeType(db, tg) == UCIS_TOGGLE);
    assert((ucis_GetScopeFlags(db, tg) & UCIS_SCOPE_SPECIALIZED) != 0);
    assert(strcmp(ncdb_GetToggleCanonicalName(core, (ncdbScopeT)tg), "top.clk") == 0);
    assert(ncdb_GetToggleMetric(core, (ncdbScopeT)tg) == UCIS_TOGGLE_METRIC_2STOGGLE);
    assert(ncdb_GetToggleType  (core, (ncdbScopeT)tg) == UCIS_TOGGLE_TYPE_NET);
    assert(ncdb_GetToggleDir   (core, (ncdbScopeT)tg) == UCIS_TOGGLE_DIR_IN);

    assert(ucis_Close(db) == 0);
}

static void test_create_history(void)
{
    ucisT db = ucis_Open(NULL);
    assert(db);
    ncdbT core = ucis_internal_get_ncdb(db);

    ucisHistoryNodeT root = ucis_CreateHistoryNode(db, NULL, (char*)"merge_root",
                                                   NULL, UCIS_HISTORYNODE_MERGE);
    ucisHistoryNodeT t1   = ucis_CreateHistoryNode(db, root,
                                                   (char*)"run_1",
                                                   (char*)"/runs/1",
                                                   UCIS_HISTORYNODE_TEST);
    assert(root && t1 && root != t1);

    assert(ncdb_GetHistoryKind(core, (ncdbHistoryNodeT)root) == UCIS_HISTORYNODE_MERGE);
    assert(ncdb_GetHistoryKind(core, (ncdbHistoryNodeT)t1)   == UCIS_HISTORYNODE_TEST);
    assert(strcmp(ncdb_GetHistoryLogicalName(core, (ncdbHistoryNodeT)t1), "run_1") == 0);
    assert(strcmp(ncdb_GetHistoryPhysicalName(core, (ncdbHistoryNodeT)t1), "/runs/1") == 0);
    assert(ncdb_GetHistoryParent(core, (ncdbHistoryNodeT)t1) == (ncdbHistoryNodeT)root);

    assert(ucis_CreateHistoryNode(db, NULL, NULL, NULL,
                                  UCIS_HISTORYNODE_TEST) == NULL);

    assert(ucis_Close(db) == 0);
}

static void test_compose_parse_duname(void)
{
    const char *s;

    s = ucis_ComposeDUName("work", "foo", "rtl");
    assert(strcmp(s, "work.foo(rtl)") == 0);
    s = ucis_ComposeDUName("work", "foo", NULL);
    assert(strcmp(s, "work.foo") == 0);
    s = ucis_ComposeDUName(NULL, "foo", NULL);
    assert(strcmp(s, "foo") == 0);

    const char *lib = NULL, *pri = NULL, *sec = NULL;
    ucis_ParseDUName("work.foo(rtl)", &lib, &pri, &sec);
    assert(lib && strcmp(lib, "work") == 0);
    assert(pri && strcmp(pri, "foo") == 0);
    assert(sec && strcmp(sec, "rtl") == 0);

    ucis_ParseDUName("foo", &lib, &pri, &sec);
    assert(strcmp(pri, "foo") == 0);
    assert(strcmp(lib, "") == 0);
    assert(strcmp(sec, "") == 0);
}

int main(void)
{
    test_create_scope_with_srcinfo();
    test_create_instance_with_du_link();
    test_create_toggle();
    test_create_history();
    test_compose_parse_duname();
    printf("ucis scope: create scope/instance/toggle/history + compose/parse OK\n");
    return 0;
}
