"""The feature-complete conformance corpus (T17).

`tests/parquet/conftest.py:build_db` is deliberately small — it exists to make a
merge observable. This one is deliberately *wide*: it exercises every coverage
construct the UCIS object API can express, because the round-trip test is only
as strong as the database it round-trips.

Verilator emits statement, branch and toggle coverage and nothing else, so a
real corpus cannot reach covergroups, crosses, FSM, assertions or formal
results. Those have to be built by hand; that is why this file exists rather
than a corpus harvested from RTLMeter.

Everything here is constructed through the public object API, so the same
builder feeds any backend. Where the API cannot express a UCIS feature the
builder says so in a comment rather than faking it — see `KNOWN_UNREACHABLE`.

The database is built deterministically: same inputs, same tree, same order.
Nothing is randomized, so a diff between two snapshots is a real difference.
"""

from __future__ import annotations

from covsight.core.api import (
    CoverData, CoverTypeT, FlagsT, HistoryNodeKind, IntProperty, ScopeTypeT,
    SourceInfo, SourceT, StrProperty, TestStatusT,
)
from covsight.core.api.enums.cover_flags import (
    BranchCoverFlagsT, CoverFlagsT, CrossCoverFlagsT, CvgBinCoverFlagsT,
    FsmCoverFlagsT,
)
from covsight.core.api.test_data import TestData
from covsight.core.mem import MemFactory

#: UCIS features this corpus cannot reach, and why. Each has a registry entry
#: that reports as unmapped; none is silently skipped. Faking any of these
#: (writing a plausible value through a different path) would make the corpus
#: claim coverage it does not have, which is worse than the gap.
#:
#: Three distinct causes, and the difference matters.
#:
#: An earlier version of this list was largely wrong, and the way it was wrong
#: is worth keeping: it was built by *iterating* `ScopeTypeT`, `CoverTypeT` and
#: `FlagsT` and recording what did not appear. `IntFlag` iteration silently
#: skips both multi-bit composites and aliases, so `CoverTypeT.USERBITS`
#: (0xFE000000), `ScopeTypeT.RESERVEDSCOPE` and `FlagsT.IS_IMMEDIATE_ASSERT`
#: (an alias of `IS_TOP_NODE` at 0x10000 -- UCIS reuses type-qualified flag
#: positions across non-intersecting type sets) were all reported missing when
#: they were present. Probe with `getattr`, never with iteration.
KNOWN_UNREACHABLE = {
    # 1. Unimplemented in the object API.
    "S4.6": "ucis_CreateNextTransition raises UnimplError in the object API",
    "FS.5": "same constructor as S4.6",
    "FS.6": "no getFSMTransitionStates; arc endpoints are not represented",
    "FS.4": "per-bin typed properties are unreachable (bins are not Objs)",
    "SB.3": "per-bin typed properties are unreachable (bins are not Objs)",
    # 2. Not defined by UCIS 1.0 at all -- catalog errors, corrected there.
    "ST.38": "UCIS_TRANSITION is not a ucisScopeTypeT value in UCIS 1.0",
    "ST.37": "UCIS_GROUP is not a ucisScopeTypeT value in UCIS 1.0",
    "ST.24": "UCIS_TESTPLAN is not a ucisScopeTypeT value in UCIS 1.0",
    "ST.36": "UCIS_BBLOCKSCOPE does not exist; the spec's basic-block scope is "
             "UCIS_BBLOCK, which this enum calls COVBLOCK (ST.32)",
    "BT.5": "UCIS_ZINBIN is not a ucisCoverTypeT value in UCIS 1.0",
    # 3. Genuinely absent from our enums.
    "BT.4": "CoverTypeT has no SCBIN. The value is unknown from the spec copy "
            "in this repo: the markdown conversion lost its #define line, and "
            "guessing a normative bit value is not acceptable",
    "AS.5": "sequence bins need CoverTypeT.SCBIN (BT.4)",
    # 4. Settable in principle, refused by the object API. The *mapping* carries
    # these like any other scope-level property, so they stay documented; only
    # the corpus cannot populate them.
    "BR.2": "setIntProperty(BRANCH_HAS_ELSE / BRANCH_ISCASE) raises UnimplError",
    "CG.10": "RealProperty has no settable values in the object API",
}


def _cover(cover_type, count, *, at_least=1, weight=1, goal=None, limit=None):
    data = CoverData(cover_type, 0)
    data.data = count
    data.at_least = at_least
    data.weight = weight
    if goal is not None:
        data.goal = goal
    if limit is not None:
        data.limit = limit
    return data


def build_feature_complete(counts=(1, 0, 7), tag="fc", *, with_formal=True):
    """A database exercising every reachable UCIS coverage construct.

    Args:
        counts: Hit counts for the covergroup bins. Varying them across runs is
            what makes a merge observable and non-degenerate.
        tag: Suffix distinguishing this run's test name.
        with_formal: Attach formal-verification results.

    Returns:
        A MemUCIS database. Deterministic for a given set of arguments.
    """
    db = MemFactory.create()
    fh = db.createFileHandle("dut.sv", "/rtl")
    pkg_fh = db.createFileHandle("pkg.sv", "/rtl")

    # -- history: a test node and a merge node, so the history tree has depth --
    hist = db.createHistoryNode(None, "test_" + tag, "./run.sh",
                                HistoryNodeKind.TEST)
    hist.setTestData(TestData(
        teststatus=TestStatusT.OK, toolcategory="simulator",
        date="2026-08-16", simtime=12.5, timeunit="ns", seed="4242",
        cmd="./run.sh --seed 4242", user="ci",
    ))
    # Attributes on a history node land in `history_props`, not `properties`:
    # a history node belongs to a run, and appending a run must not rewrite a
    # definition table. Exercising that split is the point of setting one here.
    hist.setAttribute("regression", "nightly")

    db.createHistoryNode(None, "merge_" + tag, "./merge.sh",
                         HistoryNodeKind.MERGE)

    # -- design unit and instance -----------------------------------------
    du = db.createScope("work.dut", SourceInfo(fh, 1, 0), 1, SourceT.SV,
                        ScopeTypeT.DU_MODULE, FlagsT.UOR_SAFE_SCOPE)
    du.setAttribute("origin", "handwritten")
    du.setAttribute("revision", "3")
    du.addTag("rtl")
    du.addTag("synthesizable")

    inst = db.createInstance(
        "top", SourceInfo(fh, 10, 4), 1, SourceT.SV, ScopeTypeT.INSTANCE, du,
        FlagsT.ENABLED_STMT | FlagsT.ENABLED_BRANCH | FlagsT.ENABLED_COND
        | FlagsT.ENABLED_EXPR | FlagsT.ENABLED_FSM | FlagsT.ENABLED_TOGGLE,
    )
    inst.addTag("dut")

    _build_covergroup(db, inst, fh, counts)
    _build_fsm(inst, fh)
    _build_toggle(inst)
    _build_assertions(inst, fh)
    _build_code_coverage(inst, fh, pkg_fh)
    _build_user_defined(inst, fh)

    # -- per-test associations, sparse tier --------------------------------
    # Flat bin indices, as a simulator front end records them.
    db.record_test_association(0, 0, max(counts[0], 1))
    db.record_test_association(0, 1, 3)
    db.record_test_association(0, 5, 9)

    if with_formal:
        from covsight.core.api.enums import FormalStatusT
        db.set_formal_data(0, status=FormalStatusT.PROOF, radius=11,
                           witness="proof0.vcd")

    return db


def _build_covergroup(db, inst, fh, counts):
    """Covergroup / cover-instance / coverpoint / cross / bin scopes."""
    cg = inst.createCovergroup("addr_cg", SourceInfo(fh, 20, 0), 1, SourceT.SV)
    cg.setIntProperty(-1, IntProperty.CVG_ATLEAST, 2)
    cg.setIntProperty(-1, IntProperty.CVG_AUTOBINMAX, 64)
    cg.setIntProperty(-1, IntProperty.CVG_PERINSTANCE, 1)
    cg.setIntProperty(-1, IntProperty.CVG_DETECTOVERLAP, 1)
    cg.addTag("functional")

    cp = cg.createCoverpoint("addr", SourceInfo(fh, 21, 0), 1, SourceT.SV)
    for i, count in enumerate(counts):
        idx = cp.createNextCover("bin_%d" % i, _cover(CoverTypeT.CVGBIN, count),
                                 SourceInfo(fh, 22 + i, 0))
        idx.setAttribute("bin_kind", "auto")

    # Classified bins: the point is that ignore/illegal/default are a
    # cover_type, not a separate table.
    guarded = cp.createNextCover("guarded", _cover(CoverTypeT.CVGBIN, 0), None)
    guarded.setCoverFlags(int(CvgBinCoverFlagsT.BIN_IFF_EXISTS))

    waived = cp.createNextCover("waived", _cover(CoverTypeT.CVGBIN, 0), None)
    waived.setCoverFlags(int(CoverFlagsT.EXCLUDE_PRAGMA))

    cp.createNextCover("ignore_hi", _cover(CoverTypeT.IGNOREBIN, 0), None)
    cp.createNextCover("illegal_x", _cover(CoverTypeT.ILLEGALBIN, 0), None)
    cp.createNextCover("default", _cover(CoverTypeT.DEFAULTBIN, 4), None)

    # A second coverpoint, so the cross has two real operands.
    cp2 = cg.createCoverpoint("data", SourceInfo(fh, 26, 0), 1, SourceT.SV)
    cp2.createNextCover("lo", _cover(CoverTypeT.CVGBIN, 2), None)
    cp2.createNextCover("hi", _cover(CoverTypeT.CVGBIN, 6), None)

    # Bin-grouping scopes hanging off a coverpoint.
    binscope = cp2.createScope("auto_bins", None, 1, SourceT.SV,
                               ScopeTypeT.CVGBINSCOPE, 0)
    binscope.createNextCover("grouped", _cover(CoverTypeT.CVGBIN, 1), None)

    # Cross, with its real operand list: two coverpoints, in cross order.
    # createCross is what carries them -- createScope(type=CROSS) builds the
    # same scope with an empty operand list, which is what this corpus used
    # before the mapping could hold one (T23).
    cross = cg.createCross("addr_x_data", SourceInfo(fh, 27, 0), 1, SourceT.SV,
                           [cp, cp2])
    auto = cross.createNextCover("<bin_0,lo>", _cover(CoverTypeT.CVGBIN, 3),
                                 None)
    auto.setCoverFlags(int(CrossCoverFlagsT.IS_CROSSAUTO))
    cross.createNextCover("<bin_1,hi>", _cover(CoverTypeT.CVGBIN, 0), None)

    # Transition bins. UCIS puts them under a UCIS_TRANSITION scope, but
    # ScopeTypeT has no TRANSITION value (ST.38), so the flag on the coverpoint
    # is the only part of that shape this corpus can build.
    cp3 = cg.createCoverpoint("mode", SourceInfo(fh, 28, 0), 1, SourceT.SV)
    cp3.createNextCover("a=>b", _cover(CoverTypeT.CVGBIN, 5), None)


def _build_fsm(inst, fh):
    """FSM: states and transitions as sibling child scopes."""
    fsm = inst.createScope("ctrl_fsm", SourceInfo(fh, 40, 0), 1, SourceT.SV,
                           ScopeTypeT.FSM, FlagsT.ENABLED_FSM)
    fsm.setStringProperty(-1, StrProperty.GENERIC, "state_q")

    states = fsm.createScope("states", None, 1, SourceT.SV,
                             ScopeTypeT.FSM_STATES, 0)
    for name, count in (("IDLE", 12), ("BUSY", 5), ("DONE", 0)):
        # The state's encoded value would be UCIS_INT_FSM_STATEVAL, which is a
        # per-bin typed property and therefore unreachable; carried as an
        # extension attribute instead so the corpus still exercises the path.
        ci = states.createNextCover(name, _cover(CoverTypeT.FSMBIN, count), None)
        ci.setAttribute("stateval", name)
        if name == "IDLE":
            ci.setCoverFlags(int(FsmCoverFlagsT.IS_FSM_RESET))

    trans = fsm.createScope("trans", None, 1, SourceT.SV,
                            ScopeTypeT.FSM_TRANS, 0)
    # Arcs as ordinary bins: createNextTransition raises UnimplError, so the
    # endpoints live only in the bin name.
    for arc, count in (("IDLE->BUSY", 7), ("BUSY->DONE", 0)):
        ci = trans.createNextCover(arc, _cover(CoverTypeT.FSMBIN, count), None)
        # Same bit as IS_FSM_RESET above means something else here only because
        # the parent scope differs -- see typed_flags_for().
        ci.setCoverFlags(int(FsmCoverFlagsT.IS_FSM_TRAN))


def _build_toggle(inst):
    from covsight.core.api.enums.toggle import (
        ToggleDirT, ToggleMetricT, ToggleTypeT,
    )

    toggle = inst.createToggle("data_valid", "top.data_valid",
                               FlagsT.ENABLED_TOGGLE, ToggleMetricT._2STOGGLE,
                               ToggleTypeT.NET, ToggleDirT.INOUT)
    # Names carry the transition; a reader must join on the name, not the index.
    for name, count in (("0->1", 3), ("1->0", 0)):
        toggle.createNextCover(name, _cover(CoverTypeT.TOGGLEBIN, count), None)


def _build_assertions(inst, fh):
    """Assertion counter bins and a cover property."""
    asrt = inst.createScope("a_req_ack", SourceInfo(fh, 50, 0), 1, SourceT.SV,
                            ScopeTypeT.ASSERT, FlagsT.IS_IMMEDIATE_ASSERT)
    for name, cover_type, count in (
        ("pass", CoverTypeT.PASSBIN, 40),
        ("fail", CoverTypeT.FAILBIN, 2),
        ("vacuous", CoverTypeT.VACUOUSBIN, 5),
        ("disabled", CoverTypeT.DISABLEDBIN, 0),
        ("attempt", CoverTypeT.ATTEMPTBIN, 47),
        ("active", CoverTypeT.ACTIVEBIN, 1),
        # MAX on merge, not SUM: a high-water mark summed over runs is false.
        ("peak", CoverTypeT.PEAKACTIVEBIN, 6),
    ):
        asrt.createNextCover(name, _cover(cover_type, count), None)

    cover = inst.createScope("c_handshake", SourceInfo(fh, 55, 0), 1, SourceT.SV,
                             ScopeTypeT.COVER, 0)
    cover.createNextCover("hit", _cover(CoverTypeT.COVERBIN, 9), None)
    # A sequence bin would be UCIS_SCBIN; CoverTypeT has no such value (BT.4).


def _build_code_coverage(inst, fh, pkg_fh):
    """Statement/block, branch, condition and expression coverage."""
    block = inst.createScope("proc_always", SourceInfo(fh, 60, 0), 1, SourceT.SV,
                             ScopeTypeT.BLOCK, FlagsT.ENABLED_STMT)
    block.createNextCover("stmt_61", _cover(CoverTypeT.STMTBIN, 21),
                          SourceInfo(fh, 61, 0))
    block.createNextCover("stmt_62", _cover(CoverTypeT.STMTBIN, 0),
                          SourceInfo(fh, 62, 0))
    block.createNextCover("block", _cover(CoverTypeT.BLOCKBIN, 21), None)

    branch = inst.createScope("if_63", SourceInfo(fh, 63, 0), 1, SourceT.SV,
                              ScopeTypeT.BRANCH,
                              FlagsT.SCOPE_BLOCK_ISBRANCH)
    # UCIS_INT_BRANCH_HAS_ELSE / _ISCASE are scope-level int properties, so the
    # mapping carries them like any other; the object API's setIntProperty
    # raises UnimplError for them, so this corpus cannot populate them (BR.2).
    branch.createNextCover("then", _cover(CoverTypeT.BRANCHBIN, 18), None)
    else_arm = branch.createNextCover("else", _cover(CoverTypeT.BRANCHBIN, 3),
                                      None)
    else_arm.setCoverFlags(int(BranchCoverFlagsT.IS_BR_ELSE))

    cond = inst.createScope("cond_70", SourceInfo(fh, 70, 0), 1, SourceT.SV,
                            ScopeTypeT.COND, FlagsT.ENABLED_COND)
    cond.setStringProperty(-1, StrProperty.EXPR_TERMS, "a,b")
    cond.createNextCover("00", _cover(CoverTypeT.CONDBIN, 4), None)
    cond.createNextCover("11", _cover(CoverTypeT.CONDBIN, 0), None)

    expr = inst.createScope("expr_75", SourceInfo(pkg_fh, 75, 0), 1, SourceT.SV,
                            ScopeTypeT.EXPR, FlagsT.ENABLED_EXPR)
    expr.setStringProperty(-1, StrProperty.EXPR_TERMS, "x,y,z")
    expr.createNextCover("row_0", _cover(CoverTypeT.EXPRBIN, 2), None)


def _build_user_defined(inst, fh):
    """A user-defined bin: an ordinary coveritem with a user cover_type."""
    scope = inst.createScope("perf_counters", SourceInfo(fh, 80, 0), 1,
                             SourceT.SV, ScopeTypeT.GENERIC, 0)
    scope.setStringProperty(-1, StrProperty.GENERIC, "latency histogram")
    scope.createNextCover("bucket_0", _cover(CoverTypeT.USERBIN, 15), None)
    scope.createNextCover("bucket_1", _cover(CoverTypeT.USERBIN, 0), None)


# --------------------------------------------------------------------------
# Snapshot: one structural fingerprint, used by both L1 and L2
# --------------------------------------------------------------------------

class _Unsupported:
    """A fact the backend could not state, as distinct from a zero value.

    This distinction is the whole reason the snapshot is not a plain dict of
    values. ``MemUCIS.getFlags()`` raises ``NotImplementedError``; coercing that
    to ``0`` would make a round trip that faithfully preserved flags compare
    *equal* to one that dropped them, and the test would pass vacuously in both
    directions. Same argument as the ``promoted_props`` bitmask in the mapping:
    "the value is 0" and "I cannot answer" are different facts.
    """

    __slots__ = ()

    def __repr__(self):
        return "<unsupported>"

    def __eq__(self, other):
        return isinstance(other, _Unsupported)

    def __hash__(self):
        return hash("<unsupported>")


UNSUPPORTED = _Unsupported()


def snapshot(db):
    """A comparable, order-preserving fingerprint of a database.

    Iteration order is part of the fingerprint: UCIS defines scope and cover
    iteration order, and a backend that returns the right objects in the wrong
    order is not equivalent. Comparing sorted sets would hide exactly that.

    Values a backend cannot state come back as :data:`UNSUPPORTED`; compare
    snapshots with :func:`assert_no_loss` rather than ``==``.
    """
    return {
        "scopes": _walk(db),
        "history": _history(db),
    }


def _walk(db):
    out = []

    def visit(scope, path):
        here = path + "/" + str(scope.getScopeName())
        out.append({
            "path": here,
            "type": int(scope.getScopeType()),
            "flags": _int_or_unsupported(scope.getFlags),
            "attributes": dict(_safe(scope.getAttributes) or {}),
            "tags": sorted(_safe(scope.getTags) or ()),
            "properties": _properties(scope),
            "bins": _bins(scope),
        })
        for child in scope.scopes(-1):
            visit(child, here)

    for root in db.scopes(-1):
        visit(root, "")
    return out


def _bins(scope):
    out = []
    for ci in scope.coverItems(CoverTypeT.ALL):
        data = ci.getCoverData()
        out.append({
            "name": ci.getName(),
            "type": int(data.type),
            "count": int(data.data or 0),
            "at_least": _int_or_none(getattr(data, "at_least", None)),
            "weight": _int_or_none(getattr(data, "weight", None)),
            "flags": _int_or_unsupported(ci.getCoverFlags),
            "attributes": dict(_safe(ci.getAttributes) or {}),
        })
    return out


# --------------------------------------------------------------------------
# Comparison
# --------------------------------------------------------------------------

def assert_no_loss(source, target, *, source_name="source",
                   target_name="target"):
    """Every fact *source* can state survives into *target*.

    Not plain equality, deliberately. A backend is allowed to answer *more* than
    the source did -- the Parquet backend materializes ``UCIS_STR_UNIQUE_ID``
    and reads back scope flags that ``MemUCIS`` cannot report at all -- and
    demanding symmetry would fail the round trip for being *better* than its
    input. What must never happen is the reverse: a fact the source stated and
    the target cannot.

    Raises:
        AssertionError: naming the first path and field that lost information.
    """
    problems = []
    _cmp_scopes(source["scopes"], target["scopes"], problems)
    _cmp_list(source["history"], target["history"], "history", problems)
    if problems:
        head = f"{len(problems)} fact(s) lost from {source_name} to {target_name}:"
        raise AssertionError("\n  ".join([head] + problems[:20]))


def _cmp_scopes(src, dst, problems):
    if len(src) != len(dst):
        problems.append(
            f"scope count {len(src)} -> {len(dst)}; "
            f"missing: {sorted({s['path'] for s in src} - {d['path'] for d in dst})[:5]}"
        )
        return
    for a, b in zip(src, dst):
        if a["path"] != b["path"]:
            problems.append(f"scope order: {a['path']!r} -> {b['path']!r}")
            continue
        for field in ("type", "flags", "attributes", "tags"):
            _cmp_value(a[field], b[field], f"{a['path']}.{field}", problems)
        for key, value in a["properties"].items():
            _cmp_value(value, b["properties"].get(key, _MISSING),
                       f"{a['path']}.{key}", problems)
        _cmp_list(a["bins"], b["bins"], f"{a['path']}.bins", problems)


class _Missing:
    def __repr__(self):
        return "<absent>"


_MISSING = _Missing()


def _cmp_list(src, dst, where, problems):
    if len(src) != len(dst):
        problems.append(f"{where}: count {len(src)} -> {len(dst)}")
        return
    for i, (a, b) in enumerate(zip(src, dst)):
        for key, value in a.items():
            _cmp_value(value, b.get(key, _MISSING), f"{where}[{i}].{key}",
                       problems)


def _cmp_value(want, got, where, problems):
    if want is UNSUPPORTED or want is None:
        return  # the source could not state it; nothing to preserve
    if got is UNSUPPORTED:
        problems.append(f"{where}: {want!r} -> could not be read back")
    elif got != want:
        problems.append(f"{where}: {want!r} -> {got!r}")


def unsupported_facts(snap):
    """Which facts the snapshotted backend could not state.

    Reported by a test rather than asserted, so the oracle's own blind spots are
    visible instead of silently shrinking what the round trip proves.
    """
    out = []
    for scope in snap["scopes"]:
        if scope["flags"] is UNSUPPORTED:
            out.append(f"{scope['path']}.flags")
        for i, b in enumerate(scope["bins"]):
            if b["flags"] is UNSUPPORTED:
                out.append(f"{scope['path']}.bins[{i}].flags")
    return out


#: Properties whose value is derived rather than stored, so they legitimately
#: differ between a source database and a round-tripped one.
_DERIVED = {"SCOPE_NUM_COVERITEMS", "NUM_TESTS", "IS_MODIFIED",
            "MODIFIED_SINCE_SIM", "TOGGLE_COVERED", "SCOPE_HIER_NAME"}


def _properties(scope):
    out = {}
    for enum, getter in ((IntProperty, "getIntProperty"),
                         (StrProperty, "getStringProperty")):
        for prop in enum:
            if prop.name in _DERIVED:
                continue
            try:
                value = getattr(scope, getter)(-1, prop)
            except Exception:
                continue
            if value is not None:
                out[f"{enum.__name__}.{prop.name}"] = value
    return out


def _history(db):
    from covsight.core.api import HistoryNodeKind

    out = []
    for node in db.historyNodes(HistoryNodeKind.ALL):
        out.append({
            "name": _safe(node.getLogicalName),
            "physical": _safe(node.getPhysicalName),
            "kind": int(_safe(node.getKind) or 0),
            "status": _int_or_none(_safe(node.getTestStatus)),
            "seed": _safe(node.getSeed),
            "cmd": _safe(node.getCmd),
            "user": _safe(node.getUserName),
            "date": _safe(node.getDate),
            "tool_category": _safe(node.getToolCategory),
            "time_unit": _safe(node.getTimeUnit),
            "sim_time": _safe(node.getSimTime),
        })
    return out


def _safe(fn):
    if fn is None:
        return None
    try:
        return fn()
    except Exception:
        return None


def _int_or_none(value):
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def _int_or_unsupported(getter):
    """Distinguish "answered 0" from "raised"; see :class:`_Unsupported`."""
    try:
        value = getter()
    except Exception:
        return UNSUPPORTED
    return UNSUPPORTED if value is None else int(value)
