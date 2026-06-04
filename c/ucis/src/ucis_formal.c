/*
 * ucis_formal.c — UCIS §8.19 formal-verification API (Phase 4.5 / M5).
 *
 * Routes the scope-level formal setters/getters straight through to
 * NCDB's per-scope formal record. Per-cover formal data and the
 * formal-environment surface (assumptions, coverage context, etc.)
 * are deferred to a follow-up.
 */

#include "ucis.h"
#include "ucis_internal.h"
#include "ncdb_impl.h"

extern ncdbT ucis_internal_get_ncdb(ucisT db);

int ucis_SetFormalStatus(ucisT db, ucisScopeT scope, ucisFormalStatusT s)
{
    ncdbT core = ucis_internal_get_ncdb(db);
    if (!core || !scope) return -1;
    return ncdb_formal_set_status(core, (ncdbScopeT)scope, (uint8_t)s);
}

ucisFormalStatusT ucis_GetFormalStatus(ucisT db, ucisScopeT scope)
{
    ncdbT core = ucis_internal_get_ncdb(db);
    int v;
    if (!core || !scope) return UCIS_FORMAL_NONE;
    v = ncdb_formal_get_status(core, (ncdbScopeT)scope);
    return (v < 0) ? UCIS_FORMAL_NONE : (ucisFormalStatusT)v;
}

int ucis_SetFormalRadius(ucisT db, ucisScopeT scope, int64_t radius)
{
    ncdbT core = ucis_internal_get_ncdb(db);
    if (!core || !scope) return -1;
    return ncdb_formal_set_radius(core, (ncdbScopeT)scope, radius);
}

int64_t ucis_GetFormalRadius(ucisT db, ucisScopeT scope)
{
    ncdbT core = ucis_internal_get_ncdb(db);
    if (!core || !scope) return INT64_MIN;
    return ncdb_formal_get_radius(core, (ncdbScopeT)scope);
}

int ucis_SetFormalWitness(ucisT db, ucisScopeT scope, const char *witness)
{
    ncdbT core = ucis_internal_get_ncdb(db);
    if (!core || !scope) return -1;
    return ncdb_formal_set_witness(core, (ncdbScopeT)scope, witness);
}

const char *ucis_GetFormalWitness(ucisT db, ucisScopeT scope)
{
    ncdbT core = ucis_internal_get_ncdb(db);
    if (!core || !scope) return NULL;
    return ncdb_formal_get_witness(core, (ncdbScopeT)scope);
}
