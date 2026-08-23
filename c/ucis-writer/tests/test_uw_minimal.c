/* T-2.2 - the smallest legal UCIS-XML document, byte for byte.
 * SPDX-License-Identifier: Apache-2.0
 *
 * The schema makes sourceFiles, historyNodes and instanceCoverages all
 * minOccurs="1", so "smallest legal" means one of each -- there is no valid
 * empty document. This test pins the exact bytes; tests/ucis_writer/ validates
 * example output against the XSD. */

#include "ucis_writer.h"
#include "uw_fixture.h"

#include <assert.h>
#include <stdio.h>

static uw_memsink_t    g_m;
static ucisWriterSinkT g_sink;

static void test_bytes(void)
{
    ucisT db = uw_fixture_build(&g_m, &g_sink, 1);
    assert(ucis_Close(db) == 0);
    if (strcmp(g_m.data, UW_FIXTURE_WANT) != 0) {
        fprintf(stderr, "got:\n%s\n\nwant:\n%s\n", g_m.data, UW_FIXTURE_WANT);
        assert(0);
    }
}

static void test_unbalanced_is_reported_and_repaired(void)
{
    /* Forgetting ucis_WriteStreamScope is the most likely caller mistake, so
     * it has to be both reported and survivable: the same document comes out,
     * and ucis_Close says something was wrong. A truncated file helps nobody
     * find the missing call. */
    ucisT db = uw_fixture_build(&g_m, &g_sink, 0);
    assert(ucis_Close(db) == -1);
    assert(strcmp(g_m.data, UW_FIXTURE_WANT) == 0);
}

static void test_empty_document_is_still_valid(void)
{
    ucisT db;
    uw_memsink_free(&g_m);
    uw_memsink_init(&g_m, &g_sink);
    db = ucis_writer_OpenSinkStream(&g_sink);
    ucis_writer_set_written_time(db, "2026-07-26T12:00:00Z");

    /* A caller that recorded nothing still gets a document a reader accepts,
     * with the placeholders counted as warnings (decision D6). */
    assert(ucis_Close(db) == 0);
    assert(strstr(g_m.data, "<sourceFiles fileName=\"(unknown)\" id=\"1\"/>") != NULL);
    assert(strstr(g_m.data, "<historyNodes ") != NULL);
    assert(strstr(g_m.data, "<instanceCoverages ") != NULL);
    assert(strstr(g_m.data, "</UCIS>") != NULL);
}

static void test_pretty(void)
{
    ucisT db;
    uw_memsink_free(&g_m);
    uw_memsink_init(&g_m, &g_sink);
    db = ucis_writer_OpenSinkStream(&g_sink);
    ucis_writer_set_pretty(db, 1);
    ucis_writer_set_written_by(db, "test");
    ucis_writer_set_written_time(db, "2026-07-26T12:00:00Z");
    ucis_CreateFileHandle(db, "a.sv", NULL);
    ucis_CreateHistoryNode(db, NULL, (char*)"run1", NULL, UCIS_HISTORYNODE_TEST);
    ucis_CreateInstanceByName(db, NULL, "top", NULL, 1, UCIS_VLOG,
                              UCIS_INSTANCE, (char*)"work.top", 0);
    ucis_WriteStreamScope(db);
    assert(ucis_Close(db) == 0);
    assert(strstr(g_m.data, "\n  <sourceFiles ") != NULL);
    assert(strstr(g_m.data, "\n    <id file=") != NULL);
}

static void test_bad_handles_do_not_crash(void)
{
    /* ucisT is void*, so a caller can hand us anything. */
    int notadb = 0;
    assert(ucis_Close(NULL) == -1);
    assert(ucis_Close(&notadb) == -1);
    assert(ucis_writer_error(NULL) == UCIS_WRITER_ERR_USAGE);
    assert(ucis_writer_error_string(NULL) != NULL);
    assert(ucis_CreateFileHandle(NULL, "a.sv", NULL) == NULL);
    assert(ucis_WriteStream(NULL) == -1);
    assert(ucis_WriteStreamScope(NULL) == -1);
}

int main(void)
{
    test_bytes();
    test_unbalanced_is_reported_and_repaired();
    test_empty_document_is_still_valid();
    test_pretty();
    test_bad_handles_do_not_crash();
    uw_memsink_free(&g_m);
    printf("test_uw_minimal: ok\n");
    return 0;
}
