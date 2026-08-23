"""Phase 4 -- the DuckDB adapter (benchmark-grade).

Not a second backend: what matters is that the engine reading the same bytes
gets the *same answers* as the object API.  If these diverge, the benchmark is
comparing two different databases and its numbers mean nothing.

ClickHouse stays a manual, documented run (decision 2), so only its DDL is
checked here.
"""

import pytest

pytest.importorskip("duckdb", reason="benchmark adapter needs duckdb")

from covsight.core.api import CoverTypeT, ScopeTypeT              # noqa: E402
from covsight.core.conformance import ucis_feature
from covsight.core.parquet import ParquetUCIS                     # noqa: E402
from covsight.core.parquet import merge                           # noqa: E402
from covsight.core.parquet.duckdb_adapter import (                # noqa: E402
    DuckDbAdapter, clickhouse_ddl,
)


@ucis_feature("DB.1", level="L2")
def test_engine_merge_matches_the_object_api(multi_run):
    path, _runs, _specs = multi_run
    with DuckDbAdapter(path) as engine:
        assert engine.merged_counts() == merge.virtual(path).counts


def test_engine_run_selection_matches(multi_run):
    path, runs, _specs = multi_run
    for run_id in runs:
        with DuckDbAdapter(path, runs=run_id) as engine:
            assert engine.merged_counts() == \
                merge.virtual(path, runs=run_id).counts


def test_engine_subset_selection_matches(multi_run):
    path, runs, _specs = multi_run
    subset = [runs[0], runs[2]]
    with DuckDbAdapter(path, runs=subset) as engine:
        assert engine.merged_counts() == merge.virtual(path, runs=subset).counts


def test_coverage_percent_matches_a_walk(multi_run):
    path, _runs, _specs = multi_run
    db = ParquetUCIS(path)

    total = covered = 0
    for item in _all_bins(db):
        data = item.getCoverData()
        total += 1
        if data.data >= (data.at_least or 1):
            covered += 1

    with DuckDbAdapter(path) as engine:
        assert engine.total_bins() == total
        assert engine.covered_bins() == covered
        assert engine.coverage_percent() == pytest.approx(
            100.0 * covered / total)


def test_coverage_by_du_attributes_every_bin(multi_run):
    """Per-DU rollup: no bin may fall outside a design unit."""
    path, _runs, _specs = multi_run
    with DuckDbAdapter(path) as engine:
        rows = engine.coverage_by_du()
        assert rows
        assert all(du_id is not None for du_id, _t, _c, _p in rows)
        assert sum(total for _du, total, _c, _p in rows) == engine.total_bins()


def test_uncovered_bins_matches_a_walk(multi_run):
    path, _runs, _specs = multi_run
    db = ParquetUCIS(path)
    expected = sorted(item.getName() for item in _all_bins(db)
                      if item.getCoverData().data <
                      (item.getCoverData().at_least or 1))
    with DuckDbAdapter(path) as engine:
        assert sorted(name for _uid, name, _c in engine.uncovered_bins()) == \
            expected


def test_uncovered_bins_can_be_scoped(multi_run):
    path, _runs, _specs = multi_run
    with DuckDbAdapter(path) as engine:
        everything = engine.uncovered_bins()
        scoped = engine.uncovered_bins(scope_prefix="/top")
        assert len(scoped) <= len(everything)
        assert all(uid.startswith("/top") for uid, _n, _c in scoped)


@ucis_feature("X.14", level="L2")
def test_per_run_provenance_is_queryable(multi_run):
    """Query-time merge keeps every run -- so per-run values stay askable."""
    path, runs, specs = multi_run
    db = ParquetUCIS(path)
    bin_0 = next(item for item in _all_bins(db) if item.getName() == "bin_0")

    with DuckDbAdapter(path) as engine:
        rows = engine.per_run_counts(bin_0.coveritem_id)
    assert [run_id for run_id, _c in rows] == list(runs)
    assert [count for _r, count in rows] == [c[0] for c, _t in specs]


@ucis_feature("HL.7", level="L2")
def test_contributing_tests_are_queryable(multi_run):
    path, _runs, _specs = multi_run
    db = ParquetUCIS(path)
    bin_0 = next(item for item in _all_bins(db) if item.getName() == "bin_0")
    with DuckDbAdapter(path) as engine:
        rows = engine.contributing_tests(bin_0.coveritem_id)
    assert sorted(name for _id, name, _c in rows) == \
        ["test_a", "test_b", "test_c"]


def test_clickhouse_ddl_uses_summing_merge_tree():
    """Merge is ingest: the engine sums duplicate keys on compaction."""
    ddl = clickhouse_ddl()
    assert "SummingMergeTree" in ddl
    assert "coveritem_id" in ddl and "run_id" in ddl


def _all_bins(db):
    out = []

    def visit(scope):
        out.extend(scope.coverItems(CoverTypeT.ALL))
        for child in scope.scopes(ScopeTypeT.ALL):
            visit(child)
    for top in db.scopes(ScopeTypeT.ALL):
        visit(top)
    return out
