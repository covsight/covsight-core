/* uw_tables.h - the two resident tables: source files and history nodes.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Work items 2.1 - 2.4 and 2.6 of docs/ucis-writer-impl-plan.md.
 *
 * UCIS 1.0 3.3 makes the source-file table and the history nodes available at
 * all times, including under streaming, and 8.1.1 orders them ahead of every
 * scope. The XSD agrees: sourceFiles+ then historyNodes+ then
 * instanceCoverages+. So these two tables, and only these two, are held in
 * memory until the first instance forces them out. Both are proportional to
 * the design's file count and test count, not to its coverage-point count,
 * which is what makes the memory guarantee hold. */

#ifndef UW_TABLES_H
#define UW_TABLES_H

#include "uw_types.h"

UW_INTERNAL void uw_filetab_init(uw_filetab_t* t);
UW_INTERNAL void uw_filetab_free(uw_filetab_t* t);

/* Intern `filename` (resolved against `workdir` when relative) and return its
 * entry, reusing an existing one for a repeated path. NULL on failure. */
UW_INTERNAL uw_file_t* uw_filetab_intern(uw_db_t* db, const char* filename,
                                         const char* workdir);

UW_INTERNAL uw_file_t* uw_filetab_by_id(uw_filetab_t* t, uint32_t id);

UW_INTERNAL uw_hist_t* uw_hist_create(uw_db_t* db, const char* logicalname,
                                      const char* physicalname,
                                      ucisHistoryNodeKindT kind);
UW_INTERNAL void uw_hist_free_all(uw_db_t* db);

/* Emit sourceFiles+ and historyNodes+ and move to UW_PHASE_BODY. Idempotent;
 * called by whatever happens first to need the body. */
UW_INTERNAL int uw_tables_flush(uw_db_t* db);

/* Emit the placeholder instance a document needs when the caller created
 * none. See decision D6 in docs/ucis-writer-impl-plan.md. */
UW_INTERNAL int uw_placeholder_instance(uw_db_t* db);

/* Current UTC time as an xsd:dateTime, written into `out` (>= 21 bytes). */
UW_INTERNAL void uw_now_iso8601(char* out, size_t cap);

/* True if `s` is plausibly an xsd:dateTime. Cheap shape check, not a parser:
 * the point is to catch "Tue Jul 26 2026", not to validate leap seconds. */
UW_INTERNAL int uw_is_datetime(const char* s);

#endif /* UW_TABLES_H */
