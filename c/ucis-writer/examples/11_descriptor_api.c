/* 11_descriptor_api.c - the same coverage as 10_cross.c, written the
 * recommended way.
 *
 * Build:  cc -I../include -o 11 11_descriptor_api.c -L. -lucis_writer
 * Run:    ./11 out.xml
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Diff this against 10_cross.c. Both produce identical markup; the difference
 * is what the caller has to know.
 *
 * Everything an element needs is passed when it is created, so there is no
 * window in which a value can arrive too late, and no property call that can
 * name an option the element cannot hold. A cross names the coverpoints it
 * crosses as a field rather than as a sequence of property calls.
 *
 * The `uw_*_defaults()` form used below is deliberate: it is the only
 * initialiser that compiles clean from C99 through C++20. The compact C99
 * form -- uw_coverpoint(db, &(uw_coverpoint_desc_t){ .name = "cp" }) -- reads
 * better but can never compile as C++, because compound literals are not ISO
 * C++ in any version.
 */

#include <stdio.h>
#include "ucis_writer.h"

/* --8<-- doc:begin */
static void bin(uw_db_t* db, const char* name, uint64_t count)
{
    uw_bin_desc_t b = uw_bin_defaults();
    b.name  = name;
    b.count = count;
    uw_bin(db, &b);          /* the open scope decides what kind of bin it is */
}

int main(int argc, char** argv)
{
    const char* path = (argc > 1) ? argv[1] : "11_descriptor_api.xml";
    uw_db_t*    db;
    int         mode, len;

    uw_test_desc_t       t   = uw_test_defaults();
    uw_instance_desc_t   ins = uw_instance_defaults();
    uw_covergroup_desc_t cg  = uw_covergroup_defaults();
    uw_coverpoint_desc_t cvp = uw_coverpoint_defaults();
    uw_cross_desc_t      x   = uw_cross_defaults();

    static const char* const crossed[] = { "cp_mode", "cp_len" };

    db = uw_open(path);
    if (db == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }

    t.name = "run1";  t.passed = 1;
    uw_test(db, &t);

    ins.name = "tb.u_mon";  ins.du_name = "work.mon";
    uw_instance(db, &ins);

    cg.name = "cg_xfer";
    uw_covergroup(db, &cg);

    cvp.name = "cp_mode";
    uw_coverpoint(db, &cvp);
    for (mode = 0; mode < 2; ++mode) {
        char name[8];
        snprintf(name, sizeof(name), "%d", mode);
        bin(db, name, 500u + (unsigned)mode);
    }
    uw_end(db);

    cvp = uw_coverpoint_defaults();
    cvp.name = "cp_len";
    uw_coverpoint(db, &cvp);
    for (len = 0; len < 3; ++len) {
        char name[8];
        snprintf(name, sizeof(name), "%d", len);
        bin(db, name, 300u + (unsigned)len);
    }
    uw_end(db);

    /* The crossed coverpoints are a field, and they are what lets each cross
     * bin's name resolve to the <index> the schema requires. */
    x.name      = "x_mode_len";
    x.crossed   = crossed;
    x.n_crossed = sizeof(crossed) / sizeof(crossed[0]);
    uw_cross(db, &x);
    for (mode = 0; mode < 2; ++mode) {
        for (len = 0; len < 3; ++len) {
            char name[16];
            snprintf(name, sizeof(name), "%d,%d", mode, len);
            bin(db, name, (uint64_t)(100 + 10 * mode + len));
        }
    }
    uw_end(db);      /* cross      */
    uw_end(db);      /* covergroup */
    uw_end(db);      /* instance   */

    if (uw_error(db) != UCIS_WRITER_OK) {
        fprintf(stderr, "%s: %s\n", path, uw_error_string(db));
    }
    return uw_close(db) == 0 ? 0 : 1;
}
/* --8<-- doc:end */
