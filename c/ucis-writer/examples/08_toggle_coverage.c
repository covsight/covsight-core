/* 08_toggle_coverage.c - toggle coverage for scalars and vectors.
 *
 * Build:  cc -I../include -o 08 08_toggle_coverage.c -L. -lucis_writer
 * Run:    ./08 out.xml
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * UCIS models a vector as a toggle scope per bit nested inside the object's
 * toggle scope (spec 6.7, UIDs like "/4:top/0:w/0:1"). ucis_CreateToggle is
 * used for both: called with no toggle scope open it starts the object,
 * called with one open it starts a bit.
 *
 * A scalar has no per-bit scope, so its bins arrive straight into the object.
 * The schema still requires a toggleBit there, and the library synthesises
 * one -- you do not have to.
 *
 * Bin names are transitions: "0->1", "1->0". An enum toggle names its bins
 * after the values instead, which the library maps to an arrival at that
 * value; the raw name survives in the bin's nameComponent either way.
 */

#include <stdio.h>
#include <string.h>
#include "ucis_writer.h"

/* --8<-- doc:begin */
static void toggle_bin(ucisT db, const char* name, unsigned count)
{
    ucisCoverDataT data;
    memset(&data, 0, sizeof(data));
    data.type       = UCIS_TOGGLEBIN;
    data.flags      = UCIS_IS_32BIT;
    data.data.int32 = count;
    ucis_CreateNextCover(db, NULL, name, &data, NULL);
}

int main(int argc, char** argv)
{
    const char* path = (argc > 1) ? argv[1] : "08_toggle_coverage.xml";
    ucisT       db;
    int         bit;

    db = ucis_OpenWriteStream(path);
    if (db == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }
    ucis_CreateFileHandle(db, "rtl/dp.sv", NULL);
    ucis_CreateHistoryNode(db, NULL, (char*)"run1", NULL, UCIS_HISTORYNODE_TEST);
    ucis_CreateInstanceByName(db, NULL, "top.u_dp", NULL, 1, UCIS_SV,
                              UCIS_INSTANCE, (char*)"work.dp", UCIS_INST_ONCE);

    /* A scalar input. Bins go straight in; the toggleBit is synthesised. */
    ucis_CreateToggle(db, NULL, "clk_en", NULL, 0,
                      UCIS_TOGGLE_METRIC_2STOGGLE,
                      UCIS_TOGGLE_TYPE_NET, UCIS_TOGGLE_DIR_IN);
    toggle_bin(db, "0->1", 1024);
    toggle_bin(db, "1->0", 1023);
    ucis_WriteStreamScope(db);

    /* A 4-bit register: one nested toggle scope per bit, named by index. */
    ucis_CreateToggle(db, NULL, "count", NULL, 0,
                      UCIS_TOGGLE_METRIC_2STOGGLE,
                      UCIS_TOGGLE_TYPE_REG, UCIS_TOGGLE_DIR_INTERNAL);
    for (bit = 0; bit < 4; ++bit) {
        char index[8];
        snprintf(index, sizeof(index), "%d", bit);

        /* A toggle scope opened while one is already open is a bit. */
        ucis_CreateToggle(db, NULL, index, NULL, 0,
                          UCIS_TOGGLE_METRIC_2STOGGLE,
                          UCIS_TOGGLE_TYPE_REG, UCIS_TOGGLE_DIR_INTERNAL);
        toggle_bin(db, "0->1", (unsigned)(512 >> bit));
        toggle_bin(db, "1->0", (unsigned)(512 >> bit));
        ucis_WriteStreamScope(db);      /* closes the bit */
    }
    ucis_WriteStreamScope(db);          /* closes the object */

    /* An enum: the bins are values, not transitions. */
    ucis_CreateToggle(db, NULL, "state", NULL, 0,
                      UCIS_TOGGLE_METRIC_ENUM,
                      UCIS_TOGGLE_TYPE_REG, UCIS_TOGGLE_DIR_INTERNAL);
    toggle_bin(db, "IDLE", 900);
    toggle_bin(db, "BUSY", 75);
    toggle_bin(db, "ERROR", 0);
    ucis_WriteStreamScope(db);

    ucis_WriteStreamScope(db);          /* closes the instance */

    if (ucis_writer_error(db) != UCIS_WRITER_OK) {
        fprintf(stderr, "%s: %s\n", path, ucis_writer_error_string(db));
    }
    return ucis_Close(db) == 0 ? 0 : 1;
}
/* --8<-- doc:end */
