/*
 * Phase 2.7 acceptance test: ucis_SetTestData / ucis_GetTestData. The packer
 * must populate every field NCDB models and the getter must round-trip them,
 * including across a write/reopen cycle.
 */

#include "ucis.h"
#include "ncdb/ncdb.h"
#include "ncdb_impl.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern ncdbT ucis_internal_get_ncdb(ucisT);

static const char *TMP = "/tmp/test_ucis_testdata.ncdb";

static ucisTestDataT make_sample(void)
{
    ucisTestDataT td;
    memset(&td, 0, sizeof(td));
    td.teststatus   = UCIS_TESTSTATUS_WARNING;
    td.simtime      = 12345.5;
    td.timeunit     = "ns";
    td.runcwd       = "/sim/run42";
    td.cputime      = 67.25;
    td.seed         = "0xdeadbeef";
    td.cmd          = "vsim -coverage";
    td.args         = "+UVM_TESTNAME=foo";
    td.compulsory   = 1;
    td.date         = "2026-05-23T10:00:00Z";
    td.username     = "alice";
    td.cost         = 0.5;
    td.toolcategory = UCIS_SIM_TOOL;
    return td;
}

static void assert_round_trip(const ucisTestDataT *a, const ucisTestDataT *b)
{
    assert(a->teststatus == b->teststatus);
    assert(fabs(a->simtime - b->simtime) < 1e-9);
    assert(fabs(a->cputime - b->cputime) < 1e-9);
    assert(fabs(a->cost    - b->cost)    < 1e-9);
    assert(a->compulsory == b->compulsory);
    assert(strcmp(a->timeunit,     b->timeunit)     == 0);
    assert(strcmp(a->runcwd,       b->runcwd)       == 0);
    assert(strcmp(a->seed,         b->seed)         == 0);
    assert(strcmp(a->cmd,          b->cmd)          == 0);
    assert(strcmp(a->args,         b->args)         == 0);
    assert(strcmp(a->date,         b->date)         == 0);
    assert(strcmp(a->username,     b->username)     == 0);
    assert(strcmp(a->toolcategory, b->toolcategory) == 0);
}

static void test_inmem(void)
{
    ucisT db = ucis_Open(NULL);
    ucisHistoryNodeT h = ucis_CreateHistoryNode(db, NULL, (char*)"run", NULL,
                                                UCIS_HISTORYNODE_TEST);
    ucisTestDataT in = make_sample();
    assert(ucis_SetTestData(db, h, &in) == 0);

    ucisTestDataT out;
    assert(ucis_GetTestData(db, h, &out) == 0);
    assert_round_trip(&in, &out);

    /* NULL inputs are errors. */
    assert(ucis_SetTestData(db, NULL, &in) == -1);
    assert(ucis_GetTestData(db, h,    NULL) == -1);

    ucis_Close(db);
}

static void test_persists_across_reopen(void)
{
    unlink(TMP);
    ucisTestDataT in = make_sample();

    {
        ucisT db = ucis_Open(NULL);
        ucisHistoryNodeT h = ucis_CreateHistoryNode(db, NULL, (char*)"run",
                                                    (char*)"/runs/42",
                                                    UCIS_HISTORYNODE_TEST);
        assert(ucis_SetTestData(db, h, &in) == 0);
        assert(ucis_Write(db, TMP, NULL, 1, -1) == 0);
        ucis_Close(db);
    }
    {
        ucisT db = ucis_Open(TMP);
        ncdbT core = ucis_internal_get_ncdb(db);
        assert(core->history_count >= 1);
        ucisHistoryNodeT h = (ucisHistoryNodeT)core->history_nodes[0];
        ucisTestDataT out;
        assert(ucis_GetTestData(db, h, &out) == 0);
        assert_round_trip(&in, &out);
        ucis_Close(db);
    }
    unlink(TMP);
}

int main(void)
{
    test_inmem();
    test_persists_across_reopen();
    printf("ucis testdata: SetTestData + GetTestData round-trip + persistence OK\n");
    return 0;
}
