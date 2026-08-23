/*
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

#include "uw_public.h"

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
