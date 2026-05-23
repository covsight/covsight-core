/*
 * Phase 2.6 acceptance test: ucisSourceInfoT plumbing through per-cover and
 * per-scope fields, including survival across a write/reopen cycle (no file
 * handles after reopen, but the getter rebuilds them from the persisted path
 * via the internal cache).
 */

#include "ucis.h"
#include "ncdb/ncdb.h"
#include "ncdb_impl.h"   /* for direct tree walking in the roundtrip test */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern ncdbT ucis_internal_get_ncdb(ucisT);

static const char *TMP = "/tmp/test_ucis_srcinfo.ncdb";

static void test_set_then_get_scope(void)
{
    ucisT db = ucis_Open(NULL);
    ucisFileHandleT fh = ucis_CreateFileHandle(db, "/p/top.sv", NULL);
    ucisScopeT du = ucis_CreateScope(db, NULL, "lib.m", NULL, -1, UCIS_VLOG, UCIS_DU_MODULE, 0);
    ucisSourceInfoT si_in = { fh, 100, 9 };
    ucisScopeT blk = ucis_CreateScope(db, du, "blk", &si_in, -1, UCIS_VLOG, UCIS_BLOCK, 0);

    ucisSourceInfoT si_out;
    assert(ucis_GetScopeSourceInfo(db, blk, &si_out) == 0);
    assert(si_out.line == 100 && si_out.token == 9);
    assert(strcmp(ucis_GetFileName(db, si_out.filehandle), "/p/top.sv") == 0);
    /* Same path → returns the same cached handle. */
    assert(si_out.filehandle == fh);

    /* Scope without srcinfo returns -1. */
    assert(ucis_GetScopeSourceInfo(db, du, &si_out) == -1);

    ucis_Close(db);
}

static void test_set_then_get_cover(void)
{
    ucisT db = ucis_Open(NULL);
    ucisFileHandleT fh = ucis_CreateFileHandle(db, "/p/dut.sv", NULL);
    ucisScopeT du   = ucis_CreateScope(db, NULL, "lib.m", NULL, -1, UCIS_VLOG, UCIS_DU_MODULE, 0);
    ucisScopeT inst = ucis_CreateInstance(db, NULL, "tb.dut", NULL, -1, UCIS_VLOG, UCIS_INSTANCE, du, 0);

    ucisCoverDataT d;
    memset(&d, 0, sizeof(d));
    d.type = UCIS_STMTBIN;
    d.flags = UCIS_IS_64BIT | UCIS_HAS_COUNT;
    d.data.int64 = 1;
    ucisSourceInfoT si_in = { fh, 42, 3 };
    int idx = ucis_CreateNextCover(db, inst, "ln42", &d, &si_in);
    assert(idx == 0);

    ucisSourceInfoT si_out;
    assert(ucis_GetCoverSourceInfo(db, inst, idx, &si_out) == 0);
    assert(si_out.line == 42 && si_out.token == 3);
    assert(strcmp(ucis_GetFileName(db, si_out.filehandle), "/p/dut.sv") == 0);
    assert(si_out.filehandle == fh);

    /* Bad indices yield -1. */
    assert(ucis_GetCoverSourceInfo(db, inst, 99, &si_out) == -1);
    assert(ucis_GetCoverSourceInfo(db, inst, -1, &si_out) == -1);

    ucis_Close(db);
}

/* Find the first scope of a given type by recursive DFS over the raw NCDB
 * tree. Tests reach into impl directly since the public reader iteration
 * doesn't yet expose a typed find-first. */
static ncdbScopeT find_first_of_type(ncdbScopeT root, uint64_t type)
{
    if (!root) return NULL;
    if (root->type == type) return root;
    for (size_t i = 0; i < root->child_count; i++) {
        ncdbScopeT hit = find_first_of_type(root->children[i], type);
        if (hit) return hit;
    }
    return NULL;
}

static void test_srcinfo_survives_roundtrip(void)
{
    unlink(TMP);

    {
        ucisT db = ucis_Open(NULL);
        ucisFileHandleT fh = ucis_CreateFileHandle(db, "/p/a.sv", NULL);
        ucisScopeT du   = ucis_CreateScope(db, NULL, "lib.m", NULL, -1, UCIS_VLOG, UCIS_DU_MODULE, 0);
        ucisScopeT inst = ucis_CreateInstance(db, NULL, "tb.dut", NULL, -1, UCIS_VLOG, UCIS_INSTANCE, du, 0);
        ucisSourceInfoT si_blk = { fh, 10, 1 };
        ucisScopeT blk = ucis_CreateScope(db, inst, "b", &si_blk, -1, UCIS_VLOG, UCIS_BLOCK, 0);
        ucisCoverDataT d;
        memset(&d, 0, sizeof(d));
        d.type = UCIS_STMTBIN; d.flags = UCIS_IS_64BIT | UCIS_HAS_COUNT;
        ucisSourceInfoT si_c = { fh, 11, 0 };
        ucis_CreateNextCover(db, blk, "c0", &d, &si_c);
        assert(ucis_Write(db, TMP, NULL, 1, -1) == 0);
        ucis_Close(db);
    }

    {
        ucisT db = ucis_Open(TMP);
        assert(db);
        ncdbT core = ucis_internal_get_ncdb(db);
        ncdbScopeT blk = NULL;
        for (size_t i = 0; i < core->root_count && !blk; i++) {
            blk = find_first_of_type(core->roots[i], (uint64_t)UCIS_BLOCK);
        }
        assert(blk);

        ucisSourceInfoT si;
        assert(ucis_GetScopeSourceInfo(db, (ucisScopeT)blk, &si) == 0);
        assert(si.line == 10);
        assert(strcmp(ucis_GetFileName(db, si.filehandle), "/p/a.sv") == 0);

        ucisSourceInfoT sic;
        assert(ucis_GetCoverSourceInfo(db, (ucisScopeT)blk, 0, &sic) == 0);
        assert(sic.line == 11);
        assert(strcmp(ucis_GetFileName(db, sic.filehandle), "/p/a.sv") == 0);
        /* Path-keyed cache must return the same handle for both lookups. */
        assert(si.filehandle == sic.filehandle);

        ucis_Close(db);
    }
    unlink(TMP);
}

int main(void)
{
    test_set_then_get_scope();
    test_set_then_get_cover();
    test_srcinfo_survives_roundtrip();
    printf("ucis srcinfo: scope/cover set+get + survives write/reopen OK\n");
    return 0;
}
