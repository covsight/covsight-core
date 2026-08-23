/* uw_buf.h - output buffer, sink dispatch, integer formatting.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Work items 1.1 and 1.2 of docs/ucis-writer-impl-plan.md.
 *
 * The buffer owns the only path to the sink. Every byte of the document goes
 * through it, so this is where the sticky error state lives: once a write
 * fails, `status` latches and all subsequent output is discarded silently.
 * That is deliberate — a coverage writer must not turn an I/O failure into a
 * crash of the simulation it is instrumenting. */

#ifndef UW_BUF_H
#define UW_BUF_H

#include "uw_config.h"

typedef struct uw_buf_s {
    char*            data;      /* not owned; storage supplied by the db      */
    size_t           cap;
    size_t           len;
    ucisWriterSinkT  sink;
    int              status;    /* ucisWriterStatusT, latched                 */
    uint64_t         bytes_out; /* bytes handed to the sink, buffered ones included */
} uw_buf_t;

UW_INTERNAL void uw_buf_init(uw_buf_t* b, char* storage, size_t cap,
                             const ucisWriterSinkT* sink);

/* Latch `status` if nothing is latched yet. Returns the latched status. */
UW_INTERNAL int uw_buf_fail(uw_buf_t* b, int status);

/* Push everything buffered to the sink. */
UW_INTERNAL int uw_buf_flush(uw_buf_t* b);

/* Flush, then close the sink. Idempotent. */
UW_INTERNAL int uw_buf_finish(uw_buf_t* b);

UW_INTERNAL int uw_buf_write(uw_buf_t* b, const char* s, size_t n);
UW_INTERNAL int uw_buf_putc(uw_buf_t* b, char c);
UW_INTERNAL int uw_buf_puts(uw_buf_t* b, const char* s);

/* Decimal formatters. Hand-rolled rather than snprintf: snprintf is locale
 * sensitive, costs a format-string parse per call, and shows up as a
 * measurable fraction of runtime when a document contains tens of millions of
 * integers. */
UW_INTERNAL int uw_buf_u32(uw_buf_t* b, uint32_t v);
UW_INTERNAL int uw_buf_u64(uw_buf_t* b, uint64_t v);
UW_INTERNAL int uw_buf_i64(uw_buf_t* b, int64_t v);

/* Shortest round-tripping decimal form of a finite double; non-finite values
 * are written as 0 (xsd:double accepts INF/NaN but consumers rarely do). */
UW_INTERNAL int uw_buf_double(uw_buf_t* b, double v);

/* Write a string literal without a strlen. */
#define UW_LIT(b, s) uw_buf_write((b), "" s, sizeof(s) - 1)

#endif /* UW_BUF_H */
