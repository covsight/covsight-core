/* uw_types.h - internal object layout.
 * SPDX-License-Identifier: Apache-2.0
 *
 * None of this is visible to a consumer: the public API traffics in void*
 * handles. It lives in a header only so the modules can share it. */

#ifndef UW_TYPES_H
#define UW_TYPES_H

#include "uw_buf.h"

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

#endif /* UW_TYPES_H */
