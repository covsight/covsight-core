/* uw_stack.h - XML element stack and the ordering stage machine.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Work item 1.5 of docs/ucis-writer-impl-plan.md. */

#ifndef UW_STACK_H
#define UW_STACK_H

#include "uw_types.h"

/* Innermost open element, or NULL at document level. */
UW_INTERNAL uw_elem_t* uw_el_top(uw_db_t* db);

/* Terminate the pending start tag of the innermost element, if any, emitting
 * its deferred <id> child. Called before writing anything that is not an
 * attribute of that element. */
UW_INTERNAL int uw_el_commit(uw_db_t* db);

/* Arrange for `<id file line inlineCount>` to be emitted as the innermost
 * element's first child. The schema types all three xsd:positiveInteger, so
 * zero components are clamped to 1; `warn_on_clamp` says whether that counts
 * as lost information. */
UW_INTERNAL void uw_el_set_id(uw_db_t* db, uint32_t file, uint32_t line,
                              uint32_t inlinecount, int warn_on_clamp);

/* Number of '#'-separated operands currently staged in db->expr_terms; never
 * zero, because EXPR's subExpr is minOccurs="1". */
UW_INTERNAL uint64_t uw_expr_term_count(uw_db_t* db);

/* Emit one <subExpr> per staged operand. */
UW_INTERNAL int uw_emit_sub_exprs(uw_db_t* db);

/* Open `tag` as a child of the current element. `tag` must outlive the
 * element, which in practice means a string literal. */
UW_INTERNAL int uw_el_begin(uw_db_t* db, const char* tag, ucisScopeTypeT type);

/* Push a scope that emits no element.
 *
 * Needed where the UCIS scope tree has a level UCIS-XML does not: an
 * expression's input-contribution metric scope is a real UCIS scope, and the
 * caller will balance it with ucis_WriteStreamScope, but EXPR has no child
 * element for it. A virtual scope keeps the two models in step without
 * inventing markup. Committing the parent first is part of the contract:
 * entering it means the parent's attributes are final. */
UW_INTERNAL int uw_el_begin_virtual(uw_db_t* db, ucisScopeTypeT type);

/* Close the innermost element, collapsing it to `<tag .../>` if it never
 * acquired children. */
UW_INTERNAL int uw_el_end(uw_db_t* db);

/* Close elements until `depth` remain. Used by ucis_Close to report — and
 * then repair — an unbalanced document. */
UW_INTERNAL int uw_el_unwind(uw_db_t* db, int depth);

/* Advance the innermost element's ordering stage.
 *
 * XSD content models are xsd:sequence at every level, so the order in which a
 * caller emits coverage kinds, coverpoints versus crosses, or FSM states
 * versus transitions is not a style question — going backwards produces a
 * document no conforming reader will accept. Rather than buffer to repair it
 * (which would defeat the memory guarantee), we detect it. `what` names the
 * construct for the diagnostic. */
UW_INTERNAL int uw_stage(uw_db_t* db, unsigned stage, const char* what);

#endif /* UW_STACK_H */
