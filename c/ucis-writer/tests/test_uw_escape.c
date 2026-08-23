/* T-1.3 - uw_text: XML entity escaping and the escape-free fast path.
 * SPDX-License-Identifier: Apache-2.0 */

#include "uw_text.h"
#include "uw_test_sink.h"

#include <assert.h>
#include <stdio.h>

static uw_memsink_t    g_m;
static ucisWriterSinkT g_sink;
static uw_buf_t        g_b;
static char            g_store[64];
static unsigned long   g_warn;

static void reset(void)
{
    uw_memsink_free(&g_m);
    uw_memsink_init(&g_m, &g_sink);
    uw_buf_init(&g_b, g_store, sizeof(g_store), &g_sink);
    g_warn = 0;
}

static const char* esc(const char* s)
{
    reset();
    uw_text_escape_cstr(&g_b, s, &g_warn);
    uw_buf_flush(&g_b);
    return g_m.data ? g_m.data : "";
}

static void expect(const char* in, const char* out)
{
    const char* got = esc(in);
    if (strcmp(got, out) != 0) {
        fprintf(stderr, "escape(\"%s\") = \"%s\", want \"%s\"\n", in, got, out);
        assert(0);
    }
}

int main(void)
{
    char        big[400];
    const char* got;
    size_t      i;

    expect("", "");
    expect("plain", "plain");
    expect("a&b", "a&amp;b");
    expect("a<b", "a&lt;b");
    expect("a>b", "a&gt;b");
    expect("a\"b", "a&quot;b");
    expect("a'b", "a&apos;b");
    expect("&", "&amp;");
    expect("&&", "&amp;&amp;");
    expect("<>&\"'", "&lt;&gt;&amp;&quot;&apos;");

    /* Tab, newline and carriage return are legal XML characters but an
     * attribute-value parser normalises them to spaces, so they have to go out
     * as character references to survive a round trip. */
    expect("a\tb", "a&#9;b");
    expect("a\nb", "a&#10;b");
    expect("a\rb", "a&#13;b");

    /* Verilator-shaped names, which is the case that must stay on the fast
     * path: no escape, one buffer copy. */
    expect("top.dut.u_core.genblk1[3].u_alu", "top.dut.u_core.genblk1[3].u_alu");
    expect("$unit::my_pkg::cg_1", "$unit::my_pkg::cg_1");

    /* An escape that lands right at a buffer flush boundary must not be split
     * across two sink writes in a way that loses bytes. */
    for (i = 0; i < sizeof(big) - 1; ++i) {
        big[i] = (i % 7 == 6) ? '<' : 'x';
    }
    big[sizeof(big) - 1] = '\0';
    got = esc(big);
    {
        size_t nlt = 0, nx = 0, j;
        for (j = 0; got[j] != '\0'; ++j) {
            if (got[j] == 'x') { nx++; }
            if (got[j] == '&') { nlt++; }
        }
        assert(nlt == (sizeof(big) - 1) / 7);
        assert(nx == (sizeof(big) - 1) - nlt);
    }
    assert(g_warn == 0);

    /* Attribute helpers: a NULL value omits the attribute entirely, which is
     * how every optional XSD attribute is left out. */
    reset();
    uw_text_attr(&g_b, "name", NULL, &g_warn);
    uw_buf_flush(&g_b);
    assert(g_m.len == 0);

    reset();
    uw_text_attr(&g_b, "name", "a\"b", &g_warn);
    uw_buf_flush(&g_b);
    assert(strcmp(g_m.data, " name=\"a&quot;b\"") == 0);

    reset();
    uw_text_attr_u64(&g_b, "id", 42);
    uw_text_attr_i64(&g_b, "delta", -7);
    uw_buf_flush(&g_b);
    assert(strcmp(g_m.data, " id=\"42\" delta=\"-7\"") == 0);

    uw_memsink_free(&g_m);
    printf("test_uw_escape: ok\n");
    return 0;
}
