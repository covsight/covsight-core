/* 09_covergroup.c - a covergroup with coverpoints and per-instance data.
 *
 * Build:  cc -I../include -o 09 09_covergroup.c -L. -lucis_writer
 * Run:    ./09 out.xml
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * UCIS separates the covergroup *type* from its instances; UCIS-XML has only
 * cgInstance, which repeats the type's name and location inside its own cgId.
 * So the type scope here writes nothing of its own -- it is the thing the
 * cgInstance quotes. Create it, create instances inside it, and the mapping
 * happens for you.
 *
 * A covergroup with no cgInstance of its own is also fine: open coverpoints
 * directly in the type scope and one cgInstance is synthesised, named after
 * the type. That is what type-only coverage (per_instance off) looks like.
 *
 * Bin names carry their values: "7", "auto[7]" and "auto[3:9]" all become a
 * real <range>. A named bin like "small" has no value to emit, so its ordinal
 * stands in and a warning is counted -- the name itself is never lost.
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
    const char*     path = (argc > 1) ? argv[1] : "09_covergroup.xml";
    ucisT           db;
    ucisFileHandleT fh;
    ucisSourceInfoT si;
    ucisScopeT      cgi;
    ucisScopeT      cvp;
    int             i;

    db = ucis_OpenWriteStream(path);
    if (db == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }
    fh = ucis_CreateFileHandle(db, "tb/cg.sv", NULL);
    ucis_CreateHistoryNode(db, NULL, (char*)"run1", NULL, UCIS_HISTORYNODE_TEST);
    ucis_CreateInstanceByName(db, NULL, "tb.u_mon", NULL, 1, UCIS_SV,
                              UCIS_INSTANCE, (char*)"work.mon", UCIS_INST_ONCE);

    /* The covergroup type. Its source location becomes cgId/cgSourceId. */
    si.filehandle = fh;
    si.line       = 42;
    si.token      = 0;
    ucis_CreateScope(db, NULL, "cg_bus", &si, 1, UCIS_SV, UCIS_COVERGROUP, 0);

    /* One instance of it. Its own location becomes cgId/cginstSourceId. */
    si.line = 118;
    cgi = ucis_CreateScope(db, NULL, "cg_bus_inst", &si, 1, UCIS_SV,
                           UCIS_COVERINSTANCE, 0);
    /* Covergroup options live in an <options> element, not on the scope, and
     * are settable until the first child is written. */
    ucis_SetIntProperty(db, cgi, -1, UCIS_INT_CVG_PERINSTANCE, 1);
    ucis_SetIntProperty(db, cgi, -1, UCIS_INT_CVG_ATLEAST, 2);

    /* A coverpoint with auto bins: the names carry the values. */
    cvp = ucis_CreateScope(db, NULL, "cp_len", NULL, 1, UCIS_SV,
                           UCIS_COVERPOINT, 0);
    ucis_SetIntProperty(db, cvp, -1, UCIS_INT_CVG_AUTOBINMAX, 8);
    for (i = 0; i < 4; ++i) {
        char name[16];
        snprintf(name, sizeof(name), "auto[%d]", i);
        cvg_bin(db, name, (unsigned)(100 >> i));
    }
    ucis_WriteStreamScope(db);

    /* A coverpoint with explicit bins, one of them a range. */
    ucis_CreateScope(db, NULL, "cp_addr", NULL, 1, UCIS_SV, UCIS_COVERPOINT, 0);
    cvg_bin(db, "[0:255]", 4096);
    cvg_bin(db, "[256:511]", 12);
    ucis_WriteStreamScope(db);

    ucis_WriteStreamScope(db);      /* closes the cgInstance */
    ucis_WriteStreamScope(db);      /* closes the covergroup type */
    ucis_WriteStreamScope(db);      /* closes the instance */

    if (ucis_writer_error(db) != UCIS_WRITER_OK) {
        fprintf(stderr, "%s: %s\n", path, ucis_writer_error_string(db));
    }
    return ucis_Close(db) == 0 ? 0 : 1;
}
/* --8<-- doc:end */
