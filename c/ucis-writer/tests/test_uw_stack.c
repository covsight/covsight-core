/* T-1.5 - uw_stack: nesting, empty-element collapse, deferred <id>, depth
 * limit, and the ordering stage machine.
 * SPDX-License-Identifier: Apache-2.0 */

#include "uw_stack.h"
#include "uw_test_sink.h"

#include <assert.h>
#include <stdio.h>

static uw_memsink_t    g_m;
static ucisWriterSinkT g_sink;

static uw_db_t* open_db(void)
{
    uw_memsink_free(&g_m);
    uw_memsink_init(&g_m, &g_sink);
    return (uw_db_t*)ucis_writer_OpenSinkStream(&g_sink);
}

/* The root <UCIS> element is already open at depth 1; drop its start tag from
 * the comparison so each test reads as just the elements it wrote. */
static const char* body(uw_db_t* db)
{
    const char* p;
    uw_buf_flush(&db->buf);
    p = g_m.data ? g_m.data : "";
    return (strncmp(p, "<UCIS", 5) == 0) ? p + 5 : p;
}

static void expect(uw_db_t* db, const char* want)
{
    const char* got = body(db);
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "got \"%s\", want \"%s\"\n", got, want);
        assert(0);
    }
}

static void test_nesting(void)
{
    uw_db_t* db = open_db();
    assert(db->depth == 1);

    uw_el_begin(db, "a", 0);
    uw_el_begin(db, "b", 0);
    uw_el_end(db);
    uw_el_end(db);
    assert(db->depth == 1);
    /* `b` never acquired children, so it collapses; `a` did, so it does not. */
    expect(db, "><a><b/></a>");
    ucis_Close(db);
}

static void test_attributes_stay_open(void)
{
    uw_db_t* db = open_db();
    uw_el_begin(db, "a", 0);
    /* An element accepts attributes until something forces the start tag
     * closed. That is what lets ucis_SetIntProperty work after the create
     * call without the library buffering the object. */
    uw_buf_write(&db->buf, " k=\"1\"", 6);
    uw_el_begin(db, "b", 0);
    uw_buf_write(&db->buf, " k=\"2\"", 6);
    uw_el_end(db);
    uw_el_end(db);
    expect(db, "><a k=\"1\"><b k=\"2\"/></a>");
    ucis_Close(db);
}

static void test_deferred_id(void)
{
    uw_db_t* db = open_db();

    uw_el_begin(db, "instanceCoverages", 0);
    uw_buf_write(&db->buf, " name=\"t\"", 9);
    uw_el_set_id(db, 3, 17, 1, 1);
    /* The <id> child must appear before any other child even though it was
     * requested while the start tag was still accepting attributes. */
    uw_el_begin(db, "blockCoverage", 0);
    uw_el_end(db);
    uw_el_end(db);
    expect(db,
           "><instanceCoverages name=\"t\">"
           "<id file=\"3\" line=\"17\" inlineCount=\"1\"/>"
           "<blockCoverage/></instanceCoverages>");
    ucis_Close(db);
}

static void test_deferred_id_on_empty_scope(void)
{
    uw_db_t* db = open_db();

    uw_el_begin(db, "instanceCoverages", 0);
    uw_el_set_id(db, 1, 1, 1, 1);
    uw_el_end(db);   /* required child, so no collapse to <.../> */
    expect(db,
           "><instanceCoverages>"
           "<id file=\"1\" line=\"1\" inlineCount=\"1\"/>"
           "</instanceCoverages>");
    ucis_Close(db);
}

static void test_id_clamping(void)
{
    uw_db_t* db = open_db();

    uw_el_begin(db, "s", 0);
    uw_el_set_id(db, 0, 0, 0, 1);      /* xsd:positiveInteger: 0 is invalid */
    uw_el_end(db);
    expect(db, "><s><id file=\"1\" line=\"1\" inlineCount=\"1\"/></s>");
    assert(db->warnings == 1);
    ucis_Close(db);

    db = open_db();
    uw_el_begin(db, "s", 0);
    uw_el_set_id(db, 0, 0, 0, 0);      /* clamp without a warning */
    uw_el_end(db);
    assert(db->warnings == 0);
    ucis_Close(db);
}

static void test_unwind(void)
{
    uw_db_t* db = open_db();
    uw_el_begin(db, "a", 0);
    uw_el_begin(db, "b", 0);
    uw_el_begin(db, "c", 0);
    uw_el_unwind(db, 1);
    assert(db->depth == 1);
    expect(db, "><a><b><c/></b></a>");
    ucis_Close(db);
}

static void test_overpop(void)
{
    uw_db_t* db = open_db();
    uw_el_unwind(db, 0);                       /* closes the root */
    assert(uw_el_end(db) == UCIS_WRITER_ERR_UNBALANCED);
    /* Recorded on the database, but the output buffer is untouched: a caller
     * mistake must not discard a document that is otherwise fine. */
    assert(ucis_writer_error(db) == UCIS_WRITER_ERR_UNBALANCED);
    assert(db->buf.status == UCIS_WRITER_OK);
    ucis_Close(db);
}

static void test_depth_limit(void)
{
    uw_db_t* db = open_db();
    int      i;
    for (i = db->depth; i < UCIS_WRITER_MAX_DEPTH; ++i) {
        assert(uw_el_begin(db, "n", 0) == UCIS_WRITER_OK);
    }
    assert(uw_el_begin(db, "n", 0) == UCIS_WRITER_ERR_DEPTH);
    assert(ucis_writer_error(db) == UCIS_WRITER_ERR_DEPTH);
    ucis_Close(db);
}

static void test_stage_machine(void)
{
    uw_db_t* db = open_db();

    uw_el_begin(db, "instanceCoverages", 0);
    assert(uw_stage(db, 1, "toggleCoverage") == UCIS_WRITER_OK);
    assert(uw_stage(db, 1, "toggleCoverage") == UCIS_WRITER_OK); /* repeats ok */
    assert(uw_stage(db, 3, "branchCoverage") == UCIS_WRITER_OK);
    /* Going backwards would produce a document that violates the XSD's
     * xsd:sequence, so it is an error rather than a reordering. */
    assert(uw_stage(db, 2, "conditionCoverage") == UCIS_WRITER_ERR_ORDER);
    assert(strstr(ucis_writer_error_string(db), "conditionCoverage") != NULL);
    ucis_Close(db);
}

int main(void)
{
    test_nesting();
    test_attributes_stay_open();
    test_deferred_id();
    test_deferred_id_on_empty_scope();
    test_id_clamping();
    test_unwind();
    test_overpop();
    test_depth_limit();
    test_stage_machine();
    uw_memsink_free(&g_m);
    printf("test_uw_stack: ok\n");
    return 0;
}
