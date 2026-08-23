/* T-1.2 - uw_buf integer formatting, differentially against snprintf.
 * SPDX-License-Identifier: Apache-2.0 */

#include "uw_buf.h"
#include "uw_test_sink.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

static uw_memsink_t    g_m;
static ucisWriterSinkT g_sink;
static uw_buf_t        g_b;
static char            g_store[512];

static void reset(void)
{
    uw_memsink_free(&g_m);
    uw_memsink_init(&g_m, &g_sink);
    uw_buf_init(&g_b, g_store, sizeof(g_store), &g_sink);
}

static const char* rendered(void)
{
    uw_buf_flush(&g_b);
    return g_m.data ? g_m.data : "";
}

static void check_u64(uint64_t v)
{
    char want[32];
    snprintf(want, sizeof(want), "%" PRIu64, v);
    reset();
    assert(uw_buf_u64(&g_b, v) == UCIS_WRITER_OK);
    assert(strcmp(rendered(), want) == 0);
}

static void check_i64(int64_t v)
{
    char want[32];
    snprintf(want, sizeof(want), "%" PRId64, v);
    reset();
    assert(uw_buf_i64(&g_b, v) == UCIS_WRITER_OK);
    assert(strcmp(rendered(), want) == 0);
}

static void check_double(double v, const char* want)
{
    reset();
    assert(uw_buf_double(&g_b, v) == UCIS_WRITER_OK);
    assert(strcmp(rendered(), want) == 0);
}

int main(void)
{
    uint64_t v;
    int      i;

    check_u64(0);
    check_u64(1);
    check_u64(9);
    check_u64(10);
    check_u64(99);
    check_u64(100);
    check_u64(4294967295u);          /* UINT32_MAX */
    check_u64(4294967296ull);
    check_u64(18446744073709551615ull);   /* UINT64_MAX */

    /* Every power of ten and its neighbours: the carry boundaries are where
     * a hand-rolled formatter goes wrong. */
    v = 1;
    for (i = 0; i < 19; ++i) {
        check_u64(v - 1);
        check_u64(v);
        check_u64(v + 1);
        v *= 10;
    }

    check_i64(0);
    check_i64(-1);
    check_i64(1);
    check_i64(9223372036854775807ll);       /* INT64_MAX */
    check_i64(-9223372036854775807ll - 1);  /* INT64_MIN: negation overflows */

    check_u64(4294967295u);
    reset();
    assert(uw_buf_u32(&g_b, 4294967295u) == UCIS_WRITER_OK);
    assert(strcmp(rendered(), "4294967295") == 0);

    /* xsd:double must not pick up a locale's decimal comma, and must not
     * render as "inf"/"nan", which no consumer parses. */
    check_double(0.0, "0");
    check_double(1.5, "1.5");
    check_double(-0.25, "-0.25");
    check_double(1.0 / 0.0, "0");
    check_double(-1.0 / 0.0, "0");

    uw_memsink_free(&g_m);
    printf("test_uw_intfmt: ok\n");
    return 0;
}
