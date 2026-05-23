/*
 * Phase 2.4 acceptance test: ucis_CreateNextCover dispatch across all UCIS
 * bin types, ucis_IncrementCover, ucis_SetCoverFlag, and a full Verilator-
 * shaped round-trip (Open → DU → instance → STMT/BRANCH/EXPR/TOGGLE/FSM/
 * USER/COVER bins → Write → re-Open → counts intact).
 */

#include "ucis.h"
#include "ncdb/ncdb.h"
#include "ncdb_impl.h"   /* tests access scope/cover fields directly */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern ncdbT ucis_internal_get_ncdb(ucisT);

static const char *TMP = "/tmp/test_ucis_cover_rt.ncdb";

static int create_cover(ucisT db, ucisScopeT parent, const char *name,
                        ucisCoverTypeT type, uint64_t count)
{
    ucisCoverDataT d;
    memset(&d, 0, sizeof(d));
    d.type = type;
    d.flags = UCIS_IS_64BIT | UCIS_HAS_COUNT;
    d.data.int64 = count;
    return ucis_CreateNextCover(db, parent, name, &d, NULL);
}

static int sum_cover_counts(ncdbT core, ncdbScopeT s, int *visited)
{
    int total = 0;
    (void)core;
    for (size_t i = 0; i < s->cover_count; i++) {
        total += (int)s->covers[i]->count;
        (*visited)++;
    }
    for (size_t i = 0; i < s->child_count; i++) {
        total += sum_cover_counts(core, s->children[i], visited);
    }
    return total;
}

static void test_dispatch_all_bin_types(void)
{
    ucisT db = ucis_Open(NULL);
    assert(db);

    ucisScopeT du   = ucis_CreateScope(db, NULL, "lib.m", NULL, -1, UCIS_VLOG, UCIS_DU_MODULE, 0);
    ucisScopeT inst = ucis_CreateInstance(db, NULL, "tb.dut", NULL, -1, UCIS_VLOG, UCIS_INSTANCE, du, 0);

    /* One bin of each UCIS coveritem type the writer surface supports. */
    int idxs[10];
    int i = 0;
    idxs[i++] = create_cover(db, inst, "stmt0",    UCIS_STMTBIN,   3);
    idxs[i++] = create_cover(db, inst, "branch0",  UCIS_BRANCHBIN, 5);
    idxs[i++] = create_cover(db, inst, "expr0",    UCIS_EXPRBIN,   7);
    idxs[i++] = create_cover(db, inst, "cond0",    UCIS_CONDBIN,   11);
    idxs[i++] = create_cover(db, inst, "tog0",     UCIS_TOGGLEBIN, 13);
    idxs[i++] = create_cover(db, inst, "fsm0",     UCIS_FSMBIN,    17);
    idxs[i++] = create_cover(db, inst, "user0",    UCIS_USERBIN,   19);
    idxs[i++] = create_cover(db, inst, "cvg0",     UCIS_CVGBIN,    23);
    idxs[i++] = create_cover(db, inst, "cover0",   UCIS_COVERBIN,  29);
    idxs[i++] = create_cover(db, inst, "assert0",  UCIS_ASSERTBIN, 31);

    /* Indices must be the insertion order (0..9). */
    for (int k = 0; k < 10; k++) assert(idxs[k] == k);

    /* Each cover should retain its type byte-for-byte (NCDB enum is UCIS-aligned). */
    ncdbT core = ucis_internal_get_ncdb(db);
    static const ucisCoverTypeT expected[10] = {
        UCIS_STMTBIN, UCIS_BRANCHBIN, UCIS_EXPRBIN, UCIS_CONDBIN,
        UCIS_TOGGLEBIN, UCIS_FSMBIN, UCIS_USERBIN, UCIS_CVGBIN,
        UCIS_COVERBIN, UCIS_ASSERTBIN
    };
    for (int k = 0; k < 10; k++) {
        ncdbScopeT s = (ncdbScopeT)inst;
        assert((ucisCoverTypeT)s->covers[k]->type == expected[k]);
    }

    assert(ucis_Close(db) == 0);
}

static void test_increment_and_flags(void)
{
    ucisT db = ucis_Open(NULL);
    ucisScopeT du   = ucis_CreateScope(db, NULL, "lib.m", NULL, -1, UCIS_VLOG, UCIS_DU_MODULE, 0);
    ucisScopeT inst = ucis_CreateInstance(db, NULL, "tb.dut", NULL, -1, UCIS_VLOG, UCIS_INSTANCE, du, 0);

    int idx = create_cover(db, inst, "ln1", UCIS_STMTBIN, 1);
    assert(idx == 0);
    assert(ucis_IncrementCover(db, inst, idx, 41) == 0);
    ncdbT core = ucis_internal_get_ncdb(db);
    ncdbScopeT s = (ncdbScopeT)inst;
    assert(s->covers[0]->count == 42);

    /* Flag set/clear */
    ucis_SetCoverFlag(db, inst, idx, UCIS_IS_COVERED, 1);
    assert((s->covers[0]->flags & UCIS_IS_COVERED) != 0);
    ucis_SetCoverFlag(db, inst, idx, UCIS_IS_COVERED, 0);
    assert((s->covers[0]->flags & UCIS_IS_COVERED) == 0);

    /* Out-of-range index is a non-crashing no-op / error. */
    assert(ucis_IncrementCover(db, inst, 99, 1) == -1);
    ucis_SetCoverFlag(db, inst, 99, UCIS_IS_COVERED, 1); /* no crash */

    (void)core;
    assert(ucis_Close(db) == 0);
}

static void test_per_cover_metadata_and_srcinfo(void)
{
    ucisT db = ucis_Open(NULL);
    ucisScopeT du   = ucis_CreateScope(db, NULL, "lib.m", NULL, -1, UCIS_VLOG, UCIS_DU_MODULE, 0);
    ucisScopeT inst = ucis_CreateInstance(db, NULL, "tb.dut", NULL, -1, UCIS_VLOG, UCIS_INSTANCE, du, 0);

    ucisFileHandleT fh = ucis_CreateFileHandle(db, "/p/dut.sv", NULL);
    ucisSourceInfoT si = { fh, 100, 4 };

    ucisCoverDataT d;
    memset(&d, 0, sizeof(d));
    d.type   = UCIS_STMTBIN;
    d.flags  = UCIS_IS_32BIT | UCIS_HAS_COUNT | UCIS_HAS_GOAL | UCIS_HAS_WEIGHT | UCIS_HAS_LIMIT;
    d.data.int32 = 7;
    d.goal   = 50;
    d.weight = 3;
    d.limit  = 100;

    int idx = ucis_CreateNextCover(db, inst, "ln100", &d, &si);
    assert(idx == 0);

    ncdbT core = ucis_internal_get_ncdb(db);
    ncdbScopeT s = (ncdbScopeT)inst;
    ncdbCoverT c = s->covers[0];
    assert(c->count == 7);
    assert(c->goal  == 50);
    assert(c->weight == 3);
    assert(c->at_least == 100);
    assert(c->source_path && strcmp(c->source_path, "/p/dut.sv") == 0);
    assert(c->source_line == 100);
    assert(c->source_token == 4);
    /* Preserved flags must include what we asked for. */
    assert((c->flags & UCIS_HAS_GOAL) != 0);

    assert(ucis_Close(db) == 0);
}

static void test_roundtrip_persists_counts(void)
{
    unlink(TMP);
    ucisT db = ucis_Open(NULL);
    ucisScopeT du   = ucis_CreateScope(db, NULL, "lib.m", NULL, -1, UCIS_VLOG, UCIS_DU_MODULE, 0);
    ucisScopeT inst = ucis_CreateInstance(db, NULL, "tb.dut", NULL, -1, UCIS_VLOG, UCIS_INSTANCE, du, 0);
    ucisScopeT blk  = ucis_CreateScope(db, inst, "blk1", NULL, -1, UCIS_VLOG, UCIS_BLOCK, 0);
    create_cover(db, blk, "a", UCIS_STMTBIN, 10);
    create_cover(db, blk, "b", UCIS_STMTBIN, 20);
    create_cover(db, blk, "c", UCIS_BRANCHBIN, 5);
    assert(ucis_Write(db, TMP, NULL, 1, -1) == 0);
    assert(ucis_Close(db) == 0);

    ucisT db2 = ucis_Open(TMP);
    assert(db2);
    ncdbT core2 = ucis_internal_get_ncdb(db2);
    int visited = 0;
    int total = 0;
    /* Roots: should be [du, inst]. Sum across the whole tree. */
    for (size_t i = 0; i < core2->root_count; i++)
        total += sum_cover_counts(core2, core2->roots[i], &visited);
    assert(visited == 3);
    assert(total == 35);
    assert(ucis_Close(db2) == 0);
    unlink(TMP);
}

int main(void)
{
    test_dispatch_all_bin_types();
    test_increment_and_flags();
    test_per_cover_metadata_and_srcinfo();
    test_roundtrip_persists_counts();
    printf("ucis cover: CreateNextCover dispatch + Increment + Flag + roundtrip OK\n");
    return 0;
}
