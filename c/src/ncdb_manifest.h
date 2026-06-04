#ifndef INCLUDED_NCDB_MANIFEST_H
#define INCLUDED_NCDB_MANIFEST_H

#include "ncdb_impl.h"

/* ----------------------------------------------------------------------
 *  Schema version (Phase 4.8 / M8).
 *
 *  These numeric constants are the authoritative schema version. The
 *  legacy stringy `ncdbManifest::version` field is retained for display
 *  but readers must compare schema_version_major/minor. v4 raises
 *  schema_version_major to 4 in Phase 4.9; intermediate v4-track PRs land
 *  with major=3, minor=0, and new feature_flags bits set as each member
 *  implementation lands.
 * -------------------------------------------------------------------- */

#define NCDB_SCHEMA_VERSION_MAJOR  4
#define NCDB_SCHEMA_VERSION_MINOR  0

/* ----------------------------------------------------------------------
 *  Feature flags (Phase 4.8 / M8).
 *
 *  One bit per format extension landed by the v4 plan. Set by the writer
 *  when the corresponding member is present and populated; checked by the
 *  reader to gate optional parse paths.
 *
 *  Unknown bits (outside NCDB_FEATURE_KNOWN_MASK) must be rejected by the
 *  reader as "newer-than-supported". Deserialize itself does not enforce
 *  this — the high-level open path performs the check so unit tests can
 *  still introspect a forward-version manifest.
 *
 *  Never reassign a bit. Deprecated bits stay listed with a "RETIRED"
 *  comment.
 * -------------------------------------------------------------------- */

#define NCDB_FEATURE_UNIQUE_ID_INDEX  0x0001ULL  /* M3: unique_id_index.bin (NUID) */
#define NCDB_FEATURE_TESTS_ASSOC      0x0002ULL  /* M4: tests_assoc.bin (NTAS) */
#define NCDB_FEATURE_FORMAL           0x0004ULL  /* M5: formal.bin (NFRM) */
#define NCDB_FEATURE_METRICS          0x0008ULL  /* M6: metrics.bin (NMTR) */
#define NCDB_FEATURE_TYPED_PROPS      0x0010ULL  /* M2: per-scope/cover typed-property block */
/* 0x0020 RESERVED — was has_mixed_cover_types in earlier v4 drafts.
 *                   Superseded by D6 (synthetic-scope transform) which
 *                   needs no flag. Do not reuse. */
#define NCDB_FEATURE_SCOPE_TRAILER    0x0040ULL  /* M7: scope_tree.bin NTRL trailer */
#define NCDB_FEATURE_HIER_NAME_INDEX  0x0080ULL  /* M7: opt-in hier-name lookup */

#define NCDB_FEATURE_KNOWN_MASK \
    (NCDB_FEATURE_UNIQUE_ID_INDEX | \
     NCDB_FEATURE_TESTS_ASSOC     | \
     NCDB_FEATURE_FORMAL          | \
     NCDB_FEATURE_METRICS         | \
     NCDB_FEATURE_TYPED_PROPS     | \
     NCDB_FEATURE_SCOPE_TRAILER   | \
     NCDB_FEATURE_HIER_NAME_INDEX)

#endif
