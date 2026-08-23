/* T-2.1 - uw_tables: 1-based file ids, path dedup, workdir resolution,
 * seal-after-first-instance.
 * SPDX-License-Identifier: Apache-2.0 */

#include "uw_tables.h"
#include "uw_test_sink.h"

#include <assert.h>
#include <stdio.h>

static uw_memsink_t    g_m;
static ucisWriterSinkT g_sink;

static ucisT open_db(void)
{
    uw_memsink_free(&g_m);
    uw_memsink_init(&g_m, &g_sink);
    return ucis_writer_OpenSinkStream(&g_sink);
}

static void test_ids_are_one_based(void)
{
    ucisT      db = open_db();
    uw_file_t* a  = (uw_file_t*)ucis_CreateFileHandle(db, "a.sv", NULL);
    uw_file_t* b  = (uw_file_t*)ucis_CreateFileHandle(db, "b.sv", NULL);

    /* SOURCE_FILE/@id is xsd:positiveInteger, so there is no id 0. */
    assert(a != NULL && a->id == 1);
    assert(b != NULL && b->id == 2);
    assert(uw_filetab_by_id(&((uw_db_t*)db)->files, 1) == a);
    assert(uw_filetab_by_id(&((uw_db_t*)db)->files, 0) == NULL);
    assert(uw_filetab_by_id(&((uw_db_t*)db)->files, 3) == NULL);
    ucis_Close(db);
}

static void test_dedup(void)
{
    ucisT      db = open_db();
    uw_file_t* a1 = (uw_file_t*)ucis_CreateFileHandle(db, "a.sv", NULL);
    uw_file_t* a2 = (uw_file_t*)ucis_CreateFileHandle(db, "a.sv", NULL);

    /* Simulators hand the same path over and over, once per scope. Interning
     * is what keeps the resident table proportional to file count. */
    assert(a1 == a2);
    assert(((uw_db_t*)db)->files.count == 1);
    ucis_Close(db);
}

static void test_workdir_resolution(void)
{
    ucisT      db = open_db();
    uw_file_t* rel  = (uw_file_t*)ucis_CreateFileHandle(db, "rtl/a.sv", "/w/proj");
    uw_file_t* rel2 = (uw_file_t*)ucis_CreateFileHandle(db, "rtl/a.sv", "/w/proj/");
    uw_file_t* abs  = (uw_file_t*)ucis_CreateFileHandle(db, "/abs/b.sv", "/w/proj");

    assert(strcmp(ucis_GetFileName(db, rel), "/w/proj/rtl/a.sv") == 0);
    assert(rel == rel2);                       /* trailing slash is not a new path */
    assert(strcmp(ucis_GetFileName(db, abs), "/abs/b.sv") == 0);
    ucis_Close(db);
}

static void test_many_files_rehash(void)
{
    ucisT db = open_db();
    char  name[32];
    int   i;

    /* Force several rehashes and confirm identity and ids survive them. */
    for (i = 0; i < 500; ++i) {
        snprintf(name, sizeof(name), "src/f%d.sv", i);
        assert(((uw_file_t*)ucis_CreateFileHandle(db, name, NULL))->id ==
               (uint32_t)(i + 1));
    }
    for (i = 0; i < 500; ++i) {
        snprintf(name, sizeof(name), "src/f%d.sv", i);
        assert(((uw_file_t*)ucis_CreateFileHandle(db, name, NULL))->id ==
               (uint32_t)(i + 1));
    }
    assert(((uw_db_t*)db)->files.count == 500);
    ucis_Close(db);
}

static void test_seal_after_first_instance(void)
{
    ucisT db = open_db();
    ucis_CreateFileHandle(db, "a.sv", NULL);
    ucis_CreateInstanceByName(db, NULL, "top", NULL, 1, UCIS_VLOG,
                              UCIS_INSTANCE, "work.top", 0);

    /* The file table has already been written out. A new path arriving now
     * cannot be given an id, and silently dropping it would leave dangling
     * references in every statement that used it. */
    assert(ucis_CreateFileHandle(db, "late.sv", NULL) == NULL);
    assert(ucis_writer_error(db) == UCIS_WRITER_ERR_SEALED);
    assert(strstr(ucis_writer_error_string(db), "late.sv") != NULL);

    ucis_Close(db);
}

static void test_seal_allows_known_paths(void)
{
    ucisT db = open_db();
    ucisFileHandleT a = ucis_CreateFileHandle(db, "a.sv", NULL);
    ucis_CreateInstanceByName(db, NULL, "top", NULL, 1, UCIS_VLOG,
                              UCIS_INSTANCE, "work.top", 0);

    /* Looking a path up again after sealing is fine -- it is how a streaming
     * caller resolves a filehandle per coveritem. */
    assert(ucis_CreateFileHandle(db, "a.sv", NULL) == a);
    assert(ucis_writer_error(db) == UCIS_WRITER_OK);
    ucis_Close(db);
}

static void test_history_sealed_too(void)
{
    ucisT db = open_db();
    ucis_CreateInstanceByName(db, NULL, "top", NULL, 1, UCIS_VLOG,
                              UCIS_INSTANCE, "work.top", 0);
    assert(ucis_CreateHistoryNode(db, NULL, (char*)"t", NULL,
                                  UCIS_HISTORYNODE_TEST) == NULL);
    assert(ucis_writer_error(db) == UCIS_WRITER_ERR_SEALED);
    ucis_Close(db);
}

int main(void)
{
    test_ids_are_one_based();
    test_dedup();
    test_workdir_resolution();
    test_many_files_rehash();
    test_seal_after_first_instance();
    test_seal_allows_known_paths();
    test_history_sealed_too();
    uw_memsink_free(&g_m);
    printf("test_uw_tables: ok\n");
    return 0;
}
