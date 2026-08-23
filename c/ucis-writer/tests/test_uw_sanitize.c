/* T-1.4 - uw_text: control-character replacement and UTF-8 validation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Design identifiers on real projects are not always valid UTF-8 and not
 * always free of control characters -- they come out of preprocessors, log
 * scrapers and vendor tools. The contract is that ucis_writer emits a
 * well-formed XML 1.0 document for ANY byte sequence, and reports how much it
 * had to change. */

#include "uw_text.h"
#include "uw_test_sink.h"

#include <assert.h>
#include <stdio.h>

static uw_memsink_t    g_m;
static ucisWriterSinkT g_sink;
static uw_buf_t        g_b;
static char            g_store[256];
static unsigned long   g_warn;

static const char* esc(const char* s, size_t n)
{
    uw_memsink_free(&g_m);
    uw_memsink_init(&g_m, &g_sink);
    uw_buf_init(&g_b, g_store, sizeof(g_store), &g_sink);
    g_warn = 0;
    uw_text_escape(&g_b, s, n, &g_warn);
    uw_buf_flush(&g_b);
    return g_m.data ? g_m.data : "";
}

static void expect(const char* in, size_t n, const char* out,
                   unsigned long warns)
{
    const char* got = esc(in, n);
    if (strcmp(got, out) != 0 || g_warn != warns) {
        fprintf(stderr, "got \"%s\" (%lu warnings), want \"%s\" (%lu)\n",
                got, g_warn, out, warns);
        assert(0);
    }
}

int main(void)
{
    unsigned c;

    /* Every C0 control other than tab/LF/CR is unrepresentable in XML 1.0 --
     * not even as a numeric reference -- so it is replaced, and counted. */
    for (c = 0x00; c < 0x20; ++c) {
        char in[3];
        in[0] = 'a';
        in[1] = (char)c;
        in[2] = 'b';
        if (c == 0x09 || c == 0x0A || c == 0x0D) {
            continue;
        }
        {
            const char* got = esc(in, 3);
            assert(strcmp(got, "a?b") == 0);
            assert(g_warn == 1);
        }
    }

    /* Embedded NUL is a length-carrying case: strlen would truncate here. */
    expect("a\0b", 3, "a?b", 1);

    expect("\xC3\xA9", 2, "\xC3\xA9", 0);                 /* U+00E9 */
    expect("\xE2\x82\xAC", 3, "\xE2\x82\xAC", 0);         /* U+20AC */
    expect("\xF0\x9F\x92\xA9", 4, "\xF0\x9F\x92\xA9", 0); /* U+1F4A9 */

#if UCIS_WRITER_UTF8_CHECK
    expect("\x80", 1, "?", 1);                    /* stray continuation byte */
    expect("\xC0\xAF", 2, "??", 2);               /* overlong '/' */
    expect("\xE0\x80\xAF", 3, "???", 3);          /* overlong, 3-byte form */
    expect("\xED\xA0\x80", 3, "???", 3);          /* UTF-16 surrogate D800 */
    expect("\xF5\x80\x80\x80", 4, "????", 4);     /* beyond U+10FFFF */
    expect("\xC3", 1, "?", 1);                    /* truncated sequence */
    expect("a\xC3\xA9\x80z", 5, "a\xC3\xA9?z", 1);/* good and bad interleaved */
#endif

    uw_memsink_free(&g_m);
    printf("test_uw_sanitize: ok\n");
    return 0;
}
