/*
 * Scope creation + scope-level getters/setters.
 *
 * NCDB's scope/source/toggle enum values are byte-identical to UCIS, so most
 * of this is a thin forward to the in-tree impl helpers. The shim does the
 * extra work UCIS specializes for: DU↔instance linking, toggle metadata,
 * source-info plumbing through ucisFileHandleT, and direct writes into scope
 * fields that NCDB doesn't yet expose publicly (flags, source_type).
 */

#include "ucis_internal.h"
#include "ncdb_impl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern ncdbT      ucis_internal_get_ncdb(ucisT db);

static void apply_srcinfo(ncdbT core, ncdbScopeT scope, ucisSourceInfoT *si)
{
    if (!si || !si->filehandle) return;
    const ucis_file_handle_t *fh = (const ucis_file_handle_t*)si->filehandle;
    ncdb_SetScopeSourceInfo(core, scope, fh->filename,
                            (uint64_t)si->line, (uint64_t)si->token);
}

ucisScopeT ucis_CreateScope(ucisT             db,
                            ucisScopeT        parent,
                            const char       *name,
                            ucisSourceInfoT  *srcinfo,
                            int               weight,
                            ucisSourceT       source,
                            ucisScopeTypeT    type,
                            ucisFlagsT        flags)
{
    ncdbT core = ucis_internal_get_ncdb(db);
    if (!core || !name) return NULL;

    ncdbScopeT s = ncdb_impl_create_scope(core, (ncdbScopeT)parent,
                                          (uint64_t)type, name);
    if (!s) return NULL;
    if (weight >= 0) s->weight = (uint64_t)weight;
    s->source_type = (int)source;
    s->flags       = (uint64_t)flags;
    apply_srcinfo(core, s, srcinfo);
    return (ucisScopeT)s;
}

ucisScopeT ucis_CreateInstance(ucisT             db,
                               ucisScopeT        parent,
                               const char       *name,
                               ucisSourceInfoT  *srcinfo,
                               int               weight,
                               ucisSourceT       source,
                               ucisScopeTypeT    type,
                               ucisScopeT        du_scope,
                               ucisFlagsT        flags)
{
    /* Spec specialization: UCIS_INSTANCE → du_scope must be a UCIS_DU_*;
     * UCIS_COVERINSTANCE → du_scope must be a UCIS_COVERGROUP. We accept
     * either pairing and surface the SPECIALIZED flag the spec mandates. */
    if (!du_scope) return NULL;
    ucisScopeT s = ucis_CreateScope(db, parent, name, srcinfo, weight,
                                    source, type, flags | UCIS_SCOPE_SPECIALIZED);
    if (!s) return NULL;
    ncdb_SetScopeDU(ucis_internal_get_ncdb(db), (ncdbScopeT)s, (ncdbScopeT)du_scope);
    return s;
}

/* Match a DU by name within an entire scope tree (writer-side helper). */
typedef struct { const char *name; ncdbScopeT match; } du_search_t;

static int find_du_cb(ncdbT db, ncdbScopeT s, void *ud)
{
    du_search_t *q = (du_search_t*)ud;
    if (q->match) return 0;
    uint32_t t = ncdb_GetScopeType(db, s);
    if ((t & (uint32_t)UCIS_DU_ANY)) {
        const char *n = ncdb_GetScopeName(db, s);
        if (n && strcmp(n, q->name) == 0) { q->match = s; return 0; }
    }
    ncdb_ScopeIterate(db, s, 0xFFFFFFFFU, find_du_cb, ud);
    return 0;
}

ucisScopeT ucis_MatchDU(ucisT db, const char *name)
{
    ncdbT core = ucis_internal_get_ncdb(db);
    if (!core || !name) return NULL;
    /* Phase 4.7 / M7 — when the scope_tree trailer is loaded, bsearch
     * the sorted DU-name index in O(log n_du). Otherwise fall back to
     * the recursive O(n) scan (used for in-memory DBs or v3 fixtures). */
    {
        ncdbScopeT hit = ncdb_match_du_by_name(core, name);
        if (hit) return (ucisScopeT)hit;
    }
    {
        du_search_t q = { name, NULL };
        ncdb_ScopeIterate(core, NULL, 0xFFFFFFFFU, find_du_cb, &q);
        return (ucisScopeT)q.match;
    }
}

ucisScopeT ucis_CreateInstanceByName(ucisT             db,
                                     ucisScopeT        parent,
                                     const char       *name,
                                     ucisSourceInfoT  *srcinfo,
                                     int               weight,
                                     ucisSourceT       source,
                                     ucisScopeTypeT    type,
                                     char             *du_name,
                                     int               flags)
{
    ucisScopeT du = ucis_MatchDU(db, du_name);
    if (!du) return NULL;
    return ucis_CreateInstance(db, parent, name, srcinfo, weight,
                               source, type, du, (ucisFlagsT)flags);
}

ucisScopeT ucis_CreateToggle(ucisT              db,
                             ucisScopeT         parent,
                             const char        *name,
                             const char        *canonical_name,
                             ucisFlagsT         flags,
                             ucisToggleMetricT  toggle_metric,
                             ucisToggleTypeT    toggle_type,
                             ucisToggleDirT     toggle_dir)
{
    ucisScopeT s = ucis_CreateScope(db, parent, name, NULL, /*weight*/-1,
                                    UCIS_NONE, UCIS_TOGGLE,
                                    flags | UCIS_SCOPE_SPECIALIZED);
    if (!s) return NULL;
    ncdbT core = ucis_internal_get_ncdb(db);
    if (canonical_name)
        ncdb_SetToggleCanonicalName(core, (ncdbScopeT)s, canonical_name);
    ncdb_SetToggleMetric(core, (ncdbScopeT)s, (uint8_t)toggle_metric);
    ncdb_SetToggleType  (core, (ncdbScopeT)s, (uint8_t)toggle_type);
    ncdb_SetToggleDir   (core, (ncdbScopeT)s, (uint8_t)toggle_dir);
    return s;
}

/* -- Scope getters/setters --------------------------------------------- */

ucisScopeTypeT ucis_GetScopeType(ucisT db, ucisScopeT scope)
{
    ncdbT core = ucis_internal_get_ncdb(db);
    if (!core || !scope) return UCIS_SCOPE_ERROR;
    return (ucisScopeTypeT)ncdb_GetScopeType(core, (ncdbScopeT)scope);
}

ucisObjTypeT ucis_GetObjType(ucisT db, ucisObjT obj)
{
    /* For the writer surface we only ever pass scopes through this entry;
     * the history-vs-scope disambiguation is added in Phase 6. */
    return ucis_GetScopeType(db, (ucisScopeT)obj);
}

ucisFlagsT ucis_GetScopeFlags(ucisT db, ucisScopeT scope)
{
    (void)db;
    return scope ? (ucisFlagsT)((ncdbScopeT)scope)->flags : 0;
}

void ucis_SetScopeFlags(ucisT db, ucisScopeT scope, ucisFlagsT flags)
{
    (void)db;
    if (scope) ((ncdbScopeT)scope)->flags = (uint64_t)flags;
}

void ucis_SetScopeFlag(ucisT db, ucisScopeT scope, ucisFlagsT mask, int bitvalue)
{
    (void)db;
    if (!scope) return;
    if (bitvalue) ((ncdbScopeT)scope)->flags |= (uint64_t)mask;
    else          ((ncdbScopeT)scope)->flags &= ~(uint64_t)mask;
}

int ucis_SetScopeSourceInfo(ucisT db, ucisScopeT scope, ucisSourceInfoT *si)
{
    ncdbT core = ucis_internal_get_ncdb(db);
    if (!core || !scope || !si || !si->filehandle) return -1;
    const ucis_file_handle_t *fh = (const ucis_file_handle_t*)si->filehandle;
    return ncdb_SetScopeSourceInfo(core, (ncdbScopeT)scope, fh->filename,
                                   (uint64_t)si->line, (uint64_t)si->token);
}

int ucis_GetScopeSourceInfo(ucisT db, ucisScopeT scope, ucisSourceInfoT *si)
{
    ucis_ctx_t *ctx = ucis_ctx_of(db);
    ncdbT core = ucis_internal_get_ncdb(db);
    if (!ctx || !core || !scope || !si) return -1;
    const char *path = ncdb_GetScopeSourcePath(core, (ncdbScopeT)scope);
    if (!path || !path[0]) return -1;
    si->filehandle = (ucisFileHandleT)ucis_internal_handle_for_path(ctx, path);
    si->line  = (int)ncdb_GetScopeSourceLine (core, (ncdbScopeT)scope);
    si->token = (int)ncdb_GetScopeSourceToken(core, (ncdbScopeT)scope);
    return si->filehandle ? 0 : -1;
}

/* -- DU name compose/parse --------------------------------------------- *
 *
 * The composed form is "library.primary(secondary)#instance" where any
 * empty component is dropped. The spec promises one static buffer per
 * function; we honour that with a thread-local static.
 */

static char g_compose_buf[1024];

const char *ucis_ComposeDUName(const char *library_name,
                               const char *primary_name,
                               const char *secondary_name)
{
    const char *lib = library_name ? library_name : "";
    const char *pri = primary_name ? primary_name : "";
    const char *sec = secondary_name ? secondary_name : "";
    if (sec[0])
        snprintf(g_compose_buf, sizeof(g_compose_buf), "%s.%s(%s)", lib, pri, sec);
    else if (lib[0])
        snprintf(g_compose_buf, sizeof(g_compose_buf), "%s.%s", lib, pri);
    else
        snprintf(g_compose_buf, sizeof(g_compose_buf), "%s", pri);
    return g_compose_buf;
}

void ucis_ParseDUName(const char  *du_name,
                      const char **library_name,
                      const char **primary_name,
                      const char **secondary_name)
{
    /* Parse into a private rolling buffer; one parse outstanding at a time
     * per the spec. The three return pointers borrow into that buffer. */
    static char  buf[1024];
    static char *lib_p, *pri_p, *sec_p;
    if (!du_name) { du_name = ""; }
    strncpy(buf, du_name, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    lib_p = pri_p = sec_p = (char*)"";
    char *dot   = strchr(buf, '.');
    char *paren = strchr(buf, '(');
    char *close = paren ? strchr(paren, ')') : NULL;

    if (dot) { *dot = '\0'; lib_p = buf; pri_p = dot + 1; }
    else     { pri_p = buf; }
    if (paren) { *paren = '\0'; sec_p = paren + 1; }
    if (close) *close = '\0';

    if (library_name)   *library_name   = lib_p;
    if (primary_name)   *primary_name   = pri_p;
    if (secondary_name) *secondary_name = sec_p;
}

/* Phase 4.3 / M3 — UCIS §5.5 absolute-UID scope match.
 *
 * Two-tiered lookup:
 *   1. If the database carries a loaded NUID index (built when the
 *      archive was written), hash the input UID and bsearch. Resolve
 *      the candidate DFS index back to a scope, recompute its UID and
 *      strcmp to verify (collision-safe). O(log n).
 *   2. Otherwise, walk every scope in DFS order, recompute each UID,
 *      and strcmp until found. O(n). Used for in-memory DBs that have
 *      never been written, where there's no index on disk yet.
 *
 * The `scope` argument is reserved for relative-UID lookups (UCIS
 * §5.5.1 — "If a relative form Unique ID is supplied, matching starts
 * at scope"). Not yet implemented; callers should pass NULL for now. */
ucisScopeT ucis_MatchScopeByUniqueID(ucisT db, ucisScopeT scope,
                                     const char* uniqueID)
{
    ncdbT core;
    char uid_buf[1024];
    int  uid_len;
    (void)scope;
    if (!db || !uniqueID) return NULL;
    core = ucis_internal_get_ncdb(db);
    if (!core) return NULL;

    /* Fast path: hashed index. */
    if (core->nuid_idx) {
        uint64_t hash = ncdb_xxh64(uniqueID, strlen(uniqueID), 0);
        size_t dfs   = ncdb_unique_id_index_lookup(core->nuid_idx, hash);
        if (dfs != (size_t)-1) {
            ncdbScopeT cand = ncdb_impl_scope_by_dfs_index(core, dfs);
            if (cand) {
                uid_len = ncdb_compute_unique_id(core, cand, uid_buf, sizeof(uid_buf));
                if (uid_len > 0 && strcmp(uid_buf, uniqueID) == 0) {
                    return (ucisScopeT)cand;
                }
            }
        }
        return NULL;
    }

    /* Slow path: O(n) DFS scan. Only reached when the DB has no NUID
     * index loaded — typically an in-memory DB. */
    {
        ncdbScopeT *flat = NULL;
        size_t flat_count = 0, i;
        ucisScopeT result = NULL;
        if (ncdb_impl_dfs_flatten(core, &flat, &flat_count) != 0) return NULL;
        for (i = 0; i < flat_count; i++) {
            uid_len = ncdb_compute_unique_id(core, flat[i], uid_buf, sizeof(uid_buf));
            if (uid_len > 0 && strcmp(uid_buf, uniqueID) == 0) {
                result = (ucisScopeT)flat[i];
                break;
            }
        }
        free(flat);
        return result;
    }
}
