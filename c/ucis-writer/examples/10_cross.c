/* 10_cross.c - crossing two coverpoints.
 *
 * Build:  cc -I../include -o 10 10_cross.c -L. -lucis_writer
 * Run:    ./10 out.xml
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Two rules matter here, and both come from the schema rather than from us.
 *
 * 1. Every coverpoint of a cgInstance precedes every cross of it. Creating a
 *    coverpoint after a cross is UCIS_WRITER_ERR_ORDER, not a document a
 *    reader will quietly reject later.
 *
 * 2. A crossBin has to say which bin of each crossed coverpoint it represents,
 *    as an <index>. The C API gives a cross bin only its name -- "a,b" -- so
 *    the writer resolves each comma-separated component against the bins of
 *    the coverpoints the cross names via UCIS_STR_ITH_CROSSED_CVP_NAME. Name
 *    the crossed coverpoints in cross order and the indices come out right; a
 *    component that matches no bin becomes -1 and is counted as a warning.
 *
 * This example also shows the shorter form of a covergroup: coverpoints
 * created straight in the type scope, with the cgInstance synthesised.
 */

#include <stdio.h>
#include <string.h>
#include "ucis_writer.h"

/* --8<-- doc:begin */
static void cvg_bin(ucisT db, const char* name, unsigned count)
{
    ucisCoverDataT data;
    memset(&data, 0, sizeof(data));
    data.type       = UCIS_CVGBIN;
    data.flags      = UCIS_IS_32BIT;
    data.data.int32 = count;
    ucis_CreateNextCover(db, NULL, name, &data, NULL);
}

int main(int argc, char** argv)
{
    const char* path = (argc > 1) ? argv[1] : "10_cross.xml";
    ucisT       db;
    ucisScopeT  cross;
    int         mode, len;

    db = ucis_OpenWriteStream(path);
    if (db == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }
    ucis_CreateHistoryNode(db, NULL, (char*)"run1", NULL, UCIS_HISTORYNODE_TEST);
    ucis_CreateInstanceByName(db, NULL, "tb.u_mon", NULL, 1, UCIS_SV,
                              UCIS_INSTANCE, (char*)"work.mon", UCIS_INST_ONCE);
    ucis_CreateScope(db, NULL, "cg_xfer", NULL, 1, UCIS_SV, UCIS_COVERGROUP, 0);

    /* No cgInstance: these coverpoints get one synthesised around them. */
    ucis_CreateScope(db, NULL, "cp_mode", NULL, 1, UCIS_SV, UCIS_COVERPOINT, 0);
    for (mode = 0; mode < 2; ++mode) {
        char name[8];
        snprintf(name, sizeof(name), "%d", mode);
        cvg_bin(db, name, 500u + (unsigned)mode);
    }
    ucis_WriteStreamScope(db);

    ucis_CreateScope(db, NULL, "cp_len", NULL, 1, UCIS_SV, UCIS_COVERPOINT, 0);
    for (len = 0; len < 3; ++len) {
        char name[8];
        snprintf(name, sizeof(name), "%d", len);
        cvg_bin(db, name, 300u + (unsigned)len);
    }
    ucis_WriteStreamScope(db);

    /* Crosses come after every coverpoint, and name what they cross. */
    cross = ucis_CreateScope(db, NULL, "x_mode_len", NULL, 1, UCIS_SV,
                             UCIS_CROSS, 0);
    ucis_SetStringProperty(db, cross, -1, UCIS_STR_ITH_CROSSED_CVP_NAME,
                           "cp_mode");
    ucis_SetStringProperty(db, cross, -1, UCIS_STR_ITH_CROSSED_CVP_NAME,
                           "cp_len");
    for (mode = 0; mode < 2; ++mode) {
        for (len = 0; len < 3; ++len) {
            char name[16];
            snprintf(name, sizeof(name), "%d,%d", mode, len);
            cvg_bin(db, name, (unsigned)(100 + 10 * mode + len));
        }
    }
    ucis_WriteStreamScope(db);      /* closes the cross */

    ucis_WriteStreamScope(db);      /* closes the covergroup type */
    ucis_WriteStreamScope(db);      /* closes the instance */

    if (ucis_writer_error(db) != UCIS_WRITER_OK) {
        fprintf(stderr, "%s: %s\n", path, ucis_writer_error_string(db));
    }
    return ucis_Close(db) == 0 ? 0 : 1;
}
/* --8<-- doc:end */
