"""Test 3 -- multi-run datasets.

Decision 1 of the implementation plan: a dataset is a *database*, not a run.
These tests pin the two invariants that follow -- definition tables are written
once and never rewritten, and each run gets its own partition -- because both
are load-bearing for merge and neither is visible in a single-run test.
"""

import json
import os

import pytest

from covsight.core.api import CoverTypeT, HistoryNodeKind, ScopeTypeT
from covsight.core.conformance import ucis_feature
from covsight.core.parquet import (
    DatasetError, DefinitionMismatch, ParquetDataset, ParquetUCIS,
    ParquetWriter,
)
from covsight.core.parquet import schema as sch
from covsight.core.parquet.format_detect import (
    dataset_runs, is_parquet_dataset,
)

from .conftest import build_db


@ucis_feature("X.14", level="L1")
def test_runs_land_in_separate_partitions(multi_run):
    path, runs, _specs = multi_run
    for name in sch.MEASUREMENT_TABLES:
        for run_id in runs:
            part = path / name / ("%s=%s" % (sch.RUN_PARTITION_COL, run_id))
            assert (part / "data.parquet").exists(), "%s/%s" % (name, run_id)


@ucis_feature("X.8", level="L1")
def test_definition_tables_are_not_partitioned(multi_run):
    path, _runs, _specs = multi_run
    for name in sch.DEFINITION_TABLES:
        assert (path / name / "data.parquet").exists()
        entries = os.listdir(path / name)
        assert entries == ["data.parquet"], (name, entries)


def test_definition_tables_are_byte_identical_across_appends(tmp_path):
    """Appending a run must not rewrite a definition table."""
    path = tmp_path / "d.parquet"
    writer = ParquetWriter(path)
    writer.write(build_db(counts=(1, 2, 3), tag="a"), run_id="r0")

    def snapshot():
        return {name: (path / name / "data.parquet").read_bytes()
                for name in sch.DEFINITION_TABLES}

    before = snapshot()
    mtimes = {name: os.stat(path / name / "data.parquet").st_mtime_ns
              for name in sch.DEFINITION_TABLES}

    writer.write(build_db(counts=(9, 9, 9), tag="b"), run_id="r1")

    assert snapshot() == before
    assert {name: os.stat(path / name / "data.parquet").st_mtime_ns
            for name in sch.DEFINITION_TABLES} == mtimes


def test_manifest_records_every_run(multi_run):
    path, runs, _specs = multi_run
    manifest = json.loads((path / sch.MANIFEST_NAME).read_text())
    assert [r["run_id"] for r in manifest["runs"]] == list(runs)
    assert manifest["schema_version"] == sch.SCHEMA_VERSION
    assert manifest["format"] == sch.DATASET_FORMAT
    assert dataset_runs(path) == runs
    assert is_parquet_dataset(path)


@ucis_feature("X.14", level="L1")
def test_single_run_selection_reads_only_that_partition(multi_run):
    """Run selection prunes partitions rather than filtering rows."""
    path, runs, _specs = multi_run
    one = ParquetDataset(path, runs=runs[0])
    one.table("counts")
    assert one.read_count == 1

    everything = ParquetDataset(path)
    everything.table("counts")
    assert everything.read_count == len(runs)


def test_per_run_values_are_recoverable(multi_run):
    """Provenance survives: each run's own counts stay readable."""
    path, runs, specs = multi_run
    for run_id, (counts, _tag) in zip(runs, specs):
        db = ParquetUCIS(path, runs=run_id)
        assert _cvg_counts(db) == list(counts)


@ucis_feature("X.14", level="L1")
def test_merged_view_sums_across_runs(multi_run):
    path, runs, specs = multi_run
    merged = ParquetUCIS(path)
    assert merged.selected_runs == runs
    expected = [sum(c[i] for c, _t in specs) for i in range(3)]
    assert _cvg_counts(merged) == expected


def test_run_subset_merges_only_that_subset(multi_run):
    path, runs, specs = multi_run
    subset = ParquetUCIS(path, runs=[runs[0], runs[2]])
    expected = [specs[0][0][i] + specs[2][0][i] for i in range(3)]
    assert _cvg_counts(subset) == expected


@ucis_feature("H11.1", "X.8", level="L1")
def test_each_run_keeps_its_own_history(multi_run):
    path, runs, _specs = multi_run
    merged = ParquetUCIS(path)
    nodes = list(merged.historyNodes(HistoryNodeKind.TEST))
    assert [n.getLogicalName() for n in nodes] == ["test_a", "test_b", "test_c"]
    assert [n.run_id for n in nodes] == list(runs)
    assert merged.getNumTests() == len(runs)
    assert list(ParquetUCIS(path, runs=runs[1]).historyNodes(
        HistoryNodeKind.TEST))[0].getLogicalName() == "test_b"


def test_reappending_a_run_is_refused(multi_run):
    """Idempotency: re-loading a run must not double-count."""
    path, runs, _specs = multi_run
    writer = ParquetWriter(path)
    with pytest.raises(FileExistsError):
        writer.write(build_db(counts=(1, 0, 7), tag="a"), run_id=runs[0])
    assert dataset_runs(path) == runs


def test_replacing_a_run_is_explicit(multi_run):
    path, runs, specs = multi_run
    ParquetWriter(path).write(build_db(counts=(0, 0, 0), tag="a"),
                              run_id=runs[0], replace_run=True)
    expected = [specs[1][0][i] + specs[2][0][i] for i in range(3)]
    assert _cvg_counts(ParquetUCIS(path)) == expected
    assert dataset_runs(path) == runs


def test_default_run_ids_do_not_collide(tmp_path):
    path = tmp_path / "auto.parquet"
    writer = ParquetWriter(path)
    ids = [writer.write(build_db(tag=str(i))) for i in range(3)]
    assert ids == ["run-0000", "run-0001", "run-0002"]
    assert dataset_runs(path) == tuple(ids)


@ucis_feature("X.10", level="L1")
def test_mismatched_definitions_are_refused(tmp_path):
    """Schema drift is a merge error, not a silent pick of one side."""
    path = tmp_path / "drift.parquet"
    writer = ParquetWriter(path)
    writer.write(build_db(counts=(1, 2, 3), tag="a"), run_id="r0")
    with pytest.raises(DefinitionMismatch) as excinfo:
        writer.write(build_db(counts=(1, 2), tag="b"), run_id="r1")
    assert "coveritems" in str(excinfo.value)
    assert dataset_runs(path) == ("r0",)


def test_unknown_run_is_an_error(multi_run):
    path, _runs, _specs = multi_run
    with pytest.raises(DatasetError):
        ParquetUCIS(path, runs="nope")


def test_missing_manifest_is_an_error(tmp_path):
    (tmp_path / "empty").mkdir()
    with pytest.raises(DatasetError):
        ParquetDataset(tmp_path / "empty")


def _cvg_counts(db):
    """Counts of the three covergroup bins, in iteration order."""
    out = []

    def visit(scope):
        for item in scope.coverItems(CoverTypeT.CVGBIN):
            out.append(item.getCoverData().data)
        for child in scope.scopes(ScopeTypeT.ALL):
            visit(child)
    for top in db.scopes(ScopeTypeT.ALL):
        visit(top)
    return out


def test_same_shape_but_different_definitions_is_refused(tmp_path):
    """Drift with an identical row count must still be caught by value.

    The cheap row-count check would pass here; only the column comparison
    catches a renamed bin, and a merge across it would silently attribute
    counts to the wrong bin.
    """
    from covsight.core.api import CoverData, SourceT

    def db_with_bin_names(names):
        from covsight.core.mem import MemFactory
        db = MemFactory.create()
        du = db.createScope("work.m", None, 1, SourceT.SV,
                            ScopeTypeT.DU_MODULE, 0)
        inst = db.createInstance("top", None, 1, SourceT.SV,
                                 ScopeTypeT.INSTANCE, du, 0)
        cg = inst.createCovergroup("cg", None, 1, SourceT.SV)
        cp = cg.createCoverpoint("cp", None, 1, SourceT.SV)
        for name in names:
            data = CoverData(CoverTypeT.CVGBIN, 0)
            data.data = 1
            cp.createNextCover(name, data, None)
        return db

    path = tmp_path / "rename.parquet"
    writer = ParquetWriter(path)
    writer.write(db_with_bin_names(["a", "b", "c"]), run_id="r0")

    with pytest.raises(DefinitionMismatch) as excinfo:
        writer.write(db_with_bin_names(["a", "b", "RENAMED"]), run_id="r1")
    assert "name" in str(excinfo.value)
    assert dataset_runs(path) == ("r0",)
