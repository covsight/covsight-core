/*
 * ucis.h - Unified Coverage Interoperability Standard (UCIS) 1.0 C API
 *
 * Public header for the NCDB UCIS writer surface (Phase 2 of the UCIS impl
 * plan; see docs/ucis-impl-plan.md).
 *
 * The token names, function signatures, enum/struct layouts, and one-hot
 * scope/cover type bits in this header are mandated by the Accellera
 * UCIS 1.0 specification (June 2012). The full text of the spec ships
 * alongside this repo in UCIS_Version_1.0_Final_June-2012.md. This header
 * exposes the *writer subset* used by NCDB and by the Verilator-direct
 * single-header carve-out; non-writer entry points (streaming read,
 * iteration, attribute query, formal-status, merge) are intentionally
 * omitted and will be added in Phase 6.
 */

#ifndef UCIS_API_H
#define UCIS_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _MSC_VER
#  define UCIS_INT64_LITERAL(v) ((int64_t)v)
#else
#  define UCIS_INT64_LITERAL(v) (v##LL)
#endif

#ifndef INT64_LITERAL
#  define INT64_LITERAL(v) UCIS_INT64_LITERAL(v)
#endif
#ifndef INT64_ZERO
#  define INT64_ZERO  UCIS_INT64_LITERAL(0)
#endif
#ifndef INT64_NEG1
#  define INT64_NEG1  UCIS_INT64_LITERAL(-1)
#endif

/* ------------------------------------------------------------------------
 *  Core opaque types
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
typedef void* ucisVersionHandleT;

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
 *  Database lifecycle (writer surface)
 * ---------------------------------------------------------------------- */

/* Create an in-memory database, optionally populated from a file. */
ucisT ucis_Open(const char* name);

/* Open a database for write-streaming. */
ucisT ucis_OpenWriteStream(const char* name);

/* Flush the current in-flight object during write-streaming. */
int ucis_WriteStream(ucisT db);

/* Flush the current scope during write-streaming and pop to its parent. */
int ucis_WriteStreamScope(ucisT db);

/* Persist (a subset of) the in-memory database to file.
 *   scope==NULL writes everything; covertype==-1 selects all types. */
int ucis_Write(ucisT       db,
               const char* file,
               ucisScopeT  scope,
               int         recurse,
               int         covertype);

/* Invalidate the handle; release memory. */
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

/* Specialized constructor for files attached to a DU's per-DU file table. */
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
int ucis_GetTestData(ucisT db, ucisHistoryNodeT node, ucisTestDataT* data);

/* ------------------------------------------------------------------------
 *  Scope types  (one-hot bits for ucisScopeTypeT)
 *
 *  Definitions reproduced verbatim from the UCIS 1.0 spec - the bit values
 *  are normative and must match for any interop with conforming readers.
 * ---------------------------------------------------------------------- */

#define UCIS_TOGGLE          INT64_LITERAL(0x0000000000000001)
#define UCIS_BRANCH          INT64_LITERAL(0x0000000000000002)
#define UCIS_EXPR            INT64_LITERAL(0x0000000000000004)
#define UCIS_COND            INT64_LITERAL(0x0000000000000008)
#define UCIS_INSTANCE        INT64_LITERAL(0x0000000000000010)
#define UCIS_PROCESS         INT64_LITERAL(0x0000000000000020)
#define UCIS_BLOCK           INT64_LITERAL(0x0000000000000040)
#define UCIS_FUNCTION        INT64_LITERAL(0x0000000000000080)
#define UCIS_FORKJOIN        INT64_LITERAL(0x0000000000000100)
#define UCIS_GENERATE        INT64_LITERAL(0x0000000000000200)
#define UCIS_GENERIC         INT64_LITERAL(0x0000000000000400)
#define UCIS_CLASS           INT64_LITERAL(0x0000000000000800)
#define UCIS_COVERGROUP      INT64_LITERAL(0x0000000000001000)
#define UCIS_COVERINSTANCE   INT64_LITERAL(0x0000000000002000)
#define UCIS_COVERPOINT      INT64_LITERAL(0x0000000000004000)
#define UCIS_CROSS           INT64_LITERAL(0x0000000000008000)
#define UCIS_COVER           INT64_LITERAL(0x0000000000010000)
#define UCIS_ASSERT          INT64_LITERAL(0x0000000000020000)
#define UCIS_PROGRAM         INT64_LITERAL(0x0000000000040000)
#define UCIS_PACKAGE         INT64_LITERAL(0x0000000000080000)
#define UCIS_TASK            INT64_LITERAL(0x0000000000100000)
#define UCIS_INTERFACE       INT64_LITERAL(0x0000000000200000)
#define UCIS_FSM             INT64_LITERAL(0x0000000000400000)
#define UCIS_TESTPLAN        INT64_LITERAL(0x0000000000800000)
#define UCIS_DU_MODULE       INT64_LITERAL(0x0000000001000000)
#define UCIS_DU_ARCH         INT64_LITERAL(0x0000000002000000)
#define UCIS_DU_PACKAGE      INT64_LITERAL(0x0000000004000000)
#define UCIS_DU_PROGRAM      INT64_LITERAL(0x0000000008000000)
#define UCIS_DU_INTERFACE    INT64_LITERAL(0x0000000010000000)
#define UCIS_FSM_STATES      INT64_LITERAL(0x0000000020000000)
#define UCIS_FSM_TRANS       INT64_LITERAL(0x0000000040000000)
#define UCIS_COVBLOCK        INT64_LITERAL(0x0000000080000000)
#define UCIS_CVGBINSCOPE     INT64_LITERAL(0x0000000100000000)
#define UCIS_ILLEGALBINSCOPE INT64_LITERAL(0x0000000200000000)
#define UCIS_IGNOREBINSCOPE  INT64_LITERAL(0x0000000400000000)
#define UCIS_BBLOCKSCOPE     INT64_LITERAL(0x0000000800000000)
#define UCIS_GROUP           INT64_LITERAL(0x0000001000000000)
#define UCIS_TRANSITION      INT64_LITERAL(0x0000002000000000)
#define UCIS_RESERVEDSCOPE   INT64_LITERAL(0xFF00000000000000)
#define UCIS_SCOPE_ERROR     INT64_LITERAL(0x0000000000000000)

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

#define UCIS_CVGBIN          INT64_LITERAL(0x0000000000000001)
#define UCIS_COVERBIN        INT64_LITERAL(0x0000000000000002)
#define UCIS_ASSERTBIN       INT64_LITERAL(0x0000000000000004)
#define UCIS_SCBIN           INT64_LITERAL(0x0000000000000008)
#define UCIS_ZINBIN          INT64_LITERAL(0x0000000000000010)
#define UCIS_STMTBIN         INT64_LITERAL(0x0000000000000020)
#define UCIS_BRANCHBIN       INT64_LITERAL(0x0000000000000040)
#define UCIS_EXPRBIN         INT64_LITERAL(0x0000000000000080)
#define UCIS_CONDBIN         INT64_LITERAL(0x0000000000000100)
#define UCIS_TOGGLEBIN       INT64_LITERAL(0x0000000000000200)
#define UCIS_PASSBIN         INT64_LITERAL(0x0000000000000400)
#define UCIS_FSMBIN          INT64_LITERAL(0x0000000000000800)
#define UCIS_USERBIN         INT64_LITERAL(0x0000000000001000)
#define UCIS_GENERICBIN      UCIS_USERBIN
#define UCIS_COUNT           INT64_LITERAL(0x0000000000002000)
#define UCIS_FAILBIN         INT64_LITERAL(0x0000000000004000)
#define UCIS_VACUOUSBIN      INT64_LITERAL(0x0000000000008000)
#define UCIS_DISABLEDBIN     INT64_LITERAL(0x0000000000010000)
#define UCIS_ATTEMPTBIN      INT64_LITERAL(0x0000000000020000)
#define UCIS_ACTIVEBIN       INT64_LITERAL(0x0000000000040000)
#define UCIS_IGNOREBIN       INT64_LITERAL(0x0000000000080000)
#define UCIS_ILLEGALBIN      INT64_LITERAL(0x0000000000100000)
#define UCIS_DEFAULTBIN      INT64_LITERAL(0x0000000000200000)
#define UCIS_PEAKACTIVEBIN   INT64_LITERAL(0x0000000000400000)
#define UCIS_BLOCKBIN        INT64_LITERAL(0x0000000001000000)
#define UCIS_USERBITS        INT64_LITERAL(0x00000000FE000000)
#define UCIS_RESERVEDBIN     INT64_LITERAL(0xFF00000000000000)

#define UCIS_STATEBIN UCIS_FSMBIN   /* alias used in spec prose for FSM-state bins */
#define UCIS_TRANSBIN UCIS_FSMBIN   /* alias used in spec prose for FSM-arc  bins */

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
 *  Scope creation (writer)
 * ---------------------------------------------------------------------- */

ucisScopeT ucis_CreateScope(ucisT             db,
                            ucisScopeT        parent,
                            const char*       name,
                            ucisSourceInfoT*  srcinfo,
                            int               weight,
                            ucisSourceT       source,
                            ucisScopeTypeT    type,
                            ucisFlagsT        flags);

/* Specialized: instance scope must link to a DU; coverinstance must link
 * to a covergroup type scope. */
ucisScopeT ucis_CreateInstance(ucisT             db,
                               ucisScopeT        parent,
                               const char*       name,
                               ucisSourceInfoT*  srcinfo,
                               int               weight,
                               ucisSourceT       source,
                               ucisScopeTypeT    type,
                               ucisScopeT        du_scope,
                               ucisFlagsT        flags);

/* Write-streaming variant: link by DU/covergroup name string. */
ucisScopeT ucis_CreateInstanceByName(ucisT             db,
                                     ucisScopeT        parent,
                                     const char*       name,
                                     ucisSourceInfoT*  srcinfo,
                                     int               weight,
                                     ucisSourceT       source,
                                     ucisScopeTypeT    type,
                                     char*             du_name,
                                     int               flags);

/* Toggle scope: enforces the bin shape implied by toggle_metric. */
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

/* Covergroup-family specialized scope constructors. */
ucisScopeT ucis_CreateCross(ucisT             db,
                            ucisScopeT        parent,
                            const char*       name,
                            ucisSourceInfoT*  srcinfo,
                            int               weight,
                            ucisSourceT       source,
                            int               num_points,
                            ucisScopeT*       points);

ucisScopeT ucis_CreateCrossByName(ucisT             db,
                                  ucisScopeT        parent,
                                  const char*       name,
                                  ucisSourceInfoT*  srcinfo,
                                  int               weight,
                                  ucisSourceT       source,
                                  int               num_points,
                                  char**            point_names);

/* DU-name composer/parser (library.primary(secondary)#instance). */
const char* ucis_ComposeDUName(const char* library_name,
                               const char* primary_name,
                               const char* secondary_name);
void        ucis_ParseDUName(const char*  du_name,
                             const char** library_name,
                             const char** primary_name,
                             const char** secondary_name);

ucisScopeT     ucis_MatchDU(ucisT db, const char* name);
ucisScopeTypeT ucis_GetScopeType(ucisT db, ucisScopeT scope);
ucisObjTypeT   ucis_GetObjType(ucisT db, ucisObjT obj);

ucisFlagsT ucis_GetScopeFlags(ucisT db, ucisScopeT scope);
void       ucis_SetScopeFlags(ucisT db, ucisScopeT scope, ucisFlagsT flags);
void       ucis_SetScopeFlag(ucisT db, ucisScopeT scope, ucisFlagsT mask, int bitvalue);

int ucis_SetScopeSourceInfo(ucisT db, ucisScopeT scope, ucisSourceInfoT* sourceinfo);
int ucis_GetScopeSourceInfo(ucisT db, ucisScopeT scope, ucisSourceInfoT* sourceinfo);

/* Coveritem source-info getter. Fills *sourceinfo with a filehandle whose
 * filename matches what was stored at CreateNextCover time. The returned
 * filehandle lives until ucis_Close and may be reused across calls. */
int ucis_GetCoverSourceInfo(ucisT db, ucisScopeT parent, int coverindex,
                            ucisSourceInfoT* sourceinfo);

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

int ucis_IncrementCover(ucisT       db,
                        ucisScopeT  parent,
                        int         coverindex,
                        int64_t     increment);

void ucis_SetCoverFlag(ucisT db, ucisScopeT parent, int coverindex,
                       ucisFlagsT mask, int bitvalue);

/* ------------------------------------------------------------------------
 *  Properties (typed get/set keyed on enum constants)
 *
 *  Object identification follows the UCIS convention:
 *    - obj==NULL      : the database itself
 *    - obj=ucisScopeT, coverindex==-1 : the scope
 *    - obj=ucisScopeT, coverindex>=0  : the coveritem at that index
 *    - obj=ucisHistoryNodeT, coverindex ignored
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
    UCIS_STR_FSM_STATEVAR     /* FSM state-variable name */
} ucisStringPropertyEnumT;

typedef enum {
    UCIS_REAL_HIST_CPUTIME,
    UCIS_REAL_TEST_SIMTIME,
    UCIS_REAL_TEST_COST,
    UCIS_REAL_CVG_INST_AVERAGE
} ucisRealPropertyEnumT;

typedef enum {
    UCIS_HANDLE_SCOPE_PARENT,
    UCIS_HANDLE_SCOPE_TOP,
    UCIS_HANDLE_INSTANCE_DU,
    UCIS_HANDLE_HIST_NODE_PARENT,
    UCIS_HANDLE_HIST_NODE_ROOT
} ucisHandleEnumT;

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

int ucis_SetHandleProperty(ucisT            db,
                           ucisObjT         obj,
                           ucisHandleEnumT  property,
                           ucisObjT         value);

/* Mirrored getters (writer surface only includes the ones Verilator needs
 * for round-trip validation; the full read surface lands in Phase 6). */
int          ucis_GetIntProperty(ucisT db, ucisObjT obj, int coverindex,
                                 ucisIntPropertyEnumT property);
const char*  ucis_GetStringProperty(ucisT db, ucisObjT obj, int coverindex,
                                    ucisStringPropertyEnumT property);
double       ucis_GetRealProperty(ucisT db, ucisObjT obj, int coverindex,
                                  ucisRealPropertyEnumT property);

/* ------------------------------------------------------------------------
 *  Typed attributes (extended user-defined key/value pairs)
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

int ucis_AttrRemove(ucisT db, ucisObjT obj, int coverindex,
                    const char* key);

/* ------------------------------------------------------------------------
 *  Version query
 * ---------------------------------------------------------------------- */

ucisVersionHandleT ucis_GetAPIVersion(void);
ucisVersionHandleT ucis_GetDBVersion(ucisT db);
const char*        ucis_GetVersionStringProperty(ucisVersionHandleT versionH,
                                                 ucisStringPropertyEnumT property);

#ifdef __cplusplus
}
#endif

#endif /* UCIS_API_H */
