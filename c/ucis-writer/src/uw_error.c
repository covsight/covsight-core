/* uw_error.c - handle validation, sticky error state, diagnostics.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Work item 1.7 of docs/ucis-writer-impl-plan.md.
 *
 * Nothing in this library aborts, longjmps, or writes to stderr on its own.
 * A coverage writer is linked into somebody else's simulation; the worst thing
 * it can do is take that simulation down over a bad file path. Errors latch on
 * the database and ucis_Close reports the first one.
 *
 * Two classes, kept apart (see uw_db_t::err): a caller mistake drops that one
 * call and the document keeps being written, because the rest of the data is
 * still good and still valid. Only an I/O or allocation failure stops output,
 * because after one of those there is nowhere for output to go. */

#include "uw_types.h"

#include <stdlib.h>
#include <string.h>

UW_INTERNAL uw_db_t* uw_db_check(ucisT db)
{
    uw_db_t* d = (uw_db_t*)db;
    if (d == NULL || d->magic != UW_DB_MAGIC) {
        return NULL;
    }
    return d;
}

static void uw_ctx_set(uw_db_t* db, const char* what, const char* detail)
{
    size_t n = 0;
    size_t cap = sizeof(db->errctx) - 1;

    if (what == NULL) {
        what = "error";
    }
    n = strlen(what);
    if (n > cap) {
        n = cap;
    }
    memcpy(db->errctx, what, n);

    if (detail != NULL && n + 2 < cap) {
        size_t dn;
        memcpy(db->errctx + n, ": ", 2);
        n += 2;
        dn = strlen(detail);
        if (dn > cap - n) {
            dn = cap - n;
        }
        memcpy(db->errctx + n, detail, dn);
        n += dn;
    }
    db->errctx[n] = '\0';
}

/* UCIS 1.0 provides a process-wide error handler. Ours is advisory: the
 * database, not the callback, is the authoritative record of what went wrong,
 * because a per-process hook cannot say which of several open databases
 * failed. Registering one is still useful for getting a message to a log. */
static ucis_ErrorHandler uw_error_handler  = NULL;
static void*             uw_error_userdata = NULL;

UW_INTERNAL int uw_fail(uw_db_t* db, int status, const char* what,
                        const char* detail)
{
    int first = (db->err == UCIS_WRITER_OK);
    if (first) {
        uw_ctx_set(db, what, detail);
        db->err = status;
    }
    /* Only failures that make output impossible stop output. */
    if (status == UCIS_WRITER_ERR_IO || status == UCIS_WRITER_ERR_ALLOC) {
        uw_buf_fail(&db->buf, status);
    }
    if (first && uw_error_handler != NULL) {
        ucisErrorT err;
        err.msgno    = status;
        err.severity = UCIS_MSG_ERROR;
        err.msgstr   = db->errctx;
        uw_error_handler(uw_error_userdata, &err);
    }
    return status;
}

UW_INTERNAL char* uw_strdup(uw_db_t* db, const char* s)
{
    size_t n;
    char*  p;
    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    p = (char*)malloc(n);
    if (p == NULL) {
        uw_fail(db, UCIS_WRITER_ERR_ALLOC, "out of memory", NULL);
        return NULL;
    }
    memcpy(p, s, n);
    return p;
}

/* ---- public error surface -------------------------------------------- */

const char* ucis_writer_status_name(ucisWriterStatusT status)
{
    switch (status) {
        case UCIS_WRITER_OK:             return "ok";
        case UCIS_WRITER_ERR_ALLOC:      return "out of memory";
        case UCIS_WRITER_ERR_IO:         return "output write failed";
        case UCIS_WRITER_ERR_USAGE:      return "invalid argument";
        case UCIS_WRITER_ERR_ORDER:      return "ordering contract violated";
        case UCIS_WRITER_ERR_SEALED:     return "table already written";
        case UCIS_WRITER_ERR_DEPTH:      return "scope nesting too deep";
        case UCIS_WRITER_ERR_UNBALANCED: return "unbalanced scopes";
        case UCIS_WRITER_ERR_STATE:      return "call not legal in this state";
        default:                         return "unknown error";
    }
}

ucisWriterStatusT ucis_writer_error(ucisT db)
{
    uw_db_t* d = uw_db_check(db);
    if (d == NULL) {
        return UCIS_WRITER_ERR_USAGE;
    }
    return (ucisWriterStatusT)(d->err ? d->err : d->buf.status);
}

const char* ucis_writer_error_string(ucisT db)
{
    uw_db_t* d = uw_db_check(db);
    if (d == NULL) {
        return "not a ucis_writer database handle";
    }
    if (d->err == UCIS_WRITER_OK && d->buf.status == UCIS_WRITER_OK) {
        return "ok";
    }
    if (d->errctx[0] != '\0') {
        return d->errctx;
    }
    return ucis_writer_status_name(
        (ucisWriterStatusT)(d->err ? d->err : d->buf.status));
}

unsigned long ucis_writer_warnings(ucisT db)
{
    uw_db_t* d = uw_db_check(db);
    return d ? d->warnings : 0ul;
}

uint64_t ucis_writer_bytes_written(ucisT db)
{
    uw_db_t* d = uw_db_check(db);
    return d ? d->buf.bytes_out : 0u;
}

int ucis_writer_set_pretty(ucisT db, int enable)
{
    uw_db_t* d = uw_db_check(db);
    if (d == NULL) {
        return -1;
    }
    d->pretty = enable ? 1 : 0;
    return 0;
}

void ucis_RegisterErrorHandler(ucis_ErrorHandler errHandle, void* userdata)
{
    uw_error_handler  = errHandle;
    uw_error_userdata = userdata;
}
