/* 05_statement_coverage.c - line/statement coverage across several files.
 *
 * Build:  cc -I../include -o 05 05_statement_coverage.c -L. -lucis_writer
 * Run:    ./05 out.xml
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Statement coverage is the shape most tools have already: a count per
 * (file, line). This is the example to copy if that is what you have.
 *
 * Two things to notice:
 *
 *   - Every file handle is created BEFORE the first instance. The source-file
 *     table is written out at that point and sealed; a handle requested later
 *     is an error, because there is no longer an id to give it.
 *
 *   - The coveritems for one instance arrive together. This library will not
 *     buffer to reorder them, because doing so would put the whole design in
 *     memory. See 16_ordering_contract.c.
 */

#include <stdio.h>
#include <string.h>
#include "ucis_writer.h"

/* Pretend this came out of a simulation. */
typedef struct {
    const char* instance;
    const char* module;
    int         file;      /* index into FILES */
    int         line;
    unsigned    count;
} point_t;

static const char* FILES[] = { "rtl/alu.sv", "rtl/regfile.sv" };

static const point_t POINTS[] = {
    { "top.u_alu",     "work.alu",     0,  42,  17 },
    { "top.u_alu",     "work.alu",     0,  43,  17 },
    { "top.u_alu",     "work.alu",     0,  47,   0 },   /* never executed */
    { "top.u_regfile", "work.regfile", 1, 101, 250 },
    { "top.u_regfile", "work.regfile", 1, 105,   3 },
};

/* --8<-- doc:begin */
int main(int argc, char** argv)
{
    const char*     path = (argc > 1) ? argv[1] : "05_statement_coverage.xml";
    ucisT           db;
    ucisFileHandleT handles[sizeof(FILES) / sizeof(FILES[0])];
    const char*     open_instance = NULL;
    size_t          i;

    db = ucis_OpenWriteStream(path);
    if (db == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }

    /* Every file, up front. */
    for (i = 0; i < sizeof(FILES) / sizeof(FILES[0]); ++i) {
        handles[i] = ucis_CreateFileHandle(db, FILES[i], NULL);
    }
    ucis_CreateHistoryNode(db, NULL, (char*)"run1", NULL, UCIS_HISTORYNODE_TEST);

    for (i = 0; i < sizeof(POINTS) / sizeof(POINTS[0]); ++i) {
        const point_t*  p = &POINTS[i];
        ucisCoverDataT  data;
        ucisSourceInfoT srcinfo;
        char            binname[64];

        /* Points are already grouped by instance, so a change of instance
         * means the previous one is finished. */
        if (open_instance == NULL || strcmp(open_instance, p->instance) != 0) {
            if (open_instance != NULL) {
                ucis_WriteStreamScope(db);
            }
            ucis_CreateInstanceByName(db, NULL, p->instance, NULL, 1, UCIS_SV,
                                      UCIS_INSTANCE, (char*)p->module,
                                      UCIS_INST_ONCE);
            open_instance = p->instance;
        }

        srcinfo.filehandle = handles[p->file];
        srcinfo.line       = p->line;
        srcinfo.token      = 0;

        memset(&data, 0, sizeof(data));
        data.type  = UCIS_STMTBIN;
        data.flags = UCIS_IS_32BIT | UCIS_HAS_COUNT;
        data.data.int32 = p->count;

        /* The UOR name identifies the point across runs and across tools.
         * `#stmt#<file>#<line>#<inline>#` is the UCIS convention. */
        snprintf(binname, sizeof(binname), "#stmt#%d#%d#1#", p->file + 1, p->line);
        ucis_CreateNextCover(db, NULL, binname, &data, &srcinfo);
    }
    if (open_instance != NULL) {
        ucis_WriteStreamScope(db);
    }

    if (ucis_writer_error(db) != UCIS_WRITER_OK) {
        fprintf(stderr, "%s: %s\n", path, ucis_writer_error_string(db));
    }
    if (ucis_Close(db) != 0) {
        fprintf(stderr, "%s: write failed\n", path);
        return 1;
    }
    return 0;
}
/* --8<-- doc:end */
