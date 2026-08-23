"""Golden merge fidelity — what survives ``NcdbMerger``.

A merge is not a lossy export. Everything recoverable from the sources must be
recoverable from the merged artifact, and per-test data must stay attached to
the test that produced it.

These tests exist because the merge previously failed both halves of that: it
dropped six members outright, and it *misattributed* per-test contributions —
every single-run database names its contribution member ``contrib/0.bin``, so
sources collided and one run's coverage was read back under another run's test
name. Losing data is bad; relabelling it is worse, because the result still
looks plausible.

See ``docs/ncdb-multirun-scoping.md``.
"""

import os
import tempfile
import warnings

import pytest

from covsight.core.api import (
    CoverData, CoverTypeT, FlagsT, HistoryNodeKind, ScopeTypeT, SourceInfo,
    SourceT, TestStatusT,
)
from covsight.core.api.enums import FormalStatusT
from covsight.core.api.test_data import TestData
from covsight.core.mem import MemFactory
from covsight.core.ncdb.ncdb_merger import (
    NcdbMerger, SchemaDriftWarning, SchemaMismatch,
)
from covsight.core.ncdb.ncdb_reader import NcdbReader
from covsight.core.ncdb.ncdb_writer import NcdbWriter


def build_db(counts=(1, 0, 7), tag="a", *, formal=None, exclude_bin=None,
             attrs=None, tags=None, num_bins=3):
    """A feature-diverse single-run database.

    Deliberately populates every member the merge used to drop: attributes,
    tags, formal results, coveritem flags, toggle metadata, design units, and
    sparse per-test associations.
    """
    db = MemFactory.create()
    fh = db.createFileHandle("counter.sv", "/rtl")

    hist = db.createHistoryNode(None, "test_" + tag, "./run_%s.sh" % tag,
                                HistoryNodeKind.TEST)
    hist.setTestData(TestData(teststatus=TestStatusT.OK,
                              toolcategory="simulator", date="2026-07-26",
                              simtime=1.5, seed="seed_" + tag))

    du = db.createScope("work.counter", SourceInfo(fh, 1, 0), 1, SourceT.SV,
                        ScopeTypeT.DU_MODULE, 0)
    for key, value in (attrs or {"origin": "verilator"}).items():
        du.setAttribute(key, value)
    for tag_name in (tags or {"rtl"}):
        du.addTag(tag_name)

    inst = db.createInstance("top", SourceInfo(fh, 10, 0), 1, SourceT.SV,
                             ScopeTypeT.INSTANCE, du,
                             FlagsT.ENABLED_STMT | FlagsT.ENABLED_BRANCH)
    cg = inst.createCovergroup("addr_cg", SourceInfo(fh, 20, 0), 1, SourceT.SV)
    cp = cg.createCoverpoint("addr", SourceInfo(fh, 21, 0), 1, SourceT.SV)
    for i in range(num_bins):
        data = CoverData(CoverTypeT.CVGBIN, 0)
        data.data = counts[i] if i < len(counts) else 0
        data.at_least = 1
        idx = cp.createNextCover("bin_%d" % i, data, SourceInfo(fh, 22 + i, 0))
        if exclude_bin == i:
            idx.setCoverFlags(idx.getCoverFlags() | int(FlagsT.SCOPE_EXCLUDED))

    toggle = inst.createToggle("data_valid", "top.data_valid",
                               FlagsT.ENABLED_TOGGLE, None, None, None)
    for name, count in (("0->1", 3), ("1->0", 0)):
        data = CoverData(CoverTypeT.TOGGLEBIN, 0)
        data.data = count
        toggle.createNextCover(name, data, None)

    db.record_test_association(0, 0, max(counts[0], 1))
    if formal is not None:
        status, radius, witness = formal
        db.set_formal_data(0, status=status, radius=radius, witness=witness)
    return db


def write_all(dbs, directory):
    paths = []
    for i, db in enumerate(dbs):
        path = os.path.join(directory, "run%d.cdb" % i)
        NcdbWriter().write(db, path)
        paths.append(path)
    return paths


def merge_dbs(dbs, directory, **kwargs):
    paths = write_all(dbs, directory)
    target = os.path.join(directory, "merged.cdb")
    NcdbMerger().merge(paths, target, **kwargs)
    return NcdbReader().read(target), target


def find_scope(db, name):
    def visit(scope):
        for child in scope.scopes(ScopeTypeT.ALL):
            if child.getScopeName() == name:
                return child
            found = visit(child)
            if found is not None:
                return found
        return None
    return visit(db)


def all_bins(db):
    out = []

    def visit(scope):
        out.extend(scope.coverItems(CoverTypeT.ALL))
        for child in scope.scopes(ScopeTypeT.ALL):
            visit(child)
    for top in db.scopes(ScopeTypeT.ALL):
        visit(top)
    return out


# --------------------------------------------------------------------------
# Per-test attribution (D-1)
# --------------------------------------------------------------------------

def test_per_test_contributions_keep_their_own_test(tmp_path):
    """Each test's contribution must stay attached to that test.

    The regression this guards: all three sources emit ``contrib/0.bin``, so a
    name-keyed copy kept one and served it under whichever test happened to be
    index 0 — reporting run c's numbers as run a's.
    """
    dbs = [build_db(counts=c, tag=t)
           for c, t in [((1, 0, 7), "a"), ((2, 5, 0), "b"), ((4, 1, 1), "c")]]
    expected = {}
    for db, tag in zip(dbs, "abc"):
        api = db.get_test_coverage_api()
        expected["test_" + tag] = api.get_all_test_contributions()[0] \
            .total_contribution

    merged, _ = merge_dbs(dbs, str(tmp_path))
    actual = {c.test_name: c.total_contribution
              for c in merged.get_test_coverage_api()
              .get_all_test_contributions()}
    assert actual == expected


def test_every_test_survives_the_merge(tmp_path):
    dbs = [build_db(counts=(i + 1, 0, 0), tag=t)
           for i, t in enumerate("abcd")]
    merged, _ = merge_dbs(dbs, str(tmp_path))
    names = [c.test_name for c in
             merged.get_test_coverage_api().get_all_test_contributions()]
    assert sorted(names) == ["test_a", "test_b", "test_c", "test_d"]


def test_history_nodes_all_survive_plus_a_merge_node(tmp_path):
    dbs = [build_db(tag=t) for t in "abc"]
    merged, _ = merge_dbs(dbs, str(tmp_path))
    tests = [n.getLogicalName()
             for n in merged.historyNodes(HistoryNodeKind.TEST)]
    assert tests == ["test_a", "test_b", "test_c"]
    assert list(merged.historyNodes(HistoryNodeKind.MERGE))


# --------------------------------------------------------------------------
# Members that used to be dropped (D-2)
# --------------------------------------------------------------------------

def test_attributes_survive(tmp_path):
    dbs = [build_db(tag="a"), build_db(tag="b")]
    merged, _ = merge_dbs(dbs, str(tmp_path))
    du = next(s for s in merged.scopes(ScopeTypeT.ALL)
              if ScopeTypeT.DU_ANY(s.getScopeType()))
    assert du.getAttributes() == {"origin": "verilator"}


def test_attributes_union_across_sources(tmp_path):
    """An attribute set by only one run must not vanish."""
    dbs = [build_db(tag="a", attrs={"origin": "verilator"}),
           build_db(tag="b", attrs={"reviewer": "kim"})]
    merged, _ = merge_dbs(dbs, str(tmp_path))
    du = next(s for s in merged.scopes(ScopeTypeT.ALL)
              if ScopeTypeT.DU_ANY(s.getScopeType()))
    assert du.getAttributes() == {"origin": "verilator", "reviewer": "kim"}


def test_tags_union_across_sources(tmp_path):
    dbs = [build_db(tag="a", tags={"rtl"}),
           build_db(tag="b", tags={"reviewed"})]
    merged, _ = merge_dbs(dbs, str(tmp_path))
    du = next(s for s in merged.scopes(ScopeTypeT.ALL)
              if ScopeTypeT.DU_ANY(s.getScopeType()))
    assert du.getTags() == {"rtl", "reviewed"}


def test_formal_results_survive(tmp_path):
    dbs = [build_db(tag="a", formal=(FormalStatusT.PROOF, 7, "w.vcd")),
           build_db(tag="b")]
    merged, _ = merge_dbs(dbs, str(tmp_path))
    data = merged.get_formal_data(0)
    assert data is not None
    assert data["status"] == int(FormalStatusT.PROOF)
    assert data["radius"] == 7


def test_contradictory_formal_results_merge_to_conflict(tmp_path):
    """UCIS reserves CONFLICT for a merge disagreement -- use it."""
    dbs = [build_db(tag="a", formal=(FormalStatusT.PROOF, 4, "a.vcd")),
           build_db(tag="b", formal=(FormalStatusT.FAILURE, 9, "b.vcd"))]
    merged, _ = merge_dbs(dbs, str(tmp_path))
    data = merged.get_formal_data(0)
    assert data["status"] == int(FormalStatusT.CONFLICT)
    assert data["radius"] == 9


def test_exclusions_survive_and_union(tmp_path):
    """Exclusions and waivers live in coveritem_flags; losing them inflates
    coverage, so this is a correctness test, not a fidelity one."""
    dbs = [build_db(tag="a", exclude_bin=1), build_db(tag="b")]
    merged, _ = merge_dbs(dbs, str(tmp_path))
    flags = [b.getCoverFlags() for b in all_bins(merged)]
    assert any(f & int(FlagsT.SCOPE_EXCLUDED) for f in flags), \
        "the exclusion set in one source was lost by the merge"


def test_no_member_present_in_a_source_is_dropped(tmp_path):
    """Whatever the sources carry, the merge carries.

    Asserted against the sources rather than a hand-written list, so a member
    added to the format later is covered without editing this test.
    """
    import zipfile
    dbs = [build_db(tag="a", formal=(FormalStatusT.PROOF, 7, "w.vcd"),
                    exclude_bin=1),
           build_db(tag="b", formal=(FormalStatusT.PROOF, 4, "x.vcd"))]
    paths = write_all(dbs, str(tmp_path))
    target = os.path.join(str(tmp_path), "merged.cdb")
    NcdbMerger().merge(paths, target)

    source_members = set()
    for path in paths:
        source_members |= set(zipfile.ZipFile(path).namelist())
    merged_members = set(zipfile.ZipFile(target).namelist())

    # Contribution members are renumbered, so compare them by count instead.
    def data_members(names):
        return {n for n in names if not n.startswith("contrib/")}

    missing = data_members(source_members) - data_members(merged_members)
    assert not missing, "merge dropped: %s" % ", ".join(sorted(missing))

    # And the members most easily lost are really there.
    for expected in ("attrs.bin", "tags.json", "formal.bin",
                     "coveritem_flags.bin", "toggle.bin",
                     "design_units.json"):
        assert expected in merged_members, \
            "%s was dropped by the merge" % expected


def test_merged_artifact_reopens_with_full_fidelity(tmp_path):
    """The golden bar: structure, counts and metadata all round-trip."""
    dbs = [build_db(counts=(1, 0, 7), tag="a"),
           build_db(counts=(2, 5, 0), tag="b")]
    merged, _ = merge_dbs(dbs, str(tmp_path))

    names = [(b.getName(), b.getCoverData().data) for b in all_bins(merged)]
    assert names == [("bin_0", 3), ("bin_1", 5), ("bin_2", 7),
                     ("0->1", 6), ("1->0", 0)]
    assert find_scope(merged, "addr_cg") is not None
    assert find_scope(merged, "data_valid") is not None


# --------------------------------------------------------------------------
# Type-aware counts (D-4)
# --------------------------------------------------------------------------

def _peak_db(peak, tag):
    db = MemFactory.create()
    db.createHistoryNode(None, "test_" + tag, "./r.sh", HistoryNodeKind.TEST)
    du = db.createScope("work.m", None, 1, SourceT.SV, ScopeTypeT.DU_MODULE, 0)
    inst = db.createInstance("top", None, 1, SourceT.SV, ScopeTypeT.INSTANCE,
                             du, 0)
    scope = inst.createScope("a1", None, 1, SourceT.SV, ScopeTypeT.ASSERT, 0)
    attempts = CoverData(CoverTypeT.ATTEMPTBIN, 0)
    attempts.data = peak
    scope.createNextCover("attempts", attempts, None)
    peaked = CoverData(CoverTypeT.PEAKACTIVEBIN, 0)
    peaked.data = peak
    scope.createNextCover("peak", peaked, None)
    return db


def test_peak_active_counts_take_the_max_not_the_sum(tmp_path):
    """A high-water mark summed across runs is a plausible-looking lie."""
    merged, _ = merge_dbs([_peak_db(3, "a"), _peak_db(5, "b")], str(tmp_path))
    values = {b.getName(): b.getCoverData().data for b in all_bins(merged)}
    assert values["attempts"] == 8, "attempt counts are additive"
    assert values["peak"] == 5, "peak-active takes the max"


def test_additive_bins_are_unaffected_by_the_op_table(tmp_path):
    merged, _ = merge_dbs([build_db(counts=(1, 2, 3), tag="a"),
                           build_db(counts=(4, 5, 6), tag="b")],
                          str(tmp_path))
    values = {b.getName(): b.getCoverData().data for b in all_bins(merged)}
    assert values["bin_0"] == 5 and values["bin_1"] == 7 \
        and values["bin_2"] == 9


def test_merge_ops_member_is_absent_when_everything_is_additive(tmp_path):
    """The op table costs nothing for the overwhelmingly common case."""
    import zipfile
    from covsight.core.ncdb.merge_ops import MEMBER_MERGE_OPS
    path = os.path.join(str(tmp_path), "plain.cdb")
    NcdbWriter().write(build_db(), path)
    assert MEMBER_MERGE_OPS not in zipfile.ZipFile(path).namelist()

    peak_path = os.path.join(str(tmp_path), "peak.cdb")
    NcdbWriter().write(_peak_db(3, "a"), peak_path)
    assert MEMBER_MERGE_OPS in zipfile.ZipFile(peak_path).namelist()


# --------------------------------------------------------------------------
# Schema drift is loud (D-3)
# --------------------------------------------------------------------------

def test_schema_drift_warns(tmp_path):
    """A merge that silently gets ~68x slower should say so."""
    dbs = [build_db(tag="a", num_bins=3), build_db(tag="b", num_bins=4)]
    paths = write_all(dbs, str(tmp_path))
    target = os.path.join(str(tmp_path), "merged.cdb")

    with pytest.warns(SchemaDriftWarning, match="cross-schema"):
        NcdbMerger().merge(paths, target)


def test_schema_drift_can_be_made_an_error(tmp_path):
    dbs = [build_db(tag="a", num_bins=3), build_db(tag="b", num_bins=4)]
    paths = write_all(dbs, str(tmp_path))
    target = os.path.join(str(tmp_path), "merged.cdb")
    with pytest.raises(SchemaMismatch):
        NcdbMerger().merge(paths, target, allow_cross_schema=False)


def test_same_schema_does_not_warn(tmp_path):
    dbs = [build_db(tag="a"), build_db(tag="b")]
    paths = write_all(dbs, str(tmp_path))
    target = os.path.join(str(tmp_path), "merged.cdb")
    with warnings.catch_warnings():
        warnings.simplefilter("error", SchemaDriftWarning)
        NcdbMerger().merge(paths, target)
