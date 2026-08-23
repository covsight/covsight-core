/* T-4.1 - functional coverage: covergroups, coverpoints, crosses.
 * SPDX-License-Identifier: Apache-2.0
 *
 * The schema rules being defended here are the ones a caller cannot see:
 * <options> is required and must come first, cgId must follow it, every
 * coverpoint precedes every cross, and a crossBin must carry one index per
 * crossed coverpoint. Each is asserted on the bytes, because "the call
 * returned 0" says nothing about whether the document is loadable. */

#include "ucis_writer.h"
#include "uw_test_sink.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uw_memsink_t    g_m;
static ucisWriterSinkT g_sink;

/* The source-file table seals when the first instance is created, so any file
 * handle a test needs has to be interned before that. */
static ucisFileHandleT g_file;

static ucisT open_db(void)
{
    ucisT db;
    uw_memsink_free(&g_m);
    uw_memsink_init(&g_m, &g_sink);
    db = ucis_writer_OpenSinkStream(&g_sink);
    ucis_writer_set_written_time(db, "2026-07-27T00:00:00Z");
    g_file = ucis_CreateFileHandle(db, "tb/cg.sv", NULL);
    ucis_CreateHistoryNode(db, NULL, (char*)"t", NULL, UCIS_HISTORYNODE_TEST);
    ucis_CreateInstanceByName(db, NULL, "top", NULL, 1, UCIS_SV,
                              UCIS_INSTANCE, (char*)"work.top", 0);
    return db;
}

static void bin(ucisT db, ucisCoverTypeT type, const char* name, unsigned count)
{
    ucisCoverDataT data;
    memset(&data, 0, sizeof(data));
    data.type       = type;
    data.flags      = UCIS_IS_32BIT;
    data.data.int32 = count;
    ucis_CreateNextCover(db, NULL, name, &data, NULL);
}

/* Where in the output `needle` starts, or (size_t)-1. */
static size_t at(const char* needle)
{
    const char* p = strstr(g_m.data, needle);
    return p ? (size_t)(p - g_m.data) : (size_t)-1;
}

static size_t count_of(const char* needle)
{
    const char* p = g_m.data;
    size_t      n = 0;
    size_t      len = strlen(needle);
    while ((p = strstr(p, needle)) != NULL) {
        n++;
        p += len;
    }
    return n;
}

/* A covergroup type scope emits nothing of its own; its name and location
 * reappear inside each cgInstance's cgId. */
static void test_type_scope_is_virtual(void)
{
    ucisT           db = open_db();
    ucisSourceInfoT si;

    si.filehandle = g_file;
    si.line       = 42;
    si.token      = 0;
    ucis_CreateScope(db, NULL, "cg_a", &si, 1, UCIS_SV, UCIS_COVERGROUP, 0);
    si.line = 99;
    ucis_CreateScope(db, NULL, "inst0", &si, 1, UCIS_SV, UCIS_COVERINSTANCE, 0);
    ucis_CreateScope(db, NULL, "cp", NULL, 1, UCIS_SV, UCIS_COVERPOINT, 0);
    bin(db, UCIS_CVGBIN, "0", 7);
    ucis_WriteStreamScope(db);   /* coverpoint */
    ucis_WriteStreamScope(db);   /* cgInstance */
    ucis_WriteStreamScope(db);   /* covergroup type */
    ucis_WriteStreamScope(db);   /* instance */
    assert(ucis_Close(db) == 0);

    /* No element is named after the type ... */
    assert(at("<covergroup ") == (size_t)-1);
    assert(at("<cg_a") == (size_t)-1);
    /* ... but its identity and both source locations survive in the cgId. */
    assert(at("cgName=\"cg_a\"") != (size_t)-1);
    assert(at("<cgId cgName=\"cg_a\" moduleName=\"work.top\">") != (size_t)-1);
    assert(at("<cginstSourceId file=\"1\" line=\"99\" inlineCount=\"1\"/>")
           != (size_t)-1);
    assert(at("<cgSourceId file=\"1\" line=\"42\" inlineCount=\"1\"/>")
           != (size_t)-1);
}

/* CGINSTANCE, COVERPOINT and CROSS all require <options> first, whether or not
 * the caller set any option on them. */
static void test_options_is_always_first(void)
{
    ucisT db = open_db();

    ucis_CreateScope(db, NULL, "cg_a", NULL, 1, UCIS_SV, UCIS_COVERGROUP, 0);
    ucis_CreateScope(db, NULL, "cp", NULL, 1, UCIS_SV, UCIS_COVERPOINT, 0);
    bin(db, UCIS_CVGBIN, "0", 1);
    ucis_WriteStreamScope(db);
    ucis_WriteStreamScope(db);
    ucis_WriteStreamScope(db);
    assert(ucis_Close(db) == 0);

    /* One for the cgInstance, one for the coverpoint, neither with a single
     * attribute the caller did not ask for. */
    assert(count_of("<options/>") == 2);
    assert(at("<cgInstance") < at("<options/>"));
    assert(at("<options/>") < at("<cgId"));
    assert(at("<coverpoint") < at("<coverpointBin"));
}

/* Options arrive as properties after the scope exists, so they have to be held
 * until the start tag closes -- and they must land on the right option type. */
static void test_options_attributes(void)
{
    ucisT      db = open_db();
    ucisScopeT cgi;
    ucisScopeT cvp;

    ucis_CreateScope(db, NULL, "cg_a", NULL, 1, UCIS_SV, UCIS_COVERGROUP, 0);
    cgi = ucis_CreateScope(db, NULL, "i0", NULL, 1, UCIS_SV,
                           UCIS_COVERINSTANCE, 0);
    assert(ucis_SetIntProperty(db, cgi, -1, UCIS_INT_CVG_PERINSTANCE, 1) == 0);
    assert(ucis_SetIntProperty(db, cgi, -1, UCIS_INT_SCOPE_WEIGHT, 3) == 0);

    cvp = ucis_CreateScope(db, NULL, "cp", NULL, 1, UCIS_SV, UCIS_COVERPOINT, 0);
    assert(ucis_SetIntProperty(db, cvp, -1, UCIS_INT_CVG_AUTOBINMAX, 8) == 0);
    /* per_instance is a CGINST_OPTIONS attribute only; on a coverpoint it has
     * nowhere legal to go and must be reported, not quietly emitted. */
    assert(ucis_SetIntProperty(db, cvp, -1, UCIS_INT_CVG_PERINSTANCE, 1) != 0);
    bin(db, UCIS_CVGBIN, "0", 1);
    ucis_WriteStreamScope(db);
    ucis_WriteStreamScope(db);
    ucis_WriteStreamScope(db);
    ucis_WriteStreamScope(db);
    ucis_Close(db);

    assert(at("<options weight=\"3\" per_instance=\"true\"/>") != (size_t)-1);
    assert(at("<options auto_bin_max=\"8\"/>") != (size_t)-1);
    /* The rejected property left no trace on the coverpoint. */
    assert(at("<options auto_bin_max=\"8\" per_instance") == (size_t)-1);
}

/* A covergroup whose coverpoints hang off the type scope directly -- type-only
 * coverage -- gets one cgInstance synthesised, and ucis_WriteStreamScope must
 * close through it without the caller knowing it exists. */
static void test_cginstance_is_synthesised(void)
{
    ucisT db = open_db();

    ucis_CreateScope(db, NULL, "cg_a", NULL, 1, UCIS_SV, UCIS_COVERGROUP, 0);
    ucis_CreateScope(db, NULL, "cp0", NULL, 1, UCIS_SV, UCIS_COVERPOINT, 0);
    bin(db, UCIS_CVGBIN, "0", 1);
    ucis_WriteStreamScope(db);
    ucis_CreateScope(db, NULL, "cp1", NULL, 1, UCIS_SV, UCIS_COVERPOINT, 0);
    bin(db, UCIS_CVGBIN, "0", 1);
    ucis_WriteStreamScope(db);
    ucis_WriteStreamScope(db);   /* covergroup type */
    ucis_WriteStreamScope(db);   /* instance */
    assert(ucis_Close(db) == 0);

    /* One instance named after the type, holding both coverpoints. */
    assert(count_of("<cgInstance") == 1);
    assert(at("<cgInstance name=\"cg_a\" key=\"cg_a\">") != (size_t)-1);
    assert(count_of("<coverpoint ") == 2);
    assert(at("</cgInstance>") > at("name=\"cp1\""));
}

/* CGINSTANCE's sequence puts coverpoint* before cross*. Going back is not a
 * style question: no conforming reader accepts the result. */
static void test_coverpoint_after_cross_is_an_error(void)
{
    ucisT db = open_db();

    ucis_CreateScope(db, NULL, "cg_a", NULL, 1, UCIS_SV, UCIS_COVERGROUP, 0);
    ucis_CreateScope(db, NULL, "cp", NULL, 1, UCIS_SV, UCIS_COVERPOINT, 0);
    bin(db, UCIS_CVGBIN, "0", 1);
    ucis_WriteStreamScope(db);
    ucis_CreateScope(db, NULL, "x", NULL, 1, UCIS_SV, UCIS_CROSS, 0);
    bin(db, UCIS_CVGBIN, "0", 1);
    ucis_WriteStreamScope(db);

    assert(ucis_CreateScope(db, NULL, "late", NULL, 1, UCIS_SV,
                            UCIS_COVERPOINT, 0) == NULL);
    assert(ucis_writer_error(db) == UCIS_WRITER_ERR_ORDER);

    ucis_WriteStreamScope(db);
    ucis_WriteStreamScope(db);
    assert(ucis_Close(db) != 0);

    /* Rejected means absent. A document that reported the error and then
     * emitted the element anyway would be the worst of both. */
    assert(at("name=\"late\"") == (size_t)-1);
    assert(at("<cross ") != (size_t)-1);
}

/* A crossBin carries one index per crossed coverpoint, recovered from the
 * components of its name. */
static void test_cross_indices(void)
{
    ucisT      db = open_db();
    ucisScopeT cross;

    ucis_CreateScope(db, NULL, "cg_a", NULL, 1, UCIS_SV, UCIS_COVERGROUP, 0);

    ucis_CreateScope(db, NULL, "cp_a", NULL, 1, UCIS_SV, UCIS_COVERPOINT, 0);
    bin(db, UCIS_CVGBIN, "lo", 1);
    bin(db, UCIS_CVGBIN, "hi", 2);
    ucis_WriteStreamScope(db);

    ucis_CreateScope(db, NULL, "cp_b", NULL, 1, UCIS_SV, UCIS_COVERPOINT, 0);
    bin(db, UCIS_CVGBIN, "red", 3);
    bin(db, UCIS_CVGBIN, "green", 4);
    bin(db, UCIS_CVGBIN, "blue", 5);
    ucis_WriteStreamScope(db);

    cross = ucis_CreateScope(db, NULL, "x", NULL, 1, UCIS_SV, UCIS_CROSS, 0);
    assert(ucis_SetStringProperty(db, cross, -1,
                                  UCIS_STR_ITH_CROSSED_CVP_NAME, "cp_a") == 0);
    assert(ucis_SetStringProperty(db, cross, -1,
                                  UCIS_STR_ITH_CROSSED_CVP_NAME, "cp_b") == 0);
    bin(db, UCIS_CVGBIN, "hi,blue", 9);
    bin(db, UCIS_CVGBIN, "lo,red", 8);
    /* A component that names no bin of its coverpoint: the position it refers
     * to is genuinely unknown, and -1 says so rather than pointing somewhere
     * plausible and wrong. */
    bin(db, UCIS_CVGBIN, "hi,mauve", 0);
    ucis_WriteStreamScope(db);
    ucis_WriteStreamScope(db);
    ucis_WriteStreamScope(db);
    ucis_Close(db);

    assert(at("<crossExpr>cp_a</crossExpr><crossExpr>cp_b</crossExpr>")
           != (size_t)-1);
    assert(at("name=\"hi,blue\" key=\"hi,blue\" type=\"bins\">"
              "<index>1</index><index>2</index>") != (size_t)-1);
    assert(at("name=\"lo,red\" key=\"lo,red\" type=\"bins\">"
              "<index>0</index><index>0</index>") != (size_t)-1);
    assert(at("name=\"hi,mauve\" key=\"hi,mauve\" type=\"bins\">"
              "<index>1</index><index>-1</index>") != (size_t)-1);
}

/* Bin names carry their values in the forms tools produce; when they do not,
 * the ordinal stands in and the caller is told. */
static void test_bin_ranges(void)
{
    ucisT         db = open_db();
    unsigned long warnings;

    ucis_CreateScope(db, NULL, "cg_a", NULL, 1, UCIS_SV, UCIS_COVERGROUP, 0);
    ucis_CreateScope(db, NULL, "cp", NULL, 1, UCIS_SV, UCIS_COVERPOINT, 0);
    bin(db, UCIS_CVGBIN, "7", 1);
    bin(db, UCIS_CVGBIN, "auto[9]", 1);
    bin(db, UCIS_CVGBIN, "auto[3:9]", 1);
    bin(db, UCIS_CVGBIN, "-4", 1);
    bin(db, UCIS_ILLEGALBIN, "small", 0);
    ucis_WriteStreamScope(db);
    ucis_WriteStreamScope(db);
    warnings = ucis_writer_warnings(db);
    ucis_WriteStreamScope(db);
    ucis_Close(db);

    assert(at("name=\"7\" key=\"7\" type=\"bins\"><range from=\"7\" to=\"7\"")
           != (size_t)-1);
    assert(at("name=\"auto[9]\" key=\"auto[9]\" type=\"bins\">"
              "<range from=\"9\" to=\"9\"") != (size_t)-1);
    assert(at("name=\"auto[3:9]\" key=\"auto[3:9]\" type=\"bins\">"
              "<range from=\"3\" to=\"9\"") != (size_t)-1);
    assert(at("name=\"-4\" key=\"-4\" type=\"bins\">"
              "<range from=\"-4\" to=\"-4\"") != (size_t)-1);
    /* The unparseable one keeps its name and falls back to its ordinal. */
    assert(at("name=\"small\" key=\"small\" type=\"illegal\">"
              "<range from=\"4\" to=\"4\"") != (size_t)-1);
    assert(at("nameComponent=\"small\"") != (size_t)-1);
    /* Exactly one substitution, and the caller can see it happened. */
    assert(warnings == 1);
}

/* A bin can only be emitted where the schema has somewhere to put it. */
static void test_bin_outside_a_coverpoint(void)
{
    ucisT db = open_db();

    ucis_CreateScope(db, NULL, "cg_a", NULL, 1, UCIS_SV, UCIS_COVERGROUP, 0);
    bin(db, UCIS_CVGBIN, "orphan", 1);
    assert(ucis_writer_error(db) == UCIS_WRITER_ERR_STATE);
    ucis_WriteStreamScope(db);
    ucis_WriteStreamScope(db);
    ucis_Close(db);

    assert(at("orphan") == (size_t)-1);
}

int main(void)
{
    test_type_scope_is_virtual();
    test_options_is_always_first();
    test_options_attributes();
    test_cginstance_is_synthesised();
    test_coverpoint_after_cross_is_an_error();
    test_cross_indices();
    test_bin_ranges();
    test_bin_outside_a_coverpoint();
    uw_memsink_free(&g_m);
    printf("ok\n");
    return 0;
}
