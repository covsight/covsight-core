/* uw_cvg.h - functional coverage: covergroups, coverpoints, crosses.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 4 of docs/ucis-writer-impl-plan.md.
 *
 * UCIS and UCIS-XML disagree about the shape of covergroup coverage in two
 * places, and both are handled here.
 *
 * First, UCIS has a covergroup *type* scope containing coverinstance scopes;
 * UCIS-XML has only cgInstance, which carries the type's name and source
 * location inside its own cgId. So the type scope emits nothing (a virtual
 * scope, the same device D11 uses for the expression metric level) and a
 * covergroup with no per-instance scopes -- type-only coverage, the common
 * case when per_instance is off -- gets one cgInstance synthesised for it.
 *
 * Second, CGINSTANCE, COVERPOINT and CROSS each require an <options> element
 * as their first child, whose attributes arrive as properties after the scope
 * exists. That is the same deferral <id> already needed, so it uses the same
 * mechanism: staged on the element, written when the start tag closes. */

#ifndef UW_CVG_H
#define UW_CVG_H

#include "uw_types.h"

/* Which option element to emit. The three types share a shape but not an
 * attribute list, and an attribute on the wrong one is a validation error. */
enum {
    UW_OPT_NONE       = 0,
    UW_OPT_CGINST     = 1,
    UW_OPT_COVERPOINT = 2,
    UW_OPT_CROSS      = 3
};

/* Ordering stages within a cgInstance. CGINSTANCE's sequence puts every
 * coverpoint before every cross, so a caller that interleaves them produces a
 * document no conforming reader accepts. */
enum {
    UW_CGSTAGE_COVERPOINT = 1,
    UW_CGSTAGE_CROSS      = 2
};

/* Clear the option staging area. Called as each optioned scope opens, so one
 * scope's options cannot leak into the next. */
UW_INTERNAL void uw_opts_reset(uw_db_t* db);

/* Record an option from ucis_SetIntProperty. Returns 0 if `property` is not an
 * option of `variant`, so the caller can fall through to its other cases. */
UW_INTERNAL int uw_opts_set(uw_db_t* db, unsigned variant,
                            ucisIntPropertyEnumT property, int value);

/* Deferred children, emitted by uw_el_commit when the start tag closes:
 * <options/> (plus a cross's crossExpr list) and <cgId>. */
UW_INTERNAL int uw_emit_options(uw_db_t* db, unsigned variant);
UW_INTERNAL int uw_emit_cgid(uw_db_t* db, uw_elem_t* el);

/* covergroupCoverage, then a virtual scope standing for the covergroup type. */
UW_INTERNAL int uw_open_covergroup(uw_db_t* db, const char* name,
                                   const ucisSourceInfoT* srcinfo);

/* One cgInstance of the enclosing covergroup type. */
UW_INTERNAL int uw_open_coverinstance(uw_db_t* db, const char* name,
                                      const ucisSourceInfoT* srcinfo);

UW_INTERNAL int uw_open_coverpoint(uw_db_t* db, const char* name);
UW_INTERNAL int uw_open_cross(uw_db_t* db, const char* name);

/* A UCIS_CVGBIN / IGNOREBIN / ILLEGALBIN / DEFAULTBIN coveritem, routed to
 * coverpointBin or crossBin by the scope it lands in. */
UW_INTERNAL int uw_emit_cvg_bin(uw_db_t* db, const char* name,
                                const ucisCoverDataT* data);

/* Name one of the coverpoints the pending cross crosses
 * (UCIS_STR_ITH_CROSSED_CVP_NAME). Determines both the crossExpr list and
 * which coverpoint each component of a cross bin's name is looked up in. */
UW_INTERNAL int uw_cross_add_cvp(uw_db_t* db, const char* name);

/* Release the per-cgInstance bin-ordinal table. Called when a cgInstance
 * closes: it is the only state in the library proportional to content, and it
 * must not outlive the instance that justified it. */
UW_INTERNAL void uw_cvptab_reset(uw_cvptab_t* t);

#endif /* UW_CVG_H */
