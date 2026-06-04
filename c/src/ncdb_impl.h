#ifndef INCLUDED_NCDB_IMPL_H
#define INCLUDED_NCDB_IMPL_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ncdb/ncdb.h"

#define NCDB_FORMAT "NCDB"
#define NCDB_VERSION "4.0"  /* Phase 4.9: typed props, NUID, NTAS, NFRM, NMTR, NTRL, R7 fix. */
#define NCDB_GENERATOR "ncdb-c"
#define NCDB_HISTORY_FORMAT_V1 "v1"

#define NCDB_MEMBER_MANIFEST   "manifest.json"
#define NCDB_MEMBER_STRINGS    "strings.bin"
#define NCDB_MEMBER_SCOPE_TREE "scope_tree.bin"
#define NCDB_MEMBER_COUNTS     "counts.bin"
#define NCDB_MEMBER_HISTORY    "history.json"
#define NCDB_MEMBER_SOURCES    "sources.json"
#define NCDB_MEMBER_ATTRS      "attrs.bin"
#define NCDB_MEMBER_TOGGLE     "toggle.bin"
#define NCDB_MEMBER_FSM        "fsm.bin"
#define NCDB_MEMBER_CROSS      "cross.bin"
#define NCDB_MEMBER_ISSUES     "issues.bin"
#define NCDB_MEMBER_ISSUES_HISTORY "issues_history.bin"
#define NCDB_MEMBER_TAGS       "tags.bin"
#define NCDB_MEMBER_UNIQUE_ID  "unique_id_index.bin"   /* Phase 4.3 / M3 */
#define NCDB_MEMBER_TESTS_ASSOC "tests_assoc.bin"      /* Phase 4.4 / M4 */
#define NCDB_MEMBER_FORMAL      "formal.bin"           /* Phase 4.5 / M5 */
#define NCDB_MEMBER_METRICS     "metrics.bin"          /* Phase 4.6 / M6 */

#define NCDB_SCOPE_MARKER_REGULAR     0x00
#define NCDB_SCOPE_MARKER_TOGGLE_PAIR 0x01

#define NCDB_PRESENCE_FLAGS       0x01
#define NCDB_PRESENCE_SOURCE      0x02
#define NCDB_PRESENCE_WEIGHT      0x04
#define NCDB_PRESENCE_AT_LEAST    0x08
#define NCDB_PRESENCE_DU_LINK     0x10   /* Phase 1.4: scope→DU dfs idx (instance only) */
#define NCDB_PRESENCE_GOAL        0x20
#define NCDB_PRESENCE_SOURCE_TYPE 0x40
#define NCDB_PRESENCE_DU_SIG      0x80   /* Phase 1.4: DU scope signature string */
#define NCDB_PRESENCE_EXT_COVERS  0x100  /* Phase 1.5: per-cover extended fields */
#define NCDB_PRESENCE_EXPR_TERMS  0x200  /* Phase 1.9: coverpoint expression terms */
#define NCDB_PRESENCE_DU_FILES    0x400  /* Phase 1.8: per-DU file index list */
#define NCDB_PRESENCE_TYPED_PROPS 0x1000 /* Phase 4.2 / M2: scope-level typed-property block */

#define NCDB_COUNTS_MODE_UINT32 0
#define NCDB_COUNTS_MODE_VARINT 1

/* Phase 4.1 / M1 — R7 fix: synthetic per-bin-type child scopes.
 *
 * When a caller would add covers of multiple distinct types under one
 * parent scope (the assertion / cover-property cases where UCIS allows
 * PASS+FAIL+ATTEMPT+ACTIVE+... siblings), `ncdb_impl_create_cover` keeps
 * the first cover's type on the parent and reparents subsequent
 * different-typed covers into synthetic children, one per bin-type.
 *
 * Each synthetic child carries:
 *   - the parent's UCIS scope type (preserves type-filtered iteration),
 *   - `UCIS_SCOPE_INTERNAL` set in its flags so UCIS readers filter it,
 *   - `NCDB_SCOPE_FLAG_R7_SYNTHETIC` set (a single bit within
 *     UCIS_SCOPE_INTERNAL's reserved nibble) so we can detect "this is
 *     an R7-split synthetic" specifically, vs. a user-internal scope,
 *   - a name like `<pass>` / `<fail>` / `<bintype_0xNN>`.
 *
 * The on-disk `scope_tree.bin` format is unchanged — synthetics
 * serialize as regular child scopes. Readers that respect
 * UCIS_SCOPE_INTERNAL flatten transparently on iteration.
 *
 * Bit values mirror the UCIS spec (§6, scope flags); core NCDB defines
 * its own copy so it doesn't have to depend on the UCIS shim header. */
#define NCDB_SCOPE_FLAG_INTERNAL     0xF0000000U  /* UCIS_SCOPE_INTERNAL */
#define NCDB_SCOPE_FLAG_R7_SYNTHETIC 0x10000000U  /* bit within INTERNAL */

#define NCDB_TOGGLE_BIN_0_TO_1 "0 -> 1"
#define NCDB_TOGGLE_BIN_1_TO_0 "1 -> 0"

typedef struct ncdb_issues_s ncdb_issues_t;
typedef struct ncdb_nuid_index_s ncdb_nuid_index_t;

typedef struct ncdb_buf_s {
    uint8_t *data;
    size_t size;
    size_t cap;
} ncdbBuf;

typedef struct ncdb_string_table_s {
    char **items;
    uint32_t count;
    uint32_t cap;
} ncdbStringTable;

typedef struct ncdb_attr_entry_s {
    char         *key;        /* owned */
    ncdbAttrType  type;
    int32_t       i32;
    int64_t       i64;
    float         f32;
    double        f64;
    char         *str;        /* owned (for STRING) */
    uint8_t      *blob;       /* owned (for BYTES) */
    size_t        blob_size;
} ncdb_attr_entry_t;

typedef struct ncdb_attr_table_s {
    ncdb_attr_entry_t *entries;
    size_t count;
    size_t cap;
} ncdb_attr_table_t;

/* Phase 4.2 / M2 — typed-property table. Numeric-keyed sibling of the
 * attribute table, used for known UCIS properties to dodge the
 * string-key overhead. */
typedef struct ncdb_prop_entry_s {
    uint16_t prop_id;      /* see ncdb_props.h */
    uint8_t  type_tag;     /* NCDB_PROP_TYPE_* */
    union {
        int32_t  i32;
        int64_t  i64;
        double   d;
        char    *str;      /* owned (NCDB_PROP_TYPE_STRING) */
    } v;
} ncdb_prop_entry_t;

typedef struct ncdb_prop_table_s {
    ncdb_prop_entry_t *entries;
    size_t count;
    size_t cap;
} ncdb_prop_table_t;

typedef struct ncdb_manifest_s {
    char *format;
    char *version;
    char *ucis_version;
    char *created;
    char *path_separator;
    uint64_t scope_count;
    uint64_t coveritem_count;
    uint64_t test_count;
    uint64_t total_hits;
    uint64_t covered_bins;
    char *schema_hash;
    char *generator;
    char *history_format;
    /* UCIS vendor identification (Phase 1.7). NULL/empty when unset. */
    char *vendor_id;
    char *vendor_tool;
    char *vendor_tool_version;
    char *ucis_standard;
    /* v4 schema version fields (Phase 4.8 / M8). Numeric replacement for
     * the legacy stringy `version` field; readers compare these instead.
     * v3 fixtures (manifest binary v1) deserialize with major=3, minor=0,
     * feature_flags=0, n_history_nodes/n_associations derived from existing
     * data. */
    uint32_t schema_version_major;
    uint32_t schema_version_minor;
    uint64_t feature_flags;
    uint64_t n_history_nodes;
    uint64_t n_associations;
} ncdbManifest;

struct ncdb_cover_s {
    struct ncdb_scope_s *parent;
    uint64_t type;
    char *name;
    uint64_t count;
    uint32_t flags;
    uint64_t at_least;
    /* Phase 1.5: per-cover UCIS fields. All optional; 0/NULL/-1 = unset. */
    char    *source_path;       /* owned */
    uint64_t source_line;
    uint64_t source_token;
    uint64_t weight;            /* 0 = use parent's */
    int64_t  goal;              /* -1 = use parent's */
    char    *comment;           /* owned */
    ncdb_attr_table_t attrs;
    ncdb_prop_table_t props;    /* Phase 4.2 / M2 */
    /* Phase 4.4 / M4 — sorted list of "real-test slot indices" for
     * history-node↔coveritem association. NULL when no associations
     * recorded (the common case). Slot indices are positions in the
     * db-level real_tests vector, not raw history-node array indices,
     * so MERGE nodes are excluded by construction. */
    struct ncdb_assoc_s *assoc;
};

#define NCDB_COVER_PRESENCE_SOURCE      0x01U
#define NCDB_COVER_PRESENCE_WEIGHT      0x02U
#define NCDB_COVER_PRESENCE_GOAL        0x04U
#define NCDB_COVER_PRESENCE_COMMENT     0x08U
#define NCDB_COVER_PRESENCE_AT_LEAST    0x10U
#define NCDB_COVER_PRESENCE_TYPED_PROPS 0x20U  /* Phase 4.2 / M2 */

struct ncdb_scope_s {
    struct ncdb_scope_s *parent;
    uint64_t type;
    char *name;
    uint64_t flags;
    int source_type;
    uint64_t weight;
    int64_t goal;
    uint64_t at_least;
    char *source_path;
    uint64_t source_line;
    uint64_t source_token;
    /* Phase 1.4: UCIS DU↔instance link.
     *   For instance scopes: pointer to the DU defining this instance, or NULL.
     *   For DU scopes:       du_signature string (per UCIS UCIS_STR_DU_SIGNATURE), or NULL.
     * Not owned (du_scope points into the same tree). */
    struct ncdb_scope_s *du_scope;
    char *du_signature;
    char *expr_terms;            /* Phase 1.9: UCIS_STR_EXPR_TERMS for coverpoints/expr */
    /* Phase 1.8: per-DU file table. Indices into the global db->sources list,
     * in DU-local order. Empty for non-DU scopes. */
    uint32_t *du_files;
    size_t du_file_count;
    size_t du_file_cap;
    struct ncdb_cover_s **covers;
    size_t cover_count;
    size_t cover_cap;
    struct ncdb_scope_s **children;
    size_t child_count;
    size_t child_cap;
    /* CROSS: references to crossed coverpoint scopes (not owned) */
    struct ncdb_scope_s **crossed_points;
    size_t crossed_count;
    size_t crossed_cap;
    ncdb_attr_table_t attrs;
    /* Toggle-scope metadata (only meaningful when type == NCDB_SCOPE_TOGGLE) */
    char     *toggle_canonical;   /* NULL if unset; owned */
    uint8_t   toggle_metric;      /* 0 = default (NCDB_TOGGLE_METRIC_2STOGGLE) */
    uint8_t   toggle_type;        /* 0 = default (NCDB_TOGGLE_TYPE_NET)        */
    uint8_t   toggle_dir;         /* 0 = default (NCDB_TOGGLE_DIR_INTERNAL)    */
    /* FSM-scope metadata (only meaningful when type == NCDB_SCOPE_FSM):
     * sparse list of non-sequential state-name → state-value overrides. */
    struct ncdb_fsm_state_s *fsm_states;
    size_t fsm_state_count;
    size_t fsm_state_cap;
    /* Tags: scope-level string labels (UCIS tag set). */
    char **tags;
    size_t tag_count;
    size_t tag_cap;
    ncdb_prop_table_t props;    /* Phase 4.2 / M2 */
};

struct ncdb_fsm_state_s {
    char    *name;   /* owned */
    uint32_t value;  /* UCIS_INT_FSM_STATEVAL */
};

struct ncdb_history_node_s {
    uint32_t kind;
    char *logical_name;
    char *physical_name;
    /* TestData fields */
    char    *user_name;
    char    *seed;
    char    *tool_category;
    char    *comment;
    char    *date;
    char    *run_cwd;
    char    *cmd;
    char    *args;
    char    *time_unit;
    char    *vendor_id;
    char    *vendor_tool;
    char    *vendor_tool_version;
    char    *same_tests;
    uint32_t test_status;
    int      compulsory;
    double   sim_time;
    double   cpu_time;
    double   cost;
    /* Parent index (UCIS history forms a tree). SIZE_MAX = no parent. */
    size_t   parent_idx;
    ncdb_attr_table_t attrs;
};

struct ncdb_s {
    char *path;
    char *path_separator;
    char *last_error;
    struct ncdb_scope_s **roots;
    size_t root_count;
    size_t root_cap;
    struct ncdb_history_node_s **history_nodes;
    size_t history_count;
    size_t history_cap;
    char **sources;
    size_t source_count;
    size_t source_cap;
    ncdb_issues_t *issues;
    uint8_t *issues_hist_data;
    size_t issues_hist_len;
    ncdb_attr_table_t attrs;
    /* Vendor identification (Phase 1.7). Owned. */
    char *vendor_id;
    char *vendor_tool;
    char *vendor_tool_version;
    char *ucis_standard;
    /* v4 feature flags (Phase 4.8 / M8). Each member writer sets its
     * NCDB_FEATURE_* bit during serialize so the manifest reflects what's
     * actually on disk. Reset to 0 by ncdb_impl_reset_db. */
    uint64_t feature_flags;
    /* Loaded NUID index (Phase 4.3 / M3). NULL when no index is on disk
     * or after reset. Freed by ncdb_Close. */
    ncdb_nuid_index_t *nuid_idx;
    /* Formal records (Phase 4.5 / M5). Sparse array of per-scope formal
     * status / radius / witness. Owned. */
    struct ncdb_formal_record_s *formal_records;
    size_t formal_record_count;
    size_t formal_record_cap;
    /* Metric definitions (Phase 4.6 / M6). Tiny table — one record per
     * UCIS metric the tool supports. */
    struct ncdb_metric_def_s *metrics;
    size_t metric_count;
    size_t metric_cap;
    /* DU-name → scope index (Phase 4.7 / M7), sorted by name for
     * bsearch. Built from the scope_tree.bin trailer on read. */
    struct ncdb_du_lookup_s {
        char       *name;       /* owned */
        ncdbScopeT  scope;
    } *du_lookup;
    size_t du_lookup_count;
};

typedef struct ncdb_zip_member_s {
    const char *name;
    const uint8_t *data;
    size_t size;
    int store;
} ncdbZipMember;

char *ncdb_impl_strdup(const char *s);
void ncdb_impl_set_error(ncdbT db, const char *fmt, ...);
void ncdb_impl_buf_init(ncdbBuf *buf);
int ncdb_impl_buf_reserve(ncdbBuf *buf, size_t addl);
int ncdb_impl_buf_append(ncdbBuf *buf, const void *data, size_t len);
void ncdb_impl_buf_free(ncdbBuf *buf);
void ncdb_impl_reset_db(ncdbT db);

ncdbScopeT ncdb_impl_create_scope(ncdbT db, ncdbScopeT parent, uint64_t type, const char *name);
ncdbCoverT ncdb_impl_create_cover(ncdbT db, ncdbScopeT scope, uint64_t type, const char *name, uint64_t count);
ncdbHistoryNodeT ncdb_impl_create_history(ncdbT db, uint32_t kind, const char *logical_name, const char *physical_name);
int ncdb_impl_add_source(ncdbT db, const char *path);
int ncdb_impl_get_source_id(ncdbT db, const char *path, int create);
size_t ncdb_impl_count_scopes(ncdbT db);
size_t ncdb_impl_count_covers(ncdbT db);
int ncdb_impl_add_cross_point(ncdbScopeT cross_scope, ncdbScopeT cp);

uint32_t ncdb_cover_default_flags(uint64_t cover_type);
uint64_t ncdb_cover_default_at_least(uint64_t cover_type);

int ncdb_zip_read_member(const char *path, const char *member, uint8_t **data, size_t *size, char *errbuf, size_t errbuf_sz);
int ncdb_zip_write_archive(const char *path, const ncdbZipMember *members, size_t count, char *errbuf, size_t errbuf_sz);

int ncdb_varint_encode_uint64(uint64_t value, ncdbBuf *buf);
int ncdb_varint_decode_uint64(const uint8_t *data, size_t size, size_t *offset, uint64_t *value);
int ncdb_varint_encode_many(const uint64_t *values, size_t count, ncdbBuf *buf);
int ncdb_varint_decode_many(const uint8_t *data, size_t size, size_t *offset, size_t count, uint64_t *values);

void ncdb_strings_init(ncdbStringTable *tab);
void ncdb_strings_free(ncdbStringTable *tab);
int ncdb_strings_add(ncdbStringTable *tab, const char *s);
const char *ncdb_strings_get(const ncdbStringTable *tab, uint64_t idx);
int ncdb_strings_serialize(const ncdbStringTable *tab, ncdbBuf *out);
int ncdb_strings_deserialize(const uint8_t *data, size_t size, ncdbStringTable *tab, char *errbuf, size_t errbuf_sz);

int ncdb_counts_serialize(const uint64_t *counts, size_t count, ncdbBuf *out);
int ncdb_counts_deserialize(const uint8_t *data, size_t size, uint64_t **counts, size_t *count, char *errbuf, size_t errbuf_sz);

int ncdb_scope_tree_serialize(ncdbT db, ncdbStringTable *strings, ncdbBuf *tree_out, ncdbBuf *counts_tmp);
int ncdb_scope_tree_deserialize(ncdbT db, const uint8_t *data, size_t size, const ncdbStringTable *strings, const uint64_t *counts, size_t count_count, char *errbuf, size_t errbuf_sz);

void ncdb_manifest_init(ncdbManifest *m);
void ncdb_manifest_free(ncdbManifest *m);
int ncdb_manifest_build(ncdbT db, const uint8_t *scope_tree, size_t scope_tree_size, const uint64_t *counts, size_t count_count, ncdbManifest *m);
int ncdb_manifest_serialize(const ncdbManifest *m, ncdbBuf *out);
int ncdb_manifest_deserialize(const uint8_t *data, size_t size, ncdbManifest *m, char *errbuf, size_t errbuf_sz);

int ncdb_history_serialize(ncdbT db, ncdbBuf *out);
int ncdb_history_deserialize(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz);

int ncdb_sources_serialize(ncdbT db, ncdbBuf *out);
int ncdb_sources_deserialize(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz);

int  ncdb_attrs_serialize(ncdbT db, ncdbBuf *out);
int  ncdb_attrs_deserialize(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz);
int  ncdb_toggle_serialize(ncdbT db, ncdbBuf *out);
int  ncdb_fsm_serialize(ncdbT db, ncdbBuf *out);
int  ncdb_tags_serialize(ncdbT db, ncdbBuf *out);
int  ncdb_tags_deserialize(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz);
void ncdb_attr_table_init(ncdb_attr_table_t *t);
void ncdb_attr_table_free(ncdb_attr_table_t *t);
int  ncdb_attr_table_set(ncdb_attr_table_t *t, const char *key, const ncdbAttrValue *v);
int  ncdb_attr_table_get(const ncdb_attr_table_t *t, const char *key, ncdbAttrValue *out);
int  ncdb_attr_table_iterate(const ncdb_attr_table_t *t,
                             int (*cb)(const char *, const ncdbAttrValue *, void *),
                             void *ud);
/* Phase 4.1 / M1 — logical cover view (flatten R7 synthetic children). */
size_t      ncdb_scope_logical_cover_count(ncdbScopeT scope);
ncdbCoverT  ncdb_scope_logical_cover_at(ncdbScopeT scope, size_t idx);

/* Phase 4.2 / M2 — typed-property table. */
void  ncdb_prop_table_init(ncdb_prop_table_t *t);
void  ncdb_prop_table_free(ncdb_prop_table_t *t);
int   ncdb_prop_table_set_int32 (ncdb_prop_table_t *t, uint16_t id, int32_t v);
int   ncdb_prop_table_set_int64 (ncdb_prop_table_t *t, uint16_t id, int64_t v);
int   ncdb_prop_table_set_double(ncdb_prop_table_t *t, uint16_t id, double v);
int   ncdb_prop_table_set_string(ncdb_prop_table_t *t, uint16_t id, const char *s);
/* Getters return 0 on success, -1 if the prop isn't set. */
int          ncdb_prop_table_get_int   (const ncdb_prop_table_t *t, uint16_t id, int64_t *out);
int          ncdb_prop_table_get_double(const ncdb_prop_table_t *t, uint16_t id, double  *out);
const char  *ncdb_prop_table_get_string(const ncdb_prop_table_t *t, uint16_t id);
int          ncdb_prop_table_remove    (ncdb_prop_table_t *t, uint16_t id);
/* Serialize/deserialize a typed-property block (no magic — caller's
 * presence bit gates the block). Strings flow through `strings`. */
int  ncdb_prop_table_serialize  (const ncdb_prop_table_t *t,
                                 ncdbStringTable *strings, ncdbBuf *out);
int  ncdb_prop_table_deserialize(ncdb_prop_table_t *t,
                                 const uint8_t *data, size_t size, size_t *off,
                                 const ncdbStringTable *strings);

/* DFS scope index: walk in canonical order matching scope_tree.bin */
int  ncdb_impl_scope_dfs_index(ncdbT db, ncdbScopeT target, size_t *out_idx);
ncdbScopeT ncdb_impl_scope_by_dfs_index(ncdbT db, size_t idx);
int  ncdb_impl_history_index(ncdbT db, ncdbHistoryNodeT h, size_t *out_idx);
/* Allocate and fill a flat DFS-ordered scope array. Caller frees *out via free(). */
int  ncdb_impl_dfs_flatten(ncdbT db, ncdbScopeT **out, size_t *out_count);
int ncdb_toggle_deserialize(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz);

/* Phase 4.3 / M3 — unique-ID computation + NUID index. */
uint64_t ncdb_xxh64(const void *input, size_t len, uint64_t seed);
int      ncdb_compute_unique_id(ncdbT db, ncdbScopeT scope,
                                char *buf, size_t bufsz);

int    ncdb_unique_id_index_serialize  (ncdbT db, ncdbBuf *out);

/* Phase 4.4 / M4 — tests↔coveritem association.
 *
 * Build-time storage on ncdb_cover_s::assoc is a sorted dedupe'd vector
 * of "real-test slot indices" — positions in the per-DB real_tests
 * list (history nodes of kind TEST/NONE; MERGE nodes are derivable on
 * read and never stored). The writer caller drives this via
 * ncdb_AssocCoverHistory which translates a history-node pointer into
 * its real-test slot. */
struct ncdb_assoc_s {
    uint32_t *slots;    /* sorted, dedupe'd */
    size_t    count;
    size_t    cap;
};
void   ncdb_assoc_free(struct ncdb_assoc_s *a);
int    ncdb_tests_assoc_serialize  (ncdbT db, ncdbBuf *out);
int    ncdb_tests_assoc_deserialize(ncdbT db, const uint8_t *data, size_t size,
                                    char *errbuf, size_t errbuf_sz);

/* Phase 4.5 / M5 — per-scope formal-verification record. */
struct ncdb_formal_record_s {
    ncdbScopeT scope;        /* target (scope only for now; cover-level deferred) */
    uint32_t   presence;     /* bit0=status, bit1=radius, bit2=witness */
    uint8_t    status;       /* ucisFormalStatusT */
    int64_t    radius;
    char      *witness;      /* owned, NUL-terminated */
};
int     ncdb_formal_set_status (ncdbT db, ncdbScopeT s, uint8_t status);
int     ncdb_formal_get_status (ncdbT db, ncdbScopeT s);   /* -1 if unset */
int     ncdb_formal_set_radius (ncdbT db, ncdbScopeT s, int64_t radius);
int64_t ncdb_formal_get_radius (ncdbT db, ncdbScopeT s);   /* INT64_MIN if unset */
int     ncdb_formal_set_witness(ncdbT db, ncdbScopeT s, const char *witness);
const char *ncdb_formal_get_witness(ncdbT db, ncdbScopeT s);
int     ncdb_formal_serialize  (ncdbT db, ncdbBuf *out);
int     ncdb_formal_deserialize(ncdbT db, const uint8_t *data, size_t size,
                                char *errbuf, size_t errbuf_sz);
void    ncdb_formal_free       (ncdbT db);   /* frees records array */

/* Phase 4.6 / M6 — metric definitions (UCIS metric naming model, §4.9). */
struct ncdb_metric_def_s {
    uint32_t metric_id;
    char    *name;            /* owned, UCIS URL-style metric identifier */
    uint8_t  mode;            /* UCIS METRIC_MODE enum */
    uint64_t target_type_mask; /* scope types this metric applies to */
};
int    ncdb_metric_add        (ncdbT db, uint32_t id, const char *name,
                               uint8_t mode, uint64_t target_type_mask);
size_t ncdb_metric_count      (ncdbT db);
const struct ncdb_metric_def_s *ncdb_metric_get(ncdbT db, size_t idx);
/* Phase 4.7 / M7 — bsearch the DU-name index loaded from the
 * scope_tree.bin trailer. Returns NULL when no trailer was present or
 * `name` doesn't match a DU. */
ncdbScopeT ncdb_match_du_by_name(ncdbT db, const char *name);

int    ncdb_metric_serialize  (ncdbT db, ncdbBuf *out);
int    ncdb_metric_deserialize(ncdbT db, const uint8_t *data, size_t size,
                               char *errbuf, size_t errbuf_sz);
void   ncdb_metric_free       (ncdbT db);
int    ncdb_unique_id_index_deserialize(const uint8_t *data, size_t size,
                                        ncdb_nuid_index_t **out_idx,
                                        char *errbuf, size_t errbuf_sz);
void   ncdb_unique_id_index_free       (ncdb_nuid_index_t *idx);
size_t ncdb_unique_id_index_lookup     (const ncdb_nuid_index_t *idx,
                                        uint64_t hash);
int ncdb_cross_deserialize(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz);
int ncdb_fsm_deserialize(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz);

#endif
