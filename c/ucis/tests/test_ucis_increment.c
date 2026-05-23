/*
 * Phase 2.9 acceptance test: ucis_IncrementCover in streaming-writer mode.
 *
 * Verilator's intended emission pattern is: open the DB once, create scopes
 * and bins with count=0, then call IncrementCover many times as the design
 * runs, then ucis_Close persists the result. This test exercises that flow
 * and the spec-defined edge cases.
 */

#include "ucis.h"
#include "ncdb/ncdb.h"
#include "ncdb_impl.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern ncdbT ucis_internal_get_ncdb(ucisT);

static const char *TMP = "/tmp/test_ucis_increment.ncdb";

static int make_zero_cover(ucisT db, ucisScopeT parent, const char *name,
                           ucisCoverTypeT type)
{
    ucisCoverDataT d;
    memset(&d, 0, sizeof(d));
    d.type  = type;
    d.flags = UCIS_IS_64BIT | UCIS_HAS_COUNT;
    return ucis_CreateNextCover(db, parent, name, &d, NULL);
}

static void test_repeated_increments_accumulate(void)
{
    ucisT db = ucis_Open(NULL);
    ucisScopeT du   = ucis_CreateScope(db, NULL, "lib.m", NULL, -1, UCIS_VLOG, UCIS_DU_MODULE, 0);
    ucisScopeT inst = ucis_CreateInstance(db, NULL, "tb.dut", NULL, -1, UCIS_VLOG, UCIS_INSTANCE, du, 0);
    int idx = make_zero_cover(db, inst, "ln1", UCIS_STMTBIN);
    assert(idx == 0);

    /* 1000 single-step increments mirror a sim-time-loop. */
    for (int i = 0; i < 1000; i++) assert(ucis_IncrementCover(db, inst, idx, 1) == 0);
    /* One bulk increment. */
    assert(ucis_IncrementCover(db, inst, idx, 12345) == 0);

    ncdbScopeT s = (ncdbScopeT)inst;
    assert(s->covers[0]->count == 1000 + 12345);

    ucis_Close(db);
}

static void test_vector_bin_rejected(void)
{
    ucisT db = ucis_Open(NULL);
    ucisScopeT du   = ucis_CreateScope(db, NULL, "lib.m", NULL, -1, UCIS_VLOG, UCIS_DU_MODULE, 0);
    ucisScopeT inst = ucis_CreateInstance(db, NULL, "tb.dut", NULL, -1, UCIS_VLOG, UCIS_INSTANCE, du, 0);

    /* Vector coveritem: spec says IncrementCover is undefined on these. */
    unsigned char bits[4] = {0xFF, 0, 0, 0};
    ucisCoverDataT d;
    memset(&d, 0, sizeof(d));
    d.type = UCIS_USERBIN;
    d.flags = UCIS_IS_VECTOR;
    d.data.bytevector = bits;
    d.bitlen = 32;
    int idx = ucis_CreateNextCover(db, inst, "vec0", &d, NULL);
    assert(idx == 0);
    assert(ucis_IncrementCover(db, inst, idx, 1) == -1);

    ucis_Close(db);
}

static void test_error_paths(void)
{
    ucisT db = ucis_Open(NULL);
    ucisScopeT du   = ucis_CreateScope(db, NULL, "lib.m", NULL, -1, UCIS_VLOG, UCIS_DU_MODULE, 0);
    ucisScopeT inst = ucis_CreateInstance(db, NULL, "tb.dut", NULL, -1, UCIS_VLOG, UCIS_INSTANCE, du, 0);
    int idx = make_zero_cover(db, inst, "ln1", UCIS_STMTBIN);

    assert(ucis_IncrementCover(db, NULL, idx, 1)  == -1);
    assert(ucis_IncrementCover(db, inst, -1, 1)   == -1);
    assert(ucis_IncrementCover(db, inst, 999, 1)  == -1);
    /* Valid call still works after errors. */
    assert(ucis_IncrementCover(db, inst, idx, 7) == 0);
    assert(((ncdbScopeT)inst)->covers[idx]->count == 7);

    ucis_Close(db);
}

static int sum_covers_recursive(ncdbScopeT s, uint64_t want_type)
{
    int t = 0;
    for (size_t i = 0; i < s->cover_count; i++)
        if (s->covers[i]->type == want_type) t += (int)s->covers[i]->count;
    for (size_t i = 0; i < s->child_count; i++)
        t += sum_covers_recursive(s->children[i], want_type);
    return t;
}

static void test_writestream_flow(void)
{
    unlink(TMP);
    ucisT db = ucis_OpenWriteStream(TMP);
    assert(db);

    /* Realistic shape: one typed cover-scope per bin family. NCDB's scope-
     * tree serializer stores a single cover-type word per parent scope, so
     * mixing UCIS_STMTBIN and UCIS_BRANCHBIN as siblings of the same scope
     * collapses on write. UCIS code-coverage scopes are typed by construction
     * (UCIS_BLOCK for line, UCIS_BRANCH for branch) and don't hit this. */
    ucisScopeT du    = ucis_CreateScope(db, NULL, "lib.m", NULL, -1, UCIS_VLOG, UCIS_DU_MODULE, 0);
    ucisScopeT inst  = ucis_CreateInstance(db, NULL, "tb.dut", NULL, -1, UCIS_VLOG, UCIS_INSTANCE, du, 0);
    ucisScopeT blk_a = ucis_CreateScope(db, inst, "lns",    NULL, -1, UCIS_VLOG, UCIS_BLOCK,  0);
    ucisScopeT br_b  = ucis_CreateScope(db, inst, "br1",    NULL, -1, UCIS_VLOG, UCIS_BRANCH, 0);
    int a = make_zero_cover(db, blk_a, "stmt0",   UCIS_STMTBIN);
    int b = make_zero_cover(db, br_b,  "branch0", UCIS_BRANCHBIN);

    for (int i = 0; i < 5; i++) {
        assert(ucis_IncrementCover(db, blk_a, a, 2) == 0);
        assert(ucis_IncrementCover(db, br_b,  b, 1) == 0);
        /* Spec-permitted intermediate flush during streaming. */
        assert(ucis_WriteStream(db) == 0);
    }
    assert(ucis_Close(db) == 0);

    ucisT db2 = ucis_Open(TMP);
    ncdbT core = ucis_internal_get_ncdb(db2);
    int total_stmt = 0, total_branch = 0;
    for (size_t i = 0; i < core->root_count; i++) {
        total_stmt   += sum_covers_recursive(core->roots[i], (uint64_t)UCIS_STMTBIN);
        total_branch += sum_covers_recursive(core->roots[i], (uint64_t)UCIS_BRANCHBIN);
    }
    assert(total_stmt   == 10);
    assert(total_branch == 5);
    ucis_Close(db2);
    unlink(TMP);
}

int main(void)
{
    test_repeated_increments_accumulate();
    test_vector_bin_rejected();
    test_error_paths();
    test_writestream_flow();
    printf("ucis increment: accumulate + reject-vector + error-paths + write-stream OK\n");
    return 0;
}
