/* T-3.1 - the ordering contract, from the caller's side.
 * SPDX-License-Identifier: Apache-2.0
 *
 * D5 says grouping by (instance, kind) is the caller's job and that we detect
 * violations rather than buffering to repair them. These are the tests that
 * make "detect" mean something. Each case here is a document that would be
 * rejected by a conforming reader if we emitted it. */

#include "ucis_writer.h"
#include "uw_test_sink.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uw_memsink_t    g_m;
static ucisWriterSinkT g_sink;

static ucisT open_db(ucisFileHandleT* f)
{
    ucisT db;
    uw_memsink_free(&g_m);
    uw_memsink_init(&g_m, &g_sink);
    db = ucis_writer_OpenSinkStream(&g_sink);
    ucis_writer_set_written_time(db, "2026-07-27T00:00:00Z");
    *f = ucis_CreateFileHandle(db, "a.sv", NULL);
    ucis_CreateHistoryNode(db, NULL, (char*)"t", NULL, UCIS_HISTORYNODE_TEST);
    return db;
}

static void stmt(ucisT db, ucisFileHandleT f, int line, unsigned count)
{
    ucisCoverDataT  data;
    ucisSourceInfoT si;
    si.filehandle = f;
    si.line       = line;
    si.token      = 0;
    memset(&data, 0, sizeof(data));
    data.type       = UCIS_STMTBIN;
    data.flags      = UCIS_IS_32BIT;
    data.data.int32 = count;
    ucis_CreateNextCover(db, NULL, "#stmt#1#1#1#", &data, &si);
}

static void test_statements_stream(void)
{
    ucisFileHandleT f;
    ucisT           db = open_db(&f);

    ucis_CreateInstanceByName(db, NULL, "top", NULL, 1, UCIS_SV,
                              UCIS_INSTANCE, (char*)"work.top", 0);
    stmt(db, f, 10, 5);
    stmt(db, f, 11, 0);
    ucis_WriteStreamScope(db);
    assert(ucis_Close(db) == 0);

    /* One blockCoverage wrapper for both statements: the wrapper is opened by
     * the first item of a kind and stays open while that kind continues. */
    assert(strstr(g_m.data, "<blockCoverage>") != NULL);
    {
        const char* p = strstr(g_m.data, "<blockCoverage>");
        assert(strstr(p + 1, "<blockCoverage>") == NULL);
    }
    assert(strstr(g_m.data, "<statement alias=\"#stmt#1#1#1#\">"
                            "<id file=\"1\" line=\"10\" inlineCount=\"1\"/>"
                            "<bin><contents coverageCount=\"5\"/></bin>"
                            "</statement>") != NULL);
    assert(strstr(g_m.data, "coverageCount=\"0\"") != NULL);
}

static void test_new_instance_closes_the_previous_one(void)
{
    ucisFileHandleT f;
    ucisT           db = open_db(&f);

    ucis_CreateInstanceByName(db, NULL, "a", NULL, 1, UCIS_SV,
                              UCIS_INSTANCE, (char*)"work.a", 0);
    stmt(db, f, 1, 1);
    /* No ucis_WriteStreamScope: instanceCoverages is a flat list, so opening
     * the next instance has to close this one. */
    ucis_CreateInstanceByName(db, NULL, "b", NULL, 1, UCIS_SV,
                              UCIS_INSTANCE, (char*)"work.b", 0);
    stmt(db, f, 2, 2);
    ucis_WriteStreamScope(db);
    assert(ucis_Close(db) == 0);

    assert(strstr(g_m.data, "</instanceCoverages><instanceCoverages name=\"b\"") != NULL);
    assert(strstr(g_m.data, "instanceId=\"2\"") != NULL);
}

static void test_du_name_flows_into_module_name(void)
{
    ucisFileHandleT f;
    ucisT           db = open_db(&f);

    /* A design unit has no element in UCIS-XML; its name survives as the
     * @moduleName of instances created after it. */
    ucis_CreateScope(db, NULL, "work.alu", NULL, 1, UCIS_SV,
                     UCIS_DU_MODULE, UCIS_ENABLED_STMT);
    ucis_CreateScope(db, NULL, "top.u_alu", NULL, 1, UCIS_SV,
                     UCIS_INSTANCE, UCIS_INST_ONCE);
    stmt(db, f, 1, 1);
    ucis_WriteStreamScope(db);
    assert(ucis_Close(db) == 0);
    assert(strstr(g_m.data, "name=\"top.u_alu\" key=\"top.u_alu\""
                            " moduleName=\"work.alu\" instanceId=\"1\"") != NULL);
}

static void test_coverage_outside_an_instance_is_an_error(void)
{
    ucisFileHandleT f;
    ucisT           db = open_db(&f);

    stmt(db, f, 1, 1);            /* no instance has been created */
    assert(ucis_writer_error(db) == UCIS_WRITER_ERR_STATE);
    assert(strstr(ucis_writer_error_string(db), "outside any instance") != NULL);
    ucis_Close(db);
}

static void test_coveritem_properties_are_refused(void)
{
    ucisFileHandleT f;
    ucisT           db = open_db(&f);

    ucis_CreateInstanceByName(db, NULL, "top", NULL, 1, UCIS_SV,
                              UCIS_INSTANCE, (char*)"work.top", 0);
    stmt(db, f, 1, 1);
    /* D7: a coveritem is emitted the moment it is created, so there is no
     * later moment at which a property could still reach it. Saying so beats
     * accepting the call and dropping the value. */
    assert(ucis_SetIntProperty(db, (ucisObjT)db, 0,
                               UCIS_INT_COVER_WEIGHT, 3) == -1);
    assert(ucis_writer_error(db) == UCIS_WRITER_ERR_ORDER);
    ucis_Close(db);
}

static void test_scope_property_after_attributes_is_refused(void)
{
    ucisFileHandleT f;
    ucisT           db = open_db(&f);
    ucisScopeT      inst;

    inst = ucis_CreateInstanceByName(db, NULL, "top", NULL, 1, UCIS_SV,
                                     UCIS_INSTANCE, (char*)"work.top", 0);
    stmt(db, f, 1, 1);            /* forces the instance's start tag closed */
    assert(ucis_SetIntProperty(db, inst, -1, UCIS_INT_SCOPE_WEIGHT, 7) == -1);
    assert(ucis_writer_error(db) == UCIS_WRITER_ERR_ORDER);
    ucis_Close(db);
}

static void test_history_properties_apply_before_the_flush(void)
{
    ucisFileHandleT  f;
    ucisT            db;
    ucisHistoryNodeT h;

    uw_memsink_free(&g_m);
    uw_memsink_init(&g_m, &g_sink);
    db = ucis_writer_OpenSinkStream(&g_sink);
    ucis_writer_set_written_time(db, "2026-07-27T00:00:00Z");
    f = ucis_CreateFileHandle(db, "a.sv", NULL);
    h = ucis_CreateHistoryNode(db, NULL, (char*)"t", NULL, UCIS_HISTORYNODE_TEST);

    assert(ucis_SetStringProperty(db, h, -1, UCIS_STR_HIST_TOOLCATEGORY,
                                  "UCIS:Emulator") == 0);
    assert(ucis_SetStringProperty(db, NULL, -1, UCIS_STR_VER_VENDOR_TOOL,
                                  "my_sim") == 0);
    assert(ucis_SetRealProperty(db, h, -1, UCIS_REAL_HIST_CPUTIME, 1.5) == 0);
    assert(ucis_SetIntProperty(db, h, -1, UCIS_INT_TEST_STATUS,
                               UCIS_TESTSTATUS_ERROR) == 0);

    ucis_CreateInstanceByName(db, NULL, "top", NULL, 1, UCIS_SV,
                              UCIS_INSTANCE, (char*)"work.top", 0);
    stmt(db, f, 1, 1);

    /* The tables are gone now; a late edit has nowhere to land. */
    assert(ucis_SetStringProperty(db, h, -1, UCIS_STR_COMMENT, "late") == -1);
    assert(ucis_writer_error(db) == UCIS_WRITER_ERR_SEALED);

    ucis_WriteStreamScope(db);
    ucis_Close(db);
    assert(strstr(g_m.data, "toolCategory=\"UCIS:Emulator\"") != NULL);
    assert(strstr(g_m.data, "vendorTool=\"my_sim\"") != NULL);
    assert(strstr(g_m.data, "cpuTime=\"1.5\"") != NULL);
    assert(strstr(g_m.data, "testStatus=\"false\"") != NULL);
    assert(strstr(g_m.data, "comment=") == NULL);
}

int main(void)
{
    test_statements_stream();
    test_new_instance_closes_the_previous_one();
    test_du_name_flows_into_module_name();
    test_coverage_outside_an_instance_is_an_error();
    test_coveritem_properties_are_refused();
    test_scope_property_after_attributes_is_refused();
    test_history_properties_apply_before_the_flush();
    uw_memsink_free(&g_m);
    printf("test_uw_order: ok\n");
    return 0;
}
