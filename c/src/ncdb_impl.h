#ifndef INCLUDED_NCDB_IMPL_H
#define INCLUDED_NCDB_IMPL_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ncdb/ncdb.h"

#define NCDB_FORMAT "NCDB"
#define NCDB_VERSION "2.0"
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

#define NCDB_SCOPE_MARKER_REGULAR     0x00
#define NCDB_SCOPE_MARKER_TOGGLE_PAIR 0x01

#define NCDB_PRESENCE_FLAGS       0x01
#define NCDB_PRESENCE_SOURCE      0x02
#define NCDB_PRESENCE_WEIGHT      0x04
#define NCDB_PRESENCE_AT_LEAST    0x08
#define NCDB_PRESENCE_GOAL        0x20
#define NCDB_PRESENCE_SOURCE_TYPE 0x40

#define NCDB_COUNTS_MODE_UINT32 0
#define NCDB_COUNTS_MODE_VARINT 1

#define NCDB_TOGGLE_BIN_0_TO_1 "0 -> 1"
#define NCDB_TOGGLE_BIN_1_TO_0 "1 -> 0"

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
} ncdbManifest;

struct ncdb_cover_s {
    struct ncdb_scope_s *parent;
    uint64_t type;
    char *name;
    uint64_t count;
    uint32_t flags;
    uint64_t at_least;
};

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
    struct ncdb_cover_s **covers;
    size_t cover_count;
    size_t cover_cap;
    struct ncdb_scope_s **children;
    size_t child_count;
    size_t child_cap;
};

struct ncdb_history_node_s {
    uint32_t kind;
    char *logical_name;
    char *physical_name;
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

int ncdb_attrs_serialize_empty(ncdbBuf *out);
int ncdb_attrs_deserialize(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz);
int ncdb_toggle_deserialize(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz);
int ncdb_cross_deserialize(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz);
int ncdb_fsm_deserialize(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz);

#endif
