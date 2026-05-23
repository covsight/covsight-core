/*
 * UCIS lifecycle: open/close/write + write-streaming + error handler +
 * path separator. Backed by the in-tree NCDB writer.
 */

#include "ucis_internal.h"

#include <stdlib.h>
#include <string.h>

/* Spec says error handler is global, not per-db. */
static ucis_ErrorHandler g_err_handler = NULL;
static void             *g_err_userdata = NULL;

static char *ucis_strdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static void free_file_handles(ucis_ctx_t *ctx)
{
    ucis_file_handle_t *fh = ctx->file_handles;
    while (fh) {
        ucis_file_handle_t *next = fh->next;
        free(fh->filename);
        free(fh->fileworkdir);
        free(fh);
        fh = next;
    }
    ctx->file_handles = NULL;
}

void ucis_RegisterErrorHandler(ucis_ErrorHandler errHandle, void *userdata)
{
    g_err_handler  = errHandle;
    g_err_userdata = userdata;
}

/* Internal accessor: unwrap the underlying ncdb handle. Used by other shim
 * TUs (and by in-tree tests until the public property API lands in 2.5). */
ncdbT ucis_internal_get_ncdb(ucisT db)
{
    ucis_ctx_t *ctx = ucis_ctx_of(db);
    return ctx ? ctx->ncdb : NULL;
}

/* Internal accessor used by other shim TUs to surface ncdb errors. */
void ucis_internal_report_error(int msgno, ucisMsgSeverityT severity, const char *msg)
{
    if (!g_err_handler) return;
    ucisErrorT e;
    e.msgno    = msgno;
    e.severity = severity;
    e.msgstr   = msg ? msg : "";
    g_err_handler(g_err_userdata, &e);
}

ucisT ucis_Open(const char *name)
{
    ucis_ctx_t *ctx = (ucis_ctx_t*)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    ctx->mode = UCIS_MODE_INMEM;
    ctx->ncdb = ncdb_Open(name);   /* ncdb_Open(NULL) → empty in-mem db    */
    if (!ctx->ncdb) {
        free(ctx);
        return NULL;
    }
    return (ucisT)ctx;
}

ucisT ucis_OpenWriteStream(const char *name)
{
    /* Implementation: buffer everything in-memory and flush on Close.
     * This matches the spec contract (ucis_OpenWriteStream returns a
     * restricted handle, and ucis_WriteStream/Close finalize the file) and
     * lets the writer subset share its code path with in-memory mode. */
    ucis_ctx_t *ctx = (ucis_ctx_t*)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    ctx->mode = UCIS_MODE_WRITESTREAM;
    ctx->ncdb = ncdb_Open(NULL);
    if (!ctx->ncdb) { free(ctx); return NULL; }
    ctx->stream_path = ucis_strdup(name);
    if (name && !ctx->stream_path) {
        ncdb_Close(ctx->ncdb);
        free(ctx);
        return NULL;
    }
    return (ucisT)ctx;
}

int ucis_WriteStream(ucisT db)
{
    /* In-memory implementation: no incremental flush needed. */
    ucis_ctx_t *ctx = ucis_ctx_of(db);
    if (!ctx || ctx->mode != UCIS_MODE_WRITESTREAM) return -1;
    return 0;
}

int ucis_WriteStreamScope(ucisT db)
{
    /* Scope popping is a no-op in the in-memory model; scope ownership is
     * tracked by the application via the returned ucisScopeT handles. */
    ucis_ctx_t *ctx = ucis_ctx_of(db);
    if (!ctx || ctx->mode != UCIS_MODE_WRITESTREAM) return -1;
    return 0;
}

int ucis_Write(ucisT       db,
               const char *file,
               ucisScopeT  scope,
               int         recurse,
               int         covertype)
{
    ucis_ctx_t *ctx = ucis_ctx_of(db);
    if (!ctx || !ctx->ncdb || !file) return -1;
    /* Phase 2.2: full-db write only. The (scope, recurse, covertype) subset
     * filter is wired up in Phase 6 alongside the read API. */
    (void)scope; (void)recurse; (void)covertype;
    return ncdb_Write(ctx->ncdb, file);
}

int ucis_Close(ucisT db)
{
    ucis_ctx_t *ctx = ucis_ctx_of(db);
    if (!ctx) return -1;
    int rc = 0;
    if (ctx->mode == UCIS_MODE_WRITESTREAM && ctx->stream_path && ctx->ncdb) {
        rc = ncdb_Write(ctx->ncdb, ctx->stream_path);
    }
    free_file_handles(ctx);
    if (ctx->ncdb) ncdb_Close(ctx->ncdb);
    free(ctx->stream_path);
    free(ctx);
    return rc;
}

int ucis_SetPathSeparator(ucisT db, char separator)
{
    /* NCDB's path separator is a string; UCIS hands us a single char. The
     * underlying setter isn't exposed in the public ncdb header, so for now
     * we only accept the default '/' that NCDB initializes with. */
    ucis_ctx_t *ctx = ucis_ctx_of(db);
    if (!ctx) return -1;
    if (separator != '/') return -1;
    return 0;
}

char ucis_GetPathSeparator(ucisT db)
{
    (void)db;
    return '/';
}
