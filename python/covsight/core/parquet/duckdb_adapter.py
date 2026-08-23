"""DuckDB adapter -- benchmark-grade, not productionized.

Queries the Parquet dataset *in place* (``read_parquet`` with Hive
partitioning), so there is no load step and no second copy of the data.  Its
job is to answer the benchmark's two questions that the object API answers only
by full scan:

* **targeted query** -- "coverage for design unit X", "uncovered bins in module
  Y" -- as predicate and column pushdown rather than a walk;
* **merge** -- ``GROUP BY coveritem_id`` across ``run_id`` partitions, server
  side, with no data movement.

Deliberately thin.  This is not a second UCIS backend: correctness lives in
:mod:`covsight.core.parquet.backend`, and this module exists to show what a
columnar engine does with the same bytes.  Kept behind the ``duckdb`` extra.

ClickHouse is a manual, documented run (decision 2 of the implementation
plan) -- :func:`clickhouse_ddl` emits the DDL for it so the in-repo benchmark
job needs no service dependency.
"""

import os

from covsight.core.parquet import schema as sch
from covsight.core.parquet.format_detect import dataset_runs


def _require_duckdb():
    try:
        import duckdb
    except ImportError as exc:
        raise ImportError(
            "the DuckDB adapter needs duckdb: pip install "
            "'covsight-core[duckdb]'") from exc
    return duckdb


class DuckDbAdapter:
    """Query a Parquet coverage dataset with DuckDB.

    Args:
        path: Dataset directory.
        runs: Runs to expose; None for all.  Applied as a ``run_id`` predicate
            so it prunes partitions instead of filtering rows.

    Example:
        >>> with DuckDbAdapter("cov.parquet") as db:
        ...     db.coverage_by_du()
    """

    def __init__(self, path, runs=None):
        self._duckdb = _require_duckdb()
        self.path = str(path)
        all_runs = dataset_runs(self.path)
        if runs is None:
            self.runs = tuple(all_runs)
        elif isinstance(runs, str):
            self.runs = (runs,)
        else:
            self.runs = tuple(runs)
        self._con = None

    # -- connection --------------------------------------------------------

    @property
    def con(self):
        if self._con is None:
            self._con = self._duckdb.connect()
            self._create_views()
        return self._con

    def close(self):
        if self._con is not None:
            self._con.close()
            self._con = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def _glob(self, name):
        if name in sch.MEASUREMENT_TABLES:
            return os.path.join(self.path, name, "*", "*.parquet")
        return os.path.join(self.path, name, "*.parquet")

    def _create_views(self):
        """One view per table, reading the Parquet files where they lie."""
        for name in sch.TABLES:
            glob = self._glob(name)
            hive = "true" if name in sch.MEASUREMENT_TABLES else "false"
            if not _has_files(self.path, name):
                continue
            self._con.execute(
                "CREATE OR REPLACE VIEW %s AS SELECT * FROM read_parquet("
                "'%s', hive_partitioning=%s)" % (name, glob, hive))

        self._con.execute(
            "CREATE OR REPLACE VIEW selected_counts AS "
            "SELECT * FROM counts WHERE %s" % self._run_predicate())

        # Type-aware merge, in SQL: additive bins sum, peak-active bins take
        # the maximum.  Same rule as the Python read path -- if these two ever
        # disagree, the benchmark is comparing different answers.
        self._con.execute(
            "CREATE OR REPLACE VIEW merged_counts AS "
            "SELECT c.coveritem_id, "
            "       CASE WHEN i.cover_type = %d THEN MAX(c.count) "
            "            ELSE SUM(c.count) END AS count "
            "FROM selected_counts c "
            "JOIN coveritems i USING (coveritem_id) "
            "GROUP BY c.coveritem_id, i.cover_type" % _peak_active_type())

    def _run_predicate(self):
        if not self.runs:
            return "1=0"
        quoted = ", ".join("'%s'" % r.replace("'", "''") for r in self.runs)
        return "run_id IN (%s)" % quoted

    # -- queries -----------------------------------------------------------

    def sql(self, query, *params):
        """Run *query* and return rows as a list of tuples."""
        return self.con.execute(query, list(params)).fetchall()

    def merged_counts(self) -> dict:
        """``coveritem_id`` -> merged count, computed by the engine."""
        return {cid: count
                for cid, count in self.sql(
                    "SELECT coveritem_id, count FROM merged_counts")}

    def total_bins(self) -> int:
        return self.sql("SELECT count(*) FROM coveritems")[0][0]

    def covered_bins(self) -> int:
        """Bins whose merged count reaches ``at_least``."""
        return self.sql(
            "SELECT count(*) FROM merged_counts m "
            "JOIN coveritems i USING (coveritem_id) "
            "WHERE m.count >= coalesce(i.at_least, 1)")[0][0]

    def coverage_percent(self) -> float:
        total = self.total_bins()
        return 0.0 if not total else 100.0 * self.covered_bins() / total

    def coverage_by_du(self):
        """``(du_id, total, covered, percent)`` per design unit.

        The query the benchmark contrasts with NCDB: a rollup that touches
        four columns of three tables, not the whole database.
        """
        return self.sql(
            "SELECT s.du_id, "
            "       count(*) AS total, "
            "       sum(CASE WHEN m.count >= coalesce(i.at_least, 1) "
            "                THEN 1 ELSE 0 END) AS covered, "
            "       100.0 * sum(CASE WHEN m.count >= coalesce(i.at_least, 1) "
            "                        THEN 1 ELSE 0 END) / count(*) AS pct "
            "FROM coveritems i "
            "JOIN scopes s ON s.unique_id = i.scope_id "
            "LEFT JOIN merged_counts m USING (coveritem_id) "
            "GROUP BY s.du_id ORDER BY s.du_id")

    def uncovered_bins(self, scope_prefix=None, limit=None):
        """Uncovered bins, optionally restricted to a scope-path prefix."""
        query = ("SELECT s.unique_id, i.name, coalesce(m.count, 0) AS count "
                 "FROM coveritems i "
                 "JOIN scopes s ON s.unique_id = i.scope_id "
                 "LEFT JOIN merged_counts m USING (coveritem_id) "
                 "WHERE coalesce(m.count, 0) < coalesce(i.at_least, 1)")
        params = []
        if scope_prefix is not None:
            query += " AND s.unique_id LIKE ?"
            params.append(scope_prefix + "%")
        query += " ORDER BY s.dfs_ordinal, i.local_index"
        if limit is not None:
            query += " LIMIT %d" % int(limit)
        return self.sql(query, *params)

    def per_run_counts(self, coveritem_id):
        """``(run_id, count)`` for one bin -- the provenance a merge keeps."""
        return self.sql(
            "SELECT run_id, count FROM selected_counts "
            "WHERE coveritem_id = ? ORDER BY run_id", coveritem_id)

    def contributing_tests(self, coveritem_id):
        """Which tests hit a bin, across every selected run."""
        return self.sql(
            "SELECT a.test_id, h.logical_name, a.count "
            "FROM test_cover_assoc a "
            "LEFT JOIN history_nodes h ON h.node_id = a.test_id "
            "WHERE a.coveritem_id = ? ORDER BY a.count DESC", coveritem_id)


def _has_files(path, name):
    directory = os.path.join(path, name)
    if not os.path.isdir(directory):
        return False
    for _root, _dirs, files in os.walk(directory):
        if any(f.endswith(".parquet") for f in files):
            return True
    return False


def _peak_active_type():
    from covsight.core.api import CoverTypeT
    return int(CoverTypeT.PEAKACTIVEBIN)


# --------------------------------------------------------------------------
# ClickHouse (manual run)
# --------------------------------------------------------------------------

def clickhouse_ddl(table="counts") -> str:
    """DDL for the engine-native incremental merge model.

    ``SummingMergeTree`` sums duplicate keys during background compaction, so
    merging a run *is* inserting it.  Emitted rather than executed: the in-repo
    benchmark job takes no service dependency (decision 2), and the JSONL
    schema keeps the ClickHouse columns reserved so a manual run drops into the
    same results table.
    """
    return (
        "CREATE TABLE IF NOT EXISTS %s (\n"
        "    run_id       String,\n"
        "    coveritem_id Int64,\n"
        "    count        Int64\n"
        ") ENGINE = SummingMergeTree()\n"
        "ORDER BY (coveritem_id, run_id);\n"
        "-- Merge is ingest: INSERT each run, then read with\n"
        "--   SELECT coveritem_id, sum(count) FROM %s GROUP BY coveritem_id\n"
        "-- (or FINAL) to get the exact value before compaction settles.\n"
        % (table, table)
    )
