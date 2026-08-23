"""Test 2 -- the correctness bar: ``UCIS -> Parquet -> UCIS`` is lossless.

Losslessness is what makes this a faithful UCIS *backend* rather than a lossy
export, so this compares the whole object-API surface, in iteration order, not
a summary.
"""

import pytest

from covsight.core.api import (
    CoverTypeT, HistoryNodeKind, IntProperty, ScopeTypeT, StrProperty,
)
from covsight.core.api.unimpl_error import UnimplError
from covsight.core.conformance import ucis_feature
from covsight.core.parquet import ParquetUCIS, ParquetWriter

from .conftest import build_db, walk_facts


@pytest.fixture
def roundtrip(tmp_path):
    db = build_db()
    path = tmp_path / "rt.parquet"
    ParquetWriter(path).write(db, run_id="r0")
    return db, ParquetUCIS(path)


@ucis_feature("DB.1", "DB.3", "C6.3", "C6.6", level="L1")
def test_every_api_fact_survives(roundtrip):
    source, restored = roundtrip
    assert walk_facts(restored) == walk_facts(source)


@ucis_feature("S4.1", "IT.1", "IT.2", level="L1")
def test_scope_count_and_order(roundtrip):
    source, restored = roundtrip

    def names(db):
        out = []

        def visit(scope):
            out.append(scope.getScopeName())
            for child in scope.scopes(ScopeTypeT.ALL):
                visit(child)
        for top in db.scopes(ScopeTypeT.ALL):
            visit(top)
        return out

    assert names(restored) == names(source)


@ucis_feature(
    "A9.1", "A9.2", "A9.3", "A9.5", "T13.2", "T13.5", "IT.9", "X.11",
    level="L1",
)
def test_attributes_and_tags_survive(roundtrip):
    """UCIS+ extensions ship as property rows, so they must come back too."""
    _source, restored = roundtrip
    du = next(s for s in restored.scopes(ScopeTypeT.ALL)
              if ScopeTypeT.DU_ANY(s.getScopeType()))
    assert du.getAttributes() == {"origin": "verilator"}
    assert du.getAttribute("origin") == "verilator"
    assert du.getTags() == {"rtl"}
    assert du.hasTag("rtl")
    assert not du.hasTag("nope")


@ucis_feature("A9.1", "A9.4", "X.11", level="L1")
def test_coveritem_attributes_survive(roundtrip):
    _source, restored = roundtrip
    bins = _all_bins(restored)
    tagged = [b for b in bins if b.getAttributes()]
    assert len(tagged) == 3
    assert all(b.getAttributes() == {"bin_kind": "auto"} for b in tagged)


@ucis_feature("CG.9", "CG.10", level="L1")
def test_covergroup_options_survive(roundtrip):
    source, restored = roundtrip
    src_cg = _find(source, "addr_cg")
    dst_cg = _find(restored, "addr_cg")
    for prop in (IntProperty.CVG_ATLEAST, IntProperty.CVG_AUTOBINMAX,
                 IntProperty.CVG_PERINSTANCE, IntProperty.CVG_MERGEINSTANCES):
        assert dst_cg.getIntProperty(-1, prop) == \
            src_cg.getIntProperty(-1, prop), prop.name


@ucis_feature("DB.6", "X.6", level="L1")
def test_unsupported_property_still_raises(roundtrip):
    """A property the source could not answer must not gain an answer.

    ``weight = 1`` and "this backend does not implement SCOPE_WEIGHT" are
    different facts; a promoted column alone cannot tell them apart, which is
    what the promotion bitmask is for.

    Asked of a *coverpoint*, which has no toggle semantics at all. This used to
    ask the toggle scope, back when ``MemToggleInstanceScope`` answered no
    toggle property -- that was a bug (toggles under a DU-linked instance lost
    their metric, type and direction), and fixing it removed the example rather
    than the behaviour under test. ``TOGGLE_COVERED`` is still a promoted
    property, so this still exercises the bitmask.
    """
    source, restored = roundtrip
    src_cp = _find(source, "addr")
    dst_cp = _find(restored, "addr")
    with pytest.raises(UnimplError):
        src_cp.getIntProperty(-1, IntProperty.TOGGLE_COVERED)
    with pytest.raises(UnimplError):
        dst_cp.getIntProperty(-1, IntProperty.TOGGLE_COVERED)


@ucis_feature("S4.11", "S4.15", level="L1")
def test_unique_id_is_the_primary_key(roundtrip):
    _source, restored = roundtrip
    cg = _find(restored, "addr_cg")
    uid = cg.getStringProperty(-1, StrProperty.UNIQUE_ID)
    assert uid
    assert restored.matchScopeByUniqueId(uid) is cg


@ucis_feature("S4.2", "S4.3", "S4.10", level="L1")
def test_instance_du_link_survives(roundtrip):
    _source, restored = roundtrip
    inst = _find(restored, "top")
    du = inst.getInstanceDu()
    assert du is not None
    assert du.getScopeName() == "work.counter"
    assert ScopeTypeT.DU_ANY(du.getScopeType())


@ucis_feature("H11.1", "H11.4", "H11.7", "H11.8", level="L1")
def test_history_and_test_data_survive(roundtrip):
    source, restored = roundtrip
    src = list(source.historyNodes(HistoryNodeKind.TEST))
    dst = list(restored.historyNodes(HistoryNodeKind.TEST))
    assert len(dst) == len(src) == 1
    for getter in ("getLogicalName", "getPhysicalName", "getSeed",
                   "getSimTime", "getTimeUnit", "getRunCwd", "getUserName",
                   "getToolCategory", "getDate", "getCmd"):
        assert getattr(dst[0], getter)() == getattr(src[0], getter)(), getter


@ucis_feature("HL.6", "HL.7", level="L1")
def test_test_cover_associations_survive(roundtrip):
    source, restored = roundtrip
    expected = source.get_test_coverage_api().get_all_test_contributions()
    actual = restored.get_test_coverage_api().get_all_test_contributions()
    assert [(a.test_name, a.total_items, a.total_contribution) for a in actual] \
        == [(e.test_name, e.total_items, e.total_contribution)
            for e in expected]


@ucis_feature("FM.1", "FM.2", "FM.3", level="L1")
def test_formal_data_survives(roundtrip):
    source, restored = roundtrip
    assert restored.get_formal_data(0) == source.get_formal_data(0)
    assert restored.get_formal_data(1) is None


@ucis_feature("F10.1", "F10.2", "F10.4", "DB.7", level="L1")
def test_source_files_survive(roundtrip):
    source, restored = roundtrip
    assert [f.getFileName() for f in restored.getSourceFiles()] == \
        [f.getFileName() for f in source.getSourceFiles()]


def test_parquet_to_parquet_is_stable(tmp_path):
    """Re-writing a Parquet-backed database reproduces it exactly."""
    db = build_db()
    first = tmp_path / "first.parquet"
    second = tmp_path / "second.parquet"
    ParquetWriter(first).write(db, run_id="r0")
    ParquetUCIS(first).write(str(second))
    assert walk_facts(ParquetUCIS(second)) == walk_facts(ParquetUCIS(first))


def test_backend_is_read_only(roundtrip):
    """Mutation raises rather than silently dropping the change."""
    _source, restored = roundtrip
    cg = _find(restored, "addr_cg")
    with pytest.raises(UnimplError):
        cg.setGoal(50)
    with pytest.raises(UnimplError):
        cg.createCovergroup("nope", None, 1, 0)
    with pytest.raises(UnimplError):
        next(iter(_all_bins(restored))).incrementCover()
    with pytest.raises(UnimplError):
        restored.createHistoryNode(None, "x", "y", HistoryNodeKind.TEST)


def _all_bins(db):
    out = []

    def visit(scope):
        out.extend(scope.coverItems(CoverTypeT.ALL))
        for child in scope.scopes(ScopeTypeT.ALL):
            visit(child)
    for top in db.scopes(ScopeTypeT.ALL):
        visit(top)
    return out


def _find(db, name):
    def visit(scope):
        for child in scope.scopes(ScopeTypeT.ALL):
            if child.getScopeName() == name:
                return child
            found = visit(child)
            if found is not None:
                return found
        return None
    return visit(db)
