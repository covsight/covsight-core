/*
 * ucis_writer.h - UCIS 1.0 write-streaming to UCIS-XML, in one file.
 *
 * GENERATED FILE - DO NOT EDIT.
 * Regenerate with:  python3 tools/amalgamate.py --output c/ucis-writer/include/ucis_writer.h
 * Sources:          c/ucis-writer/src/
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Usage: include this header anywhere for declarations. In exactly ONE
 * translation unit, define UCIS_WRITER_IMPLEMENTATION before including it:
 *
 *     #define UCIS_WRITER_IMPLEMENTATION
 *     #include "ucis_writer.h"
 */
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

#endif /* UW_PUBLIC_H *//*
 * ucis_writer - vendorable UCIS 1.0 write-streaming library (UCIS-XML backend)
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
 * This header declares the subset of the Accellera UCIS 1.0 (June 2012) C API
 * needed to *write-stream* a coverage database, plus a small number of
 * `ucis_writer_*` extensions that the standard does not provide (output sinks,
 * error inspection).
 *
 * The token names, signatures, enum/struct layouts and one-hot type bits below
 * are mandated by the standard. Non-write-streaming entry points (read,
 * iteration, query, merge) are deliberately absent rather than stubbed; see
 * decision D4 in docs/ucis-writer-impl-plan.md.
 *
 * This header and c/ucis/include/ucis.h define the same UCIS 1.0 identifiers
 * by design. They cannot both be included in one translation unit.
 */

#ifndef UCIS_WRITER_API_H
#define UCIS_WRITER_API_H

#ifdef UCIS_API_H
#  error "ucis_writer.h and ucis.h both define the UCIS 1.0 API; include only one"
#endif

#include <stddef.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------
 *  Core opaque types  (UCIS 1.0 Annex B)
 * ---------------------------------------------------------------------- */

typedef uint64_t       ucisObjTypeT;
typedef ucisObjTypeT   ucisScopeTypeT;
typedef ucisObjTypeT   ucisCoverTypeT;
typedef uint64_t       ucisScopeMaskTypeT;
typedef uint64_t       ucisCoverMaskTypeT;

#ifndef DEFINE_UCIST
#define DEFINE_UCIST
typedef void* ucisT;
#endif

typedef void* ucisScopeT;
typedef void* ucisObjT;         /* a ucisScopeT or a ucisHistoryNodeT */
typedef void* ucisFileHandleT;
typedef void* ucisHistoryNodeT;

typedef unsigned int ucisFlagsT;

/* ------------------------------------------------------------------------
 *  Source location
 * ---------------------------------------------------------------------- */

typedef struct {
    ucisFileHandleT filehandle;
    int             line;
    int             token;
} ucisSourceInfoT;

/* ------------------------------------------------------------------------
 *  Error reporting
 * ---------------------------------------------------------------------- */

typedef enum {
    UCIS_MSG_INFO,
    UCIS_MSG_WARNING,
    UCIS_MSG_ERROR
} ucisMsgSeverityT;

typedef struct ucisErr_s {
    int               msgno;
    ucisMsgSeverityT  severity;
    const char*       msgstr;
} ucisErrorT;

typedef void (*ucis_ErrorHandler)(void* userdata, ucisErrorT* errdata);

void ucis_RegisterErrorHandler(ucis_ErrorHandler errHandle, void* userdata);

/* ------------------------------------------------------------------------
 *  Writer extensions (not part of UCIS 1.0)
 *
 *  The standard has no notion of an output sink and no way to ask a database
 *  what went wrong. Both are needed by the consumers this library targets, so
 *  they are provided under a distinct `ucis_writer_` prefix.
 * ---------------------------------------------------------------------- */

ucisT ucis_writer_OpenSinkStream(const ucisWriterSinkT* sink);

/* First error latched on `db`, or UCIS_WRITER_OK. Never resets. */
ucisWriterStatusT ucis_writer_error(ucisT db);

/* Human-readable form of the latched error, including context where the
 * library recorded any. Valid until the next call on `db`. Never NULL. */
const char* ucis_writer_error_string(ucisT db);

/* Static description of a status code. Never NULL. */
const char* ucis_writer_status_name(ucisWriterStatusT status);

/* Count of non-fatal repairs made to the output: sanitised control
 * characters, invalid UTF-8 bytes, clamped positiveInteger fields. */
unsigned long ucis_writer_warnings(ucisT db);

/* Total bytes handed to the sink so far. */
uint64_t ucis_writer_bytes_written(ucisT db);

/* Emit newlines and indentation. Off by default: pretty output is ~8% larger
 * and exists for goldens and for reading small documents by eye. */
int ucis_writer_set_pretty(ucisT db, int enable);

/* Override the root element's @writtenBy. Defaults to the vendor identity. */
int ucis_writer_set_written_by(ucisT db, const char* who);

/* Override the root element's @writtenTime, which otherwise is the current
 * UTC time. Must be an xsd:dateTime. Set it to make output reproducible. */
int ucis_writer_set_written_time(ucisT db, const char* xsd_datetime);

/* ------------------------------------------------------------------------
 *  Database lifecycle (write-streaming only)
 * ---------------------------------------------------------------------- */

/* Open `name` for write-streaming. Returns NULL if the file cannot be
 * created. */
ucisT ucis_OpenWriteStream(const char* name);

/* Flush the in-flight object. UCIS 1.0 Annex A.14. */
int ucis_WriteStream(ucisT db);

/* Flush the current scope and pop to its parent. UCIS 1.0 Annex A.14. */
int ucis_WriteStreamScope(ucisT db);

/* Finish the document, close the sink, release `db`. Returns 0 on success;
 * non-zero if any error was latched at any point during the stream. The
 * handle is invalid afterwards either way. */
int ucis_Close(ucisT db);

/* Hierarchy path separator (default '/'). */
int  ucis_SetPathSeparator(ucisT db, char separator);
char ucis_GetPathSeparator(ucisT db);

/* ------------------------------------------------------------------------
 *  File handles
 * ---------------------------------------------------------------------- */

ucisFileHandleT ucis_CreateFileHandle(ucisT       db,
                                      const char* filename,
                                      const char* fileworkdir);

ucisFileHandleT ucis_CreateSrcFileHandle(ucisT       db,
                                         ucisScopeT  du_scope,
                                         const char* filename,
                                         const char* fileworkdir);

const char* ucis_GetFileName(ucisT db, ucisFileHandleT filehandle);

/* ------------------------------------------------------------------------
 *  History nodes
 * ---------------------------------------------------------------------- */

typedef int ucisHistoryNodeKindT;

#define UCIS_HISTORYNODE_NONE   -1
#define UCIS_HISTORYNODE_ALL     0
#define UCIS_HISTORYNODE_TEST    1
#define UCIS_HISTORYNODE_MERGE   2

#define UCIS_SIM_TOOL        "UCIS:Simulator"
#define UCIS_FORMAL_TOOL     "UCIS:Formal"
#define UCIS_ANALOG_TOOL     "UCIS:Analog"
#define UCIS_EMULATOR_TOOL   "UCIS:Emulator"
#define UCIS_MERGE_TOOL      "UCIS:Merge"

typedef enum {
    UCIS_TESTSTATUS_OK,
    UCIS_TESTSTATUS_WARNING,
    UCIS_TESTSTATUS_ERROR,
    UCIS_TESTSTATUS_FATAL,
    UCIS_TESTSTATUS_MISSING,
    UCIS_TESTSTATUS_MERGE_ERROR
} ucisTestStatusT;

typedef struct {
    ucisTestStatusT  teststatus;
    double           simtime;
    const char*      timeunit;
    const char*      runcwd;
    double           cputime;
    const char*      seed;
    const char*      cmd;
    const char*      args;
    int              compulsory;
    const char*      date;
    const char*      username;
    double           cost;
    const char*      toolcategory;
} ucisTestDataT;

ucisHistoryNodeT ucis_CreateHistoryNode(ucisT                 db,
                                        ucisHistoryNodeT      parent,
                                        char*                 logicalname,
                                        char*                 physicalname,
                                        ucisHistoryNodeKindT  kind);

int ucis_SetTestData(ucisT db, ucisHistoryNodeT node, ucisTestDataT* data);

/* ------------------------------------------------------------------------
 *  Scope types  (one-hot bits for ucisScopeTypeT; values are normative)
 * ---------------------------------------------------------------------- */

#define UCIS_TOGGLE          ((ucisScopeTypeT)0x0000000000000001ULL)
#define UCIS_BRANCH          ((ucisScopeTypeT)0x0000000000000002ULL)
#define UCIS_EXPR            ((ucisScopeTypeT)0x0000000000000004ULL)
#define UCIS_COND            ((ucisScopeTypeT)0x0000000000000008ULL)
#define UCIS_INSTANCE        ((ucisScopeTypeT)0x0000000000000010ULL)
#define UCIS_PROCESS         ((ucisScopeTypeT)0x0000000000000020ULL)
#define UCIS_BLOCK           ((ucisScopeTypeT)0x0000000000000040ULL)
#define UCIS_FUNCTION        ((ucisScopeTypeT)0x0000000000000080ULL)
#define UCIS_FORKJOIN        ((ucisScopeTypeT)0x0000000000000100ULL)
#define UCIS_GENERATE        ((ucisScopeTypeT)0x0000000000000200ULL)
#define UCIS_GENERIC         ((ucisScopeTypeT)0x0000000000000400ULL)
#define UCIS_CLASS           ((ucisScopeTypeT)0x0000000000000800ULL)
#define UCIS_COVERGROUP      ((ucisScopeTypeT)0x0000000000001000ULL)
#define UCIS_COVERINSTANCE   ((ucisScopeTypeT)0x0000000000002000ULL)
#define UCIS_COVERPOINT      ((ucisScopeTypeT)0x0000000000004000ULL)
#define UCIS_CROSS           ((ucisScopeTypeT)0x0000000000008000ULL)
#define UCIS_COVER           ((ucisScopeTypeT)0x0000000000010000ULL)
#define UCIS_ASSERT          ((ucisScopeTypeT)0x0000000000020000ULL)
#define UCIS_PROGRAM         ((ucisScopeTypeT)0x0000000000040000ULL)
#define UCIS_PACKAGE         ((ucisScopeTypeT)0x0000000000080000ULL)
#define UCIS_TASK            ((ucisScopeTypeT)0x0000000000100000ULL)
#define UCIS_INTERFACE       ((ucisScopeTypeT)0x0000000000200000ULL)
#define UCIS_FSM             ((ucisScopeTypeT)0x0000000000400000ULL)
#define UCIS_TESTPLAN        ((ucisScopeTypeT)0x0000000000800000ULL)
#define UCIS_DU_MODULE       ((ucisScopeTypeT)0x0000000001000000ULL)
#define UCIS_DU_ARCH         ((ucisScopeTypeT)0x0000000002000000ULL)
#define UCIS_DU_PACKAGE      ((ucisScopeTypeT)0x0000000004000000ULL)
#define UCIS_DU_PROGRAM      ((ucisScopeTypeT)0x0000000008000000ULL)
#define UCIS_DU_INTERFACE    ((ucisScopeTypeT)0x0000000010000000ULL)
#define UCIS_FSM_STATES      ((ucisScopeTypeT)0x0000000020000000ULL)
#define UCIS_FSM_TRANS       ((ucisScopeTypeT)0x0000000040000000ULL)
#define UCIS_COVBLOCK        ((ucisScopeTypeT)0x0000000080000000ULL)
#define UCIS_CVGBINSCOPE     ((ucisScopeTypeT)0x0000000100000000ULL)
#define UCIS_ILLEGALBINSCOPE ((ucisScopeTypeT)0x0000000200000000ULL)
#define UCIS_IGNOREBINSCOPE  ((ucisScopeTypeT)0x0000000400000000ULL)
#define UCIS_BBLOCKSCOPE     ((ucisScopeTypeT)0x0000000800000000ULL)
#define UCIS_GROUP           ((ucisScopeTypeT)0x0000001000000000ULL)
#define UCIS_TRANSITION      ((ucisScopeTypeT)0x0000002000000000ULL)
#define UCIS_RESERVEDSCOPE   ((ucisScopeTypeT)0xFF00000000000000ULL)
#define UCIS_SCOPE_ERROR     ((ucisScopeTypeT)0x0000000000000000ULL)

#define UCIS_DU_ANY ((ucisScopeMaskTypeT)(UCIS_DU_MODULE | UCIS_DU_ARCH | \
                                          UCIS_DU_PACKAGE | UCIS_DU_PROGRAM | \
                                          UCIS_DU_INTERFACE))

#define UCIS_CVG_SCOPE ((ucisScopeMaskTypeT)(UCIS_COVERGROUP | UCIS_COVERINSTANCE | \
                                             UCIS_COVERPOINT  | UCIS_CVGBINSCOPE | \
                                             UCIS_ILLEGALBINSCOPE | UCIS_IGNOREBINSCOPE | \
                                             UCIS_CROSS))

/* ------------------------------------------------------------------------
 *  Cover (bin) types  (one-hot bits for ucisCoverTypeT)
 * ---------------------------------------------------------------------- */

#define UCIS_CVGBIN          ((ucisCoverTypeT)0x0000000000000001ULL)
#define UCIS_COVERBIN        ((ucisCoverTypeT)0x0000000000000002ULL)
#define UCIS_ASSERTBIN       ((ucisCoverTypeT)0x0000000000000004ULL)
#define UCIS_SCBIN           ((ucisCoverTypeT)0x0000000000000008ULL)
#define UCIS_ZINBIN          ((ucisCoverTypeT)0x0000000000000010ULL)
#define UCIS_STMTBIN         ((ucisCoverTypeT)0x0000000000000020ULL)
#define UCIS_BRANCHBIN       ((ucisCoverTypeT)0x0000000000000040ULL)
#define UCIS_EXPRBIN         ((ucisCoverTypeT)0x0000000000000080ULL)
#define UCIS_CONDBIN         ((ucisCoverTypeT)0x0000000000000100ULL)
#define UCIS_TOGGLEBIN       ((ucisCoverTypeT)0x0000000000000200ULL)
#define UCIS_PASSBIN         ((ucisCoverTypeT)0x0000000000000400ULL)
#define UCIS_FSMBIN          ((ucisCoverTypeT)0x0000000000000800ULL)
#define UCIS_USERBIN         ((ucisCoverTypeT)0x0000000000001000ULL)
#define UCIS_GENERICBIN      UCIS_USERBIN
#define UCIS_COUNT           ((ucisCoverTypeT)0x0000000000002000ULL)
#define UCIS_FAILBIN         ((ucisCoverTypeT)0x0000000000004000ULL)
#define UCIS_VACUOUSBIN      ((ucisCoverTypeT)0x0000000000008000ULL)
#define UCIS_DISABLEDBIN     ((ucisCoverTypeT)0x0000000000010000ULL)
#define UCIS_ATTEMPTBIN      ((ucisCoverTypeT)0x0000000000020000ULL)
#define UCIS_ACTIVEBIN       ((ucisCoverTypeT)0x0000000000040000ULL)
#define UCIS_IGNOREBIN       ((ucisCoverTypeT)0x0000000000080000ULL)
#define UCIS_ILLEGALBIN      ((ucisCoverTypeT)0x0000000000100000ULL)
#define UCIS_DEFAULTBIN      ((ucisCoverTypeT)0x0000000000200000ULL)
#define UCIS_PEAKACTIVEBIN   ((ucisCoverTypeT)0x0000000000400000ULL)
#define UCIS_BLOCKBIN        ((ucisCoverTypeT)0x0000000001000000ULL)
#define UCIS_USERBITS        ((ucisCoverTypeT)0x00000000FE000000ULL)
#define UCIS_RESERVEDBIN     ((ucisCoverTypeT)0xFF00000000000000ULL)

#define UCIS_STATEBIN UCIS_FSMBIN
#define UCIS_TRANSBIN UCIS_FSMBIN

/* ------------------------------------------------------------------------
 *  Scope flags
 * ---------------------------------------------------------------------- */

#define UCIS_INST_ONCE                  0x00000001U
#define UCIS_ENABLED_STMT               0x00000002U
#define UCIS_ENABLED_BRANCH             0x00000004U
#define UCIS_ENABLED_COND               0x00000008U
#define UCIS_ENABLED_EXPR               0x00000010U
#define UCIS_ENABLED_FSM                0x00000020U
#define UCIS_ENABLED_TOGGLE             0x00000040U
#define UCIS_SCOPE_UNDER_DU             0x00000100U
#define UCIS_SCOPE_EXCLUDED             0x00000200U
#define UCIS_SCOPE_PRAGMA_EXCLUDED      0x00000400U
#define UCIS_SCOPE_PRAGMA_CLEARED       0x00000800U
#define UCIS_SCOPE_SPECIALIZED          0x00001000U
#define UCIS_UOR_SAFE_SCOPE             0x00002000U
#define UCIS_UOR_SAFE_SCOPE_ALLCOVERS   0x00004000U
#define UCIS_IS_TOP_NODE                0x00010000U
#define UCIS_IS_IMMEDIATE_ASSERT        0x00010000U
#define UCIS_SCOPE_CVG_AUTO             0x00010000U
#define UCIS_SCOPE_CVG_SCALAR           0x00020000U
#define UCIS_SCOPE_CVG_VECTOR           0x00040000U
#define UCIS_SCOPE_CVG_TRANSITION       0x00080000U
#define UCIS_SCOPE_IFF_EXISTS           0x00100000U
#define UCIS_SCOPE_SAMPLE_TRUE          0x00200000U
#define UCIS_ENABLED_BLOCK              0x00800000U
#define UCIS_SCOPE_BLOCK_ISBRANCH       0x01000000U
#define UCIS_SCOPE_EXPR_ISHIERARCHICAL  0x02000000U
#define UCIS_SCOPEFLAG_MARK             0x08000000U
#define UCIS_SCOPE_INTERNAL             0xF0000000U

/* ------------------------------------------------------------------------
 *  Coveritem flags
 * ---------------------------------------------------------------------- */

#define UCIS_IS_32BIT             0x00000001U
#define UCIS_IS_64BIT             0x00000002U
#define UCIS_IS_VECTOR            0x00000004U
#define UCIS_HAS_GOAL             0x00000008U
#define UCIS_HAS_WEIGHT           0x00000010U
#define UCIS_EXCLUDE_PRAGMA       0x00000020U
#define UCIS_EXCLUDE_FILE         0x00000040U
#define UCIS_EXCLUDE_INST         0x00000080U
#define UCIS_EXCLUDE_AUTO         0x00000100U
#define UCIS_ENABLED              0x00000200U
#define UCIS_HAS_LIMIT            0x00000400U
#define UCIS_HAS_COUNT            0x00000800U
#define UCIS_IS_COVERED           0x00001000U
#define UCIS_UOR_SAFE_COVERITEM   0x00002000U
#define UCIS_CLEAR_PRAGMA         0x00004000U
#define UCIS_HAS_ACTION           0x00010000U
#define UCIS_IS_TLW_ENABLED       0x00020000U
#define UCIS_LOG_ON               0x00040000U
#define UCIS_IS_EOS_NOTE          0x00080000U
#define UCIS_IS_FSM_RESET         0x00010000U
#define UCIS_IS_FSM_TRAN          0x00020000U
#define UCIS_IS_BR_ELSE           0x00010000U
#define UCIS_BIN_IFF_EXISTS       0x00010000U
#define UCIS_BIN_SAMPLE_TRUE      0x00020000U
#define UCIS_IS_CROSSAUTO         0x00040000U

#define UCIS_EXCLUDED  (UCIS_EXCLUDE_FILE | UCIS_EXCLUDE_PRAGMA | \
                        UCIS_EXCLUDE_INST | UCIS_EXCLUDE_AUTO)

/* ------------------------------------------------------------------------
 *  Source language
 * ---------------------------------------------------------------------- */

typedef enum {
    UCIS_VHDL,
    UCIS_VLOG,
    UCIS_SV,
    UCIS_SYSTEMC,
    UCIS_PSL_VHDL,
    UCIS_PSL_VLOG,
    UCIS_PSL_SV,
    UCIS_PSL_SYSTEMC,
    UCIS_E,
    UCIS_VERA,
    UCIS_NONE,
    UCIS_OTHER,
    UCIS_SOURCE_ERROR
} ucisSourceT;

/* ------------------------------------------------------------------------
 *  Scope creation
 *
 *  In write-streaming mode `parent` must be NULL: the parent is the implicit
 *  current scope. Passing non-NULL is a UCIS_WRITER_ERR_USAGE.
 * ---------------------------------------------------------------------- */

ucisScopeT ucis_CreateScope(ucisT             db,
                            ucisScopeT        parent,
                            const char*       name,
                            ucisSourceInfoT*  srcinfo,
                            int               weight,
                            ucisSourceT       source,
                            ucisScopeTypeT    type,
                            ucisFlagsT        flags);

ucisScopeT ucis_CreateInstanceByName(ucisT             db,
                                     ucisScopeT        parent,
                                     const char*       name,
                                     ucisSourceInfoT*  srcinfo,
                                     int               weight,
                                     ucisSourceT       source,
                                     ucisScopeTypeT    type,
                                     char*             du_name,
                                     int               flags);

typedef enum {
    UCIS_TOGGLE_METRIC_NOBINS = 1,
    UCIS_TOGGLE_METRIC_ENUM,
    UCIS_TOGGLE_METRIC_TRANSITION,
    UCIS_TOGGLE_METRIC_2STOGGLE,
    UCIS_TOGGLE_METRIC_ZTOGGLE,
    UCIS_TOGGLE_METRIC_XTOGGLE
} ucisToggleMetricT;

typedef enum {
    UCIS_TOGGLE_TYPE_NET = 1,
    UCIS_TOGGLE_TYPE_REG = 2
} ucisToggleTypeT;

typedef enum {
    UCIS_TOGGLE_DIR_INTERNAL = 1,
    UCIS_TOGGLE_DIR_IN,
    UCIS_TOGGLE_DIR_OUT,
    UCIS_TOGGLE_DIR_INOUT
} ucisToggleDirT;

ucisScopeT ucis_CreateToggle(ucisT              db,
                             ucisScopeT         parent,
                             const char*        name,
                             const char*        canonical_name,
                             ucisFlagsT         flags,
                             ucisToggleMetricT  toggle_metric,
                             ucisToggleTypeT    toggle_type,
                             ucisToggleDirT     toggle_dir);

ucisScopeT ucis_CreateCrossByName(ucisT             db,
                                  ucisScopeT        parent,
                                  const char*       name,
                                  ucisSourceInfoT*  srcinfo,
                                  int               weight,
                                  ucisSourceT       source,
                                  int               num_points,
                                  char**            point_names);

const char* ucis_ComposeDUName(const char* library_name,
                               const char* primary_name,
                               const char* secondary_name);

/* ------------------------------------------------------------------------
 *  Coveritems
 * ---------------------------------------------------------------------- */

typedef union {
    uint64_t        int64;       /* if UCIS_IS_64BIT */
    uint32_t        int32;       /* if UCIS_IS_32BIT */
    unsigned char*  bytevector;  /* if UCIS_IS_VECTOR */
} ucisCoverDataValueT;

typedef struct {
    ucisCoverTypeT       type;
    ucisFlagsT           flags;
    ucisCoverDataValueT  data;
    int                  goal;     /* if UCIS_HAS_GOAL */
    int                  weight;   /* if UCIS_HAS_WEIGHT */
    int                  limit;    /* if UCIS_HAS_LIMIT */
    int                  bitlen;   /* bitlen of data.bytevector */
} ucisCoverDataT;

int ucis_CreateNextCover(ucisT             db,
                         ucisScopeT        parent,
                         const char*       name,
                         ucisCoverDataT*   data,
                         ucisSourceInfoT*  sourceinfo);

/* ------------------------------------------------------------------------
 *  Properties
 * ---------------------------------------------------------------------- */

typedef enum {
    UCIS_INT_IS_MODIFIED,
    UCIS_INT_MODIFIED_SINCE_SIM,
    UCIS_INT_NUM_TESTS,
    UCIS_INT_SCOPE_WEIGHT,
    UCIS_INT_SCOPE_GOAL,
    UCIS_INT_SCOPE_SOURCE_TYPE,
    UCIS_INT_NUM_CROSSED_CVPS,
    UCIS_INT_SCOPE_IS_UNDER_DU,
    UCIS_INT_SCOPE_IS_UNDER_COVERINSTANCE,
    UCIS_INT_SCOPE_NUM_COVERITEMS,
    UCIS_INT_SCOPE_NUM_EXPR_TERMS,
    UCIS_INT_TOGGLE_TYPE,
    UCIS_INT_TOGGLE_DIR,
    UCIS_INT_TOGGLE_COVERED,
    UCIS_INT_BRANCH_HAS_ELSE,
    UCIS_INT_BRANCH_ISCASE,
    UCIS_INT_COVER_GOAL,
    UCIS_INT_COVER_LIMIT,
    UCIS_INT_COVER_WEIGHT,
    UCIS_INT_TEST_STATUS,
    UCIS_INT_TEST_COMPULSORY,
    UCIS_INT_STMT_INDEX,
    UCIS_INT_BRANCH_COUNT,
    UCIS_INT_FSM_STATEVAL,
    UCIS_INT_CVG_ATLEAST,
    UCIS_INT_CVG_AUTOBINMAX,
    UCIS_INT_CVG_DETECTOVERLAP,
    UCIS_INT_CVG_NUMPRINTMISSING,
    UCIS_INT_CVG_STROBE,
    UCIS_INT_CVG_PERINSTANCE,
    UCIS_INT_CVG_GETINSTCOV,
    UCIS_INT_CVG_MERGEINSTANCES,
    UCIS_INT_TOGGLE_METRIC
} ucisIntPropertyEnumT;

typedef enum {
    UCIS_STR_FILE_NAME,
    UCIS_STR_SCOPE_NAME,
    UCIS_STR_SCOPE_HIER_NAME,
    UCIS_STR_INSTANCE_DU_NAME,
    UCIS_STR_UNIQUE_ID,
    UCIS_STR_VER_STANDARD,
    UCIS_STR_VER_STANDARD_VERSION,
    UCIS_STR_VER_VENDOR_ID,
    UCIS_STR_VER_VENDOR_TOOL,
    UCIS_STR_VER_VENDOR_VERSION,
    UCIS_STR_GENERIC,
    UCIS_STR_ITH_CROSSED_CVP_NAME,
    UCIS_STR_HIST_CMDLINE,
    UCIS_STR_HIST_RUNCWD,
    UCIS_STR_COMMENT,
    UCIS_STR_TEST_TIMEUNIT,
    UCIS_STR_TEST_DATE,
    UCIS_STR_TEST_SIMARGS,
    UCIS_STR_TEST_USERNAME,
    UCIS_STR_TEST_NAME,
    UCIS_STR_TEST_SEED,
    UCIS_STR_TEST_HOSTNAME,
    UCIS_STR_TEST_HOSTOS,
    UCIS_STR_EXPR_TERMS,
    UCIS_STR_TOGGLE_CANON_NAME,
    UCIS_STR_UNIQUE_ID_ALIAS,
    UCIS_STR_DESIGN_VERSION_ID,
    UCIS_STR_DU_SIGNATURE,
    UCIS_STR_HIST_TOOLCATEGORY,
    UCIS_STR_HIST_LOG_NAME,
    UCIS_STR_HIST_PHYS_NAME,
    UCIS_STR_FSM_STATEVAR
} ucisStringPropertyEnumT;

typedef enum {
    UCIS_REAL_HIST_CPUTIME,
    UCIS_REAL_TEST_SIMTIME,
    UCIS_REAL_TEST_COST,
    UCIS_REAL_CVG_INST_AVERAGE
} ucisRealPropertyEnumT;

int ucis_SetIntProperty(ucisT                 db,
                        ucisObjT              obj,
                        int                   coverindex,
                        ucisIntPropertyEnumT  property,
                        int                   value);

int ucis_SetStringProperty(ucisT                    db,
                           ucisObjT                 obj,
                           int                      coverindex,
                           ucisStringPropertyEnumT  property,
                           const char*              value);

int ucis_SetRealProperty(ucisT                  db,
                         ucisObjT               obj,
                         int                    coverindex,
                         ucisRealPropertyEnumT  property,
                         double                 value);

/* ------------------------------------------------------------------------
 *  Typed attributes
 * ---------------------------------------------------------------------- */

typedef enum {
    UCIS_ATTR_INT,
    UCIS_ATTR_FLOAT,
    UCIS_ATTR_DOUBLE,
    UCIS_ATTR_STRING,
    UCIS_ATTR_MEMBLK,
    UCIS_ATTR_INT64
} ucisAttrTypeT;

typedef struct {
    ucisAttrTypeT type;
    union {
        int64_t      i64value;
        int          ivalue;
        float        fvalue;
        double       dvalue;
        const char*  svalue;
        struct {
            int             size;
            unsigned char*  data;
        } mvalue;
    } u;
} ucisAttrValueT;

int ucis_AttrAdd(ucisT db, ucisObjT obj, int coverindex,
                 const char* key, ucisAttrValueT* value);

#ifdef __cplusplus
}
#endif

#endif /* UCIS_WRITER_API_H */
#ifdef UCIS_WRITER_IMPLEMENTATION
#ifndef UCIS_WRITER_IMPLEMENTED
#define UCIS_WRITER_IMPLEMENTED

/* One translation unit: give every internal helper internal linkage. */
#define UCIS_WRITER_AMALGAMATED 1

/* ==== uw_config.h ============================================================= */

/* uw_config.h - build-time knobs and internal linkage decoration.
 * SPDX-License-Identifier: Apache-2.0 */



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

/* ==== uw_buf.h ================================================================ */

/* uw_buf.h - output buffer, sink dispatch, integer formatting.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Work items 1.1 and 1.2 of docs/ucis-writer-impl-plan.md.
 *
 * The buffer owns the only path to the sink. Every byte of the document goes
 * through it, so this is where the sticky error state lives: once a write
 * fails, `status` latches and all subsequent output is discarded silently.
 * That is deliberate — a coverage writer must not turn an I/O failure into a
 * crash of the simulation it is instrumenting. */



typedef struct uw_buf_s {
    char*            data;      /* not owned; storage supplied by the db      */
    size_t           cap;
    size_t           len;
    ucisWriterSinkT  sink;
    int              status;    /* ucisWriterStatusT, latched                 */
    uint64_t         bytes_out; /* bytes handed to the sink, buffered ones included */
} uw_buf_t;

UW_INTERNAL void uw_buf_init(uw_buf_t* b, char* storage, size_t cap,
                             const ucisWriterSinkT* sink);

/* Latch `status` if nothing is latched yet. Returns the latched status. */
UW_INTERNAL int uw_buf_fail(uw_buf_t* b, int status);

/* Push everything buffered to the sink. */
UW_INTERNAL int uw_buf_flush(uw_buf_t* b);

/* Flush, then close the sink. Idempotent. */
UW_INTERNAL int uw_buf_finish(uw_buf_t* b);

UW_INTERNAL int uw_buf_write(uw_buf_t* b, const char* s, size_t n);
UW_INTERNAL int uw_buf_putc(uw_buf_t* b, char c);
UW_INTERNAL int uw_buf_puts(uw_buf_t* b, const char* s);

/* Decimal formatters. Hand-rolled rather than snprintf: snprintf is locale
 * sensitive, costs a format-string parse per call, and shows up as a
 * measurable fraction of runtime when a document contains tens of millions of
 * integers. */
UW_INTERNAL int uw_buf_u32(uw_buf_t* b, uint32_t v);
UW_INTERNAL int uw_buf_u64(uw_buf_t* b, uint64_t v);
UW_INTERNAL int uw_buf_i64(uw_buf_t* b, int64_t v);

/* Shortest round-tripping decimal form of a finite double; non-finite values
 * are written as 0 (xsd:double accepts INF/NaN but consumers rarely do). */
UW_INTERNAL int uw_buf_double(uw_buf_t* b, double v);

/* Write a string literal without a strlen. */
#define UW_LIT(b, s) uw_buf_write((b), "" s, sizeof(s) - 1)

/* ==== uw_types.h ============================================================== */

/* uw_types.h - internal object layout.
 * SPDX-License-Identifier: Apache-2.0
 *
 * None of this is visible to a consumer: the public API traffics in void*
 * handles. It lives in a header only so the modules can share it. */



#include <stdio.h>

/* Handle validation. ucisT is void*, so a caller can hand us anything; a magic
 * word turns "passed the wrong pointer" into a clean UCIS_WRITER_ERR_USAGE
 * instead of a segfault inside a simulator. */
#define UW_DB_MAGIC 0x55435357u   /* 'UCSW' */

/* One entry per open XML element.
 *
 * `pending` means the start tag has been written as `<tag` and is still
 * accepting attributes. Because XML puts every attribute before the '>', we
 * can stream attributes straight into the output buffer as the caller sets
 * properties, and terminate the tag only when something else needs to be
 * written. That is why this library never buffers an object. */
typedef struct {
    const char*     tag;      /* XSD element name; always a string literal */
    ucisScopeTypeT  type;     /* UCIS scope type that produced it, 0 if synthetic */
    unsigned        stage;    /* monotonic ordering stage within this element */
    unsigned char   pending;
    unsigned char   haskids;
    /* Library-owned: a wrapper we opened on the caller's behalf (the root, a
     * coverage-kind element), not a scope the caller created. ucis_WriteStream-
     * Scope closes through these to reach the caller's scope, because the
     * caller never asked for them and cannot be expected to count them. */
    unsigned char   owned;

    /* Several XSD types (INSTANCE_COVERAGE, STATEMENT, ...) require an <id>
     * child before anything else. It cannot be written when the element opens,
     * because attributes may still be arriving; it must be written before the
     * first real child. So it rides along here and is emitted at the moment
     * the start tag is terminated. */
    unsigned char   needs_id;
    uint32_t        id_file;
    uint32_t        id_line;
    uint32_t        id_inline;

    /* EXPR is the awkward one. Its required @exprString/@index/@width and its
     * required subExpr+ children all derive from UCIS_STR_EXPR_TERMS, which
     * arrives as a property *after* the scope is created. Both therefore wait
     * until the start tag is terminated -- the attributes written just before
     * the '>', the subExpr elements just after. */
    unsigned char   needs_expr;
    uint32_t        expr_index;

    /* CGINSTANCE, COVERPOINT and CROSS each require an <options> element as
     * their first child, and cgInstance a <cgId> straight after it. Every
     * attribute those two carry arrives as a property *after* the scope is
     * created, so like <id> they wait here and go out when the start tag is
     * terminated. UW_OPT_* selects which of the three option types it is;
     * they share a shape but not an attribute list. */
    unsigned char   needs_options;
    unsigned char   needs_cgid;
} uw_elem_t;

/* ---- covergroup options (design 6.2) ---------------------------------- */

/* Which fields the caller actually set. Every option attribute has a schema
 * default, so emitting one the caller never mentioned would assert a value we
 * were not told -- and on a document with thousands of coverpoints it would
 * cost real bytes to say nothing. */
enum {
    UW_OPTSET_WEIGHT         = 1u << 0,
    UW_OPTSET_GOAL           = 1u << 1,
    UW_OPTSET_AT_LEAST       = 1u << 2,
    UW_OPTSET_AUTO_BIN_MAX   = 1u << 3,
    UW_OPTSET_DETECT_OVERLAP = 1u << 4,
    UW_OPTSET_PER_INSTANCE   = 1u << 5,
    UW_OPTSET_MERGE_INST     = 1u << 6,
    UW_OPTSET_PRINT_MISSING  = 1u << 7
};

typedef struct {
    int32_t  weight;
    int32_t  goal;
    int32_t  at_least;
    int32_t  auto_bin_max;
    int32_t  num_print_missing;
    int32_t  detect_overlap;
    int32_t  per_instance;
    int32_t  merge_instances;
    uint32_t set;
} uw_opts_t;

/* Only one element can have options pending at a time: uw_el_begin commits the
 * parent before pushing a child, so a cgInstance's options are already out by
 * the time its first coverpoint exists. One staging area therefore suffices,
 * and uw_elem_t stays small. */

/* ---- coverpoint bin ordinals, for cross indices (D12) ----------------- */

/* CROSS_BIN requires index+ : the position of the participating bin within
 * each crossed coverpoint. The C API gives a cross bin only its name, which
 * for "a,b" is the two coverpoint bin names joined -- so the indices have to
 * be recovered by looking those names up.
 *
 * We store a 64-bit hash of (coverpoint ordinal, bin name) rather than the
 * name itself: 12 bytes per bin with no allocation per bin, and no caller
 * string retained. A collision would silently mis-index one cross bin; over a
 * million bins the chance of any collision at all is about 3e-8, which is well
 * below the rate at which the data being described is itself wrong. */
typedef struct {
    uint64_t hash;    /* 0 means an empty slot */
    uint32_t index;
} uw_cvpslot_t;

typedef struct {
    uw_cvpslot_t* slots;
    size_t        cap;         /* power of two, 0 until the first insert */
    size_t        used;
    uint32_t      cvp_count;   /* coverpoints seen so far in this cgInstance */
    uint32_t      bin_count;   /* bins seen so far in the current coverpoint */
} uw_cvptab_t;

/* Crossed coverpoints named on the pending cross, via
 * UCIS_STR_ITH_CROSSED_CVP_NAME. Bounded rather than grown: a cross of more
 * than this many coverpoints has more bins than any tool will read. */
#define UW_MAX_CROSSED 16

/* ---- resident source-file table (design 3.4) -------------------------- */

/* Typedef'd in uw_public.h, which is where the handle is public. Defining it
 * again here would be a duplicate typedef -- legal in C11, not in C99. */
struct uw_file_s {
    struct uw_file_s* hnext;
    uint32_t          id;      /* 1-based; xsd:positiveInteger */
    size_t            len;
    char              name[1]; /* trailing allocation */
};

typedef struct {
    uw_file_t** buckets;
    size_t      nbuckets;
    uw_file_t** byid;      /* byid[i] is the file with id i+1 */
    size_t      byid_cap;
    uint32_t    count;
    int         sealed;    /* set when the first instance is created */
} uw_filetab_t;

/* ---- resident history nodes ------------------------------------------ */

typedef struct uw_hist_s {
    struct uw_hist_s*    next;
    uint32_t             id;
    uint32_t             parent_id;
    int                  has_parent;
    ucisHistoryNodeKindT kind;

    int      has_testdata;
    ucisTestStatusT teststatus;
    int      compulsory;
    double   simtime;
    double   cputime;
    double   cost;

    char* logicalname;
    char* physicalname;
    char* timeunit;
    char* runcwd;
    char* seed;
    char* cmd;
    char* args;
    char* date;
    char* username;
    char* toolcategory;

    char* vendor_id;
    char* vendor_tool;
    char* vendor_version;
    char* comment;
} uw_hist_t;

/* ---- document phase --------------------------------------------------- */

enum {
    UW_PHASE_TABLES = 0,   /* file handles and history nodes accepted */
    UW_PHASE_BODY   = 1,   /* tables emitted; instances streaming      */
    UW_PHASE_CLOSED = 2
};

/* ---- the database ----------------------------------------------------- */

struct uw_db_s {
    uint32_t      magic;
    uw_buf_t      buf;
    char*         bufstore;
    unsigned long warnings;
    int           pretty;
    char          path_sep;
    /* Two different failures, deliberately kept apart.
     *
     * `err` is the first thing that went wrong at all, including caller
     * mistakes: an ordering violation, a property set too late, a scope
     * created in the wrong place. Those drop one call and nothing else -- the
     * rest of the document is still worth writing and still valid, so output
     * continues and ucis_Close reports.
     *
     * `buf.status` is the subset that makes output impossible (I/O failure,
     * allocation failure). That one stops everything.
     *
     * Collapsing the two would mean a mistyped property call silently
     * truncates a six-hour simulation's coverage, which is far worse than the
     * mistake itself. */
    int           err;
    char          errctx[192];

    uw_elem_t     stack[UCIS_WRITER_MAX_DEPTH];
    int           depth;

    int           phase;
    char*         written_by;
    char*         written_time;

    /* Where the open instanceCoverages element sits on the stack (0 = none),
     * which coverage-kind wrapper is open inside it, and which of
     * BLOCK_COVERAGE's three mutually exclusive forms that wrapper committed
     * to. Together these are the whole of the in-instance ordering state. */
    int           inst_depth;
    unsigned      cur_kind;
    ucisCoverTypeT block_mode;

    /* Design units have no element in UCIS-XML; only the name survives, as an
     * instance's @moduleName. See D8. */
    char*         cur_du;

    /* Condition/expression state (D11). Fixed buffers rather than allocations:
     * only one expression can be pending and only one metric scope open at a
     * time, and both strings are short by construction -- operand names and
     * metric names like "UCIS:FULL". Overlong input is truncated and counted
     * rather than growing the writer's footprint per expression. */
    char          expr_terms[256];   /* '#'-separated operand list */
    /* Whether expr_terms came from UCIS_STR_EXPR_TERMS or is just the scope
     * name standing in. It matters because UOR scope names are themselves
     * '#'-delimited ("#cond#1#44#1#"), so splitting one would invent four
     * operands out of a file and line number. */
    int           expr_terms_explicit;
    char          metric[64];        /* enclosing metric scope, "" if none */
    uint32_t      expr_count;        /* per-instance ordinal for EXPR/@index */

    /* Functional coverage (Phase 4). The covergroup *type* scope has no
     * element in UCIS-XML -- CGINSTANCE carries the type's identity in its
     * cgId instead -- so it becomes a virtual scope and its name and source
     * location wait here for the cgInstance that will quote them. */
    uw_opts_t     opts;
    char          cg_name[128];
    uint32_t      cg_file;
    uint32_t      cg_line;
    char          cg_module[128];    /* enclosing instance's @moduleName */

    uint32_t      cross_cvp[UW_MAX_CROSSED];
    char          cross_names[UW_MAX_CROSSED][64];
    unsigned      cross_n;

    uw_cvptab_t   cvpbins;

    /* Document-wide vendor identity, inherited by history nodes that do not
     * override it. */
    char*         vendor_id;
    char*         vendor_tool;
    char*         vendor_version;

    uw_filetab_t  files;
    uw_hist_t*    hist_head;
    uw_hist_t*    hist_tail;
    uint32_t      hist_count;
    uint32_t      inst_count;

    FILE*         own_file;   /* non-NULL when opened by path */
};

/* Validate and cast a public handle. Returns NULL if it is not one of ours. */
UW_INTERNAL uw_db_t* uw_db_check(ucisT db);

/* Latch `status` and record context. `detail` may be NULL. Returns the latched
 * status, which is always non-zero, so internal call sites can propagate it
 * directly. Public entry points translate to the UCIS convention (0 / -1). */
UW_INTERNAL int uw_fail(uw_db_t* db, int status, const char* what,
                        const char* detail);

/* strdup that latches UCIS_WRITER_ERR_ALLOC on failure. NULL in, NULL out. */
UW_INTERNAL char* uw_strdup(uw_db_t* db, const char* s);

/* ==== uw_text.h =============================================================== */

/* uw_text.h - XML escaping and text sanitisation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Work items 1.3 and 1.4 of docs/ucis-writer-impl-plan.md.
 *
 * Caller strings are design identifiers, file paths and command lines. They
 * are not trusted to be XML-safe and, on real designs, they are not always
 * valid UTF-8 either. Everything that reaches the document goes through
 * uw_text_escape(), which guarantees well-formed XML 1.0 output for any byte
 * sequence, counting each repair it makes. */



/* Escape and emit `n` bytes. `nwarn` accumulates the number of bytes that had
 * to be replaced because XML 1.0 cannot represent them; it may be NULL. */
UW_INTERNAL int uw_text_escape(uw_buf_t* b, const char* s, size_t n,
                               unsigned long* nwarn);

UW_INTERNAL int uw_text_escape_cstr(uw_buf_t* b, const char* s,
                                    unsigned long* nwarn);

/* Emit ` name="value"`. `name` must be a literal produced by this library and
 * is written without escaping; `value` is escaped. A NULL `value` emits
 * nothing at all, which is how optional attributes are omitted. */
UW_INTERNAL int uw_text_attr(uw_buf_t* b, const char* name, const char* value,
                             unsigned long* nwarn);

/* As uw_text_attr, for a value that is a slice of a larger string rather than
 * a NUL-terminated one. */
UW_INTERNAL int uw_text_attr_n(uw_buf_t* b, const char* name, const char* value,
                               size_t len, unsigned long* nwarn);

UW_INTERNAL int uw_text_attr_u64(uw_buf_t* b, const char* name, uint64_t value);
UW_INTERNAL int uw_text_attr_i64(uw_buf_t* b, const char* name, int64_t value);
UW_INTERNAL int uw_text_attr_double(uw_buf_t* b, const char* name, double value);

/* ==== uw_stack.h ============================================================== */

/* uw_stack.h - XML element stack and the ordering stage machine.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Work item 1.5 of docs/ucis-writer-impl-plan.md. */



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

/* ==== uw_tables.h ============================================================= */

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

/* ==== uw_xml.h ================================================================ */

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

/* ==== uw_cvg.h ================================================================ */

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

/* ==== uw_api.h ================================================================ */

/* uw_api.h - internals shared between the entry points and the emitters.
 * SPDX-License-Identifier: Apache-2.0 */



/* Write the root element's required attributes and stop accepting changes to
 * them. Idempotent. */
UW_INTERNAL int uw_root_seal(uw_db_t* db);

/* Transition into the document body: seal the root, then emit the resident
 * source-file and history-node tables if they have not gone out yet. Every
 * entry point that writes body content calls this first. */
UW_INTERNAL int uw_body(uw_db_t* db);

/* ==== uw_buf.c ================================================================ */

/* uw_buf.c - output buffer, sink dispatch, integer formatting.
 * SPDX-License-Identifier: Apache-2.0 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

UW_INTERNAL void uw_buf_init(uw_buf_t* b, char* storage, size_t cap,
                             const ucisWriterSinkT* sink)
{
    b->data      = storage;
    b->cap       = cap;
    b->len       = 0;
    b->status    = UCIS_WRITER_OK;
    b->bytes_out = 0;
    if (sink) {
        b->sink = *sink;
    } else {
        b->sink.write = NULL;
        b->sink.close = NULL;
        b->sink.ctx   = NULL;
    }
}

UW_INTERNAL int uw_buf_fail(uw_buf_t* b, int status)
{
    if (b->status == UCIS_WRITER_OK) {
        b->status = status;
    }
    return b->status;
}

/* Hand `n` bytes straight to the sink, bypassing the buffer. */
static int uw_buf_emit(uw_buf_t* b, const char* s, size_t n)
{
    if (b->status != UCIS_WRITER_OK) {
        return b->status;
    }
    if (n == 0) {
        return UCIS_WRITER_OK;
    }
    if (b->sink.write == NULL) {
        return uw_buf_fail(b, UCIS_WRITER_ERR_STATE);
    }
    if (b->sink.write(b->sink.ctx, s, n) != 0) {
        return uw_buf_fail(b, UCIS_WRITER_ERR_IO);
    }
    b->bytes_out += (uint64_t)n;
    return UCIS_WRITER_OK;
}

UW_INTERNAL int uw_buf_flush(uw_buf_t* b)
{
    size_t n = b->len;
    b->len = 0;               /* drop the bytes even on failure: retrying a
                               * failed sink would double-write on recovery */
    if (b->status != UCIS_WRITER_OK) {
        return b->status;
    }
    return uw_buf_emit(b, b->data, n);
}

UW_INTERNAL int uw_buf_finish(uw_buf_t* b)
{
    int rc = uw_buf_flush(b);
    if (b->sink.close) {
        int (*close_fn)(void*) = b->sink.close;
        void* ctx = b->sink.ctx;
        b->sink.close = NULL;   /* idempotent */
        b->sink.write = NULL;
        if (close_fn(ctx) != 0) {
            rc = uw_buf_fail(b, UCIS_WRITER_ERR_IO);
        }
    } else {
        b->sink.write = NULL;
    }
    return rc;
}

UW_INTERNAL int uw_buf_write(uw_buf_t* b, const char* s, size_t n)
{
    if (b->status != UCIS_WRITER_OK) {
        return b->status;
    }
    if (n == 0) {
        return UCIS_WRITER_OK;
    }
    if (b->len + n <= b->cap) {
        memcpy(b->data + b->len, s, n);
        b->len += n;
        return UCIS_WRITER_OK;
    }
    /* Doesn't fit. Flush what we have, then either buffer or pass through. */
    if (uw_buf_flush(b) != UCIS_WRITER_OK) {
        return b->status;
    }
    if (n <= b->cap) {
        memcpy(b->data, s, n);
        b->len = n;
        return UCIS_WRITER_OK;
    }
    return uw_buf_emit(b, s, n);
}

UW_INTERNAL int uw_buf_putc(uw_buf_t* b, char c)
{
    if (b->status != UCIS_WRITER_OK) {
        return b->status;
    }
    if (b->len == b->cap && uw_buf_flush(b) != UCIS_WRITER_OK) {
        return b->status;
    }
    b->data[b->len++] = c;
    return UCIS_WRITER_OK;
}

UW_INTERNAL int uw_buf_puts(uw_buf_t* b, const char* s)
{
    if (s == NULL) {
        return b->status;
    }
    return uw_buf_write(b, s, strlen(s));
}

/* 20 digits is the widest uint64_t (18446744073709551615). */
#define UW_DIGITS_MAX 20

UW_INTERNAL int uw_buf_u64(uw_buf_t* b, uint64_t v)
{
    char tmp[UW_DIGITS_MAX];
    int  i = UW_DIGITS_MAX;

    if (v == 0) {
        return uw_buf_putc(b, '0');
    }
    while (v != 0) {
        tmp[--i] = (char)('0' + (int)(v % 10u));
        v /= 10u;
    }
    return uw_buf_write(b, tmp + i, (size_t)(UW_DIGITS_MAX - i));
}

UW_INTERNAL int uw_buf_u32(uw_buf_t* b, uint32_t v)
{
    return uw_buf_u64(b, (uint64_t)v);
}

UW_INTERNAL int uw_buf_i64(uw_buf_t* b, int64_t v)
{
    uint64_t mag;
    if (v < 0) {
        if (uw_buf_putc(b, '-') != UCIS_WRITER_OK) {
            return b->status;
        }
        /* Negate in unsigned space: -INT64_MIN overflows int64_t. */
        mag = (uint64_t)(-(v + 1)) + 1u;
    } else {
        mag = (uint64_t)v;
    }
    return uw_buf_u64(b, mag);
}

UW_INTERNAL int uw_buf_double(uw_buf_t* b, double v)
{
    char tmp[40];
    int  n;
    int  i;

    /* Only test-data fields (simtime, cputime, cost) are doubles, so a
     * handful per document; snprintf is affordable here where it is not in
     * the integer path. The round-trip check keeps the common case short. */
    if (!(v == v) || v > 1.0e308 || v < -1.0e308) {  /* NaN or infinity */
        return uw_buf_putc(b, '0');
    }
    n = snprintf(tmp, sizeof(tmp), "%.15g", v);
    if (n < 0 || (size_t)n >= sizeof(tmp)) {
        return uw_buf_putc(b, '0');
    }
    if (strtod(tmp, NULL) != v) {
        n = snprintf(tmp, sizeof(tmp), "%.17g", v);
        if (n < 0 || (size_t)n >= sizeof(tmp)) {
            return uw_buf_putc(b, '0');
        }
    }
    /* snprintf honours LC_NUMERIC; xsd:double does not. */
    for (i = 0; i < n; ++i) {
        if (tmp[i] == ',') {
            tmp[i] = '.';
        }
    }
    return uw_buf_write(b, tmp, (size_t)n);
}

/* ==== uw_text.c =============================================================== */

/* uw_text.c - XML escaping and text sanitisation.
 * SPDX-License-Identifier: Apache-2.0 */


#include <string.h>

enum {
    UW_CH_PLAIN  = 0,
    UW_CH_AMP    = 1,   /* &  */
    UW_CH_LT     = 2,   /* <  */
    UW_CH_GT     = 3,   /* >  */
    UW_CH_QUOT   = 4,   /* "  */
    UW_CH_APOS   = 5,   /* '  */
    UW_CH_NUMREF = 6,   /* tab/LF/CR: legal, but must be a char ref inside an
                         * attribute or the parser normalises it to a space */
    UW_CH_BAD    = 7    /* C0 control that XML 1.0 cannot represent at all */
};

/* Classification of the 128 ASCII bytes. Bytes >= 0x80 are handled by the
 * UTF-8 path and are not in this table. */
static const unsigned char uw_class_ascii[128] = {
    /* 0x00 */ 7, 7, 7, 7, 7, 7, 7, 7, 7, 6, 6, 7, 7, 6, 7, 7,
    /* 0x10 */ 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    /* 0x20 */ 0, 0, 4, 0, 0, 0, 1, 5, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x30 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 3, 0,
    /* 0x40 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x50 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x60 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x70 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

#if UCIS_WRITER_UTF8_CHECK
/* Length of the valid UTF-8 sequence starting at s[0], or 0 if the bytes there
 * are not one. Rejects overlong forms, surrogates (D800-DFFF, which are not
 * legal XML characters), and anything above U+10FFFF. */
static size_t uw_utf8_len(const unsigned char* s, size_t avail)
{
    unsigned char c0 = s[0];
    unsigned long cp;
    size_t        need;
    size_t        i;

    if (c0 < 0xC2u) {
        return 0;                       /* continuation byte, or overlong 2-byte */
    } else if (c0 < 0xE0u) {
        need = 2; cp = (unsigned long)(c0 & 0x1Fu);
    } else if (c0 < 0xF0u) {
        need = 3; cp = (unsigned long)(c0 & 0x0Fu);
    } else if (c0 < 0xF5u) {
        need = 4; cp = (unsigned long)(c0 & 0x07u);
    } else {
        return 0;
    }
    if (avail < need) {
        return 0;
    }
    for (i = 1; i < need; ++i) {
        if ((s[i] & 0xC0u) != 0x80u) {
            return 0;
        }
        cp = (cp << 6) | (unsigned long)(s[i] & 0x3Fu);
    }
    if (need == 3 && cp < 0x800ul) {
        return 0;                       /* overlong */
    }
    if (need == 4 && cp < 0x10000ul) {
        return 0;                       /* overlong */
    }
    if (cp >= 0xD800ul && cp <= 0xDFFFul) {
        return 0;                       /* surrogate: not an XML character */
    }
    if (cp > 0x10FFFFul || cp == 0xFFFEul || cp == 0xFFFFul) {
        return 0;
    }
    return need;
}
#endif /* UCIS_WRITER_UTF8_CHECK */

UW_INTERNAL int uw_text_escape(uw_buf_t* b, const char* s, size_t n,
                               unsigned long* nwarn)
{
    size_t i = 0;
    size_t run = 0;   /* start of the current copy-verbatim span */

    if (s == NULL || b->status != UCIS_WRITER_OK) {
        return b->status;
    }

    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        unsigned      cls;
        const char*   rep;
        size_t        replen;

        if (c >= 0x80u) {
#if UCIS_WRITER_UTF8_CHECK
            size_t seq = uw_utf8_len((const unsigned char*)s + i, n - i);
            if (seq != 0) {
                i += seq;               /* valid: stays in the verbatim span */
                continue;
            }
            cls = UW_CH_BAD;
#else
            i++;
            continue;
#endif
        } else {
            cls = uw_class_ascii[c];
            if (cls == UW_CH_PLAIN) {
                i++;
                continue;
            }
        }

        /* Emit everything up to here verbatim, then the replacement. */
        if (i > run) {
            if (uw_buf_write(b, s + run, i - run) != UCIS_WRITER_OK) {
                return b->status;
            }
        }
        switch (cls) {
            case UW_CH_AMP:  rep = "&amp;";  replen = 5; break;
            case UW_CH_LT:   rep = "&lt;";   replen = 4; break;
            case UW_CH_GT:   rep = "&gt;";   replen = 4; break;
            case UW_CH_QUOT: rep = "&quot;"; replen = 6; break;
            case UW_CH_APOS: rep = "&apos;"; replen = 6; break;
            case UW_CH_NUMREF:
                rep = (c == 0x09u) ? "&#9;" : (c == 0x0Au) ? "&#10;" : "&#13;";
                replen = strlen(rep);
                break;
            default:
                /* Not representable. U+FFFD would be friendlier but widens the
                 * byte, and these strings are keys that consumers match on;
                 * a fixed-width '?' keeps offsets predictable. */
                rep = "?";
                replen = 1;
                if (nwarn) {
                    (*nwarn)++;
                }
                break;
        }
        if (uw_buf_write(b, rep, replen) != UCIS_WRITER_OK) {
            return b->status;
        }
        i++;
        run = i;
    }
    if (i > run) {
        return uw_buf_write(b, s + run, i - run);
    }
    return b->status;
}

UW_INTERNAL int uw_text_escape_cstr(uw_buf_t* b, const char* s,
                                    unsigned long* nwarn)
{
    if (s == NULL) {
        return b->status;
    }
    return uw_text_escape(b, s, strlen(s), nwarn);
}

UW_INTERNAL int uw_text_attr(uw_buf_t* b, const char* name, const char* value,
                             unsigned long* nwarn)
{
    if (value == NULL) {
        return b->status;
    }
    if (uw_buf_putc(b, ' ') != UCIS_WRITER_OK) { return b->status; }
    if (uw_buf_puts(b, name) != UCIS_WRITER_OK) { return b->status; }
    if (UW_LIT(b, "=\"") != UCIS_WRITER_OK) { return b->status; }
    if (uw_text_escape_cstr(b, value, nwarn) != UCIS_WRITER_OK) { return b->status; }
    return uw_buf_putc(b, '"');
}

UW_INTERNAL int uw_text_attr_n(uw_buf_t* b, const char* name, const char* value,
                               size_t len, unsigned long* nwarn)
{
    if (value == NULL) {
        return b->status;
    }
    if (uw_buf_putc(b, ' ') != UCIS_WRITER_OK) { return b->status; }
    if (uw_buf_puts(b, name) != UCIS_WRITER_OK) { return b->status; }
    if (UW_LIT(b, "=\"") != UCIS_WRITER_OK) { return b->status; }
    if (uw_text_escape(b, value, len, nwarn) != UCIS_WRITER_OK) { return b->status; }
    return uw_buf_putc(b, '"');
}

UW_INTERNAL int uw_text_attr_u64(uw_buf_t* b, const char* name, uint64_t value)
{
    if (uw_buf_putc(b, ' ') != UCIS_WRITER_OK) { return b->status; }
    if (uw_buf_puts(b, name) != UCIS_WRITER_OK) { return b->status; }
    if (UW_LIT(b, "=\"") != UCIS_WRITER_OK) { return b->status; }
    if (uw_buf_u64(b, value) != UCIS_WRITER_OK) { return b->status; }
    return uw_buf_putc(b, '"');
}

UW_INTERNAL int uw_text_attr_i64(uw_buf_t* b, const char* name, int64_t value)
{
    if (uw_buf_putc(b, ' ') != UCIS_WRITER_OK) { return b->status; }
    if (uw_buf_puts(b, name) != UCIS_WRITER_OK) { return b->status; }
    if (UW_LIT(b, "=\"") != UCIS_WRITER_OK) { return b->status; }
    if (uw_buf_i64(b, value) != UCIS_WRITER_OK) { return b->status; }
    return uw_buf_putc(b, '"');
}

UW_INTERNAL int uw_text_attr_double(uw_buf_t* b, const char* name, double value)
{
    if (uw_buf_putc(b, ' ') != UCIS_WRITER_OK) { return b->status; }
    if (uw_buf_puts(b, name) != UCIS_WRITER_OK) { return b->status; }
    if (UW_LIT(b, "=\"") != UCIS_WRITER_OK) { return b->status; }
    if (uw_buf_double(b, value) != UCIS_WRITER_OK) { return b->status; }
    return uw_buf_putc(b, '"');
}

/* ==== uw_error.c ============================================================== */

/* uw_error.c - handle validation, sticky error state, diagnostics.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Work item 1.7 of docs/ucis-writer-impl-plan.md.
 *
 * Nothing in this library aborts, longjmps, or writes to stderr on its own.
 * A coverage writer is linked into somebody else's simulation; the worst thing
 * it can do is take that simulation down over a bad file path. Errors latch on
 * the database and ucis_Close reports the first one.
 *
 * Two classes, kept apart (see uw_db_t::err): a caller mistake drops that one
 * call and the document keeps being written, because the rest of the data is
 * still good and still valid. Only an I/O or allocation failure stops output,
 * because after one of those there is nowhere for output to go. */


#include <stdlib.h>
#include <string.h>

UW_INTERNAL uw_db_t* uw_db_check(ucisT db)
{
    uw_db_t* d = (uw_db_t*)db;
    if (d == NULL || d->magic != UW_DB_MAGIC) {
        return NULL;
    }
    return d;
}

static void uw_ctx_set(uw_db_t* db, const char* what, const char* detail)
{
    size_t n = 0;
    size_t cap = sizeof(db->errctx) - 1;

    if (what == NULL) {
        what = "error";
    }
    n = strlen(what);
    if (n > cap) {
        n = cap;
    }
    memcpy(db->errctx, what, n);

    if (detail != NULL && n + 2 < cap) {
        size_t dn;
        memcpy(db->errctx + n, ": ", 2);
        n += 2;
        dn = strlen(detail);
        if (dn > cap - n) {
            dn = cap - n;
        }
        memcpy(db->errctx + n, detail, dn);
        n += dn;
    }
    db->errctx[n] = '\0';
}

/* UCIS 1.0 provides a process-wide error handler. Ours is advisory: the
 * database, not the callback, is the authoritative record of what went wrong,
 * because a per-process hook cannot say which of several open databases
 * failed. Registering one is still useful for getting a message to a log. */
static ucis_ErrorHandler uw_error_handler  = NULL;
static void*             uw_error_userdata = NULL;

UW_INTERNAL int uw_fail(uw_db_t* db, int status, const char* what,
                        const char* detail)
{
    int first = (db->err == UCIS_WRITER_OK);
    if (first) {
        uw_ctx_set(db, what, detail);
        db->err = status;
    }
    /* Only failures that make output impossible stop output. */
    if (status == UCIS_WRITER_ERR_IO || status == UCIS_WRITER_ERR_ALLOC) {
        uw_buf_fail(&db->buf, status);
    }
    if (first && uw_error_handler != NULL) {
        ucisErrorT err;
        err.msgno    = status;
        err.severity = UCIS_MSG_ERROR;
        err.msgstr   = db->errctx;
        uw_error_handler(uw_error_userdata, &err);
    }
    return status;
}

UW_INTERNAL char* uw_strdup(uw_db_t* db, const char* s)
{
    size_t n;
    char*  p;
    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    p = (char*)malloc(n);
    if (p == NULL) {
        uw_fail(db, UCIS_WRITER_ERR_ALLOC, "out of memory", NULL);
        return NULL;
    }
    memcpy(p, s, n);
    return p;
}

/* ---- public error surface -------------------------------------------- */

const char* ucis_writer_status_name(ucisWriterStatusT status)
{
    switch (status) {
        case UCIS_WRITER_OK:             return "ok";
        case UCIS_WRITER_ERR_ALLOC:      return "out of memory";
        case UCIS_WRITER_ERR_IO:         return "output write failed";
        case UCIS_WRITER_ERR_USAGE:      return "invalid argument";
        case UCIS_WRITER_ERR_ORDER:      return "ordering contract violated";
        case UCIS_WRITER_ERR_SEALED:     return "table already written";
        case UCIS_WRITER_ERR_DEPTH:      return "scope nesting too deep";
        case UCIS_WRITER_ERR_UNBALANCED: return "unbalanced scopes";
        case UCIS_WRITER_ERR_STATE:      return "call not legal in this state";
        default:                         return "unknown error";
    }
}

ucisWriterStatusT ucis_writer_error(ucisT db)
{
    uw_db_t* d = uw_db_check(db);
    if (d == NULL) {
        return UCIS_WRITER_ERR_USAGE;
    }
    return (ucisWriterStatusT)(d->err ? d->err : d->buf.status);
}

const char* ucis_writer_error_string(ucisT db)
{
    uw_db_t* d = uw_db_check(db);
    if (d == NULL) {
        return "not a ucis_writer database handle";
    }
    if (d->err == UCIS_WRITER_OK && d->buf.status == UCIS_WRITER_OK) {
        return "ok";
    }
    if (d->errctx[0] != '\0') {
        return d->errctx;
    }
    return ucis_writer_status_name(
        (ucisWriterStatusT)(d->err ? d->err : d->buf.status));
}

unsigned long ucis_writer_warnings(ucisT db)
{
    uw_db_t* d = uw_db_check(db);
    return d ? d->warnings : 0ul;
}

uint64_t ucis_writer_bytes_written(ucisT db)
{
    uw_db_t* d = uw_db_check(db);
    return d ? d->buf.bytes_out : 0u;
}

int ucis_writer_set_pretty(ucisT db, int enable)
{
    uw_db_t* d = uw_db_check(db);
    if (d == NULL) {
        return -1;
    }
    d->pretty = enable ? 1 : 0;
    return 0;
}

void ucis_RegisterErrorHandler(ucis_ErrorHandler errHandle, void* userdata)
{
    uw_error_handler  = errHandle;
    uw_error_userdata = userdata;
}

/* ==== uw_stack.c ============================================================== */

/* uw_stack.c - XML element stack and the ordering stage machine.
 * SPDX-License-Identifier: Apache-2.0 */


static const char uw_indent_spaces[] =
    "                                                                ";
#define UW_INDENT_MAX (sizeof(uw_indent_spaces) - 1)

/* Indentation counts elements, not stack entries: virtual scopes take up a
 * stack slot but produce no markup to indent under. */
static int uw_real_depth(uw_db_t* db, int depth)
{
    int n = 0;
    int i;
    for (i = 0; i < depth && i < db->depth; ++i) {
        if (db->stack[i].tag != NULL) {
            n++;
        }
    }
    return n;
}

static int uw_newline(uw_db_t* db, int depth)
{
    size_t want;
    if (!db->pretty) {
        return db->buf.status;
    }
    if (uw_buf_putc(&db->buf, '\n') != UCIS_WRITER_OK) {
        return db->buf.status;
    }
    depth = uw_real_depth(db, depth);
    want = (size_t)(depth > 0 ? depth : 0) * 2u;
    if (want > UW_INDENT_MAX) {
        want = UW_INDENT_MAX;
    }
    return uw_buf_write(&db->buf, uw_indent_spaces, want);
}

UW_INTERNAL uw_elem_t* uw_el_top(uw_db_t* db)
{
    return db->depth > 0 ? &db->stack[db->depth - 1] : NULL;
}

UW_INTERNAL int uw_el_commit(uw_db_t* db)
{
    uw_elem_t* top = uw_el_top(db);
    if (top == NULL || !top->pending) {
        return db->buf.status;
    }
    top->pending = 0;
    if (top->needs_expr) {
        /* Written last among the attributes because UCIS_STR_EXPR_TERMS may
         * have arrived at any point since the scope was created. */
        uw_text_attr(&db->buf, "exprString", db->expr_terms, &db->warnings);
        uw_text_attr_u64(&db->buf, "index", top->expr_index);
        uw_text_attr_u64(&db->buf, "width", uw_expr_term_count(db));
    }
    if (uw_buf_putc(&db->buf, '>') != UCIS_WRITER_OK) {
        return db->buf.status;
    }
    /* Deferred children, in the order their content models require. Each flag
     * is cleared before its emitter runs, because the emitter opens elements
     * and so re-enters this function. */
    if (top->needs_options) {
        unsigned variant = top->needs_options;
        top->needs_options = UW_OPT_NONE;
        top->haskids       = 1;
        if (uw_emit_options(db, variant) != UCIS_WRITER_OK) {
            return db->buf.status;
        }
    }
    if (top->needs_cgid) {
        top->needs_cgid = 0;
        top->haskids    = 1;
        if (uw_emit_cgid(db, top) != UCIS_WRITER_OK) {
            return db->buf.status;
        }
    }
    if (top->needs_id) {
        top->needs_id = 0;
        top->haskids  = 1;
        if (uw_newline(db, db->depth) != UCIS_WRITER_OK) {
            return db->buf.status;
        }
        UW_LIT(&db->buf, "<id file=\"");
        uw_buf_u32(&db->buf, top->id_file);
        UW_LIT(&db->buf, "\" line=\"");
        uw_buf_u32(&db->buf, top->id_line);
        UW_LIT(&db->buf, "\" inlineCount=\"");
        uw_buf_u32(&db->buf, top->id_inline);
        UW_LIT(&db->buf, "\"/>");
    }
    if (top->needs_expr) {
        top->needs_expr = 0;
        top->haskids    = 1;
        uw_emit_sub_exprs(db);
    }
    return db->buf.status;
}

UW_INTERNAL uint64_t uw_expr_term_count(uw_db_t* db)
{
    uint64_t    n = 0;
    const char* p = db->expr_terms;

    if (!db->expr_terms_explicit) {
        return 1u;      /* the scope name is one opaque term, not a list */
    }
    while (*p != '\0') {
        while (*p == '#') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        n++;
        while (*p != '\0' && *p != '#') {
            p++;
        }
    }
    return n ? n : 1u;   /* subExpr+ means never zero */
}

/* EXPR requires subExpr+ before its bins. The operand list arrives as one
 * '#'-separated string (spec 6.5.4.1) and becomes one element per operand. */
UW_INTERNAL int uw_emit_sub_exprs(uw_db_t* db)
{
    const char* p = db->expr_terms;
    int         any = 0;

    if (!db->expr_terms_explicit) {
        UW_TRY(uw_el_begin(db, "subExpr", 0));
        uw_el_commit(db);
        uw_text_escape_cstr(&db->buf, db->expr_terms, &db->warnings);
        return uw_el_end(db);
    }
    while (*p != '\0') {
        const char* start;
        while (*p == '#') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        start = p;
        while (*p != '\0' && *p != '#') {
            p++;
        }
        UW_TRY(uw_el_begin(db, "subExpr", 0));
        uw_el_commit(db);
        uw_text_escape(&db->buf, start, (size_t)(p - start), &db->warnings);
        UW_TRY(uw_el_end(db));
        any = 1;
    }
    if (!any) {
        UW_TRY(uw_el_begin(db, "subExpr", 0));
        UW_TRY(uw_el_end(db));
    }
    return db->buf.status;
}

UW_INTERNAL void uw_el_set_id(uw_db_t* db, uint32_t file, uint32_t line,
                              uint32_t inlinecount, int warn_on_clamp)
{
    uw_elem_t* top = uw_el_top(db);
    if (top == NULL) {
        return;
    }
    /* All three are xsd:positiveInteger, so zero is not representable and is
     * clamped to 1. Whether that deserves a warning depends on the caller:
     * "I have no source information for this instance" is ordinary, while a
     * statement bin whose line number came out as 0 is a real loss. */
    if (warn_on_clamp && (file == 0 || line == 0 || inlinecount == 0)) {
        db->warnings++;
    }
    top->needs_id  = 1;
    top->id_file   = file ? file : 1u;
    top->id_line   = line ? line : 1u;
    top->id_inline = inlinecount ? inlinecount : 1u;
}

UW_INTERNAL int uw_el_begin(uw_db_t* db, const char* tag, ucisScopeTypeT type)
{
    uw_elem_t* top = uw_el_top(db);
    uw_elem_t* el;

    if (db->depth >= UCIS_WRITER_MAX_DEPTH) {
        return uw_fail(db, UCIS_WRITER_ERR_DEPTH, "scope nesting too deep", tag);
    }
    if (uw_el_commit(db) != UCIS_WRITER_OK) {
        return db->buf.status;
    }
    /* Virtual scopes are transparent: the element that will actually contain
     * this child is the nearest real ancestor. */
    {
        int i;
        for (i = db->depth - 1; i >= 0; --i) {
            db->stack[i].haskids = 1;
            if (db->stack[i].tag != NULL) {
                break;
            }
        }
    }
    (void)top;
    if (uw_newline(db, db->depth) != UCIS_WRITER_OK) {
        return db->buf.status;
    }
    if (uw_buf_putc(&db->buf, '<') != UCIS_WRITER_OK) {
        return db->buf.status;
    }
    if (uw_buf_puts(&db->buf, tag) != UCIS_WRITER_OK) {
        return db->buf.status;
    }

    el = &db->stack[db->depth++];
    el->tag       = tag;
    el->type      = type;
    el->stage     = 0;
    el->pending   = 1;
    el->haskids   = 0;
    el->owned     = (type == 0);   /* only caller scopes carry a UCIS type */
    el->needs_id  = 0;
    el->needs_expr = 0;
    el->expr_index = 0;
    el->needs_options = UW_OPT_NONE;
    el->needs_cgid    = 0;
    el->id_file   = 0;
    el->id_line   = 0;
    el->id_inline = 0;
    return db->buf.status;
}

UW_INTERNAL int uw_el_begin_virtual(uw_db_t* db, ucisScopeTypeT type)
{
    uw_elem_t* el;

    if (db->depth >= UCIS_WRITER_MAX_DEPTH) {
        return uw_fail(db, UCIS_WRITER_ERR_DEPTH, "scope nesting too deep",
                       "metric scope");
    }
    UW_TRY(uw_el_commit(db));

    el = &db->stack[db->depth++];
    el->tag        = NULL;      /* what makes it virtual */
    el->type       = type;
    el->stage      = 0;
    el->pending    = 0;
    el->haskids    = 0;
    el->owned      = 0;         /* the caller opened it and will close it */
    el->needs_id   = 0;
    el->needs_expr = 0;
    el->expr_index = 0;
    el->needs_options = UW_OPT_NONE;
    el->needs_cgid    = 0;
    el->id_file    = 0;
    el->id_line    = 0;
    el->id_inline  = 0;
    return db->buf.status;
}

UW_INTERNAL int uw_el_end(uw_db_t* db)
{
    uw_elem_t* el;

    if (db->depth <= 0) {
        return uw_fail(db, UCIS_WRITER_ERR_UNBALANCED,
                       "no scope is open", NULL);
    }
    el = &db->stack[db->depth - 1];
    if (el->needs_id || el->needs_options || el->needs_cgid) {
        /* Required children cannot be dropped just because nothing else was
         * written into the scope. An empty coverpoint still owes an <options>,
         * and an <id> is required whether or not the scope has bins. */
        if (uw_el_commit(db) != UCIS_WRITER_OK) {
            return db->buf.status;
        }
    }
    el = &db->stack[--db->depth];
    if (el->tag == NULL) {
        /* Virtual: nothing was emitted, so there is nothing to close. The
         * metric name dies with it. */
        db->metric[0] = '\0';
        return db->buf.status;
    }
    if (el->type == UCIS_INSTANCE) {
        /* The in-instance ordering state dies with the instance. */
        db->inst_depth = 0;
        db->cur_kind   = 0;
        db->block_mode = 0;
    }
    if (el->type == UCIS_COVERINSTANCE) {
        /* The bin-ordinal table is the one thing this library holds that grows
         * with content, and it exists only to index this instance's crosses.
         * It must not outlive the instance that justified it. */
        uw_cvptab_reset(&db->cvpbins);
    }

    if (el->pending) {
        /* Never acquired children: collapse to an empty element. This is what
         * keeps documents made mostly of leaf bins from paying for close tags. */
        return uw_buf_write(&db->buf, "/>", 2);
    }
    if (el->haskids && uw_newline(db, db->depth) != UCIS_WRITER_OK) {
        return db->buf.status;
    }
    if (uw_buf_write(&db->buf, "</", 2) != UCIS_WRITER_OK) {
        return db->buf.status;
    }
    if (uw_buf_puts(&db->buf, el->tag) != UCIS_WRITER_OK) {
        return db->buf.status;
    }
    return uw_buf_putc(&db->buf, '>');
}

UW_INTERNAL int uw_el_unwind(uw_db_t* db, int depth)
{
    while (db->depth > depth) {
        if (uw_el_end(db) != UCIS_WRITER_OK) {
            return db->buf.status;
        }
    }
    return db->buf.status;
}

UW_INTERNAL int uw_stage(uw_db_t* db, unsigned stage, const char* what)
{
    uw_elem_t* top = uw_el_top(db);
    if (top == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE, "no scope is open", what);
    }
    if (stage < top->stage) {
        return uw_fail(db, UCIS_WRITER_ERR_ORDER,
                       "out of order within the enclosing scope", what);
    }
    top->stage = stage;
    return UCIS_WRITER_OK;
}

/* ==== uw_tables.c ============================================================= */

/* uw_tables.c - source-file table and history nodes.
 * SPDX-License-Identifier: Apache-2.0 */


#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- source files ----------------------------------------------------- */

#define UW_FILETAB_INIT_BUCKETS 64

UW_INTERNAL void uw_filetab_init(uw_filetab_t* t)
{
    t->buckets  = NULL;
    t->nbuckets = 0;
    t->byid     = NULL;
    t->byid_cap = 0;
    t->count    = 0;
    t->sealed   = 0;
}

UW_INTERNAL void uw_filetab_free(uw_filetab_t* t)
{
    uint32_t i;
    for (i = 0; i < t->count; ++i) {
        free(t->byid[i]);
    }
    free(t->byid);
    free(t->buckets);
    uw_filetab_init(t);
}

static size_t uw_hash(const char* s, size_t n)
{
    size_t h = (size_t)1469598103u;   /* FNV-1a, 32-bit basis */
    size_t i;
    for (i = 0; i < n; ++i) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    return h;
}

static int uw_filetab_grow(uw_db_t* db, uw_filetab_t* t)
{
    size_t      newn = t->nbuckets ? t->nbuckets * 2 : UW_FILETAB_INIT_BUCKETS;
    uw_file_t** nb   = (uw_file_t**)calloc(newn, sizeof(uw_file_t*));
    uint32_t    i;

    if (nb == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_ALLOC, "file table", NULL);
    }
    for (i = 0; i < t->count; ++i) {
        uw_file_t* f = t->byid[i];
        size_t     b = uw_hash(f->name, f->len) & (newn - 1);
        f->hnext = nb[b];
        nb[b] = f;
    }
    free(t->buckets);
    t->buckets  = nb;
    t->nbuckets = newn;
    return UCIS_WRITER_OK;
}

UW_INTERNAL uw_file_t* uw_filetab_by_id(uw_filetab_t* t, uint32_t id)
{
    if (id == 0 || id > t->count) {
        return NULL;
    }
    return t->byid[id - 1];
}

UW_INTERNAL uw_file_t* uw_filetab_intern(uw_db_t* db, const char* filename,
                                         const char* workdir)
{
    uw_filetab_t* t = &db->files;
    size_t        wlen = 0;
    size_t        flen;
    size_t        total;
    size_t        bucket;
    uw_file_t*    f;
    char*         p;

    if (filename == NULL) {
        uw_fail(db, UCIS_WRITER_ERR_USAGE, "file name is NULL", NULL);
        return NULL;
    }
    /* A relative path is only meaningful next to the directory it was
     * relative to, and SOURCE_FILE has nowhere to record that separately. */
    if (workdir != NULL && workdir[0] != '\0' && filename[0] != '/') {
        wlen = strlen(workdir);
        if (wlen > 0 && workdir[wlen - 1] == '/') {
            wlen--;    /* we add exactly one separator below */
        }
    }
    flen  = strlen(filename);
    total = wlen ? wlen + 1 + flen : flen;

    if (t->nbuckets == 0 && uw_filetab_grow(db, t) != UCIS_WRITER_OK) {
        return NULL;
    }
    /* Build the entry first: the hash is over the joined name, so there is no
     * cheaper way to look up a path that came in as workdir + relative name. */
    f = (uw_file_t*)malloc(sizeof(uw_file_t) + total);
    if (f == NULL) {
        uw_fail(db, UCIS_WRITER_ERR_ALLOC, "file table entry", filename);
        return NULL;
    }
    p = f->name;
    if (wlen) {
        memcpy(p, workdir, wlen);
        p += wlen;
        *p++ = '/';
    }
    memcpy(p, filename, flen);
    f->name[total] = '\0';
    f->len = total;

    bucket = uw_hash(f->name, total) & (t->nbuckets - 1);
    {
        uw_file_t* cur;
        for (cur = t->buckets[bucket]; cur != NULL; cur = cur->hnext) {
            if (cur->len == total && memcmp(cur->name, f->name, total) == 0) {
                free(f);
                return cur;      /* dedup: the same path gets the same id */
            }
        }
    }

    if (t->sealed) {
        free(f);
        uw_fail(db, UCIS_WRITER_ERR_SEALED,
                "source file created after the first instance", filename);
        return NULL;
    }

    if ((size_t)t->count == t->byid_cap) {
        size_t      newcap = t->byid_cap ? t->byid_cap * 2 : UW_FILETAB_INIT_BUCKETS;
        uw_file_t** nid    = (uw_file_t**)realloc(t->byid, newcap * sizeof(uw_file_t*));
        if (nid == NULL) {
            free(f);
            uw_fail(db, UCIS_WRITER_ERR_ALLOC, "file table index", NULL);
            return NULL;
        }
        t->byid    = nid;
        t->byid_cap = newcap;
    }

    f->id = ++t->count;               /* 1-based: xsd:positiveInteger */
    t->byid[f->id - 1] = f;
    f->hnext = t->buckets[bucket];
    t->buckets[bucket] = f;

    if (t->count * 2 > t->nbuckets) {
        uw_filetab_grow(db, t);       /* failure here only costs us speed */
    }
    return f;
}

/* ---- history nodes ---------------------------------------------------- */

UW_INTERNAL uw_hist_t* uw_hist_create(uw_db_t* db, const char* logicalname,
                                      const char* physicalname,
                                      ucisHistoryNodeKindT kind)
{
    uw_hist_t* h = (uw_hist_t*)calloc(1, sizeof(uw_hist_t));
    if (h == NULL) {
        uw_fail(db, UCIS_WRITER_ERR_ALLOC, "history node", NULL);
        return NULL;
    }
    h->id           = db->hist_count++;
    h->kind         = kind;
    h->teststatus   = UCIS_TESTSTATUS_OK;
    h->logicalname  = uw_strdup(db, logicalname);
    h->physicalname = uw_strdup(db, physicalname);

    if (db->hist_tail) {
        db->hist_tail->next = h;
    } else {
        db->hist_head = h;
    }
    db->hist_tail = h;
    return h;
}

UW_INTERNAL void uw_hist_free_all(uw_db_t* db)
{
    uw_hist_t* h = db->hist_head;
    while (h != NULL) {
        uw_hist_t* next = h->next;
        free(h->logicalname);
        free(h->physicalname);
        free(h->timeunit);
        free(h->runcwd);
        free(h->seed);
        free(h->cmd);
        free(h->args);
        free(h->date);
        free(h->username);
        free(h->toolcategory);
        free(h->vendor_id);
        free(h->vendor_tool);
        free(h->vendor_version);
        free(h->comment);
        free(h);
        h = next;
    }
    db->hist_head  = NULL;
    db->hist_tail  = NULL;
    db->hist_count = 0;
}

/* ---- time ------------------------------------------------------------- */

UW_INTERNAL void uw_now_iso8601(char* out, size_t cap)
{
    time_t     now = time(NULL);
    struct tm* g   = gmtime(&now);

    if (g == NULL || cap < 21) {
        if (cap > 0) {
            out[0] = '\0';
        }
        return;
    }
    /* Hand-formatted rather than strftime: strftime's %F/%T are C99-optional
     * in practice on some platforms, and this avoids any locale involvement. */
    out[0]  = (char)('0' + ((g->tm_year + 1900) / 1000) % 10);
    out[1]  = (char)('0' + ((g->tm_year + 1900) / 100) % 10);
    out[2]  = (char)('0' + ((g->tm_year + 1900) / 10) % 10);
    out[3]  = (char)('0' + ((g->tm_year + 1900) % 10));
    out[4]  = '-';
    out[5]  = (char)('0' + ((g->tm_mon + 1) / 10));
    out[6]  = (char)('0' + ((g->tm_mon + 1) % 10));
    out[7]  = '-';
    out[8]  = (char)('0' + (g->tm_mday / 10));
    out[9]  = (char)('0' + (g->tm_mday % 10));
    out[10] = 'T';
    out[11] = (char)('0' + (g->tm_hour / 10));
    out[12] = (char)('0' + (g->tm_hour % 10));
    out[13] = ':';
    out[14] = (char)('0' + (g->tm_min / 10));
    out[15] = (char)('0' + (g->tm_min % 10));
    out[16] = ':';
    out[17] = (char)('0' + ((g->tm_sec > 59 ? 59 : g->tm_sec) / 10));
    out[18] = (char)('0' + ((g->tm_sec > 59 ? 59 : g->tm_sec) % 10));
    out[19] = 'Z';
    out[20] = '\0';
}

UW_INTERNAL int uw_is_datetime(const char* s)
{
    int i;
    if (s == NULL) {
        return 0;
    }
    for (i = 0; i < 19; ++i) {
        if (s[i] == '\0') {
            return 0;
        }
    }
    return s[4] == '-' && s[7] == '-' && s[10] == 'T' &&
           s[13] == ':' && s[16] == ':';
}

/* ---- emission --------------------------------------------------------- */

static const char* uw_hist_kind_name(ucisHistoryNodeKindT kind)
{
    switch (kind) {
        case UCIS_HISTORYNODE_TEST:  return "TEST";
        case UCIS_HISTORYNODE_MERGE: return "MERGE";
        case UCIS_HISTORYNODE_ALL:   return "ALL";
        default:                     return NULL;
    }
}

static int uw_emit_source_files(uw_db_t* db)
{
    uint32_t i;

    if (db->files.count == 0) {
        /* sourceFiles has minOccurs="1". A caller that recorded no source
         * files still deserves a document a reader will accept, so we emit a
         * placeholder and say so through the warning counter. */
        db->warnings++;
        if (uw_el_begin(db, "sourceFiles", 0) != UCIS_WRITER_OK) {
            return db->buf.status;
        }
        uw_text_attr(&db->buf, "fileName", "(unknown)", &db->warnings);
        uw_text_attr_u64(&db->buf, "id", 1);
        return uw_el_end(db);
    }

    for (i = 0; i < db->files.count; ++i) {
        uw_file_t* f = db->files.byid[i];
        if (uw_el_begin(db, "sourceFiles", 0) != UCIS_WRITER_OK) {
            return db->buf.status;
        }
        if (uw_buf_write(&db->buf, " fileName=\"", 11) != UCIS_WRITER_OK) {
            return db->buf.status;
        }
        uw_text_escape(&db->buf, f->name, f->len, &db->warnings);
        uw_buf_putc(&db->buf, '"');
        uw_text_attr_u64(&db->buf, "id", f->id);
        if (uw_el_end(db) != UCIS_WRITER_OK) {
            return db->buf.status;
        }
    }
    return db->buf.status;
}

static int uw_emit_one_history(uw_db_t* db, const uw_hist_t* h)
{
    uw_buf_t*   b    = &db->buf;
    const char* kind = uw_hist_kind_name(h->kind);
    const char* date;

    if (uw_el_begin(db, "historyNodes", 0) != UCIS_WRITER_OK) {
        return b->status;
    }
    uw_text_attr_u64(b, "historyNodeId", h->id);
    if (h->has_parent) {
        uw_text_attr_u64(b, "parentId", h->parent_id);
    }
    uw_text_attr(b, "logicalName",
                 h->logicalname ? h->logicalname : "(unnamed)", &db->warnings);
    uw_text_attr(b, "physicalName", h->physicalname, &db->warnings);
    uw_text_attr(b, "kind", kind, &db->warnings);

    /* testStatus is xsd:boolean in the schema even though the API models six
     * states. Anything short of a clean run is reported as false, and the
     * detail survives as UCIS_INT_TEST_STATUS on the reading side. */
    uw_text_attr(b, "testStatus",
                 h->teststatus == UCIS_TESTSTATUS_OK ? "true" : "false", NULL);

    if (h->has_testdata) {
        uw_text_attr_double(b, "simtime", h->simtime);
        uw_text_attr(b, "timeunit", h->timeunit, &db->warnings);
        uw_text_attr(b, "runCwd", h->runcwd, &db->warnings);
        uw_text_attr_double(b, "cpuTime", h->cputime);
        uw_text_attr(b, "seed", h->seed, &db->warnings);
        uw_text_attr(b, "cmd", h->cmd, &db->warnings);
        uw_text_attr(b, "args", h->args, &db->warnings);
        if (h->compulsory) {
            uw_text_attr(b, "compulsory", "1", NULL);
        }
    }

    date = h->date;
    if (!uw_is_datetime(date)) {
        if (date != NULL) {
            db->warnings++;      /* caller supplied something unparseable */
        }
        date = db->written_time;
    }
    uw_text_attr(b, "date", date, &db->warnings);
    uw_text_attr(b, "userName", h->username, &db->warnings);
    if (h->has_testdata) {
        uw_text_attr_double(b, "cost", h->cost);
    }
    uw_text_attr(b, "toolCategory",
                 h->toolcategory ? h->toolcategory : UCIS_SIM_TOOL, &db->warnings);
    uw_text_attr(b, "ucisVersion", UCIS_WRITER_UCIS_VERSION, NULL);
    /* Per-node override, then the database-wide default, then ours. */
    uw_text_attr(b, "vendorId",
                 h->vendor_id ? h->vendor_id
                              : db->vendor_id ? db->vendor_id
                                              : UCIS_WRITER_VENDOR_ID, &db->warnings);
    uw_text_attr(b, "vendorTool",
                 h->vendor_tool ? h->vendor_tool
                                : db->vendor_tool ? db->vendor_tool
                                                  : UCIS_WRITER_VENDOR_TOOL, &db->warnings);
    uw_text_attr(b, "vendorToolVersion",
                 h->vendor_version ? h->vendor_version
                                   : db->vendor_version ? db->vendor_version
                                                        : UCIS_WRITER_VERSION, &db->warnings);
    uw_text_attr(b, "comment", h->comment, &db->warnings);

    return uw_el_end(db);
}

static int uw_emit_history_nodes(uw_db_t* db)
{
    const uw_hist_t* h;

    if (db->hist_head == NULL) {
        /* historyNodes has minOccurs="1"; same repair as sourceFiles. */
        uw_hist_t synth;
        memset(&synth, 0, sizeof(synth));
        synth.teststatus = UCIS_TESTSTATUS_OK;
        db->warnings++;
        return uw_emit_one_history(db, &synth);
    }
    for (h = db->hist_head; h != NULL; h = h->next) {
        if (uw_emit_one_history(db, h) != UCIS_WRITER_OK) {
            return db->buf.status;
        }
    }
    return db->buf.status;
}

UW_INTERNAL int uw_placeholder_instance(uw_db_t* db)
{
    /* instanceCoverages is minOccurs="1" too, so a run that recorded nothing
     * still needs one. Emitting a named placeholder keeps the document
     * readable by any conforming tool and makes the omission obvious to a
     * human, which silently writing an invalid file would not. */
    db->warnings++;
    if (uw_el_begin(db, "instanceCoverages", UCIS_INSTANCE) != UCIS_WRITER_OK) {
        return db->buf.status;
    }
    uw_text_attr(&db->buf, "name", "(none)", NULL);
    uw_text_attr(&db->buf, "key", "(none)", NULL);
    uw_el_set_id(db, 1, 1, 1, 0);
    return uw_el_end(db);
}

UW_INTERNAL int uw_tables_flush(uw_db_t* db)
{
    if (db->phase != UW_PHASE_TABLES) {
        return db->buf.status;
    }
    db->phase        = UW_PHASE_BODY;
    db->files.sealed = 1;

    if (uw_emit_source_files(db) != UCIS_WRITER_OK) {
        return db->buf.status;
    }
    return uw_emit_history_nodes(db);
}

/* ==== uw_xml.c ================================================================ */

/* uw_xml.c - UCIS construct to XSD element dispatch.
 * SPDX-License-Identifier: Apache-2.0 */


#include <string.h>

static const char* uw_kind_tag(unsigned kind)
{
    switch (kind) {
        case UW_KIND_TOGGLE: return "toggleCoverage";
        case UW_KIND_BLOCK:  return "blockCoverage";
        case UW_KIND_COND:   return "conditionCoverage";
        case UW_KIND_BRANCH: return "branchCoverage";
        case UW_KIND_FSM:    return "fsmCoverage";
        case UW_KIND_ASSERT: return "assertionCoverage";
        case UW_KIND_CVG:    return "covergroupCoverage";
        default:             return "userAttr";
    }
}

UW_INTERNAL int uw_kind_open(uw_db_t* db, unsigned kind)
{
    const char* tag = uw_kind_tag(kind);

    if (db->inst_depth == 0) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "coverage created outside any instance", tag);
    }
    if (db->cur_kind == kind) {
        /* Same kind as the last item: keep the wrapper, discard any scope
         * the caller left open beneath it. */
        return uw_el_unwind(db, db->inst_depth + 1);
    }
    UW_TRY(uw_el_unwind(db, db->inst_depth));
    /* uw_stage now applies to the instance element, which is innermost. It is
     * what turns "you interleaved your coverage kinds" into a diagnosable
     * error instead of a document readers silently reject. */
    UW_TRY(uw_stage(db, kind, tag));
    db->cur_kind   = kind;
    db->block_mode = 0;
    return uw_el_begin(db, tag, 0);
}

UW_INTERNAL uint64_t uw_cover_count(uw_db_t* db, const ucisCoverDataT* data)
{
    if (data->flags & UCIS_IS_VECTOR) {
        /* BIN_CONTENTS/@coverageCount is a single nonNegativeInteger; a
         * vector bin has no scalar to put there. The data is not lost
         * silently -- it is counted. */
        db->warnings++;
        return 0u;
    }
    if (data->flags & UCIS_IS_64BIT) {
        return data->data.int64;
    }
    return (uint64_t)data->data.int32;
}

UW_INTERNAL int uw_emit_obj_attrs(uw_db_t* db, const char* alias,
                                  const ucisCoverDataT* data)
{
    uw_buf_t* b = &db->buf;

    /* STATEMENT and friends carry no @name, so the caller's UOR name goes to
     * @alias -- the only string slot objAttributes offers. */
    uw_text_attr(b, "alias", alias, &db->warnings);
    if (data != NULL) {
        if (data->flags & UCIS_EXCLUDED) {
            uw_text_attr(b, "excluded", "true", NULL);
        }
        if ((data->flags & UCIS_HAS_WEIGHT) && data->weight != 1) {
            uw_text_attr_i64(b, "weight", data->weight);
        }
    }
    return b->status;
}

UW_INTERNAL int uw_emit_bin(uw_db_t* db, const char* tag, const char* name,
                            const ucisCoverDataT* data)
{
    uw_buf_t* b = &db->buf;

    if (uw_el_begin(db, tag, 0) != UCIS_WRITER_OK) {
        return b->status;
    }
    if (data != NULL && (data->flags & UCIS_HAS_GOAL) && data->goal > 0) {
        uw_text_attr_i64(b, "coverageCountGoal", data->goal);
    }
    if (uw_el_begin(db, "contents", 0) != UCIS_WRITER_OK) {
        return b->status;
    }
    uw_text_attr(b, "nameComponent", name, &db->warnings);
    uw_text_attr_u64(b, "coverageCount",
                     data ? uw_cover_count(db, data) : 0u);
    if (uw_el_end(db) != UCIS_WRITER_OK) {   /* </contents> */
        return b->status;
    }
    return uw_el_end(db);                     /* </bin> */
}

UW_INTERNAL int uw_emit_statement(uw_db_t* db, const char* name,
                                  const ucisCoverDataT* data,
                                  const ucisSourceInfoT* srcinfo)
{
    uint32_t fileid = 0;
    uint32_t line   = 0;

    UW_TRY(uw_kind_open(db, UW_KIND_BLOCK));
    /* BLOCK_COVERAGE is an xsd:choice of process+ | block+ | statement+, so a
     * blockCoverage that has emitted statements cannot also emit blocks. */
    if (db->block_mode != 0 && db->block_mode != UCIS_STMTBIN) {
        return uw_fail(db, UCIS_WRITER_ERR_ORDER,
                       "blockCoverage already contains a different block form",
                       "statement");
    }
    db->block_mode = UCIS_STMTBIN;

    UW_TRY(uw_el_begin(db, "statement", 0));
    uw_emit_obj_attrs(db, name, data);

    if (srcinfo != NULL) {
        uw_file_t* f = (uw_file_t*)srcinfo->filehandle;
        if (f != NULL) {
            fileid = f->id;
        }
        line = srcinfo->line > 0 ? (uint32_t)srcinfo->line : 0u;
    }
    /* A statement bin without a line number has lost the thing that makes it
     * useful, so unlike an instance this clamp is worth a warning. */
    uw_el_set_id(db, fileid, line, 1u, 1);

    UW_TRY(uw_emit_bin(db, "bin", NULL, data));
    return uw_el_end(db);                     /* </statement> */
}

/* ---- condition and expression ----------------------------------------- */

UW_INTERNAL void uw_copy_bounded(uw_db_t* db, char* dst, size_t cap,
                                 const char* src)
{
    size_t n;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        db->warnings++;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

UW_INTERNAL int uw_open_expr(uw_db_t* db, const char* name,
                             const ucisSourceInfoT* srcinfo)
{
    uw_elem_t* top = uw_el_top(db);
    uint32_t   fileid = 0;
    uint32_t   line   = 0;

    /* A second expression scope inside an open one is an input-contribution
     * metric scope (spec 6.5.4.1). UCIS-XML has no element for that level --
     * EXPR requires bin+ directly -- so it becomes a virtual scope: it
     * balances against ucis_WriteStreamScope but emits nothing, and its name
     * rides along on each bin as @typeComponent. See D11. */
    if (top != NULL && (top->type == UCIS_EXPR || top->type == UCIS_COND)) {
        uw_copy_bounded(db, db->metric, sizeof(db->metric), name);
        return uw_el_begin_virtual(db, top->type);
    }

    UW_TRY(uw_kind_open(db, UW_KIND_COND));
    UW_TRY(uw_el_begin(db, "expr", UCIS_EXPR));

    uw_text_attr(&db->buf, "name", name, &db->warnings);
    uw_text_attr(&db->buf, "key", name, &db->warnings);

    /* Default the operand list to the scope name; UCIS_STR_EXPR_TERMS
     * overwrites it if the caller supplies one. subExpr is minOccurs="1", so
     * there always has to be something. */
    uw_copy_bounded(db, db->expr_terms, sizeof(db->expr_terms), name);
    db->expr_terms_explicit = 0;
    db->metric[0] = '\0';

    {
        uw_elem_t* el = uw_el_top(db);
        el->needs_expr = 1;
        el->expr_index = ++db->expr_count;
    }
    if (srcinfo != NULL) {
        uw_file_t* f = (uw_file_t*)srcinfo->filehandle;
        if (f != NULL) {
            fileid = f->id;
        }
        line = srcinfo->line > 0 ? (uint32_t)srcinfo->line : 0u;
    }
    uw_el_set_id(db, fileid, line, 1u, 1);
    return db->buf.status;
}

UW_INTERNAL int uw_emit_expr_bin(uw_db_t* db, const char* name,
                                 const ucisCoverDataT* data)
{
    uw_elem_t* top = uw_el_top(db);

    if (top == NULL || (top->type != UCIS_EXPR && top->type != UCIS_COND)) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "expression bin outside an expression scope", name);
    }
    /* Bins created in the metric scope belong to the expression that encloses
     * it, which uw_el_begin_virtual has left as the innermost real element. */
    UW_TRY(uw_el_begin(db, "bin", 0));
    if (data != NULL && (data->flags & UCIS_HAS_GOAL) && data->goal > 0) {
        uw_text_attr_i64(&db->buf, "coverageCountGoal", data->goal);
    }
    UW_TRY(uw_el_begin(db, "contents", 0));
    uw_text_attr(&db->buf, "nameComponent", name, &db->warnings);
    if (db->metric[0] != '\0') {
        /* The metric level is gone as structure; this is where it survives. */
        uw_text_attr(&db->buf, "typeComponent", db->metric, &db->warnings);
    }
    uw_text_attr_u64(&db->buf, "coverageCount",
                     data ? uw_cover_count(db, data) : 0u);
    UW_TRY(uw_el_end(db));
    return uw_el_end(db);
}

/* ---- branch ----------------------------------------------------------- */

UW_INTERNAL int uw_open_branch_statement(uw_db_t* db, const char* name,
                                         const ucisSourceInfoT* srcinfo)
{
    uint32_t fileid = 0;
    uint32_t line   = 0;

    UW_TRY(uw_kind_open(db, UW_KIND_BRANCH));
    UW_TRY(uw_el_begin(db, "statement", UCIS_BRANCH));
    /* @statementType is required and the API has no argument for it; "if" is
     * the overwhelmingly common case and UCIS_INT_BRANCH_ISCASE overrides it
     * while the start tag is still pending. */
    uw_text_attr(&db->buf, "statementType", "if", NULL);
    uw_emit_obj_attrs(db, name, NULL);

    if (srcinfo != NULL) {
        uw_file_t* f = (uw_file_t*)srcinfo->filehandle;
        if (f != NULL) {
            fileid = f->id;
        }
        line = srcinfo->line > 0 ? (uint32_t)srcinfo->line : 0u;
    }
    uw_el_set_id(db, fileid, line, 1u, 1);
    return db->buf.status;
}

UW_INTERNAL int uw_emit_branch(uw_db_t* db, const char* name,
                               const ucisCoverDataT* data,
                               const ucisSourceInfoT* srcinfo)
{
    uw_elem_t* top = uw_el_top(db);
    uint32_t   fileid = 0;
    uint32_t   line   = 0;

    if (top == NULL || top->type != UCIS_BRANCH) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "branch bin outside a branch scope", name);
    }
    /* BRANCH carries no attributes at all, so the arm's name ("if", "else",
     * a case label) has only one place to go: the bin's nameComponent. */
    UW_TRY(uw_el_begin(db, "branch", 0));
    if (srcinfo != NULL) {
        uw_file_t* f = (uw_file_t*)srcinfo->filehandle;
        if (f != NULL) {
            fileid = f->id;
        }
        line = srcinfo->line > 0 ? (uint32_t)srcinfo->line : 0u;
    }
    if (fileid == 0 || line == 0) {
        /* Fall back to the enclosing statement's location rather than to 1:1,
         * which would point at the wrong file entirely. */
        fileid = top->id_file ? top->id_file : fileid;
        line   = top->id_line ? top->id_line : line;
    }
    uw_el_set_id(db, fileid, line, 1u, 0);

    UW_TRY(uw_emit_bin(db, "branchBin", name, data));
    return uw_el_end(db);                     /* </branch> */
}

/* ---- toggle ----------------------------------------------------------- */

static const char* uw_toggle_type_name(ucisToggleTypeT t)
{
    switch (t) {
        case UCIS_TOGGLE_TYPE_NET: return "net";
        case UCIS_TOGGLE_TYPE_REG: return "reg";
        default:                   return NULL;
    }
}

static const char* uw_toggle_dir_name(ucisToggleDirT d)
{
    switch (d) {
        case UCIS_TOGGLE_DIR_INTERNAL: return "internal";
        case UCIS_TOGGLE_DIR_IN:       return "in";
        case UCIS_TOGGLE_DIR_OUT:      return "out";
        case UCIS_TOGGLE_DIR_INOUT:    return "inout";
        default:                       return NULL;
    }
}

/* True if `s` is all decimal digits, in which case it is a bit index and
 * TOGGLE_BIT can carry an <index> element as well as a name. */
static int uw_all_digits(const char* s, uint64_t* out)
{
    uint64_t v = 0;
    size_t   i;
    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    for (i = 0; s[i] != '\0'; ++i) {
        if (s[i] < '0' || s[i] > '9' || i >= 19) {
            return 0;
        }
        v = v * 10u + (uint64_t)(s[i] - '0');
    }
    *out = v;
    return 1;
}

static int uw_open_toggle_bit(uw_db_t* db, const char* name)
{
    uint64_t index;

    UW_TRY(uw_el_begin(db, "toggleBit", UCIS_TOGGLE));
    uw_text_attr(&db->buf, "name", name, &db->warnings);
    uw_text_attr(&db->buf, "key", name, &db->warnings);

    if (uw_all_digits(name, &index)) {
        UW_TRY(uw_el_begin(db, "index", 0));
        uw_el_commit(db);
        uw_buf_u64(&db->buf, index);
        return uw_el_end(db);
    }
    return db->buf.status;
}

UW_INTERNAL int uw_open_toggle(uw_db_t* db, const char* name,
                               const char* canonical_name,
                               ucisToggleTypeT type, ucisToggleDirT dir)
{
    uw_elem_t* top = uw_el_top(db);

    /* UCIS nests a toggle scope per bit inside the object's toggle scope
     * (spec 6.7: "/4:top/0:w/0:1"). A toggle scope opened while one is already
     * open is therefore a bit, not another object. */
    if (top != NULL && top->type == UCIS_TOGGLE &&
        strcmp(top->tag, "toggleObject") == 0) {
        return uw_open_toggle_bit(db, name);
    }

    UW_TRY(uw_kind_open(db, UW_KIND_TOGGLE));
    UW_TRY(uw_el_begin(db, "toggleObject", UCIS_TOGGLE));
    uw_text_attr(&db->buf, "name", name, &db->warnings);
    uw_text_attr(&db->buf, "key", canonical_name ? canonical_name : name,
                 &db->warnings);
    uw_text_attr(&db->buf, "type", uw_toggle_type_name(type), NULL);
    uw_text_attr(&db->buf, "portDirection", uw_toggle_dir_name(dir), NULL);

    /* TOGGLE_OBJECT requires an <id>, but ucis_CreateToggle takes no source
     * info, so there is nothing to put in it. Not a warning: the API simply
     * does not carry the data. */
    uw_el_set_id(db, 0, 0, 1u, 0);
    return db->buf.status;
}

UW_INTERNAL int uw_emit_toggle_bin(uw_db_t* db, const char* name,
                                   const ucisCoverDataT* data)
{
    uw_elem_t*  top = uw_el_top(db);
    const char* arrow;
    const char* from;
    size_t      fromlen;
    const char* to;

    if (top == NULL || top->type != UCIS_TOGGLE) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "toggle bin outside a toggle scope", name);
    }
    if (strcmp(top->tag, "toggleObject") == 0) {
        /* Bins arriving straight into the object mean a scalar: TOGGLE_OBJECT
         * still requires toggleBit+, so synthesise the one bit. Its name is
         * the index within the object; the signal's name is on the object. */
        UW_TRY(uw_open_toggle_bit(db, "0"));
        top = uw_el_top(db);
        top->owned = 1;    /* ours, so ucis_WriteStreamScope closes through it */
    }

    /* Bin names are "0->1", "1 -> 0" and so on (spec 6.7.1). TOGGLE requires
     * both @from and @to, so a name that is not a transition -- an enum toggle
     * names its bins after the values -- becomes an arrival at a state from an
     * unstated one. The name survives verbatim in nameComponent either way. */
    arrow = NULL;
    if (name != NULL) {
        const char* p;
        for (p = name; p[0] != '\0' && p[1] != '\0'; ++p) {
            if (p[0] == '-' && p[1] == '>') {
                arrow = p;
                break;
            }
        }
    }
    if (arrow != NULL) {
        from    = name;
        fromlen = (size_t)(arrow - name);
        while (fromlen > 0 && from[fromlen - 1] == ' ') {
            fromlen--;
        }
        to = arrow + 2;
        while (*to == ' ') {
            to++;
        }
    } else {
        from    = "";
        fromlen = 0;
        to      = name ? name : "";
    }

    UW_TRY(uw_el_begin(db, "toggle", 0));
    uw_text_attr_n(&db->buf, "from", from, fromlen, &db->warnings);
    uw_text_attr(&db->buf, "to", to, &db->warnings);

    UW_TRY(uw_emit_bin(db, "bin", name, data));
    return uw_el_end(db);                     /* </toggle> */
}

/* ==== uw_cvg.c ================================================================ */

/* uw_cvg.c - functional coverage: covergroups, coverpoints, crosses.
 * SPDX-License-Identifier: Apache-2.0 */


#include <stdlib.h>
#include <string.h>

/* ---- option staging --------------------------------------------------- */

UW_INTERNAL void uw_opts_reset(uw_db_t* db)
{
    memset(&db->opts, 0, sizeof(db->opts));
}

UW_INTERNAL int uw_opts_set(uw_db_t* db, unsigned variant,
                            ucisIntPropertyEnumT property, int value)
{
    uw_opts_t* o = &db->opts;

    /* Common to all three option types. */
    switch (property) {
        case UCIS_INT_SCOPE_WEIGHT:
            o->weight = value; o->set |= UW_OPTSET_WEIGHT; return 1;
        case UCIS_INT_SCOPE_GOAL:
            o->goal = value; o->set |= UW_OPTSET_GOAL; return 1;
        case UCIS_INT_CVG_ATLEAST:
            o->at_least = value; o->set |= UW_OPTSET_AT_LEAST; return 1;
        default: break;
    }
    if (variant == UW_OPT_CGINST || variant == UW_OPT_COVERPOINT) {
        switch (property) {
            case UCIS_INT_CVG_AUTOBINMAX:
                o->auto_bin_max = value; o->set |= UW_OPTSET_AUTO_BIN_MAX; return 1;
            case UCIS_INT_CVG_DETECTOVERLAP:
                o->detect_overlap = value; o->set |= UW_OPTSET_DETECT_OVERLAP; return 1;
            default: break;
        }
    }
    if (variant == UW_OPT_CGINST || variant == UW_OPT_CROSS) {
        if (property == UCIS_INT_CVG_NUMPRINTMISSING) {
            o->num_print_missing = value; o->set |= UW_OPTSET_PRINT_MISSING;
            return 1;
        }
    }
    if (variant == UW_OPT_CGINST) {
        switch (property) {
            case UCIS_INT_CVG_PERINSTANCE:
                o->per_instance = value; o->set |= UW_OPTSET_PER_INSTANCE; return 1;
            case UCIS_INT_CVG_MERGEINSTANCES:
                o->merge_instances = value; o->set |= UW_OPTSET_MERGE_INST; return 1;
            default: break;
        }
    }
    return 0;
}

/* xsd:nonNegativeInteger: a negative weight or goal is not representable, and
 * clamping it silently would turn a caller's sign error into a plausible
 * number. Clamp and count. */
static void uw_opt_attr_nn(uw_db_t* db, const char* name, int32_t value)
{
    if (value < 0) {
        db->warnings++;
        value = 0;
    }
    uw_text_attr_u64(&db->buf, name, (uint64_t)value);
}

UW_INTERNAL int uw_emit_options(uw_db_t* db, unsigned variant)
{
    uw_opts_t* o = &db->opts;
    unsigned   i;

    UW_TRY(uw_el_begin(db, "options", 0));
    if (o->set & UW_OPTSET_WEIGHT) {
        uw_opt_attr_nn(db, "weight", o->weight);
    }
    if (o->set & UW_OPTSET_GOAL) {
        uw_opt_attr_nn(db, "goal", o->goal);
    }
    if (o->set & UW_OPTSET_AT_LEAST) {
        uw_opt_attr_nn(db, "at_least", o->at_least);
    }
    if ((o->set & UW_OPTSET_DETECT_OVERLAP) && variant != UW_OPT_CROSS) {
        uw_text_attr(&db->buf, "detect_overlap",
                     o->detect_overlap ? "true" : "false", NULL);
    }
    if ((o->set & UW_OPTSET_AUTO_BIN_MAX) && variant != UW_OPT_CROSS) {
        uw_opt_attr_nn(db, "auto_bin_max", o->auto_bin_max);
    }
    if ((o->set & UW_OPTSET_PRINT_MISSING) && variant != UW_OPT_COVERPOINT) {
        uw_opt_attr_nn(db, "cross_num_print_missing", o->num_print_missing);
    }
    if (variant == UW_OPT_CGINST) {
        if (o->set & UW_OPTSET_PER_INSTANCE) {
            uw_text_attr(&db->buf, "per_instance",
                         o->per_instance ? "true" : "false", NULL);
        }
        if (o->set & UW_OPTSET_MERGE_INST) {
            uw_text_attr(&db->buf, "merge_instances",
                         o->merge_instances ? "true" : "false", NULL);
        }
    }
    UW_TRY(uw_el_end(db));

    if (variant != UW_OPT_CROSS) {
        return db->buf.status;
    }
    /* CROSS puts crossExpr* directly after options, and the crossed coverpoint
     * names arrived as properties on the pending element, so they go out with
     * the options rather than waiting for a child that may never come. */
    for (i = 0; i < db->cross_n; ++i) {
        UW_TRY(uw_el_begin(db, "crossExpr", 0));
        UW_TRY(uw_el_commit(db));
        uw_text_escape_cstr(&db->buf, db->cross_names[i], &db->warnings);
        UW_TRY(uw_el_end(db));
    }
    return db->buf.status;
}

/* ---- cgId ------------------------------------------------------------- */

static int uw_emit_stmt_id(uw_db_t* db, const char* tag, uint32_t file,
                           uint32_t line)
{
    UW_TRY(uw_el_begin(db, tag, 0));
    /* All three components are xsd:positiveInteger; the clamp is the same one
     * uw_el_set_id applies, and for a covergroup it is routine -- the C API
     * carries no source info for a covergroup created by name alone. */
    uw_text_attr_u64(&db->buf, "file", file ? file : 1u);
    uw_text_attr_u64(&db->buf, "line", line ? line : 1u);
    uw_text_attr_u64(&db->buf, "inlineCount", 1u);
    return uw_el_end(db);
}

UW_INTERNAL int uw_emit_cgid(uw_db_t* db, uw_elem_t* el)
{
    UW_TRY(uw_el_begin(db, "cgId", 0));
    /* Both required. @cgName is where the covergroup type's identity lands --
     * it is the only trace of the type scope in the document. */
    uw_text_attr(&db->buf, "cgName",
                 db->cg_name[0] ? db->cg_name : "(unnamed)", &db->warnings);
    uw_text_attr(&db->buf, "moduleName",
                 db->cg_module[0] ? db->cg_module : "(unknown)", &db->warnings);
    UW_TRY(uw_emit_stmt_id(db, "cginstSourceId", el->id_file, el->id_line));
    UW_TRY(uw_emit_stmt_id(db, "cgSourceId", db->cg_file, db->cg_line));
    return uw_el_end(db);
}

/* ---- coverpoint bin ordinals ------------------------------------------ */

UW_INTERNAL void uw_cvptab_reset(uw_cvptab_t* t)
{
    free(t->slots);
    t->slots     = NULL;
    t->cap       = 0;
    t->used      = 0;
    t->cvp_count = 0;
    t->bin_count = 0;
}

/* Coverpoint names live in the same table as bin names, distinguished by an
 * ordinal no coverpoint can have. */
#define UW_CVP_NAMESPACE 0xFFFFFFFFu

static uint64_t uw_cvp_hash(uint32_t cvp, const char* name)
{
    uint64_t h = 1469598103934665603ULL;   /* FNV-1a 64 */
    int      i;
    for (i = 0; i < 4; ++i) {
        h ^= (uint64_t)((cvp >> (i * 8)) & 0xFFu);
        h *= 1099511628211ULL;
    }
    if (name != NULL) {
        while (*name != '\0') {
            h ^= (uint64_t)(unsigned char)*name++;
            h *= 1099511628211ULL;
        }
    }
    return h ? h : 1u;   /* 0 marks an empty slot */
}

static int uw_cvptab_grow(uw_db_t* db, uw_cvptab_t* t)
{
    size_t        newcap = t->cap ? t->cap * 2u : 64u;
    uw_cvpslot_t* ns     = (uw_cvpslot_t*)calloc(newcap, sizeof(uw_cvpslot_t));
    size_t        i;

    if (ns == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_ALLOC, "coverpoint bin table", NULL);
    }
    for (i = 0; i < t->cap; ++i) {
        size_t j;
        if (t->slots[i].hash == 0) {
            continue;
        }
        j = (size_t)t->slots[i].hash & (newcap - 1u);
        while (ns[j].hash != 0) {
            j = (j + 1u) & (newcap - 1u);
        }
        ns[j] = t->slots[i];
    }
    free(t->slots);
    t->slots = ns;
    t->cap   = newcap;
    return UCIS_WRITER_OK;
}

/* First writer wins: a coverpoint with two bins of the same name has already
 * lost the ability to distinguish them, and the earlier index is the one a
 * reader would guess. */
static int uw_cvptab_put(uw_db_t* db, uint32_t cvp, const char* name,
                         uint32_t index)
{
    uw_cvptab_t* t = &db->cvpbins;
    uint64_t     h;
    size_t       j;

    if (t->used * 4u >= t->cap * 3u) {
        UW_TRY(uw_cvptab_grow(db, t));
    }
    h = uw_cvp_hash(cvp, name);
    j = (size_t)h & (t->cap - 1u);
    while (t->slots[j].hash != 0) {
        if (t->slots[j].hash == h) {
            return UCIS_WRITER_OK;
        }
        j = (j + 1u) & (t->cap - 1u);
    }
    t->slots[j].hash  = h;
    t->slots[j].index = index;
    t->used++;
    return UCIS_WRITER_OK;
}

/* -1 when the name was never recorded. */
static int64_t uw_cvptab_get(uw_db_t* db, uint32_t cvp, const char* name,
                             size_t len)
{
    uw_cvptab_t* t = &db->cvpbins;
    char         tmp[128];
    uint64_t     h;
    size_t       j;

    if (t->cap == 0) {
        return -1;
    }
    if (len >= sizeof(tmp)) {
        return -1;
    }
    memcpy(tmp, name, len);
    tmp[len] = '\0';

    h = uw_cvp_hash(cvp, tmp);
    j = (size_t)h & (t->cap - 1u);
    while (t->slots[j].hash != 0) {
        if (t->slots[j].hash == h) {
            return (int64_t)t->slots[j].index;
        }
        j = (j + 1u) & (t->cap - 1u);
    }
    return -1;
}

/* ---- scopes ----------------------------------------------------------- */

UW_INTERNAL int uw_open_covergroup(uw_db_t* db, const char* name,
                                   const ucisSourceInfoT* srcinfo)
{
    UW_TRY(uw_kind_open(db, UW_KIND_CVG));

    uw_copy_bounded(db, db->cg_name, sizeof(db->cg_name), name);
    db->cg_file = 0;
    db->cg_line = 0;
    if (srcinfo != NULL) {
        uw_file_t* f = (uw_file_t*)srcinfo->filehandle;
        if (f != NULL) {
            db->cg_file = f->id;
        }
        db->cg_line = srcinfo->line > 0 ? (uint32_t)srcinfo->line : 0u;
    }
    /* The type scope itself emits nothing: UCIS-XML has no element for it, and
     * its name and location reappear inside each cgInstance's cgId. */
    return uw_el_begin_virtual(db, UCIS_COVERGROUP);
}

static int uw_open_cgi(uw_db_t* db, const char* name,
                       const ucisSourceInfoT* srcinfo, int owned)
{
    uw_elem_t* el;

    UW_TRY(uw_el_begin(db, "cgInstance", UCIS_COVERINSTANCE));
    uw_text_attr(&db->buf, "name", name ? name : "(unnamed)", &db->warnings);
    uw_text_attr(&db->buf, "key", name ? name : "(unnamed)", &db->warnings);

    el = uw_el_top(db);
    el->owned         = (unsigned char)(owned ? 1 : 0);
    el->needs_options = UW_OPT_CGINST;
    el->needs_cgid    = 1;
    /* Not needs_id: cgInstance has no <id> child. These carry cginstSourceId,
     * which uw_emit_cgid writes inside the cgId. */
    if (srcinfo != NULL) {
        uw_file_t* f = (uw_file_t*)srcinfo->filehandle;
        el->id_file = f != NULL ? f->id : 0u;
        el->id_line = srcinfo->line > 0 ? (uint32_t)srcinfo->line : 0u;
    } else {
        el->id_file = db->cg_file;
        el->id_line = db->cg_line;
    }
    uw_opts_reset(db);
    uw_cvptab_reset(&db->cvpbins);
    return db->buf.status;
}

UW_INTERNAL int uw_open_coverinstance(uw_db_t* db, const char* name,
                                      const ucisSourceInfoT* srcinfo)
{
    uw_elem_t* top = uw_el_top(db);

    /* Siblings: a second coverinstance closes the first. */
    if (top != NULL && top->type == UCIS_COVERINSTANCE) {
        UW_TRY(uw_el_end(db));
        top = uw_el_top(db);
    }
    if (top == NULL || top->type != UCIS_COVERGROUP) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "coverinstance outside a covergroup", name);
    }
    return uw_open_cgi(db, name, srcinfo, 0);
}

/* Make a cgInstance the innermost element, synthesising one for a covergroup
 * whose coverpoints hang off the type scope directly -- which is what
 * type-only coverage looks like, and the only shape UCIS-XML can express. */
static int uw_cgi_ensure(uw_db_t* db)
{
    uw_elem_t* top = uw_el_top(db);

    if (top != NULL && top->type == UCIS_COVERINSTANCE) {
        return UCIS_WRITER_OK;
    }
    /* A coverpoint or cross left open by the caller: close it and start the
     * next as a sibling, the same way a repeated coverage kind does. */
    if (top != NULL && (top->type == UCIS_COVERPOINT || top->type == UCIS_CROSS)) {
        UW_TRY(uw_el_end(db));
        top = uw_el_top(db);
        if (top != NULL && top->type == UCIS_COVERINSTANCE) {
            return UCIS_WRITER_OK;
        }
    }
    if (top != NULL && top->type == UCIS_COVERGROUP) {
        return uw_open_cgi(db, db->cg_name, NULL, 1);
    }
    return uw_fail(db, UCIS_WRITER_ERR_STATE,
                   "coverpoint or cross outside a covergroup", NULL);
}

UW_INTERNAL int uw_open_coverpoint(uw_db_t* db, const char* name)
{
    uw_elem_t* el;

    UW_TRY(uw_cgi_ensure(db));
    UW_TRY(uw_stage(db, UW_CGSTAGE_COVERPOINT, "coverpoint"));

    /* Record the coverpoint's ordinal before opening it, so that a cross can
     * resolve UCIS_STR_ITH_CROSSED_CVP_NAME to a position. */
    UW_TRY(uw_cvptab_put(db, UW_CVP_NAMESPACE, name ? name : "",
                         db->cvpbins.cvp_count));
    db->cvpbins.cvp_count++;
    db->cvpbins.bin_count = 0;

    UW_TRY(uw_el_begin(db, "coverpoint", UCIS_COVERPOINT));
    uw_text_attr(&db->buf, "name", name ? name : "(unnamed)", &db->warnings);
    uw_text_attr(&db->buf, "key", name ? name : "(unnamed)", &db->warnings);

    el = uw_el_top(db);
    el->needs_options = UW_OPT_COVERPOINT;
    uw_opts_reset(db);
    return db->buf.status;
}

UW_INTERNAL int uw_open_cross(uw_db_t* db, const char* name)
{
    uw_elem_t* el;

    UW_TRY(uw_cgi_ensure(db));
    UW_TRY(uw_stage(db, UW_CGSTAGE_CROSS, "cross"));

    UW_TRY(uw_el_begin(db, "cross", UCIS_CROSS));
    uw_text_attr(&db->buf, "name", name ? name : "(unnamed)", &db->warnings);
    uw_text_attr(&db->buf, "key", name ? name : "(unnamed)", &db->warnings);

    el = uw_el_top(db);
    el->needs_options = UW_OPT_CROSS;
    uw_opts_reset(db);
    db->cross_n = 0;
    return db->buf.status;
}

UW_INTERNAL int uw_cross_add_cvp(uw_db_t* db, const char* name)
{
    int64_t ord;

    if (db->cross_n >= UW_MAX_CROSSED) {
        db->warnings++;
        return UCIS_WRITER_OK;
    }
    ord = uw_cvptab_get(db, UW_CVP_NAMESPACE, name ? name : "",
                        name ? strlen(name) : 0u);
    if (ord < 0) {
        /* Crossing a coverpoint that has not been written yet. The schema
         * ordering makes that impossible to satisfy -- every coverpoint
         * precedes every cross -- so it is a caller error, not a gap we can
         * paper over. */
        return uw_fail(db, UCIS_WRITER_ERR_ORDER,
                       "cross names a coverpoint that was not written first",
                       name);
    }
    db->cross_cvp[db->cross_n] = (uint32_t)ord;
    uw_copy_bounded(db, db->cross_names[db->cross_n],
                    sizeof(db->cross_names[0]), name);
    db->cross_n++;
    return UCIS_WRITER_OK;
}

/* ---- bins ------------------------------------------------------------- */

static const char* uw_cvgbin_type(ucisCoverTypeT type)
{
    if (type & UCIS_IGNOREBIN)  return "ignore";
    if (type & UCIS_ILLEGALBIN) return "illegal";
    if (type & UCIS_DEFAULTBIN) return "default";
    return "bins";
}

/* Read a decimal integer, returning the first byte after it. */
static const char* uw_scan_int(const char* p, const char* end, int64_t* out,
                               int* ok)
{
    int64_t v   = 0;
    int     neg = 0;
    int     any = 0;

    if (p < end && (*p == '-' || *p == '+')) {
        neg = (*p == '-');
        p++;
    }
    while (p < end && *p >= '0' && *p <= '9') {
        if (v > (int64_t)1 << 55) {   /* far past anything a bin value holds */
            *ok = 0;
            return p;
        }
        v = v * 10 + (*p - '0');
        p++;
        any = 1;
    }
    *ok  = any;
    *out = neg ? -v : v;
    return p;
}

/* Recover the value range a bin covers from its name.
 *
 * Bin names carry the values in the forms tools actually produce: "7",
 * "[7]", "auto[7]", "3:9", "auto[3:9]". When one of those parses, the range
 * we emit is the real thing. When it does not -- a named bin like "small" --
 * there is no value to emit and range/@from and @to are required, so the bin's
 * ordinal within its coverpoint stands in and the caller is warned. Nothing is
 * lost: the true name is on @name and on contents/@nameComponent. See D13. */
static int uw_parse_bin_range(const char* name, int64_t* from, int64_t* to)
{
    const char* p;
    const char* end;
    const char* lb;
    int         ok = 0;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    end = name + strlen(name);
    lb  = strchr(name, '[');
    p   = lb != NULL ? lb + 1 : name;

    p = uw_scan_int(p, end, from, &ok);
    if (!ok) {
        return 0;
    }
    *to = *from;
    if (p < end && *p == ':') {
        p = uw_scan_int(p + 1, end, to, &ok);
        if (!ok) {
            return 0;
        }
    }
    /* Anything but the closing bracket (or end of string) means the name only
     * happened to start with digits; do not pretend it was a range. */
    if (p < end && *p == ']') {
        p++;
    }
    return p == end;
}

static int uw_emit_coverpoint_bin(uw_db_t* db, const char* name,
                                  const ucisCoverDataT* data)
{
    uint32_t index = db->cvpbins.bin_count;
    int64_t  from  = 0;
    int64_t  to    = 0;

    UW_TRY(uw_cvptab_put(db, db->cvpbins.cvp_count - 1u, name ? name : "",
                         index));
    db->cvpbins.bin_count++;

    UW_TRY(uw_el_begin(db, "coverpointBin", 0));
    uw_text_attr(&db->buf, "name", name ? name : "(unnamed)", &db->warnings);
    uw_text_attr(&db->buf, "key", name ? name : "(unnamed)", &db->warnings);
    uw_text_attr(&db->buf, "type", uw_cvgbin_type(data->type), NULL);
    if (data->flags & UCIS_EXCLUDED) {
        uw_text_attr(&db->buf, "alias", "excluded", NULL);
    }

    if (!uw_parse_bin_range(name, &from, &to)) {
        from = to = (int64_t)index;
        db->warnings++;
    }
    UW_TRY(uw_el_begin(db, "range", 0));
    uw_text_attr_i64(&db->buf, "from", from);
    uw_text_attr_i64(&db->buf, "to", to);
    UW_TRY(uw_el_begin(db, "contents", 0));
    uw_text_attr(&db->buf, "nameComponent", name, &db->warnings);
    uw_text_attr_u64(&db->buf, "coverageCount", uw_cover_count(db, data));
    UW_TRY(uw_el_end(db));                  /* </contents> */
    UW_TRY(uw_el_end(db));                  /* </range> */
    return uw_el_end(db);                   /* </coverpointBin> */
}

/* One <index> per crossed coverpoint, resolved from the corresponding
 * component of the bin's name. */
static int uw_emit_cross_indices(uw_db_t* db, const char* name)
{
    const char* p = name != NULL ? name : "";
    unsigned    i = 0;

    do {
        const char* start = p;
        int64_t     ord;
        uint32_t    cvp;

        while (*p != '\0' && *p != ',') {
            p++;
        }
        cvp = (i < db->cross_n) ? db->cross_cvp[i] : i;
        ord = uw_cvptab_get(db, cvp, start, (size_t)(p - start));
        if (ord < 0) {
            /* The component does not name a bin of that coverpoint, so the
             * position it refers to is genuinely unknown. -1 says so rather
             * than pointing at an unrelated bin. */
            ord = -1;
            db->warnings++;
        }
        UW_TRY(uw_el_begin(db, "index", 0));
        UW_TRY(uw_el_commit(db));
        uw_buf_i64(&db->buf, ord);
        UW_TRY(uw_el_end(db));
        i++;
        if (*p == ',') {
            p++;
        } else {
            break;
        }
    } while (i < UW_MAX_CROSSED);

    /* index+ is minOccurs="1"; the loop above always runs at least once. */
    return db->buf.status;
}

static int uw_emit_cross_bin(uw_db_t* db, const char* name,
                             const ucisCoverDataT* data)
{
    UW_TRY(uw_el_begin(db, "crossBin", 0));
    uw_text_attr(&db->buf, "name", name ? name : "(unnamed)", &db->warnings);
    uw_text_attr(&db->buf, "key", name ? name : "(unnamed)", &db->warnings);
    uw_text_attr(&db->buf, "type", uw_cvgbin_type(data->type), NULL);

    UW_TRY(uw_emit_cross_indices(db, name));
    UW_TRY(uw_el_begin(db, "contents", 0));
    uw_text_attr(&db->buf, "nameComponent", name, &db->warnings);
    uw_text_attr_u64(&db->buf, "coverageCount", uw_cover_count(db, data));
    UW_TRY(uw_el_end(db));                  /* </contents> */
    return uw_el_end(db);                   /* </crossBin> */
}

UW_INTERNAL int uw_emit_cvg_bin(uw_db_t* db, const char* name,
                                const ucisCoverDataT* data)
{
    uw_elem_t* top = uw_el_top(db);

    if (top == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "covergroup bin outside any scope", name);
    }
    if (top->type == UCIS_COVERPOINT) {
        return uw_emit_coverpoint_bin(db, name, data);
    }
    if (top->type == UCIS_CROSS) {
        return uw_emit_cross_bin(db, name, data);
    }
    return uw_fail(db, UCIS_WRITER_ERR_STATE,
                   "covergroup bin outside a coverpoint or cross", name);
}

/* ==== uw_desc.c =============================================================== */

/* uw_desc.c - the descriptor API entry points (decision D15).
 * SPDX-License-Identifier: Apache-2.0
 *
 * Every function here takes everything its element needs in one struct, so the
 * element is written in a single pass and nothing has to be held open waiting
 * for a late property. That is the whole reason this layer exists; see D15.
 *
 * The emitters underneath are shared with the UCIS compatibility layer -- they
 * are the invariant part, the mapping from a coverage construct to the XSD's
 * element for it, and neither API changes them. */


#include <stdlib.h>
#include <string.h>

/* ---- defaults ---------------------------------------------------------
 *
 * Zero means "not set" for every field, so these are memset-equivalent today.
 * They exist anyway, for two reasons. They are the only initialiser form that
 * is clean from C99 through C++20 (D15's language-reach table), and they are
 * where a non-zero schema default would live if one is ever needed -- without
 * that, every emitter would have to re-derive it. */

#define UW_DEFAULTS(type, fn)      \
    type fn(void)                  \
    {                              \
        type d;                    \
        memset(&d, 0, sizeof(d));  \
        return d;                  \
    }

UW_DEFAULTS(uw_test_desc_t,        uw_test_defaults)
UW_DEFAULTS(uw_instance_desc_t,    uw_instance_defaults)
UW_DEFAULTS(uw_bin_desc_t,         uw_bin_defaults)
UW_DEFAULTS(uw_statement_desc_t,   uw_statement_defaults)
UW_DEFAULTS(uw_branch_desc_t,      uw_branch_defaults)
UW_DEFAULTS(uw_toggle_desc_t,      uw_toggle_defaults)
UW_DEFAULTS(uw_expr_desc_t,        uw_expr_defaults)
UW_DEFAULTS(uw_covergroup_desc_t,  uw_covergroup_defaults)
UW_DEFAULTS(uw_cginstance_desc_t,  uw_cginstance_defaults)
UW_DEFAULTS(uw_coverpoint_desc_t,  uw_coverpoint_defaults)
UW_DEFAULTS(uw_cross_desc_t,       uw_cross_defaults)

/* ---- shared helpers ---------------------------------------------------- */

/* Every entry point returns 0 or -1 in the UCIS convention; the reason is on
 * the database, not in the return value. */
static int uw_rc(uw_db_t* db)
{
    return (db->err || db->buf.status) ? -1 : 0;
}

static void uw_src_of(const uw_src_t* s, ucisSourceInfoT* out)
{
    out->filehandle = (s != NULL) ? (ucisFileHandleT)s->file : NULL;
    out->line       = (s != NULL) ? s->line : 0;
    out->token      = (s != NULL) ? s->token : 0;
}

static ucisCoverTypeT uw_bin_cover_type(uw_bin_kind_t kind)
{
    switch (kind) {
        case UW_BIN_DEFAULT: return UCIS_DEFAULTBIN;
        case UW_BIN_IGNORE:  return UCIS_IGNOREBIN;
        case UW_BIN_ILLEGAL: return UCIS_ILLEGALBIN;
        default:             return UCIS_CVGBIN;
    }
}

static void uw_cover_of(const uw_bin_desc_t* d, ucisCoverDataT* data,
                        ucisCoverTypeT type)
{
    memset(data, 0, sizeof(*data));
    data->type       = type;
    data->flags      = UCIS_IS_64BIT;
    data->data.int64 = d->count;
    if (d->goal > 0) {
        data->flags |= UCIS_HAS_GOAL;
        data->goal   = d->goal;
    }
    if (d->weight > 0) {
        data->flags |= UCIS_HAS_WEIGHT;
        data->weight = d->weight;
    }
    if (d->excluded) {
        data->flags |= UCIS_EXCLUDED;
    }
}

/* ---- lifecycle --------------------------------------------------------
 *
 * These forward to the UCIS spellings for now. The direction reverses when the
 * compat layer moves out: the descriptor API becomes the implementation and
 * ucis_* becomes the wrapper. Keeping it this way round during the transition
 * means the tree never has two copies of the open/close logic. */

uw_db_t* uw_open(const char* path)
{
    return (uw_db_t*)ucis_OpenWriteStream(path);
}

uw_db_t* uw_open_sink(const ucisWriterSinkT* sink)
{
    return (uw_db_t*)ucis_writer_OpenSinkStream(sink);
}

int uw_close(uw_db_t* db)          { return ucis_Close((ucisT)db); }
int uw_end(uw_db_t* db)            { return ucis_WriteStreamScope((ucisT)db); }

ucisWriterStatusT uw_error(uw_db_t* db)   { return ucis_writer_error((ucisT)db); }
const char* uw_error_string(uw_db_t* db)  { return ucis_writer_error_string((ucisT)db); }
const char* uw_status_name(ucisWriterStatusT s) { return ucis_writer_status_name(s); }
unsigned long uw_warnings(uw_db_t* db)    { return ucis_writer_warnings((ucisT)db); }
uint64_t uw_bytes_written(uw_db_t* db)    { return ucis_writer_bytes_written((ucisT)db); }
int uw_set_pretty(uw_db_t* db, int on)    { return ucis_writer_set_pretty((ucisT)db, on); }
int uw_set_written_by(uw_db_t* db, const char* w)   { return ucis_writer_set_written_by((ucisT)db, w); }
int uw_set_written_time(uw_db_t* db, const char* t) { return ucis_writer_set_written_time((ucisT)db, t); }

/* ---- tables ------------------------------------------------------------ */

uw_file_t* uw_file(uw_db_t* dbh, const char* path, const char* workdir)
{
    uw_db_t* db = uw_db_check((ucisT)dbh);
    if (db == NULL) {
        return NULL;
    }
    return uw_filetab_intern(db, path, workdir);
}

uw_test_t* uw_test(uw_db_t* dbh, const uw_test_desc_t* d)
{
    uw_db_t*   db = uw_db_check((ucisT)dbh);
    uw_hist_t* h;

    if (db == NULL) {
        return NULL;
    }
    if (d == NULL) {
        uw_fail(db, UCIS_WRITER_ERR_USAGE, "uw_test with NULL descriptor", NULL);
        return NULL;
    }
    if (db->phase != UW_PHASE_TABLES) {
        uw_fail(db, UCIS_WRITER_ERR_SEALED,
                "test record created after the first instance", d->name);
        return NULL;
    }
    h = uw_hist_create(db, d->name, d->physical_name,
                       (ucisHistoryNodeKindT)(d->kind ? (int)d->kind
                                                      : UCIS_HISTORYNODE_TEST));
    if (h == NULL) {
        return NULL;
    }

    h->has_testdata = 1;
    h->teststatus   = d->passed ? UCIS_TESTSTATUS_OK : UCIS_TESTSTATUS_ERROR;
    h->compulsory   = d->compulsory;
    h->simtime      = d->sim_time;
    h->cputime      = d->cpu_time;
    h->cost         = d->cost;

    h->timeunit       = uw_strdup(db, d->time_unit);
    h->runcwd         = uw_strdup(db, d->run_cwd);
    h->seed           = uw_strdup(db, d->seed);
    h->cmd            = uw_strdup(db, d->cmd);
    h->args           = uw_strdup(db, d->args);
    h->date           = uw_strdup(db, d->date);
    h->username       = uw_strdup(db, d->user_name);
    h->toolcategory   = uw_strdup(db, d->tool_category);
    h->comment        = uw_strdup(db, d->comment);
    h->vendor_id      = uw_strdup(db, d->vendor_id);
    h->vendor_tool    = uw_strdup(db, d->vendor_tool);
    h->vendor_version = uw_strdup(db, d->vendor_version);

    return (uw_test_t*)h;
}

/* ---- hierarchy --------------------------------------------------------- */

int uw_instance(uw_db_t* dbh, const uw_instance_desc_t* d)
{
    uw_db_t*        db = uw_db_check((ucisT)dbh);
    ucisSourceInfoT si;

    if (db == NULL) {
        return -1;
    }
    if (d == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "uw_instance with NULL descriptor", NULL) ? -1 : 0;
    }
    uw_src_of(&d->src, &si);
    ucis_CreateInstanceByName(db, NULL, d->name, &si, 1, UCIS_OTHER,
                              UCIS_INSTANCE, (char*)d->du_name,
                              UCIS_INST_ONCE);
    return uw_rc(db);
}

/* ---- code coverage ----------------------------------------------------- */

int uw_statement(uw_db_t* dbh, const uw_statement_desc_t* d)
{
    uw_db_t*        db = uw_db_check((ucisT)dbh);
    ucisSourceInfoT si;
    ucisCoverDataT  data;
    uw_bin_desc_t   b;

    if (db == NULL) {
        return -1;
    }
    if (d == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "uw_statement with NULL descriptor", NULL) ? -1 : 0;
    }
    b = uw_bin_defaults();
    b.count    = d->count;
    b.weight   = d->weight;
    b.excluded = d->excluded;
    uw_cover_of(&b, &data, UCIS_STMTBIN);
    uw_src_of(&d->src, &si);

    uw_emit_statement(db, d->name, &data, &si);
    return uw_rc(db);
}

int uw_branch(uw_db_t* dbh, const uw_branch_desc_t* d)
{
    uw_db_t*        db = uw_db_check((ucisT)dbh);
    ucisSourceInfoT si;

    if (db == NULL) {
        return -1;
    }
    if (d == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "uw_branch with NULL descriptor", NULL) ? -1 : 0;
    }
    uw_src_of(&d->src, &si);
    if (uw_open_branch_statement(db, d->name, &si) != UCIS_WRITER_OK) {
        return -1;
    }
    /* @statementType is required and defaults to "if". The descriptor states
     * it up front, so unlike the UCIS path there is no window in which the
     * element is already committed and the answer arrives too late. */
    if (d->is_case) {
        uw_text_attr(&db->buf, "statementType", "case", NULL);
    }
    if (d->has_else) {
        uw_text_attr(&db->buf, "hasElse", "true", NULL);
    }
    return uw_rc(db);
}

int uw_toggle(uw_db_t* dbh, const uw_toggle_desc_t* d)
{
    uw_db_t* db = uw_db_check((ucisT)dbh);

    if (db == NULL) {
        return -1;
    }
    if (d == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "uw_toggle with NULL descriptor", NULL) ? -1 : 0;
    }
    uw_open_toggle(db, d->name, d->canonical_name,
                   (ucisToggleTypeT)d->type, (ucisToggleDirT)d->dir);
    return uw_rc(db);
}

int uw_toggle_bit(uw_db_t* dbh, const char* name)
{
    uw_db_t*   db  = uw_db_check((ucisT)dbh);
    uw_elem_t* top;

    if (db == NULL) {
        return -1;
    }
    top = uw_el_top(db);
    if (top == NULL || top->type != UCIS_TOGGLE) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "uw_toggle_bit outside a toggle object", name) ? -1 : 0;
    }
    /* uw_open_toggle opens a bit rather than an object when one is already
     * open, which is exactly the nesting UCIS uses for a vector. */
    uw_open_toggle(db, name, NULL, UCIS_TOGGLE_TYPE_NET,
                   UCIS_TOGGLE_DIR_INTERNAL);
    return uw_rc(db);
}

int uw_expr(uw_db_t* dbh, const uw_expr_desc_t* d)
{
    uw_db_t*        db = uw_db_check((ucisT)dbh);
    ucisSourceInfoT si;

    if (db == NULL) {
        return -1;
    }
    if (d == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "uw_expr with NULL descriptor", NULL) ? -1 : 0;
    }
    uw_src_of(&d->src, &si);
    if (uw_open_expr(db, d->name, &si) != UCIS_WRITER_OK) {
        return -1;
    }
    if (d->terms != NULL) {
        uw_copy_bounded(db, db->expr_terms, sizeof(db->expr_terms), d->terms);
        db->expr_terms_explicit = 1;
    }
    return uw_rc(db);
}

int uw_expr_metric(uw_db_t* dbh, const char* name)
{
    uw_db_t*   db = uw_db_check((ucisT)dbh);
    uw_elem_t* top;

    if (db == NULL) {
        return -1;
    }
    top = uw_el_top(db);
    if (top == NULL || (top->type != UCIS_EXPR && top->type != UCIS_COND)) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "uw_expr_metric outside an expression", name) ? -1 : 0;
    }
    uw_open_expr(db, name, NULL);
    return uw_rc(db);
}

/* ---- functional coverage ----------------------------------------------- */

/* Copy the option fields a descriptor carries into the staging area the
 * pending element's <options> will be written from. Only fields the caller set
 * are marked: every option attribute has a schema default, and emitting one we
 * were never told would assert a value nobody chose. */
static void uw_stage_opts(uw_db_t* db, int weight, int goal, int at_least,
                          int auto_bin_max, int detect_overlap,
                          int num_print_missing, int per_instance,
                          int merge_instances, int have_cgi)
{
    uw_opts_t* o = &db->opts;

    if (weight > 0)       { o->weight = weight;     o->set |= UW_OPTSET_WEIGHT; }
    if (goal > 0)         { o->goal = goal;         o->set |= UW_OPTSET_GOAL; }
    if (at_least > 0)     { o->at_least = at_least; o->set |= UW_OPTSET_AT_LEAST; }
    if (auto_bin_max > 0) { o->auto_bin_max = auto_bin_max;
                            o->set |= UW_OPTSET_AUTO_BIN_MAX; }
    if (detect_overlap)   { o->detect_overlap = 1;
                            o->set |= UW_OPTSET_DETECT_OVERLAP; }
    if (num_print_missing > 0) { o->num_print_missing = num_print_missing;
                                 o->set |= UW_OPTSET_PRINT_MISSING; }
    if (have_cgi) {
        if (per_instance)    { o->per_instance = 1;
                               o->set |= UW_OPTSET_PER_INSTANCE; }
        if (merge_instances) { o->merge_instances = 1;
                               o->set |= UW_OPTSET_MERGE_INST; }
    }
}

int uw_covergroup(uw_db_t* dbh, const uw_covergroup_desc_t* d)
{
    uw_db_t*        db = uw_db_check((ucisT)dbh);
    ucisSourceInfoT si;

    if (db == NULL) {
        return -1;
    }
    if (d == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "uw_covergroup with NULL descriptor", NULL) ? -1 : 0;
    }
    uw_src_of(&d->src, &si);
    uw_open_covergroup(db, d->name, &si);
    return uw_rc(db);
}

int uw_cginstance(uw_db_t* dbh, const uw_cginstance_desc_t* d)
{
    uw_db_t*        db = uw_db_check((ucisT)dbh);
    ucisSourceInfoT si;

    if (db == NULL) {
        return -1;
    }
    if (d == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "uw_cginstance with NULL descriptor", NULL) ? -1 : 0;
    }
    uw_src_of(&d->src, &si);
    if (uw_open_coverinstance(db, d->name, &si) != UCIS_WRITER_OK) {
        return -1;
    }
    uw_stage_opts(db, d->weight, d->goal, d->at_least, d->auto_bin_max,
                  d->detect_overlap, d->num_print_missing, d->per_instance,
                  d->merge_instances, 1);
    return uw_rc(db);
}

int uw_coverpoint(uw_db_t* dbh, const uw_coverpoint_desc_t* d)
{
    uw_db_t* db = uw_db_check((ucisT)dbh);

    if (db == NULL) {
        return -1;
    }
    if (d == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "uw_coverpoint with NULL descriptor", NULL) ? -1 : 0;
    }
    if (uw_open_coverpoint(db, d->name) != UCIS_WRITER_OK) {
        return -1;
    }
    if (d->expr_string != NULL) {
        uw_text_attr(&db->buf, "exprString", d->expr_string, &db->warnings);
    }
    uw_stage_opts(db, d->weight, d->goal, d->at_least, d->auto_bin_max,
                  d->detect_overlap, 0, 0, 0, 0);
    return uw_rc(db);
}

int uw_cross(uw_db_t* dbh, const uw_cross_desc_t* d)
{
    uw_db_t* db = uw_db_check((ucisT)dbh);
    size_t   i;

    if (db == NULL) {
        return -1;
    }
    if (d == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "uw_cross with NULL descriptor", NULL) ? -1 : 0;
    }
    if (uw_open_cross(db, d->name) != UCIS_WRITER_OK) {
        return -1;
    }
    for (i = 0; i < d->n_crossed; ++i) {
        if (uw_cross_add_cvp(db, d->crossed[i]) != UCIS_WRITER_OK) {
            return -1;
        }
    }
    uw_stage_opts(db, d->weight, d->goal, d->at_least, 0, 0,
                  d->num_print_missing, 0, 0, 0);
    return uw_rc(db);
}

/* ---- bins --------------------------------------------------------------
 *
 * One entry point for every kind of bin, routed by the scope it lands in
 * rather than by a type constant the caller has to supply. The scope already
 * determines which element is legal, so asking the caller to say it again is
 * asking them to be wrong. */

int uw_bin(uw_db_t* dbh, const uw_bin_desc_t* d)
{
    uw_db_t*        db = uw_db_check((ucisT)dbh);
    uw_elem_t*      top;
    ucisCoverDataT  data;
    ucisSourceInfoT si;

    if (db == NULL) {
        return -1;
    }
    if (d == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "uw_bin with NULL descriptor", NULL) ? -1 : 0;
    }
    top = uw_el_top(db);
    if (top == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "uw_bin with no scope open", d->name) ? -1 : 0;
    }

    if (top->type == UCIS_BRANCH) {
        uw_cover_of(d, &data, UCIS_BRANCHBIN);
        uw_src_of(&d->src, &si);
        uw_emit_branch(db, d->name, &data, &si);
    } else if (top->type == UCIS_TOGGLE) {
        uw_cover_of(d, &data, UCIS_TOGGLEBIN);
        uw_emit_toggle_bin(db, d->name, &data);
    } else if (top->type == UCIS_EXPR || top->type == UCIS_COND) {
        uw_cover_of(d, &data, UCIS_EXPRBIN);
        uw_emit_expr_bin(db, d->name, &data);
    } else if (top->type == UCIS_COVERPOINT || top->type == UCIS_CROSS) {
        uw_cover_of(d, &data, uw_bin_cover_type(d->kind));
        uw_emit_cvg_bin(db, d->name, &data);
    } else {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "no scope open that can hold a bin", d->name) ? -1 : 0;
    }
    return uw_rc(db);
}

/* ==== uw_props.c ============================================================== */

/* uw_props.c - ucis_Set{Int,String,Real}Property.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Work item 3.11 of docs/ucis-writer-impl-plan.md.
 *
 * A property is settable only while its target can still absorb it. That is a
 * narrower window than the in-memory UCIS API offers, and it is the price of
 * never buffering an object:
 *
 *   - database properties (obj == NULL) until the first content is written
 *   - history-node properties until the tables flush, since those are resident
 *   - scope properties only while that scope's start tag is still pending
 *   - coveritem properties never; they are emitted atomically from
 *     ucisCoverDataT (see D7)
 *
 * Anything outside its window is UCIS_WRITER_ERR_ORDER, not a silent no-op.
 * A dropped weight or exclusion that nobody reports is a coverage number that
 * is quietly wrong. */


#include <stdlib.h>
#include <string.h>

/* Is `obj` one of the resident history nodes? Walking the list is fine: a
 * document has a handful of them, and this only runs on property sets. */
static uw_hist_t* uw_as_hist(uw_db_t* db, ucisObjT obj)
{
    uw_hist_t* h;
    for (h = db->hist_head; h != NULL; h = h->next) {
        if ((ucisObjT)h == obj) {
            return h;
        }
    }
    return NULL;
}

static int uw_set_owned(uw_db_t* db, char** slot, const char* value)
{
    free(*slot);
    *slot = uw_strdup(db, value);
    return (db->err || db->buf.status) ? -1 : 0;
}

/* The innermost element, if it is still accepting attributes. */
static uw_elem_t* uw_pending_scope(uw_db_t* db)
{
    uw_elem_t* top = uw_el_top(db);
    return (top != NULL && top->pending) ? top : NULL;
}

int ucis_SetStringProperty(ucisT dbh, ucisObjT obj, int coverindex,
                           ucisStringPropertyEnumT property, const char* value)
{
    uw_db_t*   db = uw_db_check(dbh);
    uw_hist_t* h;

    if (db == NULL) {
        return -1;
    }
    if (coverindex >= 0) {
        return uw_fail(db, UCIS_WRITER_ERR_ORDER,
                       "coveritem properties must be set in ucisCoverDataT",
                       NULL) ? -1 : 0;
    }

    if (obj == NULL) {
        /* Database-wide defaults, inherited by every history node that does
         * not override them. */
        switch (property) {
            case UCIS_STR_VER_VENDOR_ID:      return uw_set_owned(db, &db->vendor_id, value);
            case UCIS_STR_VER_VENDOR_TOOL:    return uw_set_owned(db, &db->vendor_tool, value);
            case UCIS_STR_VER_VENDOR_VERSION: return uw_set_owned(db, &db->vendor_version, value);
            default: break;
        }
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "unsupported database string property", NULL) ? -1 : 0;
    }

    h = uw_as_hist(db, obj);
    if (h != NULL) {
        if (db->phase != UW_PHASE_TABLES) {
            return uw_fail(db, UCIS_WRITER_ERR_SEALED,
                           "history node modified after it was written", NULL) ? -1 : 0;
        }
        switch (property) {
            case UCIS_STR_HIST_LOG_NAME:      return uw_set_owned(db, &h->logicalname, value);
            case UCIS_STR_HIST_PHYS_NAME:     return uw_set_owned(db, &h->physicalname, value);
            case UCIS_STR_HIST_TOOLCATEGORY:  return uw_set_owned(db, &h->toolcategory, value);
            case UCIS_STR_HIST_CMDLINE:       return uw_set_owned(db, &h->cmd, value);
            case UCIS_STR_HIST_RUNCWD:        return uw_set_owned(db, &h->runcwd, value);
            case UCIS_STR_COMMENT:            return uw_set_owned(db, &h->comment, value);
            case UCIS_STR_TEST_TIMEUNIT:      return uw_set_owned(db, &h->timeunit, value);
            case UCIS_STR_TEST_DATE:          return uw_set_owned(db, &h->date, value);
            case UCIS_STR_TEST_SIMARGS:       return uw_set_owned(db, &h->args, value);
            case UCIS_STR_TEST_USERNAME:      return uw_set_owned(db, &h->username, value);
            case UCIS_STR_TEST_SEED:          return uw_set_owned(db, &h->seed, value);
            case UCIS_STR_VER_VENDOR_ID:      return uw_set_owned(db, &h->vendor_id, value);
            case UCIS_STR_VER_VENDOR_TOOL:    return uw_set_owned(db, &h->vendor_tool, value);
            case UCIS_STR_VER_VENDOR_VERSION: return uw_set_owned(db, &h->vendor_version, value);
            default: break;
        }
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "unsupported history-node string property", NULL) ? -1 : 0;
    }

    /* Otherwise it is meant for the current scope. */
    if (uw_pending_scope(db) == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_ORDER,
                       "scope property set after the scope's attributes were written",
                       NULL) ? -1 : 0;
    }
    if (property == UCIS_STR_ITH_CROSSED_CVP_NAME) {
        if (uw_pending_scope(db)->type != UCIS_CROSS) {
            return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                           "crossed coverpoint named on a scope that is not a cross",
                           value) ? -1 : 0;
        }
        return uw_cross_add_cvp(db, value) == UCIS_WRITER_OK ? 0 : -1;
    }
    switch (property) {
        case UCIS_STR_INSTANCE_DU_NAME:
            uw_text_attr(&db->buf, "moduleName", value, &db->warnings);
            return 0;
        case UCIS_STR_TOGGLE_CANON_NAME:
        case UCIS_STR_UNIQUE_ID:
            uw_text_attr(&db->buf, "key", value, &db->warnings);
            return 0;
        case UCIS_STR_UNIQUE_ID_ALIAS:
            uw_text_attr(&db->buf, "alias", value, &db->warnings);
            return 0;
        case UCIS_STR_EXPR_TERMS:
            /* Not an attribute: this becomes EXPR's subExpr+ children and its
             * @width, both emitted when the start tag is terminated. */
            uw_copy_bounded(db, db->expr_terms, sizeof(db->expr_terms), value);
            db->expr_terms_explicit = 1;
            return 0;
        default: break;
    }
    return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                   "unsupported scope string property", NULL) ? -1 : 0;
}

int ucis_SetIntProperty(ucisT dbh, ucisObjT obj, int coverindex,
                        ucisIntPropertyEnumT property, int value)
{
    uw_db_t*   db = uw_db_check(dbh);
    uw_hist_t* h;

    if (db == NULL) {
        return -1;
    }
    if (coverindex >= 0) {
        return uw_fail(db, UCIS_WRITER_ERR_ORDER,
                       "coveritem properties must be set in ucisCoverDataT",
                       NULL) ? -1 : 0;
    }

    h = obj ? uw_as_hist(db, obj) : NULL;
    if (h != NULL) {
        if (db->phase != UW_PHASE_TABLES) {
            return uw_fail(db, UCIS_WRITER_ERR_SEALED,
                           "history node modified after it was written", NULL) ? -1 : 0;
        }
        switch (property) {
            case UCIS_INT_TEST_STATUS:
                h->teststatus = (ucisTestStatusT)value;
                h->has_testdata = 1;
                return 0;
            case UCIS_INT_TEST_COMPULSORY:
                h->compulsory = value;
                h->has_testdata = 1;
                return 0;
            default: break;
        }
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "unsupported history-node int property", NULL) ? -1 : 0;
    }

    if (obj == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "unsupported database int property", NULL) ? -1 : 0;
    }

    if (uw_pending_scope(db) == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_ORDER,
                       "scope property set after the scope's attributes were written",
                       NULL) ? -1 : 0;
    }
    /* On a covergroup scope these are not attributes at all: they belong in
     * the <options> element, which is emitted when the start tag closes. That
     * includes weight and goal, which COVERPOINT and CROSS carry only there. */
    if (uw_pending_scope(db)->needs_options != UW_OPT_NONE) {
        if (uw_opts_set(db, uw_pending_scope(db)->needs_options, property,
                        value)) {
            return 0;
        }
    }
    switch (property) {
        case UCIS_INT_SCOPE_WEIGHT:
            /* @weight defaults to 1 throughout the schema; emitting the
             * default would add bytes to every element for no information. */
            if (value != 1) {
                uw_text_attr_i64(&db->buf, "weight", value);
            }
            return 0;
        default: break;
    }
    return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                   "unsupported scope int property", NULL) ? -1 : 0;
}

int ucis_SetRealProperty(ucisT dbh, ucisObjT obj, int coverindex,
                         ucisRealPropertyEnumT property, double value)
{
    uw_db_t*   db = uw_db_check(dbh);
    uw_hist_t* h;

    if (db == NULL) {
        return -1;
    }
    if (coverindex >= 0) {
        return uw_fail(db, UCIS_WRITER_ERR_ORDER,
                       "coveritem properties must be set in ucisCoverDataT",
                       NULL) ? -1 : 0;
    }
    h = obj ? uw_as_hist(db, obj) : NULL;
    if (h == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "real properties apply to history nodes only", NULL) ? -1 : 0;
    }
    if (db->phase != UW_PHASE_TABLES) {
        return uw_fail(db, UCIS_WRITER_ERR_SEALED,
                       "history node modified after it was written", NULL) ? -1 : 0;
    }
    h->has_testdata = 1;
    switch (property) {
        case UCIS_REAL_HIST_CPUTIME:  h->cputime = value; return 0;
        case UCIS_REAL_TEST_SIMTIME:  h->simtime = value; return 0;
        case UCIS_REAL_TEST_COST:     h->cost    = value; return 0;
        default: break;
    }
    return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                   "unsupported real property", NULL) ? -1 : 0;
}

/* ==== uw_api.c ================================================================ */

/* uw_api.c - the ucis_* entry points.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Work items 1.8, 2.1 - 2.6 and 3.2 of docs/ucis-writer-impl-plan.md.
 *
 * Write-streaming rules enforced here, from UCIS 1.0 Annex A.14:
 *   - every `parent` argument must be NULL; the parent is the implicit
 *     current scope
 *   - strings are not copied except into the two resident tables
 *   - objects are finished by the next create call, or explicitly by
 *     ucis_WriteStream / ucis_WriteStreamScope */


#include <stdlib.h>
#include <string.h>

/* ---- FILE* sink ------------------------------------------------------- */

static int uw_file_sink_write(void* ctx, const char* data, size_t len)
{
    return fwrite(data, 1, len, (FILE*)ctx) == len ? 0 : -1;
}

static int uw_file_sink_close(void* ctx)
{
    return fclose((FILE*)ctx);
}

/* ---- construction ----------------------------------------------------- */

static uw_db_t* uw_db_new(const ucisWriterSinkT* sink)
{
    uw_db_t* db = (uw_db_t*)calloc(1, sizeof(uw_db_t));
    char     now[24];

    if (db == NULL) {
        return NULL;
    }
    db->bufstore = (char*)malloc(UCIS_WRITER_BUFSZ);
    if (db->bufstore == NULL) {
        free(db);
        return NULL;
    }
    db->magic     = UW_DB_MAGIC;
    db->path_sep  = '/';
    db->phase     = UW_PHASE_TABLES;
    uw_buf_init(&db->buf, db->bufstore, UCIS_WRITER_BUFSZ, sink);
    uw_filetab_init(&db->files);

    uw_now_iso8601(now, sizeof(now));
    db->written_time = uw_strdup(db, now);
    db->written_by   = uw_strdup(db, UCIS_WRITER_VENDOR_ID " " UCIS_WRITER_VENDOR_TOOL);

    /* The root element opens immediately but stays pending, so that
     * ucis_writer_set_written_by / _time can still change its attributes right
     * up until the first thing is written into the document. */
    uw_el_begin(db, "UCIS", 0);
    return db;
}

/* Terminate the root's attribute list. Called once, when the document body
 * starts, because writtenBy/writtenTime are settable until then. */
UW_INTERNAL int uw_root_seal(uw_db_t* db)
{
    uw_elem_t* root;
    if (db->depth < 1) {
        return db->buf.status;
    }
    root = &db->stack[0];
    if (!root->pending) {
        return db->buf.status;
    }
    uw_text_attr(&db->buf, "ucisVersion", UCIS_WRITER_UCIS_VERSION, NULL);
    uw_text_attr(&db->buf, "writtenBy",
                 db->written_by ? db->written_by : UCIS_WRITER_VENDOR_TOOL,
                 &db->warnings);
    uw_text_attr(&db->buf, "writtenTime",
                 uw_is_datetime(db->written_time) ? db->written_time
                                                  : "1970-01-01T00:00:00Z",
                 &db->warnings);
    return db->buf.status;
}

/* Everything that writes into the document body funnels through here: seal
 * the root, then flush the resident tables if they have not gone out yet. */
UW_INTERNAL int uw_body(uw_db_t* db)
{
    if (uw_root_seal(db) != UCIS_WRITER_OK) {
        return db->buf.status;
    }
    return uw_tables_flush(db);
}

ucisT ucis_writer_OpenSinkStream(const ucisWriterSinkT* sink)
{
    if (sink == NULL || sink->write == NULL) {
        return NULL;
    }
    return (ucisT)uw_db_new(sink);
}

ucisT ucis_OpenWriteStream(const char* name)
{
    ucisWriterSinkT sink;
    FILE*           fp;
    uw_db_t*        db;

    if (name == NULL) {
        return NULL;
    }
    fp = fopen(name, "wb");
    if (fp == NULL) {
        return NULL;
    }
    sink.write = uw_file_sink_write;
    sink.close = uw_file_sink_close;
    sink.ctx   = fp;

    db = uw_db_new(&sink);
    if (db == NULL) {
        fclose(fp);
        return NULL;
    }
    db->own_file = fp;
    return (ucisT)db;
}

int ucis_writer_set_written_by(ucisT dbh, const char* who)
{
    uw_db_t* db = uw_db_check(dbh);
    if (db == NULL || who == NULL) {
        return -1;
    }
    if (db->phase != UW_PHASE_TABLES || !db->stack[0].pending) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "writtenBy set after the document body started", NULL) ? -1 : 0;
    }
    free(db->written_by);
    db->written_by = uw_strdup(db, who);
    return (db->err || db->buf.status) ? -1 : 0;
}

int ucis_writer_set_written_time(ucisT dbh, const char* xsd_datetime)
{
    uw_db_t* db = uw_db_check(dbh);
    if (db == NULL || xsd_datetime == NULL) {
        return -1;
    }
    if (!uw_is_datetime(xsd_datetime)) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "not an xsd:dateTime", xsd_datetime) ? -1 : 0;
    }
    if (!db->stack[0].pending) {
        return uw_fail(db, UCIS_WRITER_ERR_STATE,
                       "writtenTime set after the document body started", NULL) ? -1 : 0;
    }
    free(db->written_time);
    db->written_time = uw_strdup(db, xsd_datetime);
    return (db->err || db->buf.status) ? -1 : 0;
}

/* ---- lifecycle -------------------------------------------------------- */

int ucis_WriteStream(ucisT dbh)
{
    uw_db_t* db = uw_db_check(dbh);
    if (db == NULL) {
        return -1;
    }
    /* "Flush the in-flight object": terminate its start tag so no further
     * attribute can be attached to it. Children are still permitted. */
    uw_el_commit(db);
    return (db->err || db->buf.status) ? -1 : 0;
}

int ucis_WriteStreamScope(ucisT dbh)
{
    uw_db_t* db = uw_db_check(dbh);
    if (db == NULL) {
        return -1;
    }
    if (db->depth <= 1) {
        /* Depth 1 is the root, which only ucis_Close may close. */
        uw_fail(db, UCIS_WRITER_ERR_UNBALANCED,
                "ucis_WriteStreamScope with no scope open", NULL);
        return -1;
    }
    /* Close through any wrappers this library opened -- coverage-kind
     * elements have no counterpart in the UCIS call sequence, so a caller
     * balancing its own scopes must not have to know they exist. */
    while (db->depth > 1) {
        int owned = db->stack[db->depth - 1].owned;
        if (uw_el_end(db) != UCIS_WRITER_OK) {
            break;
        }
        if (!owned) {
            break;
        }
    }
    return (db->err || db->buf.status) ? -1 : 0;
}

int ucis_Close(ucisT dbh)
{
    uw_db_t*    db = uw_db_check(dbh);
    const char* unbalanced;
    int         status;

    if (db == NULL) {
        return -1;
    }
    /* An unbalanced document is a caller bug worth reporting, but the file
     * still gets closed properly: half a document that no reader accepts
     * helps nobody diagnose anything. So finish the document first and latch
     * the error afterwards -- latching it now would suppress the very output
     * we are trying to salvage. */
    unbalanced = db->depth > 1 ? db->stack[db->depth - 1].tag : NULL;

    uw_body(db);                 /* emits the tables even for an empty document */
    if (db->inst_count == 0) {
        uw_placeholder_instance(db);
    }
    uw_el_unwind(db, 0);
    if (db->pretty) {
        uw_buf_putc(&db->buf, '\n');
    }
    uw_buf_finish(&db->buf);
    if (unbalanced != NULL) {
        uw_fail(db, UCIS_WRITER_ERR_UNBALANCED,
                "scope still open at ucis_Close", unbalanced);
    }

    /* Either class of failure makes the return non-zero; the caller finds out
     * which from ucis_writer_error before calling this. */
    status = db->err ? db->err : db->buf.status;

    uw_filetab_free(&db->files);
    uw_hist_free_all(db);
    uw_cvptab_reset(&db->cvpbins);
    free(db->written_by);
    free(db->written_time);
    free(db->cur_du);
    free(db->vendor_id);
    free(db->vendor_tool);
    free(db->vendor_version);
    free(db->bufstore);
    db->magic = 0;
    free(db);

    return status == UCIS_WRITER_OK ? 0 : -1;
}

int ucis_SetPathSeparator(ucisT dbh, char separator)
{
    uw_db_t* db = uw_db_check(dbh);
    if (db == NULL) {
        return -1;
    }
    db->path_sep = separator;
    return 0;
}

char ucis_GetPathSeparator(ucisT dbh)
{
    uw_db_t* db = uw_db_check(dbh);
    return db ? db->path_sep : '/';
}

/* ---- file handles ----------------------------------------------------- */

ucisFileHandleT ucis_CreateFileHandle(ucisT dbh, const char* filename,
                                      const char* fileworkdir)
{
    uw_db_t* db = uw_db_check(dbh);
    if (db == NULL) {
        return NULL;
    }
    return (ucisFileHandleT)uw_filetab_intern(db, filename, fileworkdir);
}

ucisFileHandleT ucis_CreateSrcFileHandle(ucisT dbh, ucisScopeT du_scope,
                                         const char* filename,
                                         const char* fileworkdir)
{
    /* UCIS-XML has one flat SOURCE_FILE table, not the per-DU tables the C
     * API allows, so the DU association has nowhere to go in this backend. */
    (void)du_scope;
    return ucis_CreateFileHandle(dbh, filename, fileworkdir);
}

const char* ucis_GetFileName(ucisT dbh, ucisFileHandleT filehandle)
{
    uw_file_t* f = (uw_file_t*)filehandle;
    (void)dbh;
    return f ? f->name : NULL;
}

/* ---- history nodes ---------------------------------------------------- */

ucisHistoryNodeT ucis_CreateHistoryNode(ucisT dbh, ucisHistoryNodeT parent,
                                        char* logicalname, char* physicalname,
                                        ucisHistoryNodeKindT kind)
{
    uw_db_t*   db = uw_db_check(dbh);
    uw_hist_t* h;

    if (db == NULL) {
        return NULL;
    }
    if (db->phase != UW_PHASE_TABLES) {
        uw_fail(db, UCIS_WRITER_ERR_SEALED,
                "history node created after the first instance", logicalname);
        return NULL;
    }
    h = uw_hist_create(db, logicalname, physicalname, kind);
    if (h != NULL && parent != NULL) {
        h->has_parent = 1;
        h->parent_id  = ((uw_hist_t*)parent)->id;
    }
    return (ucisHistoryNodeT)h;
}

int ucis_SetTestData(ucisT dbh, ucisHistoryNodeT node, ucisTestDataT* data)
{
    uw_db_t*   db = uw_db_check(dbh);
    uw_hist_t* h  = (uw_hist_t*)node;

    if (db == NULL) {
        return -1;
    }
    if (h == NULL || data == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE, "ucis_SetTestData", NULL) ? -1 : 0;
    }
    h->has_testdata = 1;
    h->teststatus   = data->teststatus;
    h->simtime      = data->simtime;
    h->cputime      = data->cputime;
    h->cost         = data->cost;
    h->compulsory   = data->compulsory;

    free(h->timeunit);     h->timeunit     = uw_strdup(db, data->timeunit);
    free(h->runcwd);       h->runcwd       = uw_strdup(db, data->runcwd);
    free(h->seed);         h->seed         = uw_strdup(db, data->seed);
    free(h->cmd);          h->cmd          = uw_strdup(db, data->cmd);
    free(h->args);         h->args         = uw_strdup(db, data->args);
    free(h->date);         h->date         = uw_strdup(db, data->date);
    free(h->username);     h->username     = uw_strdup(db, data->username);
    free(h->toolcategory); h->toolcategory = uw_strdup(db, data->toolcategory);

    return (db->err || db->buf.status) ? -1 : 0;
}

/* ---- instances -------------------------------------------------------- */

ucisScopeT ucis_CreateInstanceByName(ucisT dbh, ucisScopeT parent,
                                     const char* name, ucisSourceInfoT* srcinfo,
                                     int weight, ucisSourceT source,
                                     ucisScopeTypeT type, char* du_name,
                                     int flags)
{
    uw_db_t*   db = uw_db_check(dbh);
    uw_file_t* f;
    uint32_t   fileid = 0;
    uint32_t   line   = 0;

    (void)weight;
    (void)source;
    (void)flags;

    if (db == NULL) {
        return NULL;
    }
    if (parent != NULL) {
        uw_fail(db, UCIS_WRITER_ERR_USAGE,
                "write-streaming requires a NULL parent", name);
        return NULL;
    }
    if (type != UCIS_INSTANCE) {
        uw_fail(db, UCIS_WRITER_ERR_USAGE,
                "ucis_CreateInstanceByName expects UCIS_INSTANCE", name);
        return NULL;
    }
    if (uw_body(db) != UCIS_WRITER_OK) {
        return NULL;
    }
    /* instanceCoverages is a flat list under the root, related by
     * @parentInstanceId rather than by nesting, so an instance always closes
     * whatever came before it. */
    if (uw_el_unwind(db, 1) != UCIS_WRITER_OK) {
        return NULL;
    }
    if (uw_el_begin(db, "instanceCoverages", UCIS_INSTANCE) != UCIS_WRITER_OK) {
        return NULL;
    }

    uw_text_attr(&db->buf, "name", name ? name : "(unnamed)", &db->warnings);
    /* @key must be unique across the document. The hierarchical instance name
     * already is, and reusing it keeps the document self-describing. */
    uw_text_attr(&db->buf, "key", name ? name : "(unnamed)", &db->warnings);
    uw_text_attr(&db->buf, "moduleName", du_name, &db->warnings);
    /* @instanceId is a free counter. @parentInstanceId is deliberately absent:
     * reconstructing it would need a resident name-to-id table growing with
     * the design's instance count, to encode a relationship the hierarchical
     * @name already carries. See D10. */
    uw_text_attr_u64(&db->buf, "instanceId", db->inst_count + 1u);

    if (srcinfo != NULL) {
        f = (uw_file_t*)srcinfo->filehandle;
        if (f != NULL) {
            fileid = f->id;
        }
        line = srcinfo->line > 0 ? (uint32_t)srcinfo->line : 0u;
    }
    /* Instances routinely have no source location — Verilator's coverage.dat
     * carries none — so a clamp here is not a warning. */
    uw_el_set_id(db, fileid, line, 1u, 0);
    db->inst_count++;
    db->inst_depth = db->depth;
    db->cur_kind   = UW_KIND_NONE;
    db->block_mode = 0;
    /* CG_ID/@moduleName is required and has no argument of its own; the design
     * unit of the instance the covergroup lives in is what it means. */
    uw_copy_bounded(db, db->cg_module, sizeof(db->cg_module), du_name);

    return (ucisScopeT)uw_el_top(db);
}

/* ---- scopes ----------------------------------------------------------- */

ucisScopeT ucis_CreateScope(ucisT dbh, ucisScopeT parent, const char* name,
                            ucisSourceInfoT* srcinfo, int weight,
                            ucisSourceT source, ucisScopeTypeT type,
                            ucisFlagsT flags)
{
    uw_db_t* db = uw_db_check(dbh);

    (void)weight;
    (void)source;
    (void)flags;

    if (db == NULL) {
        return NULL;
    }
    if (parent != NULL) {
        uw_fail(db, UCIS_WRITER_ERR_USAGE,
                "write-streaming requires a NULL parent", name);
        return NULL;
    }

    if (type & UCIS_DU_ANY) {
        /* UCIS-XML has no design-unit element (D8). The name is the only part
         * that survives, as @moduleName on instances created after it, so we
         * remember it and emit nothing. */
        free(db->cur_du);
        db->cur_du = uw_strdup(db, name);
        return (ucisScopeT)db;   /* a handle that is valid but not an element */
    }
    if (type == UCIS_INSTANCE) {
        return ucis_CreateInstanceByName(db, NULL, name, srcinfo, weight,
                                         source, type,
                                         db->cur_du, (int)flags);
    }
    if (type == UCIS_EXPR || type == UCIS_COND) {
        if (uw_open_expr(db, name, srcinfo) != UCIS_WRITER_OK) {
            return NULL;
        }
        return (ucisScopeT)uw_el_top(db);
    }
    if (type == UCIS_BRANCH) {
        if (uw_open_branch_statement(db, name, srcinfo) != UCIS_WRITER_OK) {
            return NULL;
        }
        return (ucisScopeT)uw_el_top(db);
    }
    if (type == UCIS_COVERGROUP) {
        if (uw_open_covergroup(db, name, srcinfo) != UCIS_WRITER_OK) {
            return NULL;
        }
        return (ucisScopeT)uw_el_top(db);
    }
    if (type == UCIS_COVERINSTANCE) {
        if (uw_open_coverinstance(db, name, srcinfo) != UCIS_WRITER_OK) {
            return NULL;
        }
        return (ucisScopeT)uw_el_top(db);
    }
    if (type == UCIS_COVERPOINT) {
        if (uw_open_coverpoint(db, name) != UCIS_WRITER_OK) {
            return NULL;
        }
        return (ucisScopeT)uw_el_top(db);
    }
    if (type == UCIS_CROSS) {
        if (uw_open_cross(db, name) != UCIS_WRITER_OK) {
            return NULL;
        }
        return (ucisScopeT)uw_el_top(db);
    }

    uw_fail(db, UCIS_WRITER_ERR_USAGE,
            "scope type not supported by this backend yet", name);
    return NULL;
}

/* ---- coveritems ------------------------------------------------------- */

int ucis_CreateNextCover(ucisT dbh, ucisScopeT parent, const char* name,
                         ucisCoverDataT* data, ucisSourceInfoT* sourceinfo)
{
    uw_db_t* db = uw_db_check(dbh);

    if (db == NULL) {
        return -1;
    }
    if (data == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "ucis_CreateNextCover with NULL data", name) ? -1 : 0;
    }
    /* `parent` is accepted but ignored: in write-streaming the coveritem
     * always attaches to the current scope, and there is no other scope we
     * could reach. Rejecting a non-NULL value would break callers that pass
     * the handle they just received, which is the natural thing to write. */
    (void)parent;

    if (data->type & UCIS_STMTBIN) {
        uw_emit_statement(db, name, data, sourceinfo);
    } else if (data->type & UCIS_BRANCHBIN) {
        uw_emit_branch(db, name, data, sourceinfo);
    } else if (data->type & UCIS_TOGGLEBIN) {
        uw_emit_toggle_bin(db, name, data);
    } else if (data->type & (UCIS_EXPRBIN | UCIS_CONDBIN)) {
        uw_emit_expr_bin(db, name, data);
    } else if (data->type & (UCIS_CVGBIN | UCIS_IGNOREBIN | UCIS_ILLEGALBIN |
                             UCIS_DEFAULTBIN)) {
        uw_emit_cvg_bin(db, name, data);
    } else {
        uw_fail(db, UCIS_WRITER_ERR_USAGE,
                "coveritem type not supported by this backend yet", name);
    }
    return (db->err || db->buf.status) ? -1 : 0;
}

ucisScopeT ucis_CreateToggle(ucisT dbh, ucisScopeT parent, const char* name,
                             const char* canonical_name, ucisFlagsT flags,
                             ucisToggleMetricT toggle_metric,
                             ucisToggleTypeT toggle_type,
                             ucisToggleDirT toggle_dir)
{
    uw_db_t* db = uw_db_check(dbh);

    (void)flags;
    /* The metric constrains which bin names are legal (spec 6.7.1) but has no
     * slot in TOGGLE_OBJECT, so it survives only as the bin names themselves. */
    (void)toggle_metric;

    if (db == NULL) {
        return NULL;
    }
    if (parent != NULL) {
        uw_fail(db, UCIS_WRITER_ERR_USAGE,
                "write-streaming requires a NULL parent", name);
        return NULL;
    }
    if (uw_open_toggle(db, name, canonical_name, toggle_type, toggle_dir)
        != UCIS_WRITER_OK) {
        return NULL;
    }
    return (ucisScopeT)uw_el_top(db);
}

/* ---- misc ------------------------------------------------------------- */

const char* ucis_ComposeDUName(const char* library_name,
                               const char* primary_name,
                               const char* secondary_name)
{
    /* UCIS 1.0: library.primary(secondary). The returned string is valid
     * until the next call, which is the standard's own contract. */
    static char buf[512];
    size_t      n = 0;
    size_t      i;
    const char* parts[3];
    const char* seps[3];

    parts[0] = library_name;   seps[0] = "";
    parts[1] = primary_name;   seps[1] = ".";
    parts[2] = secondary_name; seps[2] = "(";

    for (i = 0; i < 3; ++i) {
        size_t sl, pl;
        if (parts[i] == NULL || parts[i][0] == '\0') {
            continue;
        }
        sl = (n == 0) ? 0 : strlen(seps[i]);
        pl = strlen(parts[i]);
        if (n + sl + pl + 2 >= sizeof(buf)) {
            break;
        }
        memcpy(buf + n, seps[i], sl);
        n += sl;
        memcpy(buf + n, parts[i], pl);
        n += pl;
        if (i == 2) {
            buf[n++] = ')';
        }
    }
    buf[n] = '\0';
    return buf;
}

#endif /* UCIS_WRITER_IMPLEMENTED */
#endif /* UCIS_WRITER_IMPLEMENTATION */
