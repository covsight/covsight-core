/* T-1.1 - uw_buf: flush boundaries, exact fill, sink failure, error latching.
 * SPDX-License-Identifier: Apache-2.0 */

#include "uw_buf.h"
#include "uw_test_sink.h"

#include <assert.h>
#include <stdio.h>

#define CAP 16

static void with_buf(uw_buf_t* b, char* store, uw_memsink_t* m,
                     ucisWriterSinkT* sink)
{
    uw_memsink_init(m, sink);
    uw_buf_init(b, store, CAP, sink);
}

static void test_buffers_until_full(void)
{
    uw_buf_t b; char store[CAP]; uw_memsink_t m; ucisWriterSinkT sink;
    with_buf(&b, store, &m, &sink);

    assert(uw_buf_write(&b, "abc", 3) == UCIS_WRITER_OK);
    assert(m.len == 0);            /* nothing reaches the sink yet */
    assert(uw_buf_flush(&b) == UCIS_WRITER_OK);
    assert(m.len == 3 && memcmp(m.data, "abc", 3) == 0);
    uw_memsink_free(&m);
}

static void test_exact_fill(void)
{
    uw_buf_t b; char store[CAP]; uw_memsink_t m; ucisWriterSinkT sink;
    with_buf(&b, store, &m, &sink);

    /* Exactly cap bytes must fit without flushing: an off-by-one here would
     * split every document at a different place than the amalgamated build. */
    assert(uw_buf_write(&b, "0123456789abcdef", CAP) == UCIS_WRITER_OK);
    assert(m.len == 0);
    assert(uw_buf_putc(&b, 'X') == UCIS_WRITER_OK);
    assert(m.len == CAP);
    assert(uw_buf_flush(&b) == UCIS_WRITER_OK);
    assert(m.len == CAP + 1 && m.data[CAP] == 'X');
    uw_memsink_free(&m);
}

static void test_oversized_passthrough(void)
{
    uw_buf_t b; char store[CAP]; uw_memsink_t m; ucisWriterSinkT sink;
    char big[CAP * 3 + 5];
    size_t i;
    with_buf(&b, store, &m, &sink);

    for (i = 0; i < sizeof(big); ++i) {
        big[i] = (char)('a' + (int)(i % 26));
    }
    assert(uw_buf_write(&b, "hi", 2) == UCIS_WRITER_OK);
    assert(uw_buf_write(&b, big, sizeof(big)) == UCIS_WRITER_OK);
    assert(uw_buf_flush(&b) == UCIS_WRITER_OK);
    assert(m.len == 2 + sizeof(big));
    assert(memcmp(m.data, "hi", 2) == 0);
    assert(memcmp(m.data + 2, big, sizeof(big)) == 0);
    uw_memsink_free(&m);
}

static void test_sink_failure_latches(void)
{
    uw_buf_t b; char store[CAP]; uw_memsink_t m; ucisWriterSinkT sink;
    int writes_after;
    with_buf(&b, store, &m, &sink);
    m.fail_after = 0;              /* fail on the first byte offered */

    uw_buf_write(&b, "0123456789abcdefg", 17);   /* forces a flush */
    assert(b.status == UCIS_WRITER_ERR_IO);
    writes_after = m.nwrites;

    /* Once latched, the buffer must go quiet: a writer that keeps calling a
     * failing sink turns one bad write into millions. */
    assert(uw_buf_write(&b, "more", 4) == UCIS_WRITER_ERR_IO);
    assert(uw_buf_putc(&b, 'x') == UCIS_WRITER_ERR_IO);
    assert(uw_buf_u64(&b, 12345) == UCIS_WRITER_ERR_IO);
    assert(uw_buf_flush(&b) == UCIS_WRITER_ERR_IO);
    assert(m.nwrites == writes_after);
    uw_memsink_free(&m);
}

static void test_first_error_wins(void)
{
    uw_buf_t b; char store[CAP]; uw_memsink_t m; ucisWriterSinkT sink;
    with_buf(&b, store, &m, &sink);

    assert(uw_buf_fail(&b, UCIS_WRITER_ERR_ORDER) == UCIS_WRITER_ERR_ORDER);
    assert(uw_buf_fail(&b, UCIS_WRITER_ERR_IO) == UCIS_WRITER_ERR_ORDER);
    uw_memsink_free(&m);
}

static void test_finish_closes_once(void)
{
    uw_buf_t b; char store[CAP]; uw_memsink_t m; ucisWriterSinkT sink;
    with_buf(&b, store, &m, &sink);

    uw_buf_write(&b, "x", 1);
    assert(uw_buf_finish(&b) == UCIS_WRITER_OK);
    assert(m.closed == 1 && m.len == 1);
    m.closed = 0;
    assert(uw_buf_finish(&b) == UCIS_WRITER_OK);
    assert(m.closed == 0);         /* idempotent */
    uw_memsink_free(&m);
}

static void test_bytes_counted(void)
{
    uw_buf_t b; char store[CAP]; uw_memsink_t m; ucisWriterSinkT sink;
    with_buf(&b, store, &m, &sink);

    uw_buf_write(&b, "hello", 5);
    assert(b.bytes_out == 0);      /* counts what the sink has seen */
    uw_buf_flush(&b);
    assert(b.bytes_out == 5);
    uw_memsink_free(&m);
}

int main(void)
{
    test_buffers_until_full();
    test_exact_fill();
    test_oversized_passthrough();
    test_sink_failure_latches();
    test_first_error_wins();
    test_finish_closes_once();
    test_bytes_counted();
    printf("test_uw_buffer: ok\n");
    return 0;
}
