/* uw_xml.h - UCIS construct to XSD element dispatch.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 3 of docs/ucis-writer-impl-plan.md.
 *
 * INSTANCE_COVERAGE's content model is an xsd:sequence, so the coverage kinds
 * inside one instance have a fixed order and each kind appears as a wrapper
 * element around its items. The caller does not open those wrappers -- it
 * creates scopes and coveritems, and this module decides which wrapper they
 * belong in, opening and closing them as the kind changes.
 *
 * That is why the caller must arrive already grouped by (instance, kind):
 * changing kind closes the previous wrapper, and the schema will not let us
 * open it again later. Spike 1.6 confirmed this is cheap for the intended
 * consumers. */

#ifndef UW_XML_H
#define UW_XML_H

#include "uw_types.h"

/* Coverage kinds, numbered by their position in INSTANCE_COVERAGE's
 * xsd:sequence. The numbering *is* the ordering rule. */
enum {
    UW_KIND_NONE     = 0,
    UW_KIND_TOGGLE   = 1,
    UW_KIND_BLOCK    = 2,
    UW_KIND_COND     = 3,
    UW_KIND_BRANCH   = 4,
    UW_KIND_FSM      = 5,
    UW_KIND_ASSERT   = 6,
    UW_KIND_CVG      = 7,
    UW_KIND_USERATTR = 8
};

/* Make the wrapper for `kind` the innermost element, opening it if the
 * current kind differs and closing whatever was open before. Fails with
 * UCIS_WRITER_ERR_ORDER if `kind` has already been passed in this instance. */
UW_INTERNAL int uw_kind_open(uw_db_t* db, unsigned kind);

/* The scalar count carried by a coveritem, honouring UCIS_IS_32BIT /
 * UCIS_IS_64BIT. */
UW_INTERNAL uint64_t uw_cover_count(uw_db_t* db, const ucisCoverDataT* data);

/* Emit `<bin>`/`<blockBin>`/`<branchBin>` with its required <contents>. */
UW_INTERNAL int uw_emit_bin(uw_db_t* db, const char* tag, const char* name,
                            const ucisCoverDataT* data);

/* Emit the objAttributes group (alias/excluded/excludedReason/weight) from a
 * coveritem's flags. Must be called while the element is still pending. */
UW_INTERNAL int uw_emit_obj_attrs(uw_db_t* db, const char* alias,
                                  const ucisCoverDataT* data);

/* blockCoverage/statement -- one statement or line bin. */
UW_INTERNAL int uw_emit_statement(uw_db_t* db, const char* name,
                                  const ucisCoverDataT* data,
                                  const ucisSourceInfoT* srcinfo);

/* branchCoverage/statement -- opens a scope the caller closes. Each arm then
 * arrives as a UCIS_BRANCHBIN coveritem. */
UW_INTERNAL int uw_open_branch_statement(uw_db_t* db, const char* name,
                                         const ucisSourceInfoT* srcinfo);

/* One arm of the enclosing branch statement. */
UW_INTERNAL int uw_emit_branch(uw_db_t* db, const char* name,
                               const ucisCoverDataT* data,
                               const ucisSourceInfoT* srcinfo);

/* toggleCoverage/toggleObject, or a toggleBit inside one. Which it is depends
 * on whether a toggle scope is already open: UCIS models a vector as a toggle
 * scope per bit nested in the object's toggle scope. */
UW_INTERNAL int uw_open_toggle(uw_db_t* db, const char* name,
                               const char* canonical_name,
                               ucisToggleTypeT type, ucisToggleDirT dir);

/* One `<toggle from to>` inside the enclosing toggleBit, synthesising the
 * toggleBit first for a scalar object that has no per-bit scope. */
UW_INTERNAL int uw_emit_toggle_bin(uw_db_t* db, const char* name,
                                   const ucisCoverDataT* data);

/* conditionCoverage/expr, or -- if one is already open -- the input-
 * contribution metric scope inside it, which UCIS-XML does not represent as an
 * element at all. See D11. */
UW_INTERNAL int uw_open_expr(uw_db_t* db, const char* name,
                             const ucisSourceInfoT* srcinfo);

/* One bin of the enclosing expression, tagged with the metric it came from. */
UW_INTERNAL int uw_emit_expr_bin(uw_db_t* db, const char* name,
                                 const ucisCoverDataT* data);

/* Copy at most cap-1 bytes of `src` into `dst`, counting a truncation. */
UW_INTERNAL void uw_copy_bounded(uw_db_t* db, char* dst, size_t cap,
                                 const char* src);

#endif /* UW_XML_H */
