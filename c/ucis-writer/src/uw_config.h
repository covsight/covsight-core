/* uw_config.h - build-time knobs and internal linkage decoration.
 * SPDX-License-Identifier: Apache-2.0 */

#ifndef UW_CONFIG_H
#define UW_CONFIG_H

#include "ucis_writer_api.h"

/* Output buffer size. One flush per this many bytes; the sink never sees a
 * write larger than this except for a single oversized string, which is
 * passed through directly. */
#ifndef UCIS_WRITER_BUFSZ
#  define UCIS_WRITER_BUFSZ 65536
#endif

/* Maximum scope nesting. UCIS-XML's deepest legal nest is well under 16
 * (UCIS > instanceCoverages > covergroupCoverage > cgInstance > cross > bin);
 * the limit exists to turn a runaway caller into a diagnosable error rather
 * than a stack of unbounded size. */
#ifndef UCIS_WRITER_MAX_DEPTH
#  define UCIS_WRITER_MAX_DEPTH 64
#endif

/* Validate UTF-8 in caller strings and replace invalid bytes. Costs roughly
 * one extra branch per non-ASCII byte; ASCII input is unaffected. Define
 * UCIS_WRITER_NO_UTF8_CHECK to skip it and trust the caller. */
#if !defined(UCIS_WRITER_NO_UTF8_CHECK)
#  define UCIS_WRITER_UTF8_CHECK 1
#else
#  define UCIS_WRITER_UTF8_CHECK 0
#endif

/* Internal linkage decoration.
 *
 * In the split-source in-tree build the uw_* helpers have external linkage so
 * unit tests can reach them. In the amalgamated single-header build everything
 * lands in one translation unit and becomes static, so a vendoring consumer
 * gains no symbols beyond the UCIS 1.0 names it asked for.
 *
 * A consumer may not use every helper -- most will never construct a toggle
 * or ask for a file by id -- and a vendored header that trips
 * -Werror=unused-function on their build is a vendored header they delete. */
#ifdef UCIS_WRITER_AMALGAMATED
#  if defined(__GNUC__) || defined(__clang__)
#    define UW_INTERNAL static __attribute__((unused))
#  else
#    define UW_INTERNAL static
#  endif
#else
#  define UW_INTERNAL
#endif

/* Propagate the first failure out of a nested call.
 *
 * The obvious `if (inner(db) != UCIS_WRITER_OK) return db->buf.status;` is a
 * trap: caller-mistake errors deliberately do NOT touch the buffer (D9), so
 * that form detects the failure and then reports success -- and the caller
 * goes on to emit the element the inner call just rejected. Always return the
 * value the inner call gave you. */
#define UW_TRY(expr)                                    \
    do {                                                \
        int uw_rc_ = (expr);                            \
        if (uw_rc_ != UCIS_WRITER_OK) {                 \
            return uw_rc_;                              \
        }                                               \
    } while (0)

/* Debug-only invariant check. Never used for input validation: caller mistakes
 * are reported through the sticky error state, not by aborting a simulation
 * that has been running for six hours. */
#ifndef UCIS_WRITER_ASSERT
#  ifdef NDEBUG
#    define UCIS_WRITER_ASSERT(x) ((void)0)
#  else
#    include <assert.h>
#    define UCIS_WRITER_ASSERT(x) assert(x)
#  endif
#endif

#endif /* UW_CONFIG_H */
