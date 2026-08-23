/* 06_branch_coverage.c - branch coverage: if/else and case arms.
 *
 * Build:  cc -I../include -o 06 06_branch_coverage.c -L. -lucis_writer
 * Run:    ./06 out.xml
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Branch coverage is two levels, unlike statement coverage's one:
 *
 *   the branch point  -> ucis_CreateScope(..., UCIS_BRANCH, ...)
 *   each arm of it    -> ucis_CreateNextCover(..., UCIS_BRANCHBIN, ...)
 *
 * The scope is a scope, so it needs a matching ucis_WriteStreamScope. The arms
 * are coveritems, so they do not.
 *
 * The arm's name ("if", "else", a case label) ends up in the bin's
 * nameComponent. That is not a stylistic choice: the schema's BRANCH type has
 * no attributes at all, so there is nowhere else for it to go.
 */

#include <stdio.h>
#include <string.h>
#include "ucis_writer.h"

/* --8<-- doc:begin */
/* One arm of a branch: its label and how many times it was taken. */
typedef struct { const char* label; unsigned count; } arm_t;

static int emit_branch(ucisT db, ucisFileHandleT file, int line,
                       const char* uor, const arm_t* arms, size_t narms)
{
    ucisSourceInfoT srcinfo;
    size_t          i;

    srcinfo.filehandle = file;
    srcinfo.line       = line;
    srcinfo.token      = 0;

    /* The branch point. parent is NULL: the current instance is the parent. */
    if (ucis_CreateScope(db, NULL, uor, &srcinfo, 1, UCIS_SV,
                         UCIS_BRANCH, 0) == NULL) {
        return -1;
    }
    for (i = 0; i < narms; ++i) {
        ucisCoverDataT data;
        memset(&data, 0, sizeof(data));
        data.type       = UCIS_BRANCHBIN;
        data.flags      = UCIS_IS_32BIT;
        data.data.int32 = arms[i].count;
        ucis_CreateNextCover(db, NULL, arms[i].label, &data, &srcinfo);
    }
    return ucis_WriteStreamScope(db);   /* closes the branch point */
}

int main(int argc, char** argv)
{
    const char*     path = (argc > 1) ? argv[1] : "06_branch_coverage.xml";
    ucisT           db;
    ucisFileHandleT file;

    static const arm_t if_else[] = { { "if", 12 }, { "else", 0 } };
    static const arm_t case_arms[] = {
        { "IDLE", 900 }, { "READ", 44 }, { "WRITE", 31 }, { "default", 0 }
    };

    db = ucis_OpenWriteStream(path);
    if (db == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }
    file = ucis_CreateFileHandle(db, "rtl/ctrl.sv", NULL);
    ucis_CreateHistoryNode(db, NULL, (char*)"run1", NULL, UCIS_HISTORYNODE_TEST);

    ucis_CreateInstanceByName(db, NULL, "top.u_ctrl", NULL, 1, UCIS_SV,
                              UCIS_INSTANCE, (char*)"work.ctrl", UCIS_INST_ONCE);

    emit_branch(db, file, 88, "#branch#1#88#1#", if_else, 2);
    emit_branch(db, file, 96, "#branch#1#96#1#", case_arms, 4);

    ucis_WriteStreamScope(db);          /* closes the instance */

    if (ucis_writer_error(db) != UCIS_WRITER_OK) {
        fprintf(stderr, "%s: %s\n", path, ucis_writer_error_string(db));
    }
    return ucis_Close(db) == 0 ? 0 : 1;
}
/* --8<-- doc:end */
