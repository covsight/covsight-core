/*
 * Coveritem creation, count increment, and flag manipulation.
 *
 * UCIS bin-type values (UCIS_STMTBIN, UCIS_TOGGLEBIN, UCIS_CVGBIN, ...) are
 * byte-identical to NCDB_COVER_* one-hot bits, so dispatch is just a pass-
 * through of data->type. Per-cover UCIS metadata (weight/goal/source/at_least)
 * is conditional on the presence flags the UCIS spec defines on the data
 * struct.
 */

#include "ucis_internal.h"
#include "ncdb_impl.h"

#include <stdint.h>
#include <string.h>

extern ncdbT ucis_internal_get_ncdb(ucisT db);

static uint64_t initial_count(const ucisCoverDataT *d)
{
    if (!d) return 0;
    /* UCIS spec: data.int64 valid iff UCIS_IS_64BIT, data.int32 iff
     * UCIS_IS_32BIT; vector bins don't carry a scalar count. */
    if (d->flags & UCIS_IS_64BIT) return (uint64_t)d->data.int64;
    if (d->flags & UCIS_IS_32BIT) return (uint64_t)d->data.int32;
    return 0;
}

int ucis_CreateNextCover(ucisT             db,
                         ucisScopeT        parent,
                         const char       *name,
                         ucisCoverDataT   *data,
                         ucisSourceInfoT  *sourceinfo)
{
    ncdbT core = ucis_internal_get_ncdb(db);
    if (!core || !parent || !data) return -1;

    ncdbScopeT scope = (ncdbScopeT)parent;
    ncdbCoverT c = ncdb_impl_create_cover(core, scope,
                                          (uint64_t)data->type,
                                          name,
                                          initial_count(data));
    if (!c) return -1;

    /* The spec's default flags differ from NCDB's per-type defaults; respect
     * what the caller passed in verbatim. */
    c->flags = data->flags;

    if (data->flags & UCIS_HAS_GOAL)   ncdb_SetCoverGoal  (core, c, (int64_t)data->goal);
    if (data->flags & UCIS_HAS_WEIGHT) ncdb_SetCoverWeight(core, c, (uint64_t)data->weight);
    if (data->flags & UCIS_HAS_LIMIT)  c->at_least = (uint64_t)data->limit;

    if (sourceinfo && sourceinfo->filehandle) {
        const ucis_file_handle_t *fh =
            (const ucis_file_handle_t*)sourceinfo->filehandle;
        ncdb_SetCoverSource(core, c, fh->filename,
                            (uint64_t)sourceinfo->line,
                            (uint64_t)sourceinfo->token);
    }

    /* Phase 4.1 / M1: return the *logical* index (flat view across
     * parent + R7 synthetic children) so the caller can pass it back to
     * IncrementCover / SetCoverFlag transparently regardless of whether
     * the writer reparented the cover into a synthetic child. */
    return (int)(ncdb_scope_logical_cover_count(scope) - 1U);
}

int ucis_IncrementCover(ucisT       db,
                        ucisScopeT  parent,
                        int         coverindex,
                        int64_t     increment)
{
    ncdbCoverT c;
    (void)db;
    if (!parent || coverindex < 0) return -1;
    c = ncdb_scope_logical_cover_at((ncdbScopeT)parent, (size_t)coverindex);
    if (!c) return -1;
    /* UCIS spec 8.4.11: increment is undefined for vector coveritems
     * (they don't carry a scalar count). Reject explicitly. */
    if (c->flags & UCIS_IS_VECTOR) return -1;
    c->count += (uint64_t)increment;
    return 0;
}

void ucis_SetCoverFlag(ucisT       db,
                       ucisScopeT  parent,
                       int         coverindex,
                       ucisFlagsT  mask,
                       int         bitvalue)
{
    ncdbCoverT c;
    (void)db;
    if (!parent || coverindex < 0) return;
    c = ncdb_scope_logical_cover_at((ncdbScopeT)parent, (size_t)coverindex);
    if (!c) return;
    if (bitvalue) c->flags |=  (uint32_t)mask;
    else          c->flags &= ~(uint32_t)mask;
}

int ucis_GetCoverSourceInfo(ucisT             db,
                            ucisScopeT        parent,
                            int               coverindex,
                            ucisSourceInfoT  *si)
{
    ucis_ctx_t *ctx = ucis_ctx_of(db);
    ncdbT core = ucis_internal_get_ncdb(db);
    ncdbCoverT c;
    const char *path;
    if (!ctx || !core || !parent || coverindex < 0 || !si) return -1;
    c = ncdb_scope_logical_cover_at((ncdbScopeT)parent, (size_t)coverindex);
    if (!c) return -1;
    path = ncdb_GetCoverSourcePath(core, c);
    if (!path || !path[0]) return -1;
    si->filehandle = (ucisFileHandleT)ucis_internal_handle_for_path(ctx, path);
    si->line  = (int)ncdb_GetCoverSourceLine (core, c);
    si->token = (int)ncdb_GetCoverSourceToken(core, c);
    return si->filehandle ? 0 : -1;
}
