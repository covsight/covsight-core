/* uw_test_sink.h - in-memory sink shared by the C unit tests.
 * SPDX-License-Identifier: Apache-2.0 */

#ifndef UW_TEST_SINK_H
#define UW_TEST_SINK_H

/* Deliberately the public header, not src/ucis_writer_api.h: this file is
 * shared with the amalgamated-header test, which has no access to src/. */
#include "ucis_writer.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    char*  data;
    size_t len;
    size_t cap;
    long   fail_after;   /* fail once this many bytes have been accepted; <0 never */
    int    closed;
    int    nwrites;
} uw_memsink_t;

static int uw_memsink_write(void* ctx, const char* d, size_t n)
{
    uw_memsink_t* m = (uw_memsink_t*)ctx;
    m->nwrites++;
    if (m->fail_after >= 0 && (long)(m->len + n) > m->fail_after) {
        return -1;
    }
    if (m->len + n + 1 > m->cap) {
        size_t cap = m->cap ? m->cap : 256;
        char*  p;
        while (cap < m->len + n + 1) {
            cap *= 2;
        }
        p = (char*)realloc(m->data, cap);
        if (p == NULL) {
            return -1;
        }
        m->data = p;
        m->cap  = cap;
    }
    memcpy(m->data + m->len, d, n);
    m->len += n;
    m->data[m->len] = '\0';
    return 0;
}

static int uw_memsink_close(void* ctx)
{
    ((uw_memsink_t*)ctx)->closed = 1;
    return 0;
}

static void uw_memsink_init(uw_memsink_t* m, ucisWriterSinkT* sink)
{
    memset(m, 0, sizeof(*m));
    m->fail_after = -1;
    sink->write = uw_memsink_write;
    sink->close = uw_memsink_close;
    sink->ctx   = m;
}

static void uw_memsink_free(uw_memsink_t* m)
{
    free(m->data);
    m->data = NULL;
    m->len = m->cap = 0;
}

#endif /* UW_TEST_SINK_H */
