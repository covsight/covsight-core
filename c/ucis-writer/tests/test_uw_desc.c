/* T-D15.1 - the descriptor API and the UCIS API are one implementation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * D15 keeps both entry points over one set of emitters. The risk that creates
 * is the same one D1 guards for the amalgamated header: two front doors that
 * drift until they describe the same coverage differently.
 *
 * So the check is byte equality. The same logical coverage, built once through
 * each API, must produce identical markup -- not merely both-valid markup,
 * which is a much weaker claim and is what schema validation already gives us.
 *
 * Only the instanceCoverages section is compared: the document frame carries
 * timestamps and vendor identity that the two paths legitimately populate from
 * different arguments, and that is not what this test is about. */

#include "ucis_writer.h"
#include "uw_test_sink.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The coverage both paths build. Chosen to exercise a scope that nests
 * (covergroup -> cgInstance -> coverpoint), a deferred required child
 * (<options>, <cgId>), and a plain leaf (statement). */
#define CG_NAME   "cg_bus"
#define CGI_NAME  "cg_bus_i0"
#define CVP_NAME  "cp_len"

/* strdup is POSIX, not C99, and this suite builds at -std=c99 with extensions
 * off -- the same footing a vendoring consumer is on. */
static char* body_of(const char* doc)
{
    const char* start = strstr(doc, "<instanceCoverages");
    size_t      n;
    char*       out;

    assert(start != NULL);
    n = strlen(start);
    out = (char*)malloc(n + 1);
    assert(out != NULL);
    memcpy(out, start, n + 1);
    return out;
}

static char* build_via_ucis(void)
{
    uw_memsink_t    m;
    ucisWriterSinkT sink;
    ucisT           db;
    ucisScopeT      cgi;
    ucisCoverDataT  data;
    ucisSourceInfoT si;
    ucisFileHandleT f;
    char*           body;
    int             i;

    uw_memsink_init(&m, &sink);
    db = ucis_writer_OpenSinkStream(&sink);
    ucis_writer_set_written_time(db, "2026-07-27T00:00:00Z");
    f = ucis_CreateFileHandle(db, "rtl/bus.sv", NULL);
    ucis_CreateHistoryNode(db, NULL, (char*)"t", NULL, UCIS_HISTORYNODE_TEST);
    ucis_CreateInstanceByName(db, NULL, "top.u_bus", NULL, 1, UCIS_OTHER,
                              UCIS_INSTANCE, (char*)"work.bus", UCIS_INST_ONCE);

    si.filehandle = f; si.line = 12; si.token = 0;
    memset(&data, 0, sizeof(data));
    data.type = UCIS_STMTBIN; data.flags = UCIS_IS_64BIT; data.data.int64 = 77;
    ucis_CreateNextCover(db, NULL, "#stmt#1#12#", &data, &si);

    si.line = 40;
    ucis_CreateScope(db, NULL, CG_NAME, &si, 1, UCIS_OTHER, UCIS_COVERGROUP, 0);
    si.line = 51;
    cgi = ucis_CreateScope(db, NULL, CGI_NAME, &si, 1, UCIS_OTHER,
                           UCIS_COVERINSTANCE, 0);
    ucis_SetIntProperty(db, cgi, -1, UCIS_INT_CVG_ATLEAST, 2);

    ucis_CreateScope(db, NULL, CVP_NAME, NULL, 1, UCIS_OTHER,
                     UCIS_COVERPOINT, 0);
    for (i = 0; i < 3; ++i) {
        char name[8];
        snprintf(name, sizeof(name), "%d", i);
        memset(&data, 0, sizeof(data));
        data.type = UCIS_CVGBIN; data.flags = UCIS_IS_64BIT;
        data.data.int64 = (uint64_t)(10 * i);
        ucis_CreateNextCover(db, NULL, name, &data, NULL);
    }
    ucis_WriteStreamScope(db);   /* coverpoint  */
    ucis_WriteStreamScope(db);   /* cgInstance  */
    ucis_WriteStreamScope(db);   /* covergroup  */
    ucis_WriteStreamScope(db);   /* instance    */

    assert(ucis_Close(db) == 0);
    body = body_of(m.data);
    uw_memsink_free(&m);
    return body;
}

static char* build_via_descriptors(void)
{
    uw_memsink_t    m;
    ucisWriterSinkT sink;
    uw_db_t*        db;
    uw_file_t*      f;
    char*           body;
    int             i;

    uw_test_desc_t        t   = uw_test_defaults();
    uw_instance_desc_t    ins = uw_instance_defaults();
    uw_statement_desc_t   st  = uw_statement_defaults();
    uw_covergroup_desc_t  cg  = uw_covergroup_defaults();
    uw_cginstance_desc_t  cgi = uw_cginstance_defaults();
    uw_coverpoint_desc_t  cvp = uw_coverpoint_defaults();

    uw_memsink_init(&m, &sink);
    db = uw_open_sink(&sink);
    uw_set_written_time(db, "2026-07-27T00:00:00Z");
    f = uw_file(db, "rtl/bus.sv", NULL);

    t.name = "t";
    uw_test(db, &t);

    ins.name = "top.u_bus";
    ins.du_name = "work.bus";
    uw_instance(db, &ins);

    st.name = "#stmt#1#12#";
    st.count = 77;
    st.src.file = f;
    st.src.line = 12;
    uw_statement(db, &st);

    cg.name = CG_NAME;
    cg.src.file = f;
    cg.src.line = 40;
    uw_covergroup(db, &cg);

    cgi.name = CGI_NAME;
    cgi.src.file = f;
    cgi.src.line = 51;
    cgi.at_least = 2;
    uw_cginstance(db, &cgi);

    cvp.name = CVP_NAME;
    uw_coverpoint(db, &cvp);
    for (i = 0; i < 3; ++i) {
        char          name[8];
        uw_bin_desc_t b = uw_bin_defaults();
        snprintf(name, sizeof(name), "%d", i);
        b.name  = name;
        b.count = (uint64_t)(10 * i);
        uw_bin(db, &b);
    }
    uw_end(db);   /* coverpoint  */
    uw_end(db);   /* cgInstance  */
    uw_end(db);   /* covergroup  */
    uw_end(db);   /* instance    */

    assert(uw_close(db) == 0);
    body = body_of(m.data);
    uw_memsink_free(&m);
    return body;
}

static void test_apis_agree_byte_for_byte(void)
{
    char* a = build_via_ucis();
    char* b = build_via_descriptors();

    if (strcmp(a, b) != 0) {
        size_t i = 0;
        while (a[i] != '\0' && a[i] == b[i]) {
            i++;
        }
        fprintf(stderr, "APIs diverge at byte %zu\n  ucis: %.90s\n  desc: %.90s\n",
                i, a + i, b + i);
        assert(0 && "descriptor and UCIS APIs produced different markup");
    }
    free(a);
    free(b);
}

/* Open a document with one instance, ready for coverage. */
static uw_db_t* open_with_instance(uw_memsink_t* m, ucisWriterSinkT* sink)
{
    uw_db_t*           db;
    uw_test_desc_t     t   = uw_test_defaults();
    uw_instance_desc_t ins = uw_instance_defaults();

    uw_memsink_init(m, sink);
    db = uw_open_sink(sink);
    uw_set_written_time(db, "2026-07-27T00:00:00Z");
    t.name = "t";      uw_test(db, &t);
    ins.name = "top";  uw_instance(db, &ins);
    return db;
}

/* The routing uw_bin does by scope is the descriptor API's own logic rather
 * than something inherited from the emitters, so it gets its own check. */
static void test_bin_routes_by_scope(void)
{
    uw_memsink_t     m;
    ucisWriterSinkT  sink;
    uw_db_t*         db = open_with_instance(&m, &sink);
    uw_toggle_desc_t tg = uw_toggle_defaults();
    uw_bin_desc_t    b  = uw_bin_defaults();

    tg.name = "clk"; tg.type = UW_TOGGLE_NET; tg.dir = UW_TOGGLE_IN;
    assert(uw_toggle(db, &tg) == 0);

    /* No bin type is supplied: the open scope is a toggle, so this is a
     * toggle bin, and there is no way for the caller to say otherwise. */
    b.name = "0->1"; b.count = 5;
    assert(uw_bin(db, &b) == 0);
    uw_end(db);
    uw_end(db);
    assert(uw_close(db) == 0);

    assert(strstr(m.data, "<toggle from=\"0\" to=\"1\">") != NULL);
    uw_memsink_free(&m);
}

/* A bin with no scope that can hold one is refused rather than guessed at.
 *
 * Kept in its own document because failures are sticky by design: once
 * anything has gone wrong, every later call reports -1 too, so a deliberate
 * error and a success assertion cannot share a database. */
static void test_bin_without_a_scope_is_refused(void)
{
    uw_memsink_t    m;
    ucisWriterSinkT sink;
    uw_db_t*        db = open_with_instance(&m, &sink);
    uw_bin_desc_t   b  = uw_bin_defaults();

    b.name = "orphan"; b.count = 1;
    assert(uw_bin(db, &b) != 0);
    assert(uw_error(db) == UCIS_WRITER_ERR_STATE);

    uw_end(db);
    assert(uw_close(db) != 0);          /* the latched error surfaces here */

    /* Refused means absent, not merely reported. */
    assert(strstr(m.data, "orphan") == NULL);
    uw_memsink_free(&m);
}

int main(void)
{
    test_apis_agree_byte_for_byte();
    test_bin_routes_by_scope();
    test_bin_without_a_scope_is_refused();
    printf("ok\n");
    return 0;
}
