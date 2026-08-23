/* T-3.6 - a Verilator-shaped document: toggle + statement + branch in one
 * instance, and what happens when the kinds arrive out of order.
 * SPDX-License-Identifier: Apache-2.0 */

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
    *f = ucis_CreateFileHandle(db, "rtl/a.sv", NULL);
    ucis_CreateHistoryNode(db, NULL, (char*)"t", NULL, UCIS_HISTORYNODE_TEST);
    ucis_CreateInstanceByName(db, NULL, "top", NULL, 1, UCIS_SV,
                              UCIS_INSTANCE, (char*)"work.top", 0);
    return db;
}

static void cover(ucisT db, ucisCoverTypeT type, const char* name,
                  ucisFileHandleT f, int line, unsigned count)
{
    ucisCoverDataT  data;
    ucisSourceInfoT si;
    si.filehandle = f;
    si.line       = line;
    si.token      = 0;
    memset(&data, 0, sizeof(data));
    data.type       = type;
    data.flags      = UCIS_IS_32BIT;
    data.data.int32 = count;
    ucis_CreateNextCover(db, NULL, name, &data, f ? &si : NULL);
}

/* Where in the output `needle` starts, or (size_t)-1. */
static size_t at(const char* needle)
{
    const char* p = strstr(g_m.data, needle);
    return p ? (size_t)(p - g_m.data) : (size_t)-1;
}

static void test_three_kinds_in_schema_order(void)
{
    ucisFileHandleT f;
    ucisT           db = open_db(&f);
    ucisSourceInfoT bsi;
    unsigned long   warnings;

    /* INSTANCE_COVERAGE's xsd:sequence puts toggle before block before
     * branch. A caller arriving in that order is never told anything. */
    ucis_CreateToggle(db, NULL, "clk", NULL, 0, UCIS_TOGGLE_METRIC_2STOGGLE,
                      UCIS_TOGGLE_TYPE_NET, UCIS_TOGGLE_DIR_IN);
    cover(db, UCIS_TOGGLEBIN, "0->1", NULL, 0, 100);
    cover(db, UCIS_TOGGLEBIN, "1->0", NULL, 0, 99);
    ucis_WriteStreamScope(db);

    cover(db, UCIS_STMTBIN, "#stmt#1#10#1#", f, 10, 7);
    cover(db, UCIS_STMTBIN, "#stmt#1#11#1#", f, 11, 7);

    /* The branch point carries its own location. Leaving srcinfo NULL here
     * would cost a warning, and rightly so: BRANCH_STATEMENT requires an <id>
     * and there would be nothing to put in it. */
    bsi.filehandle = f;
    bsi.line       = 20;
    bsi.token      = 0;
    ucis_CreateScope(db, NULL, "#branch#1#20#1#", &bsi, 1, UCIS_SV,
                     UCIS_BRANCH, 0);
    cover(db, UCIS_BRANCHBIN, "if", f, 20, 5);
    cover(db, UCIS_BRANCHBIN, "else", f, 20, 2);
    ucis_WriteStreamScope(db);

    ucis_WriteStreamScope(db);
    warnings = ucis_writer_warnings(db);   /* before Close frees the handle */
    assert(ucis_Close(db) == 0);

    assert(at("<toggleCoverage>") < at("<blockCoverage>"));
    assert(at("<blockCoverage>") < at("<branchCoverage>"));

    /* Each kind opened exactly one wrapper. */
    assert(strstr(strstr(g_m.data, "<blockCoverage>") + 1,
                  "<blockCoverage>") == NULL);

    /* A branch arm's label has nowhere to live but the bin, because the
     * schema's BRANCH type has no attributes. */
    assert(strstr(g_m.data,
                  "<branchBin><contents nameComponent=\"if\""
                  " coverageCount=\"5\"/></branchBin>") != NULL);
    assert(strstr(g_m.data, "statementType=\"if\""
                            " alias=\"#branch#1#20#1#\"") != NULL);
    assert(warnings == 0);
}

static void test_kind_out_of_order_is_reported(void)
{
    ucisFileHandleT f;
    ucisT           db = open_db(&f);

    cover(db, UCIS_STMTBIN, "#stmt#1#10#1#", f, 10, 1);   /* blockCoverage */
    /* toggleCoverage sorts before blockCoverage, so it can no longer be
     * opened. Buffering to repair this would mean holding the instance. */
    ucis_CreateToggle(db, NULL, "clk", NULL, 0, UCIS_TOGGLE_METRIC_2STOGGLE,
                      UCIS_TOGGLE_TYPE_NET, UCIS_TOGGLE_DIR_IN);
    assert(ucis_writer_error(db) == UCIS_WRITER_ERR_ORDER);
    assert(strstr(ucis_writer_error_string(db), "toggleCoverage") != NULL);

    /* The document is still finished and still valid: a caller mistake must
     * not cost the data that was already correct (D9). */
    ucis_Close(db);
    assert(strstr(g_m.data, "</UCIS>") != NULL);
    assert(strstr(g_m.data, "coverageCount=\"1\"") != NULL);
    /* And the rejected construct really was dropped rather than emitted in
     * the wrong place -- which is the difference between a document a reader
     * accepts and one it does not. */
    assert(strstr(g_m.data, "toggleObject") == NULL);
}

static void test_kind_may_be_revisited_in_a_new_instance(void)
{
    ucisFileHandleT f;
    ucisT           db = open_db(&f);

    cover(db, UCIS_STMTBIN, "#stmt#1#1#1#", f, 1, 1);
    ucis_CreateInstanceByName(db, NULL, "top.b", NULL, 1, UCIS_SV,
                              UCIS_INSTANCE, (char*)"work.b", 0);
    /* The ordering rule is per instance; a new instance starts over. */
    ucis_CreateToggle(db, NULL, "clk", NULL, 0, UCIS_TOGGLE_METRIC_2STOGGLE,
                      UCIS_TOGGLE_TYPE_NET, UCIS_TOGGLE_DIR_IN);
    cover(db, UCIS_TOGGLEBIN, "0->1", NULL, 0, 3);
    ucis_WriteStreamScope(db);
    ucis_WriteStreamScope(db);
    assert(ucis_Close(db) == 0);
    assert(at("<blockCoverage>") < at("<toggleCoverage>"));
}

static void test_scalar_toggle_gets_a_synthetic_bit(void)
{
    ucisFileHandleT f;
    ucisT           db = open_db(&f);

    ucis_CreateToggle(db, NULL, "en", NULL, 0, UCIS_TOGGLE_METRIC_2STOGGLE,
                      UCIS_TOGGLE_TYPE_NET, UCIS_TOGGLE_DIR_IN);
    cover(db, UCIS_TOGGLEBIN, "0->1", NULL, 0, 4);
    /* One ucis_WriteStreamScope, even though the library opened a toggleBit
     * the caller never asked for. */
    assert(ucis_WriteStreamScope(db) == 0);
    ucis_WriteStreamScope(db);
    assert(ucis_Close(db) == 0);

    assert(strstr(g_m.data,
                  "<toggleObject name=\"en\" key=\"en\" type=\"net\""
                  " portDirection=\"in\">") != NULL);
    assert(strstr(g_m.data, "<toggleBit name=\"0\" key=\"0\">"
                            "<index>0</index>") != NULL);
    assert(strstr(g_m.data, "</toggleBit></toggleObject>") != NULL);
}

static void test_vector_toggle_nests_per_bit(void)
{
    ucisFileHandleT f;
    ucisT           db = open_db(&f);
    int             bit;

    ucis_CreateToggle(db, NULL, "d", "top.d", 0, UCIS_TOGGLE_METRIC_2STOGGLE,
                      UCIS_TOGGLE_TYPE_REG, UCIS_TOGGLE_DIR_OUT);
    for (bit = 0; bit < 2; ++bit) {
        char idx[4];
        idx[0] = (char)('0' + bit);
        idx[1] = '\0';
        ucis_CreateToggle(db, NULL, idx, NULL, 0, UCIS_TOGGLE_METRIC_2STOGGLE,
                          UCIS_TOGGLE_TYPE_REG, UCIS_TOGGLE_DIR_OUT);
        cover(db, UCIS_TOGGLEBIN, "0->1", NULL, 0, (unsigned)(bit + 1));
        ucis_WriteStreamScope(db);
    }
    ucis_WriteStreamScope(db);
    ucis_WriteStreamScope(db);
    assert(ucis_Close(db) == 0);

    /* @key takes the canonical name when one is given: it is what a merger
     * matches on across runs. */
    assert(strstr(g_m.data, "<toggleObject name=\"d\" key=\"top.d\"") != NULL);
    assert(strstr(g_m.data, "<toggleBit name=\"0\" key=\"0\"><index>0</index>") != NULL);
    assert(strstr(g_m.data, "<toggleBit name=\"1\" key=\"1\"><index>1</index>") != NULL);
}

static void test_enum_toggle_bins_are_arrivals(void)
{
    ucisFileHandleT f;
    ucisT           db = open_db(&f);

    ucis_CreateToggle(db, NULL, "st", NULL, 0, UCIS_TOGGLE_METRIC_ENUM,
                      UCIS_TOGGLE_TYPE_REG, UCIS_TOGGLE_DIR_INTERNAL);
    cover(db, UCIS_TOGGLEBIN, "IDLE", NULL, 0, 9);
    ucis_WriteStreamScope(db);
    ucis_WriteStreamScope(db);
    assert(ucis_Close(db) == 0);

    /* TOGGLE requires both @from and @to; an enum bin names only where the
     * variable arrived. The original name is kept in nameComponent. */
    assert(strstr(g_m.data, "<toggle from=\"\" to=\"IDLE\">") != NULL);
    assert(strstr(g_m.data, "nameComponent=\"IDLE\"") != NULL);
}

static void test_spaced_transition_names(void)
{
    ucisFileHandleT f;
    ucisT           db = open_db(&f);

    ucis_CreateToggle(db, NULL, "s", NULL, 0, UCIS_TOGGLE_METRIC_ZTOGGLE,
                      UCIS_TOGGLE_TYPE_NET, UCIS_TOGGLE_DIR_INOUT);
    /* The spec writes these both ways ("1->0" and "1 -> 0"). */
    cover(db, UCIS_TOGGLEBIN, "1 -> z", NULL, 0, 2);
    ucis_WriteStreamScope(db);
    ucis_WriteStreamScope(db);
    assert(ucis_Close(db) == 0);
    assert(strstr(g_m.data, "<toggle from=\"1\" to=\"z\">") != NULL);
}

static void test_expression_with_operand_list(void)
{
    ucisFileHandleT f;
    ucisT           db = open_db(&f);
    ucisScopeT      expr;
    ucisSourceInfoT si;

    si.filehandle = f;
    si.line       = 31;
    si.token      = 0;

    expr = ucis_CreateScope(db, NULL, "#cond#1#31#1#", &si, 1, UCIS_SV,
                            UCIS_COND, 0);
    assert(expr != NULL);
    assert(ucis_SetStringProperty(db, expr, -1, UCIS_STR_EXPR_TERMS,
                                  "enable#mode_is_2") == 0);
    /* The metric scope is a real UCIS scope with no element behind it. */
    ucis_CreateScope(db, NULL, "UCIS:FULL", NULL, 1, UCIS_SV, UCIS_COND, 0);
    cover(db, UCIS_CONDBIN, "00", NULL, 0, 41);
    cover(db, UCIS_CONDBIN, "11", NULL, 0, 0);
    ucis_WriteStreamScope(db);          /* metric */
    ucis_WriteStreamScope(db);          /* expression */
    ucis_WriteStreamScope(db);          /* instance */
    assert(ucis_Close(db) == 0);

    /* @exprString, @width and the subExpr children all come from the operand
     * list, which arrives as a property after the scope was created -- so all
     * of them are written when the start tag is finally terminated. */
    assert(strstr(g_m.data,
                  "<expr name=\"#cond#1#31#1#\" key=\"#cond#1#31#1#\""
                  " exprString=\"enable#mode_is_2\" index=\"1\" width=\"2\">"
                  "<id file=\"1\" line=\"31\" inlineCount=\"1\"/>"
                  "<subExpr>enable</subExpr><subExpr>mode_is_2</subExpr>") != NULL);
    /* The metric level survives on the bins, since EXPR has no element for it. */
    assert(strstr(g_m.data, "nameComponent=\"00\" typeComponent=\"UCIS:FULL\""
                            " coverageCount=\"41\"") != NULL);
}

static void test_expression_without_operand_list(void)
{
    ucisFileHandleT f;
    ucisT           db = open_db(&f);

    ucis_CreateScope(db, NULL, "#cond#1#44#1#", NULL, 1, UCIS_SV, UCIS_COND, 0);
    cover(db, UCIS_CONDBIN, "0", NULL, 0, 9);
    ucis_WriteStreamScope(db);
    ucis_WriteStreamScope(db);
    assert(ucis_Close(db) == 0);

    /* subExpr is minOccurs="1", so something must be emitted. The scope name
     * stands in -- but it is NOT split, even though UOR names are themselves
     * '#'-delimited: splitting one would invent four operands from a file
     * number and a line number. */
    assert(strstr(g_m.data, "<subExpr>#cond#1#44#1#</subExpr>") != NULL);
    assert(strstr(g_m.data, "width=\"1\"") != NULL);
    assert(strstr(g_m.data, "<subExpr>cond</subExpr>") == NULL);
    /* No metric scope was opened, so there is no typeComponent to record. */
    assert(strstr(g_m.data, "typeComponent") == NULL);
}

static void test_expression_index_counts_per_document(void)
{
    ucisFileHandleT f;
    ucisT           db = open_db(&f);

    ucis_CreateScope(db, NULL, "#cond#1#1#1#", NULL, 1, UCIS_SV, UCIS_COND, 0);
    cover(db, UCIS_CONDBIN, "0", NULL, 0, 1);
    ucis_WriteStreamScope(db);
    ucis_CreateScope(db, NULL, "#cond#1#2#1#", NULL, 1, UCIS_SV, UCIS_EXPR, 0);
    cover(db, UCIS_EXPRBIN, "0", NULL, 0, 1);
    ucis_WriteStreamScope(db);
    ucis_WriteStreamScope(db);
    assert(ucis_Close(db) == 0);

    assert(strstr(g_m.data, "index=\"1\"") != NULL);
    assert(strstr(g_m.data, "index=\"2\"") != NULL);
    /* UCIS_EXPR and UCIS_COND differ only in intent; both are <expr>. */
    assert(strstr(g_m.data, "<conditionCoverage>") != NULL);
}

static void test_condition_sorts_between_block_and_branch(void)
{
    ucisFileHandleT f;
    ucisT           db = open_db(&f);

    cover(db, UCIS_STMTBIN, "#stmt#1#1#1#", f, 1, 1);
    ucis_CreateScope(db, NULL, "#cond#1#2#1#", NULL, 1, UCIS_SV, UCIS_COND, 0);
    cover(db, UCIS_CONDBIN, "0", NULL, 0, 1);
    ucis_WriteStreamScope(db);
    ucis_CreateScope(db, NULL, "#branch#1#3#1#", NULL, 1, UCIS_SV,
                     UCIS_BRANCH, 0);
    cover(db, UCIS_BRANCHBIN, "if", f, 3, 1);
    ucis_WriteStreamScope(db);
    ucis_WriteStreamScope(db);
    assert(ucis_Close(db) == 0);
    assert(at("<blockCoverage>") < at("<conditionCoverage>"));
    assert(at("<conditionCoverage>") < at("<branchCoverage>"));
}

static void test_expr_bin_outside_an_expression_scope(void)
{
    ucisFileHandleT f;
    ucisT           db = open_db(&f);

    cover(db, UCIS_CONDBIN, "00", NULL, 0, 1);
    assert(ucis_writer_error(db) == UCIS_WRITER_ERR_STATE);
    assert(strstr(ucis_writer_error_string(db), "expression scope") != NULL);
    ucis_Close(db);
}

static void test_branch_bin_outside_a_branch_scope(void)
{
    ucisFileHandleT f;
    ucisT           db = open_db(&f);

    cover(db, UCIS_BRANCHBIN, "if", f, 1, 1);
    assert(ucis_writer_error(db) == UCIS_WRITER_ERR_STATE);
    assert(strstr(ucis_writer_error_string(db), "branch scope") != NULL);
    ucis_Close(db);
}

int main(void)
{
    test_three_kinds_in_schema_order();
    test_kind_out_of_order_is_reported();
    test_kind_may_be_revisited_in_a_new_instance();
    test_scalar_toggle_gets_a_synthetic_bit();
    test_vector_toggle_nests_per_bit();
    test_enum_toggle_bins_are_arrivals();
    test_spaced_transition_names();
    test_expression_with_operand_list();
    test_expression_without_operand_list();
    test_expression_index_counts_per_document();
    test_condition_sorts_between_block_and_branch();
    test_expr_bin_outside_an_expression_scope();
    test_branch_bin_outside_a_branch_scope();
    uw_memsink_free(&g_m);
    printf("test_uw_codecov: ok\n");
    return 0;
}
