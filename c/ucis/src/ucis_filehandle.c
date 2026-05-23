/*
 * File-handle creation and lookup. The shim owns the (path, workdir) strings
 * for the lifetime of the database; ucis_CreateSrcFileHandle additionally
 * threads the path through NCDB's per-DU file table (UCIS D1 / Phase 1.8).
 */

#include "ucis_internal.h"

#include <stdlib.h>
#include <string.h>

static char *ucis_strdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static ucis_file_handle_t *new_handle(ucis_ctx_t *ctx,
                                      const char *filename,
                                      const char *fileworkdir,
                                      ncdbScopeT  du_scope,
                                      int         du_local_index)
{
    ucis_file_handle_t *fh = (ucis_file_handle_t*)calloc(1, sizeof(*fh));
    if (!fh) return NULL;
    fh->filename        = ucis_strdup(filename);
    fh->fileworkdir     = ucis_strdup(fileworkdir);
    fh->du_scope        = du_scope;
    fh->du_local_index  = du_local_index;
    if (filename && !fh->filename) {
        free(fh->fileworkdir);
        free(fh);
        return NULL;
    }
    if (fileworkdir && !fh->fileworkdir) {
        free(fh->filename);
        free(fh);
        return NULL;
    }
    fh->next = ctx->file_handles;
    ctx->file_handles = fh;
    return fh;
}

ucisFileHandleT ucis_CreateFileHandle(ucisT       db,
                                      const char *filename,
                                      const char *fileworkdir)
{
    ucis_ctx_t *ctx = ucis_ctx_of(db);
    if (!ctx || !filename) return NULL;
    return (ucisFileHandleT)new_handle(ctx, filename, fileworkdir, NULL, -1);
}

ucisFileHandleT ucis_CreateSrcFileHandle(ucisT       db,
                                         ucisScopeT  du_scope,
                                         const char *filename,
                                         const char *fileworkdir)
{
    ucis_ctx_t *ctx = ucis_ctx_of(db);
    if (!ctx || !du_scope || !filename) return NULL;

    /* UCIS file IDs are local to each DU (D1). Append into the per-DU file
     * table here; the returned du_local_index will be the file's identity
     * when source-info is later serialized. */
    int idx = ncdb_DuAddFile(ctx->ncdb, (ncdbScopeT)du_scope, filename);
    if (idx < 0) return NULL;

    return (ucisFileHandleT)new_handle(ctx, filename, fileworkdir,
                                       (ncdbScopeT)du_scope, idx);
}

const char *ucis_GetFileName(ucisT db, ucisFileHandleT filehandle)
{
    (void)db;
    if (!filehandle) return NULL;
    return ((ucis_file_handle_t*)filehandle)->filename;
}

ucis_file_handle_t *ucis_internal_handle_for_path(ucis_ctx_t *ctx, const char *path)
{
    if (!ctx || !path) return NULL;
    for (ucis_file_handle_t *fh = ctx->file_handles; fh; fh = fh->next) {
        if (fh->filename && strcmp(fh->filename, path) == 0) return fh;
    }
    return new_handle(ctx, path, NULL, NULL, -1);
}
