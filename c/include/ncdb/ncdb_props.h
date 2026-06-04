/*
 * ncdb_props.h — central registry of NCDB typed-property IDs.
 *
 * Phase 4.2 / M2 introduces a typed-property block on scopes and covers
 * (gated by NCDB_PRESENCE_TYPED_PROPS / NCDB_COVER_PRESENCE_TYPED_PROPS).
 * Entries are keyed by the small numeric IDs below — *not* by string
 * key as user attributes are — so per-entry overhead drops from ~25-40 B
 * (string-keyed attribute) to ~3-5 B (numeric-keyed prop).
 *
 * Range policy (per ADR-0002 / D5):
 *   0x0001-0x7FFF   Standard UCIS-derived IDs, managed here
 *   0x8000-0xFFEF   Vendor extensions (each vendor registers a sub-range)
 *   0xFFF0-0xFFFF   Experimental / internal — never serialized publicly
 *
 * **Never reassign an ID.** Deprecated IDs stay listed with a
 * "DEPRECATED" comment marker. Readers must skip unknown IDs cleanly
 * (the type_tag byte tells them how many bytes the value occupies),
 * so older readers never crash on a newer fixture.
 */

#ifndef INCLUDED_NCDB_PROPS_H
#define INCLUDED_NCDB_PROPS_H

#include <stdint.h>

/* --- Value type tags (1 byte per entry in the serialized block) ----- */

#define NCDB_PROP_TYPE_INT32   1U
#define NCDB_PROP_TYPE_INT64   2U
#define NCDB_PROP_TYPE_DOUBLE  3U
#define NCDB_PROP_TYPE_STRING  4U   /* value is a varint string-table idx */

/* --- Range bases ---------------------------------------------------- */

#define NCDB_PROP_STD_BASE          0x0001U
#define NCDB_PROP_VENDOR_BASE       0x8000U
#define NCDB_PROP_EXPERIMENTAL_BASE 0xFFF0U

/* --- Standard IDs (0x0001-0x7FFF) ---------------------------------- *
 *
 * Numbered in order of introduction; do NOT renumber. Comment cites
 * the UCIS enum the ID corresponds to (purely informational — readers
 * compare numeric IDs only). */

/* Code-coverage long-tail properties (scope / cover level). */
#define NCDB_PROP_INT_STMT_INDEX           0x0001U  /* UCIS_INT_STMT_INDEX */
#define NCDB_PROP_INT_BRANCH_COUNT         0x0002U  /* UCIS_INT_BRANCH_COUNT */
#define NCDB_PROP_INT_BRANCH_HAS_ELSE      0x0003U  /* UCIS_INT_BRANCH_HAS_ELSE */
#define NCDB_PROP_INT_BRANCH_ISCASE        0x0004U  /* UCIS_INT_BRANCH_ISCASE */
#define NCDB_PROP_INT_FSM_STATEVAL         0x0005U  /* UCIS_INT_FSM_STATEVAL */

/* Covergroup options (scope level). */
#define NCDB_PROP_INT_CVG_AUTOBINMAX       0x0006U  /* UCIS_INT_CVG_AUTOBINMAX */
#define NCDB_PROP_INT_CVG_DETECTOVERLAP    0x0007U  /* UCIS_INT_CVG_DETECTOVERLAP */
#define NCDB_PROP_INT_CVG_NUMPRINTMISSING  0x0008U  /* UCIS_INT_CVG_NUMPRINTMISSING */
#define NCDB_PROP_INT_CVG_STROBE           0x0009U  /* UCIS_INT_CVG_STROBE */
#define NCDB_PROP_INT_CVG_PERINSTANCE      0x000AU  /* UCIS_INT_CVG_PERINSTANCE */
#define NCDB_PROP_INT_CVG_GETINSTCOV       0x000BU  /* UCIS_INT_CVG_GETINSTCOV */
#define NCDB_PROP_INT_CVG_MERGEINSTANCES   0x000CU  /* UCIS_INT_CVG_MERGEINSTANCES */

/* String long-tail (scope / db level). */
#define NCDB_PROP_STR_GENERIC              0x0040U  /* UCIS_STR_GENERIC */
#define NCDB_PROP_STR_TEST_NAME            0x0041U  /* UCIS_STR_TEST_NAME */
#define NCDB_PROP_STR_TEST_HOSTNAME        0x0042U  /* UCIS_STR_TEST_HOSTNAME */
#define NCDB_PROP_STR_TEST_HOSTOS          0x0043U  /* UCIS_STR_TEST_HOSTOS */
#define NCDB_PROP_STR_DESIGN_VERSION_ID    0x0044U  /* UCIS_STR_DESIGN_VERSION_ID */
#define NCDB_PROP_STR_FSM_STATEVAR         0x0045U  /* UCIS_STR_FSM_STATEVAR */
#define NCDB_PROP_STR_UNIQUE_ID_ALIAS      0x0046U  /* UCIS_STR_UNIQUE_ID_ALIAS */

/* Real long-tail (scope level). */
#define NCDB_PROP_REAL_CVG_INST_AVERAGE    0x0080U  /* UCIS_REAL_CVG_INST_AVERAGE */

/* Next free standard ID: 0x0047 (int/string namespace shares with reals
 * via the type_tag byte; the ID space is one flat range). */

#endif
