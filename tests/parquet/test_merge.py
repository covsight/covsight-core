"""Test 5 -- merge correctness.

Three ways of merging must agree: the query-time view, the materialized
snapshot, and the existing local ``db_merger.py``.  ``db_merger`` is the
baseline oracle -- if the columnar merge disagrees with it, the columnar merge
is wrong.
"""

import pytest

from covsight.core.api import CoverData, CoverTypeT, HistoryNodeKind, ScopeTypeT
from covsight.core.api.enums import FormalStatusT
from covsight.core.conformance import ucis_feature
from covsight.core.mem import MemFactory
from covsight.core.parquet import ParquetUCIS, ParquetWriter
from covsight.core.parquet import merge
from covsight.core.parquet.writer import DefinitionMismatch

from .conftest import build_db


def _bins(db):
    """``(scope path, bin name) -> count``, over the whole database."""
    out = {}

    def visit(scope, path):
        for item in scope.coverItems(CoverTypeT.ALL):
            out[(path, item.getName())] = item.getCoverData().data
        for child in scope.scopes(ScopeTypeT.ALL):
            visit(child, "%s/%s" % (path, child.getScopeName() or ""))
    for top in db.scopes(ScopeTypeT.ALL):
        visit(top, top.getScopeName() or "")
    return out


# --------------------------------------------------------------------------
# The three models agree
# --------------------------------------------------------------------------

@ucis_feature("DB.1", level="L3")
def test_virtual_matches_local_merge(multi_run):
    """Query-time merge == ``db_merger.py`` on the UCIS view."""
    path, _runs, specs = multi_run
    local = MemFactory.create()
    from covsight.core.merge.db_merger import DbMerger
    DbMerger().merge(local, [build_db(counts=c, tag=t) for c, t in specs])

    assert _bins(ParquetUCIS(path)) == _bins(local)


@ucis_feature("DB.1", level="L3")
def test_materialized_matches_virtual(multi_run, tmp_path):
    path, _runs, _specs = multi_run
    out = tmp_path / "snapshot.parquet"
    merge.materialize(path, out)
    assert _bins(ParquetUCIS(out)) == _bins(ParquetUCIS(path))


def test_materialized_matches_local_merge(multi_run, tmp_path):
    path, _runs, specs = multi_run
    out = tmp_path / "snapshot.parquet"
    merge.materialize(path, out)

    local = MemFactory.create()
    from covsight.core.merge.db_merger import DbMerger
    DbMerger().merge(local, [build_db(counts=c, tag=t) for c, t in specs])

    assert _bins(ParquetUCIS(out)) == _bins(local)


def test_merge_result_reports_what_it_merged(multi_run):
    path, runs, _specs = multi_run
    result = merge.virtual(path)
    assert result.runs == runs
    assert result.num_runs == len(runs)
    assert result.num_bins == 6
    assert result.total_count == sum(result.counts.values())


def test_merge_subset(multi_run):
    path, runs, specs = multi_run
    result = merge.virtual(path, runs=[runs[0], runs[1]])
    assert result.runs == (runs[0], runs[1])
    expected = specs[0][0][0] + specs[1][0][0]
    assert max(result.counts.values()) >= expected


# --------------------------------------------------------------------------
# Definitions are carried through, never merged
# --------------------------------------------------------------------------

@ucis_feature("DB.5", level="L3")
def test_definitions_are_carried_through(multi_run, tmp_path):
    """Bin definitions pass through a merge untouched."""
    path, _runs, _specs = multi_run
    out = tmp_path / "snapshot.parquet"
    merge.materialize(path, out)

    from covsight.core.parquet import schema as sch
    source, snapshot = ParquetUCIS(path), ParquetUCIS(out)
    for name in sch.DEFINITION_TABLES:
        assert snapshot.dataset.table(name).to_pydict() == \
            source.dataset.table(name).to_pydict(), name


@ucis_feature("X.10", level="L3")
def test_drifted_definitions_are_refused(tmp_path):
    first = tmp_path / "a.parquet"
    second = tmp_path / "b.parquet"
    ParquetWriter(first).write(build_db(counts=(1, 2, 3)), run_id="r0")
    ParquetWriter(second).write(build_db(counts=(1, 2)), run_id="r0")

    with pytest.raises(DefinitionMismatch):
        merge.check_definitions([first, second])


def test_matching_definitions_pass_the_check(tmp_path):
    first = tmp_path / "a.parquet"
    second = tmp_path / "b.parquet"
    ParquetWriter(first).write(build_db(counts=(1, 2, 3), tag="a"),
                               run_id="r0")
    ParquetWriter(second).write(build_db(counts=(9, 9, 9), tag="b"),
                                run_id="r0")
    assert merge.check_definitions([first, second])


def test_materialize_refuses_to_write_over_its_source(multi_run):
    path, _runs, _specs = multi_run
    with pytest.raises(ValueError):
        merge.materialize(path, path)


# --------------------------------------------------------------------------
# Type-aware merge
# --------------------------------------------------------------------------

def _peak_db(peak, tag):
    """A database with one additive bin and one peak-active bin."""
    db = MemFactory.create()
    db.createHistoryNode(None, "test_" + tag, "./run.sh",
                         HistoryNodeKind.TEST)
    du = db.createScope("work.m", None, 1, 0, ScopeTypeT.DU_MODULE, 0)
    inst = db.createInstance("top", None, 1, 0, ScopeTypeT.INSTANCE, du, 0)
    scope = inst.createScope("a1", None, 1, 0, ScopeTypeT.ASSERT, 0)

    additive = CoverData(CoverTypeT.ATTEMPTBIN, 0)
    additive.data = peak
    scope.createNextCover("attempts", additive, None)

    peaked = CoverData(CoverTypeT.PEAKACTIVEBIN, 0)
    peaked.data = peak
    scope.createNextCover("peak", peaked, None)
    return db


@ucis_feature("BT.19", "X.5", level="L3")
def test_peak_active_takes_the_maximum(tmp_path):
    """A high-water mark must not be summed across runs."""
    path = tmp_path / "peak.parquet"
    writer = ParquetWriter(path)
    writer.write(_peak_db(3, "a"), run_id="r0")
    writer.write(_peak_db(5, "b"), run_id="r1")

    bins = _bins(ParquetUCIS(path))
    attempts = next(v for (_p, name), v in bins.items() if name == "attempts")
    peak = next(v for (_p, name), v in bins.items() if name == "peak")
    assert attempts == 8, "attempt counts are additive"
    assert peak == 5, "peak-active takes the max, not the sum"


def test_duckdb_agrees_with_the_python_merge_on_peaks(tmp_path):
    """The SQL merge view and the Python read path must not diverge."""
    duckdb_adapter = pytest.importorskip(
        "covsight.core.parquet.duckdb_adapter",
        reason="benchmark adapter needs duckdb")
    path = tmp_path / "peak.parquet"
    writer = ParquetWriter(path)
    writer.write(_peak_db(3, "a"), run_id="r0")
    writer.write(_peak_db(5, "b"), run_id="r1")

    with duckdb_adapter.DuckDbAdapter(path) as engine:
        assert engine.merged_counts() == merge.virtual(path).counts


# --------------------------------------------------------------------------
# Assertion / formal status uses precedence, not sum
# --------------------------------------------------------------------------

def _formal_db(status, tag, radius):
    db = build_db(tag=tag, with_formal=False)
    db.set_formal_data(0, status=status, radius=radius,
                       witness="w_%s.vcd" % tag)
    return db


@ucis_feature("FM.1", "X.4", level="L3")
def test_contradictory_formal_status_merges_to_conflict(tmp_path):
    """Proved in one run, failed in another, is a CONFLICT -- not a winner.

    UCIS reserves ``FormalStatusT.CONFLICT`` for exactly this. Reporting a
    clean proof for an assertion that some run observed failing would be the
    worst available answer, and silently preferring the failure would hide
    that the two runs disagree.
    """
    path = tmp_path / "formal.parquet"
    writer = ParquetWriter(path)
    writer.write(_formal_db(FormalStatusT.PROOF, "a", 4), run_id="r0")
    writer.write(_formal_db(FormalStatusT.FAILURE, "b", 9), run_id="r1")

    merged = ParquetUCIS(path).get_formal_data(0)
    assert merged["status"] == int(FormalStatusT.CONFLICT)
    assert merged["radius"] == 9, "radius takes the deepest proof reached"
    assert merged["witness"] is not None, "a witness is kept for triage"


def test_agreeing_formal_status_is_preserved(tmp_path):
    """Runs that agree merge to the status they agree on, not to CONFLICT."""
    path = tmp_path / "formal-agree.parquet"
    writer = ParquetWriter(path)
    writer.write(_formal_db(FormalStatusT.PROOF, "a", 4), run_id="r0")
    writer.write(_formal_db(FormalStatusT.PROOF, "b", 9), run_id="r1")
    assert ParquetUCIS(path).get_formal_data(0)["status"] == \
        int(FormalStatusT.PROOF)


def test_inconclusive_loses_to_a_completed_proof(tmp_path):
    """A finished proof is more informative than one that timed out."""
    path = tmp_path / "formal-incon.parquet"
    writer = ParquetWriter(path)
    writer.write(_formal_db(FormalStatusT.INCONCLUSIVE, "a", 4), run_id="r0")
    writer.write(_formal_db(FormalStatusT.PROOF, "b", 9), run_id="r1")
    assert ParquetUCIS(path).get_formal_data(0)["status"] == \
        int(FormalStatusT.PROOF)


@ucis_feature("FM.1", "X.4", level="L3")
def test_formal_status_merge_is_order_independent(tmp_path):
    """Reversing the write order must not change the merged status."""
    path = tmp_path / "formal2.parquet"
    writer = ParquetWriter(path)
    writer.write(_formal_db(FormalStatusT.FAILURE, "b", 9), run_id="r0")
    writer.write(_formal_db(FormalStatusT.PROOF, "a", 4), run_id="r1")
    assert ParquetUCIS(path).get_formal_data(0)["status"] == \
        int(FormalStatusT.CONFLICT)


def test_formal_survives_materialization(tmp_path):
    path = tmp_path / "formal3.parquet"
    out = tmp_path / "formal3-merged.parquet"
    writer = ParquetWriter(path)
    writer.write(_formal_db(FormalStatusT.PROOF, "a", 4), run_id="r0")
    writer.write(_formal_db(FormalStatusT.FAILURE, "b", 9), run_id="r1")
    merge.materialize(path, out)
    assert ParquetUCIS(out).get_formal_data(0) == \
        ParquetUCIS(path).get_formal_data(0)


# --------------------------------------------------------------------------
# Associations and history
# --------------------------------------------------------------------------

@ucis_feature("HL.6", "X.7", level="L3")
def test_associations_union_across_runs(multi_run):
    path, runs, _specs = multi_run
    contributions = ParquetUCIS(path).get_test_coverage_api() \
        .get_all_test_contributions()
    assert sorted(c.test_name for c in contributions) == \
        ["test_a", "test_b", "test_c"]


@ucis_feature("H11.5", "X.8", level="L3")
def test_materialized_merge_gains_a_merge_node(multi_run, tmp_path):
    """The merge is recorded in history -- the tree is just rows."""
    path, runs, _specs = multi_run
    out = tmp_path / "snapshot.parquet"
    merge.materialize(path, out, merge_name="nightly")

    snapshot = ParquetUCIS(out)
    merges = list(snapshot.historyNodes(HistoryNodeKind.MERGE))
    assert len(merges) == 1
    assert merges[0].getLogicalName() == "nightly"
    for run_id in runs:
        assert run_id in merges[0].getComment()

    tests = list(snapshot.historyNodes(HistoryNodeKind.TEST))
    assert len(tests) == len(runs)
    assert all(t.getParent() is not None and
               t.getParent().getLogicalName() == "nightly" for t in tests)


def test_materialized_merge_keeps_test_associations(multi_run, tmp_path):
    path, _runs, _specs = multi_run
    out = tmp_path / "snapshot.parquet"
    merge.materialize(path, out)

    def names(db):
        return sorted(c.test_name for c in
                      db.get_test_coverage_api().get_all_test_contributions())

    assert names(ParquetUCIS(out)) == names(ParquetUCIS(path))


# --------------------------------------------------------------------------
# Merge as ingestion, and idempotency
# --------------------------------------------------------------------------

def test_ingest_collects_runs_from_several_datasets(tmp_path):
    """Merging N runs costs N appends, not N rewrites."""
    sources = []
    for i, counts in enumerate([(1, 0, 0), (0, 2, 0), (0, 0, 3)]):
        path = tmp_path / ("src%d.parquet" % i)
        ParquetWriter(path).write(build_db(counts=counts, tag=str(i)),
                                  run_id="run%d" % i)
        sources.append(path)

    out = tmp_path / "all.parquet"
    runs = merge.ingest(out, sources)
    assert runs == ("run0", "run1", "run2")

    cvg = {name: value for (_p, name), value in _bins(ParquetUCIS(out)).items()
           if name.startswith("bin_")}
    assert cvg == {"bin_0": 1, "bin_1": 2, "bin_2": 3}


@ucis_feature("N.4", level="L3")
def test_reingesting_a_run_is_refused(tmp_path):
    """Idempotency: keyed on (run_id, coveritem_id), so no double-counting."""
    source = tmp_path / "src.parquet"
    ParquetWriter(source).write(build_db(counts=(1, 1, 1)), run_id="run0")
    out = tmp_path / "all.parquet"
    merge.ingest(out, [source])

    with pytest.raises(FileExistsError):
        merge.ingest(out, [source])

    cvg = {name: value for (_p, name), value in _bins(ParquetUCIS(out)).items()
           if name.startswith("bin_")}
    assert cvg == {"bin_0": 1, "bin_1": 1, "bin_2": 1}, "no double-count"


def test_reingesting_can_be_skipped_explicitly(tmp_path):
    source = tmp_path / "src.parquet"
    ParquetWriter(source).write(build_db(counts=(1, 1, 1)), run_id="run0")
    out = tmp_path / "all.parquet"
    merge.ingest(out, [source])
    assert merge.ingest(out, [source], skip_existing=True) == ("run0",)

    cvg = {name: value for (_p, name), value in _bins(ParquetUCIS(out)).items()
           if name.startswith("bin_")}
    assert cvg == {"bin_0": 1, "bin_1": 1, "bin_2": 1}


def test_ingest_refuses_drifted_definitions(tmp_path):
    a = tmp_path / "a.parquet"
    b = tmp_path / "b.parquet"
    ParquetWriter(a).write(build_db(counts=(1, 2, 3)), run_id="ra")
    ParquetWriter(b).write(build_db(counts=(1, 2)), run_id="rb")
    with pytest.raises(DefinitionMismatch):
        merge.ingest(tmp_path / "out.parquet", [a, b])


@pytest.mark.parametrize("n", [1, 4, 16])
def test_merge_scales_over_runs(tmp_path, n):
    """The merge-scaling sweep, in miniature: N runs in one dataset."""
    path = tmp_path / ("scale%d.parquet" % n)
    writer = ParquetWriter(path)
    for i in range(n):
        writer.write(build_db(counts=(1, 1, 1), tag=str(i)),
                     run_id="r%04d" % i)

    result = merge.virtual(path)
    assert result.num_runs == n
    cvg = {name: value for (_p, name), value in
           _bins(ParquetUCIS(path)).items() if name.startswith("bin_")}
    assert cvg == {"bin_0": n, "bin_1": n, "bin_2": n}


# --------------------------------------------------------------------------
# Compaction (rollup in place)
# --------------------------------------------------------------------------

def test_compact_preserves_coverage_exactly(multi_run):
    """Rolling runs up must not change what the coverage *is*."""
    from covsight.core.parquet.format_detect import dataset_runs
    path, runs, _specs = multi_run
    before = _bins(ParquetUCIS(path))

    result = merge.compact(path, into_run_id="rollup-0")
    assert result.run_id == "rollup-0"
    assert result.runs == runs

    assert dataset_runs(path) == ("rollup-0",)
    assert _bins(ParquetUCIS(path)) == before


def test_compact_is_idempotent_over_rounds(tmp_path):
    """Compacting, appending, compacting again must still total correctly."""
    path = tmp_path / "rolling.parquet"
    writer = ParquetWriter(path)
    for i in range(4):
        writer.write(build_db(counts=(1, 1, 1), tag=str(i)), run_id="r%d" % i)
    merge.compact(path, into_run_id="rollup-a")

    for i in range(4, 8):
        writer.write(build_db(counts=(1, 1, 1), tag=str(i)), run_id="r%d" % i)
    merge.compact(path, into_run_id="rollup-b")

    cvg = {name: value for (_p, name), value in _bins(ParquetUCIS(path)).items()
           if name.startswith("bin_")}
    assert cvg == {"bin_0": 8, "bin_1": 8, "bin_2": 8}


def test_compact_carries_test_associations_over(multi_run):
    """"Which test covered this bin" survives a rollup."""
    path, _runs, _specs = multi_run
    before = sorted(c.test_name for c in ParquetUCIS(path)
                    .get_test_coverage_api().get_all_test_contributions())
    merge.compact(path, into_run_id="rollup-0")
    after = sorted(c.test_name for c in ParquetUCIS(path)
                   .get_test_coverage_api().get_all_test_contributions())
    assert after == before


def test_compact_can_keep_its_sources(multi_run):
    """With drop_sources=False nothing is lost at all."""
    from covsight.core.parquet.format_detect import dataset_runs
    path, runs, _specs = multi_run
    merge.compact(path, into_run_id="rollup-0", drop_sources=False)
    assert dataset_runs(path) == runs + ("rollup-0",)
    # The sources are still individually readable.
    for run_id in runs:
        assert ParquetUCIS(path, runs=run_id).selected_runs == (run_id,)


def test_compact_rolls_up_a_subset(multi_run):
    """A rolling policy compacts the old runs and leaves recent ones alone."""
    from covsight.core.parquet.format_detect import dataset_runs
    path, runs, specs = multi_run
    total = _bins(ParquetUCIS(path))

    merge.compact(path, runs=list(runs[:2]), into_run_id="rollup-old")
    assert dataset_runs(path) == (runs[2], "rollup-old")
    # Total coverage is unchanged; the recent run is still separable.
    assert _bins(ParquetUCIS(path)) == total
    assert _cvg(ParquetUCIS(path, runs=runs[2])) == list(specs[2][0])


def test_compaction_plan_splits_by_retention(multi_run):
    path, runs, _specs = multi_run
    assert merge.compaction_plan(path, keep_recent=1) == (runs[:2], runs[2:])
    assert merge.compaction_plan(path, keep_recent=0) == (runs, ())
    assert merge.compaction_plan(path, keep_recent=99) == ((), runs)


def test_compact_refuses_to_target_a_source_run(multi_run):
    path, runs, _specs = multi_run
    with pytest.raises(ValueError):
        merge.compact(path, into_run_id=runs[0])


def test_compact_records_a_merge_node(multi_run):
    path, runs, _specs = multi_run
    merge.compact(path, into_run_id="rollup-0", merge_name="nightly-rollup")
    merges = list(ParquetUCIS(path).historyNodes(HistoryNodeKind.MERGE))
    assert [m.getLogicalName() for m in merges] == ["nightly-rollup"]
    for run_id in runs:
        assert run_id in merges[0].getComment()


def _cvg(db):
    out = []

    def visit(scope):
        for item in scope.coverItems(CoverTypeT.CVGBIN):
            out.append(item.getCoverData().data)
        for child in scope.scopes(ScopeTypeT.ALL):
            visit(child)
    for top in db.scopes(ScopeTypeT.ALL):
        visit(top)
    return out


def test_compact_refuses_to_clobber_an_unrelated_run(multi_run):
    """A rollup must not silently replace a run it is not consuming."""
    path, runs, _specs = multi_run
    with pytest.raises(FileExistsError):
        merge.compact(path, runs=list(runs[:2]), into_run_id=runs[2])
