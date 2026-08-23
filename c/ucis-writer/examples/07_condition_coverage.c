/* 07_condition_coverage.c - condition and expression coverage.
 *
 * Build:  cc -I../include -o 07 07_condition_coverage.c -L. -lucis_writer
 * Run:    ./07 out.xml
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * UCIS models this in two levels (spec 6.5.4.1):
 *
 *   the expression        -> ucis_CreateScope(..., UCIS_COND or UCIS_EXPR, ...)
 *   an input-contribution -> a second such scope inside it, named for the
 *   metric                   metric ("UCIS:FULL", "UCIS:VECTOR", ...)
 *   the truth-table rows  -> UCIS_CONDBIN / UCIS_EXPRBIN coveritems
 *
 * UCIS-XML has no element for the metric level -- its EXPR type requires bins
 * directly. So the metric scope is accepted, balanced like any other scope,
 * and recorded on each bin as its typeComponent. Use UCIS_COND when the
 * expression controls a conditional statement and UCIS_EXPR otherwise; both
 * map to the same element.
 *
 * The operand list matters: set UCIS_STR_EXPR_TERMS on the expression scope
 * and the library derives both the subExpr children and @width from it.
 */

#include <stdio.h>
#include <string.h>
#include "ucis_writer.h"

/* --8<-- doc:begin */
/* One row of the truth table: which input combination, and how often. */
typedef struct { const char* row; unsigned count; } row_t;

static void emit_expression(ucisT db, ucisFileHandleT file, int line,
                            const char* uor, const char* terms,
                            const char* metric,
                            const row_t* rows, size_t nrows)
{
    ucisSourceInfoT srcinfo;
    size_t          i;
    ucisScopeT      expr;

    srcinfo.filehandle = file;
    srcinfo.line       = line;
    srcinfo.token      = 0;

    expr = ucis_CreateScope(db, NULL, uor, &srcinfo, 1, UCIS_SV,
                            UCIS_COND, 0);

    /* '#'-separated operands, in lexical order. This drives <subExpr> and
     * @width, so it is worth supplying even though it is optional. Without it
     * the expression gets one opaque subExpr naming itself. */
    ucis_SetStringProperty(db, expr, -1, UCIS_STR_EXPR_TERMS, terms);

    /* The metric scope. No element comes out of it, but it is a scope. */
    ucis_CreateScope(db, NULL, metric, NULL, 1, UCIS_SV, UCIS_COND, 0);

    for (i = 0; i < nrows; ++i) {
        ucisCoverDataT data;
        memset(&data, 0, sizeof(data));
        data.type       = UCIS_CONDBIN;
        data.flags      = UCIS_IS_32BIT;
        data.data.int32 = rows[i].count;
        ucis_CreateNextCover(db, NULL, rows[i].row, &data, NULL);
    }

    ucis_WriteStreamScope(db);   /* the metric scope */
    ucis_WriteStreamScope(db);   /* the expression */
}

int main(int argc, char** argv)
{
    const char*     path = (argc > 1) ? argv[1] : "07_condition_coverage.xml";
    ucisT           db;
    ucisFileHandleT file;

    /* if (enable && (mode == 2)) -- two inputs, four rows. */
    static const row_t and2[] = {
        { "00", 41 }, { "01", 12 }, { "10", 7 }, { "11", 0 }
    };
    /* A single-operand condition: one input, two rows. */
    static const row_t not1[] = { { "0", 900 }, { "1", 88 } };

    db = ucis_OpenWriteStream(path);
    if (db == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }
    file = ucis_CreateFileHandle(db, "rtl/dec.sv", NULL);
    ucis_CreateHistoryNode(db, NULL, (char*)"run1", NULL, UCIS_HISTORYNODE_TEST);
    ucis_CreateInstanceByName(db, NULL, "top.u_dec", NULL, 1, UCIS_SV,
                              UCIS_INSTANCE, (char*)"work.dec", UCIS_INST_ONCE);

    emit_expression(db, file, 31, "#cond#1#31#1#", "enable#mode_is_2",
                    "UCIS:FULL", and2, 4);
    emit_expression(db, file, 44, "#cond#1#44#1#", "rst_n",
                    "UCIS:FULL", not1, 2);

    ucis_WriteStreamScope(db);   /* the instance */

    if (ucis_writer_error(db) != UCIS_WRITER_OK) {
        fprintf(stderr, "%s: %s\n", path, ucis_writer_error_string(db));
    }
    return ucis_Close(db) == 0 ? 0 : 1;
}
/* --8<-- doc:end */
