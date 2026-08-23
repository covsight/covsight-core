/* uw_buf.c - output buffer, sink dispatch, integer formatting.
 * SPDX-License-Identifier: Apache-2.0 */

#include "uw_buf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

UW_INTERNAL void uw_buf_init(uw_buf_t* b, char* storage, size_t cap,
                             const ucisWriterSinkT* sink)
{
    b->data      = storage;
    b->cap       = cap;
    b->len       = 0;
    b->status    = UCIS_WRITER_OK;
    b->bytes_out = 0;
    if (sink) {
        b->sink = *sink;
    } else {
        b->sink.write = NULL;
        b->sink.close = NULL;
        b->sink.ctx   = NULL;
    }
}

UW_INTERNAL int uw_buf_fail(uw_buf_t* b, int status)
{
    if (b->status == UCIS_WRITER_OK) {
        b->status = status;
    }
    return b->status;
}

/* Hand `n` bytes straight to the sink, bypassing the buffer. */
static int uw_buf_emit(uw_buf_t* b, const char* s, size_t n)
{
    if (b->status != UCIS_WRITER_OK) {
        return b->status;
    }
    if (n == 0) {
        return UCIS_WRITER_OK;
    }
    if (b->sink.write == NULL) {
        return uw_buf_fail(b, UCIS_WRITER_ERR_STATE);
    }
    if (b->sink.write(b->sink.ctx, s, n) != 0) {
        return uw_buf_fail(b, UCIS_WRITER_ERR_IO);
    }
    b->bytes_out += (uint64_t)n;
    return UCIS_WRITER_OK;
}

UW_INTERNAL int uw_buf_flush(uw_buf_t* b)
{
    size_t n = b->len;
    b->len = 0;               /* drop the bytes even on failure: retrying a
                               * failed sink would double-write on recovery */
    if (b->status != UCIS_WRITER_OK) {
        return b->status;
    }
    return uw_buf_emit(b, b->data, n);
}

UW_INTERNAL int uw_buf_finish(uw_buf_t* b)
{
    int rc = uw_buf_flush(b);
    if (b->sink.close) {
        int (*close_fn)(void*) = b->sink.close;
        void* ctx = b->sink.ctx;
        b->sink.close = NULL;   /* idempotent */
        b->sink.write = NULL;
        if (close_fn(ctx) != 0) {
            rc = uw_buf_fail(b, UCIS_WRITER_ERR_IO);
        }
    } else {
        b->sink.write = NULL;
    }
    return rc;
}

UW_INTERNAL int uw_buf_write(uw_buf_t* b, const char* s, size_t n)
{
    if (b->status != UCIS_WRITER_OK) {
        return b->status;
    }
    if (n == 0) {
        return UCIS_WRITER_OK;
    }
    if (b->len + n <= b->cap) {
        memcpy(b->data + b->len, s, n);
        b->len += n;
        return UCIS_WRITER_OK;
    }
    /* Doesn't fit. Flush what we have, then either buffer or pass through. */
    if (uw_buf_flush(b) != UCIS_WRITER_OK) {
        return b->status;
    }
    if (n <= b->cap) {
        memcpy(b->data, s, n);
        b->len = n;
        return UCIS_WRITER_OK;
    }
    return uw_buf_emit(b, s, n);
}

UW_INTERNAL int uw_buf_putc(uw_buf_t* b, char c)
{
    if (b->status != UCIS_WRITER_OK) {
        return b->status;
    }
    if (b->len == b->cap && uw_buf_flush(b) != UCIS_WRITER_OK) {
        return b->status;
    }
    b->data[b->len++] = c;
    return UCIS_WRITER_OK;
}

UW_INTERNAL int uw_buf_puts(uw_buf_t* b, const char* s)
{
    if (s == NULL) {
        return b->status;
    }
    return uw_buf_write(b, s, strlen(s));
}

/* 20 digits is the widest uint64_t (18446744073709551615). */
#define UW_DIGITS_MAX 20

UW_INTERNAL int uw_buf_u64(uw_buf_t* b, uint64_t v)
{
    char tmp[UW_DIGITS_MAX];
    int  i = UW_DIGITS_MAX;

    if (v == 0) {
        return uw_buf_putc(b, '0');
    }
    while (v != 0) {
        tmp[--i] = (char)('0' + (int)(v % 10u));
        v /= 10u;
    }
    return uw_buf_write(b, tmp + i, (size_t)(UW_DIGITS_MAX - i));
}

UW_INTERNAL int uw_buf_u32(uw_buf_t* b, uint32_t v)
{
    return uw_buf_u64(b, (uint64_t)v);
}

UW_INTERNAL int uw_buf_i64(uw_buf_t* b, int64_t v)
{
    uint64_t mag;
    if (v < 0) {
        if (uw_buf_putc(b, '-') != UCIS_WRITER_OK) {
            return b->status;
        }
        /* Negate in unsigned space: -INT64_MIN overflows int64_t. */
        mag = (uint64_t)(-(v + 1)) + 1u;
    } else {
        mag = (uint64_t)v;
    }
    return uw_buf_u64(b, mag);
}

UW_INTERNAL int uw_buf_double(uw_buf_t* b, double v)
{
    char tmp[40];
    int  n;
    int  i;

    /* Only test-data fields (simtime, cputime, cost) are doubles, so a
     * handful per document; snprintf is affordable here where it is not in
     * the integer path. The round-trip check keeps the common case short. */
    if (!(v == v) || v > 1.0e308 || v < -1.0e308) {  /* NaN or infinity */
        return uw_buf_putc(b, '0');
    }
    n = snprintf(tmp, sizeof(tmp), "%.15g", v);
    if (n < 0 || (size_t)n >= sizeof(tmp)) {
        return uw_buf_putc(b, '0');
    }
    if (strtod(tmp, NULL) != v) {
        n = snprintf(tmp, sizeof(tmp), "%.17g", v);
        if (n < 0 || (size_t)n >= sizeof(tmp)) {
            return uw_buf_putc(b, '0');
        }
    }
    /* snprintf honours LC_NUMERIC; xsd:double does not. */
    for (i = 0; i < n; ++i) {
        if (tmp[i] == ',') {
            tmp[i] = '.';
        }
    }
    return uw_buf_write(b, tmp, (size_t)n);
}
