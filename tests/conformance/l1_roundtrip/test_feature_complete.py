"""L1: the feature-complete corpus survives a Parquet round trip intact.

The correctness bar from the mapping document: **UCIS -> Parquet -> UCIS must be
lossless**. `tests/parquet/test_roundtrip.py` already asserts that for a small
database; this asserts it for one that exercises every coverage construct the
object API can express, which is what makes the claim worth anything.

Comparison is :func:`assert_no_loss`, not ``==``: the Parquet backend legitimately
answers *more* than MemUCIS does (it materializes `UCIS_STR_UNIQUE_ID` and reads
back scope flags MemUCIS cannot report at all). Requiring symmetry would fail
the round trip for being better than its input.
"""

from __future__ import annotations

import pytest

pytest.importorskip("pyarrow", reason="Parquet backend needs pyarrow")

from covsight.core.api import CoverTypeT, ScopeTypeT           # noqa: E402
from covsight.core.conformance import ucis_feature             # noqa: E402
from covsight.core.parquet import ParquetUCIS, ParquetWriter   # noqa: E402

from ..fixtures.feature_complete import (                      # noqa: E402
    assert_no_loss, build_feature_complete, snapshot, unsupported_facts,
)


@pytest.fixture(scope="module")
def corpus():
    return build_feature_complete()


@pytest.fixture(scope="module")
def round_tripped(corpus, tmp_path_factory):
    path = tmp_path_factory.mktemp("fc") / "dataset"
    ParquetWriter(path).write(corpus, run_id="r0")
    return ParquetUCIS(path)


def _types(db, kind):
    """Every distinct scope- or cover-type value reachable in *db*."""
    found = set()

    def visit(scope):
        if kind == "scope":
            found.add(int(scope.getScopeType()))
        else:
            for ci in scope.coverItems(CoverTypeT.ALL):
                found.add(int(ci.getCoverData().type))
        for child in scope.scopes(-1):
            visit(child)

    for root in db.scopes(-1):
        visit(root)
    return found


# -- the headline ------------------------------------------------------


@ucis_feature(
    "S4.1", "S4.2", "C6.1", "C6.3", "DB.1", "DB.3", "DB.5",
    level="L1", surface="write",
)
def test_round_trip_loses_nothing(corpus, round_tripped):
    assert_no_loss(snapshot(corpus), snapshot(round_tripped),
                   source_name="MemUCIS", target_name="Parquet")


def test_the_backend_answers_more_than_the_source(corpus, round_tripped):
    """Guards against the round trip passing vacuously.

    MemUCIS cannot report scope flags at all -- `getFlags()` raises. If the
    snapshot coerced that to zero, a backend that silently dropped every flag
    would compare equal to one that preserved them. This asserts the asymmetry
    runs the right way: the source has blind spots, the Parquet view does not.
    """
    source_blind = unsupported_facts(snapshot(corpus))
    target_blind = unsupported_facts(snapshot(round_tripped))

    assert source_blind, (
        "MemUCIS used to have unreadable facts (scope flags). If that is fixed, "
        "this test has served its purpose and the corpus comparison got "
        "stricter -- delete it."
    )
    assert not target_blind, (
        f"the Parquet view cannot state {len(target_blind)} fact(s) the "
        f"comparison therefore skips: {target_blind[:5]}"
    )


# -- structure ---------------------------------------------------------


@ucis_feature(
    "ST.1", "ST.2", "ST.3", "ST.4", "ST.5", "ST.7", "ST.11", "ST.13",
    "ST.15", "ST.16", "ST.17", "ST.18", "ST.23", "ST.25", "ST.30", "ST.31",
    "ST.33", "S4.17",
    level="L1",
)
def test_every_scope_type_in_the_corpus_survives(corpus, round_tripped):
    """`scopes.scope_type` carries the UCIS enum verbatim."""
    before, after = _types(corpus, "scope"), _types(round_tripped, "scope")
    assert before == after
    # Guards the corpus itself: a builder edit that quietly stopped emitting a
    # construct would otherwise make this test weaker without failing.
    assert len(before) >= 14, f"corpus covers only {len(before)} scope types"


@ucis_feature(
    "BT.1", "BT.2", "BT.6", "BT.7", "BT.8", "BT.9", "BT.10", "BT.11",
    "BT.12", "BT.13", "BT.14", "BT.15", "BT.16", "BT.17", "BT.18", "BT.19",
    "BT.20", "BT.21", "BT.22", "BT.23", "C6.6",
    level="L1",
)
def test_every_bin_type_in_the_corpus_survives(corpus, round_tripped):
    """`coveritems.cover_type` is what the merge operator keys on, so every
    value has to survive exactly -- see X.5 for what happens when one does not."""
    before, after = _types(corpus, "cover"), _types(round_tripped, "cover")
    assert before == after
    assert len(before) >= 14, f"corpus covers only {len(before)} bin types"


@ucis_feature("CG.1", "CG.3", "CG.4", "CG.5", "CG.6", "CG.9", "S4.4",
              level="L1")
def test_covergroup_shape_survives(round_tripped):
    """Covergroup -> coverpoint -> {bins, bin scope}, plus a cross sibling."""
    cg = _find(round_tripped, ScopeTypeT.COVERGROUP, "addr_cg")
    kids = {s.getScopeName(): s for s in cg.scopes(-1)}
    assert set(kids) == {"addr", "data", "addr_x_data", "mode"}
    assert int(kids["addr_x_data"].getScopeType()) == int(ScopeTypeT.CROSS)

    # Bin classification is a cover_type, not a separate table.
    addr_bins = {ci.getName(): int(ci.getCoverData().type)
                 for ci in kids["addr"].coverItems(CoverTypeT.ALL)}
    assert addr_bins["ignore_hi"] == int(CoverTypeT.IGNOREBIN)
    assert addr_bins["illegal_x"] == int(CoverTypeT.ILLEGALBIN)
    assert addr_bins["default"] == int(CoverTypeT.DEFAULTBIN)

    # A bin-grouping scope nests under the coverpoint.
    grouped = {s.getScopeName() for s in kids["data"].scopes(-1)}
    assert grouped == {"auto_bins"}


@ucis_feature("FS.1", "FS.2", "FS.3", level="L1")
def test_fsm_states_and_transitions_are_sibling_scopes(round_tripped):
    """Both use UCIS_FSMBIN, so the parent's scope_type is the discriminator --
    the one place where the bin type alone is not enough."""
    fsm = _find(round_tripped, ScopeTypeT.FSM, "ctrl_fsm")
    kids = {s.getScopeName(): s for s in fsm.scopes(-1)}
    assert set(kids) == {"states", "trans"}
    assert int(kids["states"].getScopeType()) == int(ScopeTypeT.FSM_STATES)
    assert int(kids["trans"].getScopeType()) == int(ScopeTypeT.FSM_TRANS)

    for child in kids.values():
        for ci in child.coverItems(CoverTypeT.ALL):
            assert int(ci.getCoverData().type) == int(CoverTypeT.FSMBIN)

    assert [ci.getName() for ci in kids["states"].coverItems(CoverTypeT.ALL)] \
        == ["IDLE", "BUSY", "DONE"]


@ucis_feature("TG.1", level="L1")
def test_toggle_bins_are_addressed_by_name(round_tripped):
    """Bin names carry the transition; position is not stable across backends
    (NCDB canonicalizes a toggle pair, Parquet keeps creation order)."""
    toggle = _find(round_tripped, ScopeTypeT.TOGGLE, "data_valid")
    by_name = {ci.getName(): ci.getCoverData().data
               for ci in toggle.coverItems(CoverTypeT.ALL)}
    assert by_name == {"0->1": 3, "1->0": 0}


@ucis_feature("TG.2", "TG.3", "TG.4", "TG.6", level="L1")
def test_toggle_properties_do_not_depend_on_the_parent_scope(corpus):
    """Regression: a toggle under a DU-linked instance is still a toggle.

    ``MemToggleScope`` and ``MemToggleInstanceScope`` had duplicate accessors
    but only the first implemented the UCIS property interface, so
    ``getIntProperty(TOGGLE_TYPE)`` raised for every toggle in a real design
    hierarchy -- and since the Parquet writer reads through that interface, the
    metric, type and direction never reached the dataset. Found by this corpus:
    the small fixture put its toggle under a plain scope and never hit it.
    """
    from covsight.core.api import IntProperty, StrProperty

    toggle = _find(corpus, ScopeTypeT.TOGGLE, "data_valid")
    assert type(toggle).__name__ == "MemToggleInstanceScope", (
        "this test is only meaningful for a toggle under a DU-linked instance"
    )
    assert toggle.getIntProperty(-1, IntProperty.TOGGLE_TYPE) is not None
    assert toggle.getIntProperty(-1, IntProperty.TOGGLE_DIR) is not None
    assert toggle.getIntProperty(-1, IntProperty.TOGGLE_METRIC) is not None
    assert toggle.getStringProperty(-1, StrProperty.TOGGLE_CANON_NAME) \
        == "top.data_valid"


@ucis_feature("TG.2", "TG.3", "TG.4", "TG.6", "DB.6", level="L1", surface="write")
def test_toggle_metadata_lands_in_the_properties_table(corpus, tmp_path):
    """Asserted against the stored table, not through the object API.

    The object API cannot serve these back -- `getIntProperty(TOGGLE_TYPE)`
    raises `UnimplError` on MemUCIS *and* on the Parquet view, so an
    API-level round trip is symmetric and proves nothing. The mapping's claim
    is that the metric, type, direction and canonical name are rows in
    `properties` on the toggle scope, and that is a claim about the format,
    which is what a third-party SQL reader depends on.
    """
    path = tmp_path / "toggle"
    ParquetWriter(path).write(corpus, run_id="r0")

    rows = _properties_rows(path)
    toggle_props = {
        r["prop_id"]: r for r in rows
        if r["namespace"] == "ucis" and str(r["object_id"]).endswith("data_valid#1")
    }
    assert {"TOGGLE_TYPE", "TOGGLE_DIR", "TOGGLE_METRIC",
            "TOGGLE_CANON_NAME"} <= set(toggle_props)
    assert toggle_props["TOGGLE_CANON_NAME"]["str"] == "top.data_valid"
    # UCIS_INT_TOGGLE_COVERED is deliberately not promoted to a column: it is
    # derived from the counts, so freezing it into a definition table would make
    # the definition disagree with the measurement after a merge.
    assert "TOGGLE_COVERED" not in toggle_props


@ucis_feature("AS.1", "AS.2", "AS.3", "AS.4", level="L1")
def test_assertion_outcome_is_a_set_of_counter_bins(round_tripped):
    """Not a status field -- which is why most of them sum on merge and
    PEAKACTIVEBIN does not."""
    asrt = _find(round_tripped, ScopeTypeT.ASSERT, "a_req_ack")
    bins = {ci.getName(): int(ci.getCoverData().type)
            for ci in asrt.coverItems(CoverTypeT.ALL)}
    assert bins == {
        "pass": int(CoverTypeT.PASSBIN),
        "fail": int(CoverTypeT.FAILBIN),
        "vacuous": int(CoverTypeT.VACUOUSBIN),
        "disabled": int(CoverTypeT.DISABLEDBIN),
        "attempt": int(CoverTypeT.ATTEMPTBIN),
        "active": int(CoverTypeT.ACTIVEBIN),
        "peak": int(CoverTypeT.PEAKACTIVEBIN),
    }

    cover = _find(round_tripped, ScopeTypeT.COVER, "c_handshake")
    assert [int(ci.getCoverData().type)
            for ci in cover.coverItems(CoverTypeT.ALL)] == [int(CoverTypeT.COVERBIN)]


@ucis_feature("SB.1", "SB.2", "BR.1", "CX.1", "CX.2", "UD.1", "UD.3",
              level="L1")
def test_code_coverage_shapes_survive(round_tripped):
    block = _find(round_tripped, ScopeTypeT.BLOCK, "proc_always")
    kinds = [int(ci.getCoverData().type)
             for ci in block.coverItems(CoverTypeT.ALL)]
    assert kinds == [int(CoverTypeT.STMTBIN), int(CoverTypeT.STMTBIN),
                     int(CoverTypeT.BLOCKBIN)]

    branch = _find(round_tripped, ScopeTypeT.BRANCH, "if_63")
    assert [ci.getName() for ci in branch.coverItems(CoverTypeT.ALL)] \
        == ["then", "else"]

    cond = _find(round_tripped, ScopeTypeT.COND, "cond_70")
    assert _props(cond).get("EXPR_TERMS") == "a,b"
    expr = _find(round_tripped, ScopeTypeT.EXPR, "expr_75")
    assert _props(expr).get("EXPR_TERMS") == "x,y,z"

    user = _find(round_tripped, ScopeTypeT.GENERIC, "perf_counters")
    assert _props(user).get("GENERIC") == "latency histogram"
    assert all(int(ci.getCoverData().type) == int(CoverTypeT.USERBIN)
               for ci in user.coverItems(CoverTypeT.ALL))


# -- the extension namespace -------------------------------------------


@ucis_feature("A9.1", "A9.5", "T13.2", "T13.5", "IT.9", "X.11", "DB.4",
              level="L1")
def test_tags_and_attributes_survive_on_every_object_kind(round_tripped):
    """Both are extension-namespace rows in `properties`; `object_kind` is what
    lets one EAV table serve scopes and bins alike."""
    du = _find(round_tripped, ScopeTypeT.DU_MODULE, "work.dut")
    assert du.getAttributes() == {"origin": "handwritten", "revision": "3"}
    assert sorted(du.getTags()) == ["rtl", "synthesizable"]

    cg = _find(round_tripped, ScopeTypeT.COVERGROUP, "addr_cg")
    assert sorted(cg.getTags()) == ["functional"]

    # Attributes on a cover bin, not just on a scope.
    cp = _find(round_tripped, ScopeTypeT.COVERPOINT, "addr")
    bins = {ci.getName(): ci.getAttributes()
            for ci in cp.coverItems(CoverTypeT.ALL)}
    assert bins["bin_0"] == {"bin_kind": "auto"}

    states = _find(round_tripped, ScopeTypeT.FSM_STATES, "states")
    idle = next(iter(states.coverItems(CoverTypeT.ALL)))
    assert idle.getAttributes() == {"stateval": "IDLE"}


@ucis_feature("H11.1", "H11.3", "H11.4", "H11.5", "H11.7", "H11.8",
              level="L1")
def test_history_tree_and_test_data_survive(corpus, round_tripped):
    before = snapshot(corpus)["history"]
    after = snapshot(round_tripped)["history"]
    assert len(after) == len(before) == 2
    assert [h["name"] for h in after] == [h["name"] for h in before]
    assert {h["kind"] for h in after} == {h["kind"] for h in before}

    test_node = next(h for h in after if h["name"].startswith("test_"))
    assert test_node["seed"] == "4242"
    assert test_node["user"] == "ci"
    assert test_node["tool_category"] == "simulator"


@ucis_feature("F10.1", "F10.3", "F10.4", "DB.7", level="L1")
def test_source_locations_resolve_through_the_file_table(round_tripped):
    """File IDs are DU-local, so a location is only meaningful once resolved."""
    inst = _find(round_tripped, ScopeTypeT.INSTANCE, "top")
    info = inst.getSourceInfo()
    assert info is not None and info.line == 10

    block = _find(round_tripped, ScopeTypeT.BLOCK, "proc_always")
    lines = [ci.getSourceInfo().line for ci in block.coverItems(CoverTypeT.ALL)
             if ci.getSourceInfo() is not None]
    assert 61 in lines and 62 in lines


# -- helpers -----------------------------------------------------------


def _find(db, scope_type, name):
    want = int(scope_type)

    def visit(scope):
        if int(scope.getScopeType()) == want and scope.getScopeName() == name:
            return scope
        for child in scope.scopes(-1):
            found = visit(child)
            if found is not None:
                return found
        return None

    for root in db.scopes(-1):
        found = visit(root)
        if found is not None:
            return found
    raise AssertionError(f"no {scope_type!r} scope named {name!r} in the dataset")


def _properties_rows(dataset):
    """The `properties` table as plain dicts, the way a SQL reader sees it."""
    import glob

    import pyarrow.parquet as pq

    rows = []
    pattern = str(dataset) + "/properties/**/*.parquet"
    for file in sorted(glob.glob(pattern, recursive=True)):
        rows.extend(pq.read_table(file).to_pylist())
    assert rows, f"no properties rows under {pattern}"
    return rows


def _props(scope):
    from covsight.core.api import IntProperty, StrProperty

    out = {}
    for enum, getter in ((IntProperty, "getIntProperty"),
                         (StrProperty, "getStringProperty")):
        for prop in enum:
            try:
                value = getattr(scope, getter)(-1, prop)
            except Exception:
                continue
            if value is not None:
                out[prop.name] = value
    return out
