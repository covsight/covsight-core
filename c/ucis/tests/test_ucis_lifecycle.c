/*
 * Phase 2.2 acceptance test:
 *  - ucis_Open(NULL) yields a usable empty in-memory DB
 *  - ucis_Write produces an .ncdb that ucis_Open can read back
 *  - ucis_OpenWriteStream + ucis_Close persists at Close time
 *  - file handles survive across the DB lifetime and remember their filename
 *  - ucis_CreateSrcFileHandle threads through the per-DU file table
 *
 * Property-bag setters aren't yet implemented (Phase 2.5), so DB content is
 * tested via the underlying ncdb writer where needed.
 */

#include "ucis.h"
#include "ncdb/ncdb.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Provided by ucis_lifecycle.c for in-tree test access. */
extern ncdbScopeT ncdb_impl_create_scope(ncdbT, ncdbScopeT, uint64_t, const char*);
extern ncdbT      ucis_internal_get_ncdb(ucisT);

static const char *TMP_INMEM  = "/tmp/test_ucis_lifecycle_inmem.ncdb";
static const char *TMP_STREAM = "/tmp/test_ucis_lifecycle_stream.ncdb";

static void cleanup(void) {
    unlink(TMP_INMEM);
    unlink(TMP_STREAM);
}

static void test_inmem_roundtrip(void)
{
    ucisT db = ucis_Open(NULL);
    assert(db);
    ncdbT core = ucis_internal_get_ncdb(db);
    assert(core);

    /* Tag the DB so we can recognise it after round-trip. The public
     * property setter API lands in Phase 2.5; for now poke ncdb directly. */
    ncdb_SetVendorId    (core, "COVSIGHT");
    ncdb_SetVendorTool  (core, "ncdb-ucis");
    ncdb_SetUcisStandard(core, "1.0");

    assert(ucis_Write(db, TMP_INMEM, NULL, 1, -1) == 0);
    assert(ucis_Close(db) == 0);

    ucisT db2 = ucis_Open(TMP_INMEM);
    assert(db2);
    ncdbT core2 = ucis_internal_get_ncdb(db2);
    assert(core2);
    const char *vid  = ncdb_GetVendorId   (core2);
    const char *vtl  = ncdb_GetVendorTool (core2);
    const char *vstd = ncdb_GetUcisStandard(core2);
    assert(vid  && strcmp(vid,  "COVSIGHT")  == 0);
    assert(vtl  && strcmp(vtl,  "ncdb-ucis") == 0);
    assert(vstd && strcmp(vstd, "1.0")       == 0);
    assert(ucis_Close(db2) == 0);
}

static void test_writestream(void)
{
    ucisT db = ucis_OpenWriteStream(TMP_STREAM);
    assert(db);
    /* Stream flush calls are no-ops in the in-memory impl and must succeed. */
    assert(ucis_WriteStream(db) == 0);
    assert(ucis_WriteStreamScope(db) == 0);
    /* Persist via Close. */
    assert(ucis_Close(db) == 0);

    ucisT db2 = ucis_Open(TMP_STREAM);
    assert(db2);
    assert(ucis_Close(db2) == 0);
}

static void test_file_handles(void)
{
    ucisT db = ucis_Open(NULL);
    assert(db);

    ucisFileHandleT fh1 = ucis_CreateFileHandle(db, "/path/to/foo.sv", NULL);
    assert(fh1);
    assert(strcmp(ucis_GetFileName(db, fh1), "/path/to/foo.sv") == 0);

    ucisFileHandleT fh2 = ucis_CreateFileHandle(db, "rel.sv", "/work");
    assert(fh2);
    assert(strcmp(ucis_GetFileName(db, fh2), "rel.sv") == 0);
    assert(fh1 != fh2);

    /* NULL inputs are errors, not crashes. */
    assert(ucis_CreateFileHandle(db, NULL, NULL) == NULL);
    assert(ucis_GetFileName(db, NULL) == NULL);

    assert(ucis_Close(db) == 0);
}

static void test_src_file_handle(void)
{
    ucisT db = ucis_Open(NULL);
    assert(db);
    ncdbT core = ucis_internal_get_ncdb(db);

    /* Need a DU scope to attach files to (D1 per-DU file tables).
     * Phase 2.3 wraps this in ucis_CreateScope; for now use the impl helper. */
    ncdbScopeT du = ncdb_impl_create_scope(core, NULL, NCDB_SCOPE_DU_MODULE, "top");
    assert(du);

    ucisFileHandleT a = ucis_CreateSrcFileHandle(db, (ucisScopeT)du, "src/a.sv", NULL);
    ucisFileHandleT b = ucis_CreateSrcFileHandle(db, (ucisScopeT)du, "src/b.sv", NULL);
    assert(a && b && a != b);

    /* The DU's per-DU file table should now hold two entries in insertion order. */
    assert(ncdb_DuFileCount(core, du) == 2);
    assert(strcmp(ncdb_DuGetFile(core, du, 0), "src/a.sv") == 0);
    assert(strcmp(ncdb_DuGetFile(core, du, 1), "src/b.sv") == 0);

    assert(strcmp(ucis_GetFileName(db, a), "src/a.sv") == 0);

    assert(ucis_Close(db) == 0);
}

int main(void)
{
    cleanup();
    test_inmem_roundtrip();
    test_writestream();
    test_file_handles();
    test_src_file_handle();
    cleanup();
    printf("ucis lifecycle: open/close/write + write-stream + file-handles OK\n");
    return 0;
}
