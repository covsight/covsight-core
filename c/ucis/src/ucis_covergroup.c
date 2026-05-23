/*
 * Covergroup family specialized constructors.
 *
 * The plain UCIS_COVERGROUP / UCIS_COVERPOINT scopes are created via the
 * existing ucis_CreateScope path (covergroup → CreateScope(UCIS_COVERGROUP),
 * coverinstance → CreateInstance(UCIS_COVERINSTANCE, du_scope=cvg)), so
 * Phase 2.8 only needs to add the cross specializations - they enforce
 * the cross↔coverpoint arity relationship that UCIS spec section 7 requires.
 */

#include "ucis_internal.h"
#include "ncdb_impl.h"

#include <string.h>

extern ncdbT ucis_internal_get_ncdb(ucisT db);

ucisScopeT ucis_CreateCross(ucisT             db,
                            ucisScopeT        parent,
                            const char       *name,
                            ucisSourceInfoT  *srcinfo,
                            int               weight,
                            ucisSourceT       source,
                            int               num_points,
                            ucisScopeT       *points)
{
    if (!parent || num_points <= 0 || !points) return NULL;
    /* Cross scopes live under a covergroup or coverinstance; the points must
     * be the coverpoints already created in that same parent. We don't
     * enforce kinship here (the spec is silent on what to do if a caller
     * passes a foreign coverpoint), but we do refuse NULL elements. */
    for (int i = 0; i < num_points; i++) if (!points[i]) return NULL;

    ucisScopeT cross = ucis_CreateScope(db, parent, name, srcinfo, weight,
                                        source, UCIS_CROSS,
                                        UCIS_SCOPE_SPECIALIZED);
    if (!cross) return NULL;
    ncdbScopeT ncross = (ncdbScopeT)cross;
    for (int i = 0; i < num_points; i++) {
        if (ncdb_impl_add_cross_point(ncross, (ncdbScopeT)points[i]) != 0)
            return NULL;
    }
    return cross;
}

/* Resolve a coverpoint by name from the cross's parent scope's children. */
static ncdbScopeT find_sibling_coverpoint(ncdbScopeT parent, const char *name)
{
    if (!parent || !name) return NULL;
    for (size_t i = 0; i < parent->child_count; i++) {
        ncdbScopeT c = parent->children[i];
        if (c->type == UCIS_COVERPOINT && c->name && strcmp(c->name, name) == 0)
            return c;
    }
    return NULL;
}

ucisScopeT ucis_CreateCrossByName(ucisT             db,
                                  ucisScopeT        parent,
                                  const char       *name,
                                  ucisSourceInfoT  *srcinfo,
                                  int               weight,
                                  ucisSourceT       source,
                                  int               num_points,
                                  char            **point_names)
{
    if (!parent || num_points <= 0 || !point_names) return NULL;
    ncdbScopeT nparent = (ncdbScopeT)parent;

    /* Stage the resolved pointers on the stack first; if any lookup fails we
     * bail before mutating the database. */
    ucisScopeT stack_pts[16];
    ucisScopeT *pts = stack_pts;
    ucisScopeT *heap = NULL;
    if (num_points > 16) {
        heap = (ucisScopeT*)malloc(sizeof(*heap) * (size_t)num_points);
        if (!heap) return NULL;
        pts = heap;
    }
    for (int i = 0; i < num_points; i++) {
        pts[i] = (ucisScopeT)find_sibling_coverpoint(nparent, point_names[i]);
        if (!pts[i]) { free(heap); return NULL; }
    }

    ucisScopeT cross = ucis_CreateCross(db, parent, name, srcinfo, weight,
                                        source, num_points, pts);
    free(heap);
    return cross;
}
