/*
 * ucis_writer - stream schema-valid UCIS-XML at flat memory cost.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright the covsight contributors.
 *
 * NOTE (open question 1, docs/ucis-writer-impl-plan.md item 0.5): the licence
 * of this subdirectory is not final. Apache-2.0 matches the rest of the repo
 * but is GPLv2-incompatible, which constrains who can vendor it. A permissive
 * alternative (MIT / BSD-2) is under review. Do not ship until this is settled.
 *
 * -------------------------------------------------------------------------
 * THE DESCRIPTOR API (decision D15).
 *
 * Everything an element needs is passed at the moment it is created, in one
 * struct. That is the whole design: because nothing arrives late, no element
 * has to be held open waiting for it, and the "you set that property one call
 * too late" class of error does not exist.
 *
 * Every descriptor has a `uw_*_defaults()` companion, and you should use it:
 *
 *     uw_coverpoint_desc_t cp = uw_coverpoint_defaults();
 *     cp.name = "cp_len";
 *     cp.auto_bin_max = 8;
 *     uw_coverpoint(db, &cp);
 *
 * In C99 you may also write the compact form:
 *
 *     uw_coverpoint(db, &(uw_coverpoint_desc_t){ .name = "cp_len" });
 *
 * but note it is C-only and always will be: compound literals are not ISO C++
 * in any version, C++20 included. The `_defaults()` form above is the only one
 * that compiles clean from C99 through C++20 at -Wall -Wextra -Wpedantic. C++
 * consumers wanting a one-liner should use ucis_writer.hpp.
 *
 * ORDERING. UCIS-XML's content models are xsd:sequence throughout, so document
 * order is not a matter of style -- emitting a coverage kind, or a coverpoint
 * after a cross, out of order produces a document no conforming reader accepts.
 * Rather than buffer to repair it, which would defeat the memory guarantee,
 * this library detects it and reports UCIS_WRITER_ERR_ORDER. Callers must
 * arrive grouped by (instance, coverage kind).
 *
 * UCIS 1.0 C API COMPATIBILITY is available separately, in
 * ucis_writer_compat.h. It is not needed to use this library and costs nothing
 * if you do not include it.
 */

#ifndef UW_PUBLIC_H
#define UW_PUBLIC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Version of this library, and the standard its output targets. Both appear
 * verbatim in the output, so changing them changes goldens. */
#define UCIS_WRITER_VERSION       "0.1.0"
#define UCIS_WRITER_UCIS_VERSION  "1.0"

/* Identity written into every test record unless the caller overrides it. */
#ifndef UCIS_WRITER_VENDOR_ID
#  define UCIS_WRITER_VENDOR_ID   "covsight"
#endif
#ifndef UCIS_WRITER_VENDOR_TOOL
#  define UCIS_WRITER_VENDOR_TOOL "ucis_writer"
#endif

/* ------------------------------------------------------------------------
 *  Handles
 * ---------------------------------------------------------------------- */

typedef struct uw_db_s   uw_db_t;    /* an open output stream               */
typedef struct uw_file_s uw_file_t;  /* an interned source file             */
typedef struct uw_hist_s uw_test_t;  /* a test record (UCIS history node)   */

/* ------------------------------------------------------------------------
 *  Status
 * ---------------------------------------------------------------------- */

typedef enum {
    UCIS_WRITER_OK = 0,
    UCIS_WRITER_ERR_ALLOC,       /* out of memory                            */
    UCIS_WRITER_ERR_IO,          /* sink reported a write failure            */
    UCIS_WRITER_ERR_USAGE,       /* NULL or invalid argument                 */
    UCIS_WRITER_ERR_ORDER,       /* ordering contract violated               */
    UCIS_WRITER_ERR_SEALED,      /* too late: that part is already written   */
    UCIS_WRITER_ERR_DEPTH,       /* scope nesting beyond the fixed limit     */
    UCIS_WRITER_ERR_UNBALANCED,  /* scopes opened and closed do not match    */
    UCIS_WRITER_ERR_STATE        /* call makes no sense where it was made    */
} ucisWriterStatusT;

/* ------------------------------------------------------------------------
 *  Output sink
 *
 *  Bytes leave through here. Supply one to write somewhere that is not a file
 *  -- a socket, a compressor, a buffer. `write` returns 0 on success; a
 *  non-zero return latches UCIS_WRITER_ERR_IO and output stops. `close` may be
 *  NULL.
 * ---------------------------------------------------------------------- */

typedef struct ucisWriterSink_s {
    int   (*write)(void* ctx, const char* data, size_t len);
    int   (*close)(void* ctx);
    void*   ctx;
} ucisWriterSinkT;

/* ------------------------------------------------------------------------
 *  Lifecycle
 * ---------------------------------------------------------------------- */

uw_db_t* uw_open(const char* path);
uw_db_t* uw_open_sink(const ucisWriterSinkT* sink);

/* Finish the document, flush, close the sink, free the handle. Returns 0 if
 * nothing went wrong at any point. The handle is invalid afterwards, so read
 * uw_error()/uw_warnings() before calling this. */
int uw_close(uw_db_t* db);

/* Close the innermost open scope. Scopes this library opened on your behalf --
 * the coverage-kind wrappers, a synthesised cgInstance -- are closed through
 * transparently, so you balance only the scopes you created. */
int uw_end(uw_db_t* db);

/* ------------------------------------------------------------------------
 *  Diagnostics
 *
 *  Failures are sticky and are not raised as exceptions or aborts: a coverage
 *  writer must not take down a simulation that has been running for six hours.
 *  Caller mistakes drop the offending call and nothing else; only I/O and
 *  allocation failures stop output.
 * ---------------------------------------------------------------------- */

ucisWriterStatusT uw_error(uw_db_t* db);
const char*       uw_error_string(uw_db_t* db);
const char*       uw_status_name(ucisWriterStatusT status);

/* Count of lossy repairs: text that XML 1.0 cannot represent, values clamped
 * to fit the schema's positiveInteger fields, bin names carrying no value. A
 * document with warnings is valid; it is just not everything you meant. */
unsigned long uw_warnings(uw_db_t* db);

uint64_t uw_bytes_written(uw_db_t* db);

/* Indent the output. Off by default: it roughly doubles the byte count. */
int uw_set_pretty(uw_db_t* db, int enable);

/* Document provenance, settable until the first content is written. */
int uw_set_written_by(uw_db_t* db, const char* who);
int uw_set_written_time(uw_db_t* db, const char* xsd_datetime);

/* ------------------------------------------------------------------------
 *  Source locations
 * ---------------------------------------------------------------------- */

/* Intern a source file and return its handle. Repeated paths return the same
 * handle. Must be called before the first instance: the file table is written
 * ahead of all coverage, so it seals when coverage starts. */
uw_file_t* uw_file(uw_db_t* db, const char* path, const char* workdir);

typedef struct {
    uw_file_t* file;
    int        line;
    int        token;
} uw_src_t;

/* ------------------------------------------------------------------------
 *  Test records
 * ---------------------------------------------------------------------- */

typedef enum {
    UW_TEST_TEST = 1,
    UW_TEST_MERGE,
    UW_TEST_TEST_DERIVED
} uw_test_kind_t;

typedef struct {
    const char*    name;            /* required                              */
    const char*    physical_name;
    uw_test_kind_t kind;
    int            passed;          /* testStatus                            */
    int            compulsory;
    double         sim_time;
    double         cpu_time;
    double         cost;
    const char*    time_unit;
    const char*    run_cwd;
    const char*    seed;
    const char*    cmd;
    const char*    args;
    const char*    date;            /* xsd:dateTime; NULL means "now"        */
    const char*    user_name;
    const char*    tool_category;
    const char*    comment;
    const char*    vendor_id;       /* NULL inherits the document's identity */
    const char*    vendor_tool;
    const char*    vendor_version;
} uw_test_desc_t;

uw_test_desc_t uw_test_defaults(void);
uw_test_t*     uw_test(uw_db_t* db, const uw_test_desc_t* d);

/* ------------------------------------------------------------------------
 *  Design hierarchy
 * ---------------------------------------------------------------------- */

typedef struct {
    const char* name;        /* hierarchical instance path, required        */
    const char* du_name;     /* design unit, e.g. "work.top"                */
    uw_src_t    src;
} uw_instance_desc_t;

uw_instance_desc_t uw_instance_defaults(void);
int                uw_instance(uw_db_t* db, const uw_instance_desc_t* d);

/* ------------------------------------------------------------------------
 *  Bins
 *
 *  One shape for every kind of bin. `kind` selects the element; which kinds
 *  are legal depends on the scope you are in.
 * ---------------------------------------------------------------------- */

typedef enum {
    UW_BIN_NORMAL = 0,
    UW_BIN_DEFAULT,
    UW_BIN_IGNORE,
    UW_BIN_ILLEGAL
} uw_bin_kind_t;

typedef struct {
    const char*   name;
    uint64_t      count;
    uw_bin_kind_t kind;
    int           goal;       /* 0 = unset                                  */
    int           weight;     /* 0 = unset (the schema default is 1)        */
    int           excluded;
    uw_src_t      src;        /* where meaningful (branch arms)             */
} uw_bin_desc_t;

uw_bin_desc_t uw_bin_defaults(void);

/* Add a bin to the innermost open scope. */
int uw_bin(uw_db_t* db, const uw_bin_desc_t* d);

/* ------------------------------------------------------------------------
 *  Code coverage
 * ---------------------------------------------------------------------- */

typedef struct {
    const char* name;
    uint64_t    count;
    uw_src_t    src;
    int         weight;
    int         excluded;
} uw_statement_desc_t;

uw_statement_desc_t uw_statement_defaults(void);
int                 uw_statement(uw_db_t* db, const uw_statement_desc_t* d);

typedef struct {
    const char* name;
    uw_src_t    src;
    int         is_case;      /* case statement rather than if              */
    int         has_else;
} uw_branch_desc_t;

uw_branch_desc_t uw_branch_defaults(void);

/* Opens a scope; each arm then arrives as uw_bin(). Close with uw_end(). */
int uw_branch(uw_db_t* db, const uw_branch_desc_t* d);

typedef enum { UW_TOGGLE_NET = 1, UW_TOGGLE_REG } uw_toggle_type_t;
typedef enum {
    UW_TOGGLE_INTERNAL = 1, UW_TOGGLE_IN, UW_TOGGLE_OUT, UW_TOGGLE_INOUT
} uw_toggle_dir_t;

typedef struct {
    const char*      name;
    const char*      canonical_name;
    uw_toggle_type_t type;
    uw_toggle_dir_t  dir;
} uw_toggle_desc_t;

uw_toggle_desc_t uw_toggle_defaults(void);

/* Opens a toggle object scope. Bins arriving directly are a scalar's, and the
 * per-bit level the schema requires is synthesised. For a vector, open a bit
 * with uw_toggle_bit() per bit instead. Close with uw_end(). */
int uw_toggle(uw_db_t* db, const uw_toggle_desc_t* d);
int uw_toggle_bit(uw_db_t* db, const char* name);

typedef struct {
    const char* name;
    const char* terms;    /* '#'-separated operands; NULL uses `name` opaquely */
    uw_src_t    src;
} uw_expr_desc_t;

uw_expr_desc_t uw_expr_defaults(void);

/* Opens an expression scope. Close with uw_end(). */
int uw_expr(uw_db_t* db, const uw_expr_desc_t* d);

/* Opens the input-contribution metric level ("UCIS:FULL", ...). UCIS-XML has
 * no element for it, so it emits nothing and rides along on each bin as
 * @typeComponent -- but it balances like any scope. Close with uw_end(). */
int uw_expr_metric(uw_db_t* db, const char* name);

/* ------------------------------------------------------------------------
 *  Functional coverage
 * ---------------------------------------------------------------------- */

typedef struct {
    const char* name;
    uw_src_t    src;
} uw_covergroup_desc_t;

uw_covergroup_desc_t uw_covergroup_defaults(void);

/* Opens a covergroup type scope. It emits no element of its own -- UCIS-XML
 * carries the type's identity inside each cgInstance -- but it balances like
 * any scope. Close with uw_end(). */
int uw_covergroup(uw_db_t* db, const uw_covergroup_desc_t* d);

typedef struct {
    const char* name;
    uw_src_t    src;
    int         weight;
    int         goal;
    int         at_least;
    int         auto_bin_max;
    int         detect_overlap;
    int         num_print_missing;
    int         per_instance;
    int         merge_instances;
} uw_cginstance_desc_t;

uw_cginstance_desc_t uw_cginstance_defaults(void);

/* Opens one instance of the enclosing covergroup type. Optional: coverpoints
 * created directly in the type scope get one synthesised, which is what
 * type-only coverage looks like. Close with uw_end(). */
int uw_cginstance(uw_db_t* db, const uw_cginstance_desc_t* d);

typedef struct {
    const char* name;
    const char* expr_string;
    int         weight;
    int         goal;
    int         at_least;
    int         auto_bin_max;
    int         detect_overlap;
} uw_coverpoint_desc_t;

uw_coverpoint_desc_t uw_coverpoint_defaults(void);
int                  uw_coverpoint(uw_db_t* db, const uw_coverpoint_desc_t* d);

/* A cross must name the coverpoints it crosses, in cross order: that is both
 * the crossExpr list and how each comma-separated component of a cross bin's
 * name is resolved to the <index> the schema requires. Every named coverpoint
 * must already have been written -- the schema puts all coverpoints before all
 * crosses, so there is no ordering in which it could be otherwise. */
typedef struct {
    const char*        name;
    const char* const* crossed;      /* array of coverpoint names            */
    size_t             n_crossed;
    int                weight;
    int                goal;
    int                at_least;
    int                num_print_missing;
} uw_cross_desc_t;

uw_cross_desc_t uw_cross_defaults(void);
int             uw_cross(uw_db_t* db, const uw_cross_desc_t* d);

#ifdef __cplusplus
}
#endif

#endif /* UW_PUBLIC_H */
