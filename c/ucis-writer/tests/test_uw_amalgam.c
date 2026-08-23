/* T-6.2 - the shipped single header behaves like the sources we test.
 * SPDX-License-Identifier: Apache-2.0
 *
 * This translation unit is what a vendoring consumer writes: one file, one
 * define, no build system, no library to link. If it drifts from the split
 * build, everything else in this suite is testing something we do not ship.
 *
 * The `--check` step in CI proves the header is regenerated; this proves the
 * regenerated header works. */

#define UCIS_WRITER_IMPLEMENTATION
#include "ucis_writer.h"

#include "uw_fixture.h"

#include <assert.h>
#include <stdio.h>

static uw_memsink_t    g_m;
static ucisWriterSinkT g_sink;

int main(void)
{
    ucisT db = uw_fixture_build(&g_m, &g_sink, 1);
    assert(ucis_Close(db) == 0);
    if (strcmp(g_m.data, UW_FIXTURE_WANT) != 0) {
        fprintf(stderr, "amalgamated header output differs\ngot:\n%s\nwant:\n%s\n",
                g_m.data, UW_FIXTURE_WANT);
        return 1;
    }
    uw_memsink_free(&g_m);
    printf("test_uw_amalgam: ok\n");
    return 0;
}
