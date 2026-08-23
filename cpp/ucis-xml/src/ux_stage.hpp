// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// ux_stage.hpp - the per-scope staging arena: one POD record per coverage fact,
// their strings in a bump arena, and the sorted index arrays the renderers walk.
//
// Task I-1.7. This is the mechanism the whole caller-experience change rests on
// (design §5). The caller emits facts in whatever order suits its own data
// structures; when the scope closes, the writer sorts them and renders one
// complete <instanceCoverages> element in the schema's required order. Every
// ordering rule that rev 1 imposed on the caller becomes a sort key here.
//
// The measurement that makes this affordable (design §5.1, reproduced by
// bench/coverage/scope_distribution.py): OpenTitan's largest single scope is
// 37,404 points, so peak staging is about 4 MB against a design whose names
// alone are 230 MB.
//
// Ordering is content-derived, never arrival-derived (decision D-11). A scope's
// output depends only on the set of facts in it, which is what makes T-3 --
// shuffle the emission order, get byte-identical output -- a meaningful test
// rather than a tautology. Sequences that are inherently ordered (an expression's
// subExpr list, a cross bin's index tuple, the state path of an FSM transition)
// keep the order they were given, because there the order is the information.
#pragma once

#include "ux_intern.hpp"
#include "ux_key.hpp"

namespace UCIS_XML_NAMESPACE {

/**
 * Sampling options for a covergroup instance, coverpoint or cross.
 *
 * One struct rather than three: ``CGINST_OPTIONS``, ``COVERPOINT_OPTIONS`` and
 * ``CROSS_OPTIONS`` overlap heavily. Each object emits only the attributes its
 * own type declares, and silently ignores the rest -- so ``autoBinMax`` on a
 * cross, or ``perInstance`` on a coverpoint, does nothing.
 *
 * Attributes equal to their schema default are omitted from the output. Note
 * that a coverpoint, cross and covergroup instance each *require* an
 * ``options`` child, so a fully defaulted one still costs ten bytes --
 * ``<options/>`` -- and is never skipped.
 */
struct Options {
    uint64_t weight_;
    uint64_t goal_;
    uint64_t atLeast_;
    uint64_t autoBinMax_;
    uint64_t crossNumPrintMissing_;
    bool detectOverlap_;
    bool perInstance_;
    bool mergeInstances_;
    Text comment_;

    /** Constructs with every option at its schema default. */
    Options()
        : weight_(1), goal_(100), atLeast_(1), autoBinMax_(64),
          crossNumPrintMissing_(0), detectOverlap_(false), perInstance_(false),
          mergeInstances_(false) {}

    /**
     * Sets this object's weight when coverage is aggregated.
     *
     * :param v: The weight. Defaults to 1, which is omitted from the output.
     * :return: ``*this``, for chaining.
     */
    Options& weight(uint64_t v) { weight_ = v; return *this; }

    /**
     * Sets the coverage percentage considered complete.
     *
     * :param v: The goal. Defaults to 100.
     * :return: ``*this``, for chaining.
     */
    Options& goal(uint64_t v) { goal_ = v; return *this; }

    /**
     * Sets how many hits a bin needs before it counts as covered.
     *
     * :param v: The threshold. Defaults to 1.
     * :return: ``*this``, for chaining.
     */
    Options& atLeast(uint64_t v) { atLeast_ = v; return *this; }

    /**
     * Sets how many automatic bins may be created. Coverpoints and covergroup
     * instances only.
     *
     * :param v: The cap. Defaults to 64.
     * :return: ``*this``, for chaining.
     */
    Options& autoBinMax(uint64_t v) { autoBinMax_ = v; return *this; }

    /**
     * Sets how many uncovered cross combinations a report should list. Crosses
     * and covergroup instances only.
     *
     * :param v: The count. Defaults to 0.
     * :return: ``*this``, for chaining.
     */
    Options& crossNumPrintMissing(uint64_t v) { crossNumPrintMissing_ = v; return *this; }

    /**
     * Asks for a warning when bin ranges overlap. Coverpoints and covergroup
     * instances only.
     *
     * :param v: ``true`` to detect overlaps. Defaults to ``false``.
     * :return: ``*this``, for chaining.
     */
    Options& detectOverlap(bool v) { detectOverlap_ = v; return *this; }

    /**
     * Asks for coverage to be tracked per instance rather than per type.
     * Covergroup instances only.
     *
     * :param v: ``true`` for per-instance. Defaults to ``false``.
     * :return: ``*this``, for chaining.
     */
    Options& perInstance(bool v) { perInstance_ = v; return *this; }

    /**
     * Asks for instances of this covergroup to be merged. Covergroup instances
     * only.
     *
     * :param v: ``true`` to merge. Defaults to ``false``.
     * :return: ``*this``, for chaining.
     */
    Options& mergeInstances(bool v) { mergeInstances_ = v; return *this; }

    /**
     * Sets a free-form comment.
     *
     * :param v: The comment. Defaults to empty, which is omitted.
     * :return: ``*this``, for chaining.
     */
    Options& comment(Text v) { comment_ = v; return *this; }
};

namespace stage {

// Resolved source position: `file` is a source-file id, not a path.
struct SLoc {
    uint32_t file;
    uint32_t line;
    uint32_t inlineCount;

    SLoc() : file(1), line(1), inlineCount(1) {}
};

inline int cmp(const SLoc& a, const SLoc& b) {
    if (a.file != b.file) return a.file < b.file ? -1 : 1;
    if (a.line != b.line) return a.line < b.line ? -1 : 1;
    if (a.inlineCount != b.inlineCount) return a.inlineCount < b.inlineCount ? -1 : 1;
    return 0;
}

// Staged form of Options; `comment` has been copied into the arena.
struct SOpts {
    uint64_t weight, goal, atLeast, autoBinMax, crossNumPrintMissing;
    bool detectOverlap, perInstance, mergeInstances;
    Str comment;

    SOpts()
        : weight(1), goal(100), atLeast(1), autoBinMax(64), crossNumPrintMissing(0),
          detectOverlap(false), perInstance(false), mergeInstances(false) {}
};

struct UserAttr {
    Str key, value;
    AttrType type;
    uint32_t next;  // kNone-terminated, in the order the caller added them
};

// The uncommon attributes of an object or bin: alias, exclusion, goal, weight,
// and user attributes. Kept in a side table so the common record stays small --
// 37 K toggle records paying 48 bytes each for attributes almost none of them
// carry is exactly the cost this design cannot afford.
struct Extra {
    Str alias;
    Str excludedReason;
    uint64_t goal;    // kNoGoal when unset
    uint64_t weight;  // kNoWeight when unset
    bool excluded;
    // Head and tail of this object's user-attribute list. A tail pointer rather
    // than prepend-and-reverse, so attributes render in the order they were
    // added without a second pass.
    uint32_t attrHead;
    uint32_t attrTail;

    Extra()
        : goal(kNoGoal), weight(kNoWeight), excluded(false),
          attrHead(kNone), attrTail(kNone) {}

    static const uint64_t kNoGoal = UINT64_MAX;
    static const uint64_t kNoWeight = UINT64_MAX;
    static const uint32_t kNone = UINT32_MAX;
};

constexpr uint32_t kNone = UINT32_MAX;

// --- code coverage -------------------------------------------------------

struct RToggle {
    Str sig, bit, from, to;
    uint64_t count;
    SLoc loc;
    uint32_t slot;
};

// What is known about a signal beyond its toggle counts: where it is declared,
// and its bit range. Both optional and independent -- a tool may know the
// declaration site without the range, or the range without the site.
struct RSignal {
    Str sig;
    SLoc loc;
    int64_t left, right;
    bool downto;
    bool hasLoc;
    bool hasDim;
};

struct RStmt {
    SLoc loc;
    Str name;  // -> contents/@nameComponent
    uint64_t count;
    uint32_t slot;
};

struct RBlock {
    uint32_t parent;   // kNone for a top-level block
    uint32_t process;  // index into procs, or kNone
    Str name;
    SLoc id;
    uint64_t count;
    uint32_t slot;
};

struct RProcess {
    Str type;
    uint32_t slot;
};

struct RBlockStmt {
    uint32_t block;
    SLoc id;
};

struct RExpr {
    uint32_t parent;  // kNone for a top-level expression
    SLoc loc;
    Str name, exprString, stmtType;
    uint32_t subCount;
    uint32_t slot;
};

// EXPR's subExpr list. A separate record rather than a run in strPool because
// two expressions can be built at the same time, which would interleave their
// sub-expressions and leave neither contiguous.
struct RSubExpr {
    uint32_t expr;
    uint32_t seq;  // evaluation order; the order is the meaning
    Str text;
};

struct RExprBin {
    uint32_t expr;
    Str name;
    uint64_t count;
    uint32_t slot;
};

struct RBrStmt {
    SLoc loc;
    Str expr, stmtType;
    uint32_t parentArm;  // kNone unless nested inside another statement's arm
    uint32_t slot;
};

struct RBrArm {
    uint32_t stmt;
    SLoc loc;
    Str name;
    uint64_t count;
    uint32_t slot;
};

// --- functional coverage -------------------------------------------------

struct RFsm {
    Str name, type;
    uint32_t width;
    uint32_t slot;
};

struct RFsmState {
    uint32_t fsm;
    Str name, value;
    uint64_t count;
    uint32_t slot;
};

struct RFsmTrans {
    uint32_t fsm;
    uint32_t first, n;  // state names in strPool; the order is the transition
    uint64_t count;
    uint32_t slot;
};

// ASSERTION's eight optional bins, in the schema's declared order. Holding them
// as an array indexed by that order is what lets the caller call attempts() and
// passes() in any order and still get one legal sequence out.
enum AssertBin : uint8_t {
    kCover = 0, kPass, kFail, kVacuous, kDisabled, kAttempt, kActive, kPeakActive,
    kAssertBinCount
};

struct RAssert {
    Str name, kind;
    uint64_t bins[kAssertBinCount];
    uint32_t binSlot[kAssertBinCount];
    uint8_t present;  // bitmask over AssertBin
    uint32_t slot;
};

struct RCg {
    Str name, typeName, moduleName;
    SLoc instLoc, typeLoc;
    bool hasInstLoc, hasTypeLoc;
    SOpts opts;
    uint32_t slot;
};

struct RCgParm {
    uint32_t cg;
    uint32_t seq;  // declaration order; cgParms is a caller-ordered list
    Str name, value;
};

struct RCp {
    uint32_t cg;
    Str name, exprString;
    SOpts opts;
    uint32_t slot;
};

struct RCpBin {
    uint32_t cp;
    Str name;
    BinType type;
    uint64_t count;
    int64_t from, to;
    uint32_t seqFirst, seqCount;  // into intPool; a sequence bin when non-zero
    uint32_t slot;
};

struct RCross {
    uint32_t cg;
    Str name;
    SOpts opts;
    uint32_t exprFirst, exprCount;  // into strPool
    uint32_t slot;
};

struct RCrossBin {
    uint32_t cross;
    Str name, type;
    uint64_t count;
    uint32_t idxFirst, idxCount;  // into intPool; the tuple order is the meaning
    uint32_t slot;
};

struct RParam {
    Str name, value;
    uint32_t seq;
};

// ---------------------------------------------------------------------------
// Stage: everything staged for one scope.
//
// Vectors rather than one heterogeneous record array. The design document
// describes a single POD record sorted by (kind, group, ordinal); splitting by
// kind is the same thing with the kind key folded into the choice of vector,
// and it lets each renderer read a typed record instead of unpacking a union.
// ---------------------------------------------------------------------------
struct Stage {
    StrArena arena;

    // The document's file table and error latch. Held here rather than only on
    // Scope so that *every* handle can turn a path into a source-file id: a
    // covergroup's declaration site is as much a source location as a
    // statement's, and should not need a different route to get one.
    FileTable* files;
    Status* status;

    Str name, moduleName, alias;
    SLoc id;
    uint32_t instanceId;
    uint32_t parentInstanceId;  // kNone at the top level
    uint32_t slot;              // the scope's own alias/exclusion/userAttrs

    // metricAttributes live on the per-kind container (toggleCoverage,
    // blockCoverage, ...), not on the individual items, so they are stored per
    // kind rather than per record.
    Str metricMode[static_cast<uint32_t>(CovKind::Count)];
    uint64_t kindWeight[static_cast<uint32_t>(CovKind::Count)];

    Vec<RParam> params;
    Vec<RToggle> toggles;
    Vec<RSignal> signals;
    Vec<RStmt> stmts;
    Vec<RProcess> procs;
    Vec<RBlock> blocks;
    Vec<RBlockStmt> blockStmts;
    Vec<RExpr> exprs;
    Vec<RSubExpr> subExprs;
    Vec<RExprBin> exprBins;
    Vec<RBrStmt> brStmts;
    Vec<RBrArm> brArms;
    Vec<RFsm> fsms;
    Vec<RFsmState> fsmStates;
    Vec<RFsmTrans> fsmTrans;
    Vec<RAssert> asserts;
    Vec<RCg> cgs;
    Vec<RCgParm> cgParms;
    Vec<RCp> cps;
    Vec<RCpBin> cpBins;
    Vec<RCross> crosses;
    Vec<RCrossBin> crossBins;

    Vec<Str> strPool;
    Vec<int64_t> intPool;

    // Attribute side tables. extras[0] is a canonical empty entry so that a
    // slot value of 0 means "nothing set" without a branch at every use.
    Vec<uint32_t> slots;
    Vec<Extra> extras;
    Vec<UserAttr> userAttrs;

    // Shared merge-sort scratch; see sortInto().
    Vec<uint32_t> scratch;

    // Resolves a path against the file table, reporting against this scope's
    // name if it turns out to be a late arrival under Sources::UpFront.
    SLoc resolve(Text file, uint32_t line, uint32_t inlineCount) {
        SLoc l;
        l.file = (files && status) ? files->resolve(file, text(name), *status) : 1;
        l.line = line;
        l.inlineCount = inlineCount;
        return l;
    }

    Stage()
        : files(nullptr), status(nullptr), instanceId(0),
          parentInstanceId(kNone), slot(0) {
        extras.push(Extra());
        slot = newSlot();
        for (uint32_t i = 0; i < static_cast<uint32_t>(CovKind::Count); ++i)
            kindWeight[i] = Extra::kNoWeight;
    }

    Str str(Text t) { return arena.add(t); }
    Text text(Str s) const { return arena.get(s); }

    // Every bin-bearing record gets a slot. The indirection through `slots`
    // rather than a direct index into `extras` is what makes BinRef safe: the
    // record vectors are sorted (and reallocated) later, but a slot index never
    // moves, so a BinRef handed back to the caller stays valid for the life of
    // the scope.
    uint32_t newSlot() { return slots.push(0); }

    Extra& extraFor(uint32_t slot) {
        if (slot >= slots.len) return extras[0];
        if (slots[slot] == 0) {
            uint32_t i = extras.push(Extra());
            if (i == UINT32_MAX) return extras[0];
            slots[slot] = i;
        }
        return extras[slots[slot]];
    }

    const Extra& extraAt(uint32_t slot) const {
        if (slot >= slots.len || slots[slot] == 0) return extras[0];
        return extras[slots[slot]];
    }

    void addUserAttr(uint32_t slot, Text key, Text value, AttrType type) {
        UserAttr a;
        a.key = str(key);
        a.value = str(value);
        a.type = type;
        a.next = kNone;
        uint32_t i = userAttrs.push(a);
        if (i == UINT32_MAX) return;
        Extra& e = extraFor(slot);
        if (e.attrTail == kNone) e.attrHead = i;
        else userAttrs[e.attrTail].next = i;
        e.attrTail = i;
    }

    bool oom() const {
        return arena.oom() || toggles.oom || stmts.oom || blocks.oom || exprs.oom ||
               exprBins.oom || subExprs.oom || brStmts.oom || brArms.oom || fsms.oom || fsmStates.oom ||
               fsmTrans.oom || asserts.oom || cgs.oom || cps.oom || cpBins.oom ||
               crosses.oom || crossBins.oom || slots.oom || extras.oom ||
               userAttrs.oom || strPool.oom || intPool.oom || params.oom;
    }

    // Approximate staged footprint, checked against WriterOptions::stagingLimit.
    size_t bytes() const {
        return arena.bytes() + toggles.bytes() + stmts.bytes() + blocks.bytes() +
               blockStmts.bytes() + exprs.bytes() + exprBins.bytes() + subExprs.bytes() +
               brStmts.bytes() + brArms.bytes() + fsms.bytes() + fsmStates.bytes() +
               fsmTrans.bytes() + asserts.bytes() + cgs.bytes() + cgParms.bytes() +
               cps.bytes() + cpBins.bytes() + crosses.bytes() + crossBins.bytes() +
               strPool.bytes() + intPool.bytes() + slots.bytes() + extras.bytes() +
               userAttrs.bytes() + params.bytes() + signals.bytes() + procs.bytes();
    }

    // Fills `out` with 0..n-1 sorted by `less`. The destination is a parameter
    // rather than a member because the renderers nest: a covergroup's
    // coverpoints are sorted while the walk over sorted covergroups is still in
    // progress. The scratch buffer is safely shared, since it is only live
    // inside a single mergeSortIndex call.
    template <typename Less>
    bool sortInto(Vec<uint32_t>& out, uint32_t n, Less less) {
        out.clear();
        if (n == 0) return true;
        if (!out.reserve(n) || !scratch.reserve(n)) return false;
        out.len = n;
        scratch.len = n;
        for (uint32_t i = 0; i < n; ++i) out[i] = i;
        mergeSortIndex(out.ptr, scratch.ptr, n, less);
        return true;
    }
};

// Copies caller Options into their staged form; shared by covergroup,
// coverpoint and cross, which is why Options is one struct rather than three.
inline SOpts toStageOpts(Stage& s, const Options& o) {
    SOpts r;
    r.weight = o.weight_;
    r.goal = o.goal_;
    r.atLeast = o.atLeast_;
    r.autoBinMax = o.autoBinMax_;
    r.crossNumPrintMissing = o.crossNumPrintMissing_;
    r.detectOverlap = o.detectOverlap_;
    r.perInstance = o.perInstance_;
    r.mergeInstances = o.mergeInstances_;
    r.comment = s.str(o.comment_);
    return r;
}

}  // namespace stage
}  // namespace UCIS_XML_NAMESPACE
