#!/usr/bin/env python3
"""
coverage_to_parquet.py — sibling of coverage_to_ncdb.py: import one or more
Verilator coverage.dat files into a Parquet dataset and measure it head-to-head
against NCDB on size, write, read, targeted query and merge.

Pipeline:
    coverage.dat(s) → parse (SystemC::Coverage-3) → MemUCIS → Parquet dataset
                                                            ↘ NCDB (same ingest)

**One ingest, two emitters.** The .dat parser and `build_db` are imported from
coverage_to_ncdb.py rather than reimplemented, so both formats measure the same
coverage — the fairness control that makes the size row meaningful.

Multi-run is the point: pass several .dat files and each becomes a `run_id`
partition in one dataset, which is what the merge-scaling sweep needs. Merge is
then measured two ways — query-time (`GROUP BY` over partitions) and
materialized (a physical snapshot) — against NCDB's own NcdbMerger fast path
(element-wise count addition), which is the correct baseline: db_merger.py is a
generic object-API merge and is two orders of magnitude slower.

Usage:
    python coverage_to_parquet.py <coverage.dat> [<coverage.dat> ...] \\
        [--out dataset.parquet] [--compare-ncdb] [--iceberg] \\
        [--merge-sweep 1,4,16] [--label L] [--json-line results.jsonl]
"""

import argparse
import gzip
import json
import os
import shutil
import sys
import tempfile
import time
import zipfile

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_HERE, '..', '..', 'python'))   # covsight core
sys.path.insert(0, os.path.join(_HERE, '..'))                    # bench helpers
sys.path.insert(0, _HERE)

from coverage_to_ncdb import build_db, parse_coverage_dat        # noqa: E402

from covsight.core.api import (                                 # noqa: E402
    CoverTypeT, HistoryNodeKind, ScopeTypeT,
)
from covsight.core.parquet import ParquetUCIS, ParquetWriter    # noqa: E402
from covsight.core.parquet import merge as pq_merge             # noqa: E402


# ── helpers ───────────────────────────────────────────────────────────────────

def _dir_size(path: str) -> int:
    """Total bytes under *path*, including Iceberg/manifest overhead.

    Counted whole on purpose: quoting only the data files would hide the
    metadata a real deployment pays for.
    """
    total = 0
    for root, _dirs, files in os.walk(path):
        for name in files:
            total += os.path.getsize(os.path.join(root, name))
    return total


def _peak_rss_mb() -> float:
    import resource
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024.0


class _Timer:
    def __enter__(self):
        self.t0 = time.perf_counter()
        return self

    def __exit__(self, *exc):
        self.elapsed = time.perf_counter() - self.t0


def _full_scan(db) -> tuple:
    """Iterate every scope and bin through the UCIS API — the same walk both
    backends are asked to serve, so the read row compares like with like."""
    scopes = bins = total = 0

    def visit(scope):
        nonlocal scopes, bins, total
        scopes += 1
        for item in scope.coverItems(CoverTypeT.ALL):
            bins += 1
            total += item.getCoverData().data
        for child in scope.scopes(ScopeTypeT.ALL):
            visit(child)

    for top in db.scopes(ScopeTypeT.ALL):
        visit(top)
    return scopes, bins, total


# ── Test history / per-test contributions ─────────────────────────────────────

def add_test_history(db, num_tests, hit_frac, pattern="clustered", seed=1):
    """Attach *num_tests* history nodes with per-test contributions.

    A Verilator `coverage.dat` has no per-test breakdown, so the merge-with-
    history case cannot be measured from it directly — but leaving it out means
    benchmarking merge on the cheapest possible data, with zero history nodes
    and zero contribution members. This synthesizes it.

    The hit *pattern* matters more than the hit count, because `contrib/` is
    delta-encoded to exploit spatial locality:

    * ``clustered`` — a test exercises a few modules, hitting their bins
      together. Deltas are mostly 1 and the member nearly vanishes. This is
      what real coverage looks like.
    * ``random`` — uniformly scattered hits, no locality at all. The
      pessimistic bound, and not realistic; included so a regression in the
      encoding shows up as a number rather than as silence.

    Which of the two real per-test data resembles is an empirical question this
    harness cannot settle — RTLMeter emits no per-test breakdown — so both are
    reported as bounds.
    """
    import random

    total_bins = sum(len(list(s.coverItems(CoverTypeT.ALL)))
                     for s in _dfs_scopes(db))
    rng = random.Random(seed)
    per_test = max(1, int(total_bins * hit_frac))

    for index in range(num_tests):
        db.createHistoryNode(None, "test_%04d" % index, "./run.sh",
                             HistoryNodeKind.TEST)
        for bin_index in _hit_bins(rng, total_bins, per_test, pattern):
            db.record_test_association(index, bin_index, 1)
    return db


def _dfs_scopes(db):
    from covsight.core.ncdb.dfs_util import dfs_scope_list
    return dfs_scope_list(db)


def _hit_bins(rng, total, count, pattern):
    if pattern == "random":
        return sorted(rng.sample(range(total), min(count, total)))
    # clustered: `count` hits spread over ~40 contiguous blocks
    blocks = 40
    per_block = max(1, count // blocks)
    out = set()
    for _ in range(blocks):
        start = rng.randrange(max(1, total - per_block))
        out.update(range(start, min(total, start + per_block)))
    return sorted(out)


# ── Parquet emitter ───────────────────────────────────────────────────────────

def write_parquet(dbs, path, compression, compression_level=None):
    """Write one run per database; returns (run_ids, seconds, bytes)."""
    shutil.rmtree(path, ignore_errors=True)
    writer = ParquetWriter(path, compression=compression,
                           compression_level=compression_level)
    with _Timer() as timer:
        run_ids = [writer.write(db, run_id="run-%04d" % i)
                   for i, db in enumerate(dbs)]
    return run_ids, timer.elapsed, _dir_size(path)


def write_ncdb_runs(dbs, directory):
    """One .cdb per run — NCDB's unit is a database, so N runs are N files."""
    from covsight.core.ncdb.ncdb_writer import NcdbWriter
    os.makedirs(directory, exist_ok=True)
    paths = []
    with _Timer() as timer:
        for i, db in enumerate(dbs):
            out = os.path.join(directory, "run-%04d.cdb" % i)
            NcdbWriter().write(db, out)
            paths.append(out)
    return paths, timer.elapsed, sum(os.path.getsize(p) for p in paths)


def ncdb_members(path, top=6):
    with zipfile.ZipFile(path) as zf:
        infos = sorted(zf.infolist(), key=lambda i: i.compress_size,
                       reverse=True)
    return {i.filename: i.compress_size for i in infos[:top]}


def parquet_columns(path, top=8):
    """Per-column compressed bytes — the Parquet analogue of NCDB's members."""
    import pyarrow.parquet as pq
    sizes = {}
    for root, _dirs, files in os.walk(path):
        for name in files:
            if not name.endswith(".parquet"):
                continue
            full = os.path.join(root, name)
            table = os.path.basename(os.path.dirname(full))
            if table.startswith("run_id="):
                table = os.path.basename(
                    os.path.dirname(os.path.dirname(full)))
            metadata = pq.read_metadata(full)
            for group in range(metadata.num_row_groups):
                row_group = metadata.row_group(group)
                for col in range(row_group.num_columns):
                    column = row_group.column(col)
                    key = "%s.%s" % (table, column.path_in_schema)
                    sizes[key] = sizes.get(key, 0) + column.total_compressed_size
    return dict(sorted(sizes.items(), key=lambda kv: -kv[1])[:top])


# ── measurements ──────────────────────────────────────────────────────────────

def measure_query(path):
    """Targeted query via DuckDB: predicate/column pushdown, not a full walk."""
    try:
        from covsight.core.parquet.duckdb_adapter import DuckDbAdapter
    except ImportError:
        return None
    with DuckDbAdapter(path) as engine:
        with _Timer() as timer:
            by_du = engine.coverage_by_du()
        rollup_s = timer.elapsed
        with _Timer() as timer:
            uncovered = engine.uncovered_bins(limit=1000)
        uncovered_s = timer.elapsed
    return {"coverage_by_du_s": round(rollup_s, 4),
            "num_dus": len(by_du),
            "uncovered_bins_s": round(uncovered_s, 4),
            "num_uncovered_sampled": len(uncovered)}


def measure_merge(path, ncdb_paths, out_dir):
    """Merge, three ways: query-time, materialized, and NCDB's local merge."""
    result = {}

    with _Timer() as timer:
        virtual = pq_merge.virtual(path)
    result["parquet_virtual_s"] = round(timer.elapsed, 4)
    result["merged_bins"] = virtual.num_bins
    result["merged_total"] = virtual.total_count

    snapshot = os.path.join(out_dir, "merged.parquet")
    shutil.rmtree(snapshot, ignore_errors=True)
    with _Timer() as timer:
        pq_merge.materialize(path, snapshot)
    result["parquet_materialized_s"] = round(timer.elapsed, 4)
    result["parquet_materialized_bytes"] = _dir_size(snapshot)

    if ncdb_paths and len(ncdb_paths) > 1:
        from covsight.core.ncdb.ncdb_merger import NcdbMerger
        merged = os.path.join(out_dir, "merged.cdb")
        with _Timer() as timer:
            NcdbMerger().merge(ncdb_paths, merged)
        result["ncdb_merge_s"] = round(timer.elapsed, 4)
        result["ncdb_merged_bytes"] = os.path.getsize(merged)

    # Capability difference, not a speed number.  NCDB's merge collapses the
    # per-run count arrays into one; the query-time merge leaves every run's
    # counts in place, so per-run questions stay answerable afterwards.
    result["retains_per_run_counts"] = {"parquet": True, "ncdb": False}
    return result


def measure_iceberg(path, out_dir):
    try:
        from covsight.core.parquet import iceberg
    except ImportError:
        return None
    warehouse = os.path.join(out_dir, "warehouse")
    shutil.rmtree(warehouse, ignore_errors=True)
    try:
        catalog = iceberg.ephemeral_catalog(warehouse)
        with _Timer() as timer:
            iceberg.to_iceberg(path, catalog)
        return {"commit_s": round(timer.elapsed, 4),
                "bytes": _dir_size(warehouse)}
    except ImportError:
        return None


# ── main ──────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dat', nargs='+', help='Verilator coverage.dat file(s); '
                                           'each becomes one run partition')
    ap.add_argument('--out', help='write the Parquet dataset here (kept)')
    ap.add_argument('--compare-ncdb', action='store_true',
                    help='emit NCDB from the same ingest and compare')
    ap.add_argument('--iceberg', action='store_true',
                    help='also measure an ephemeral-Iceberg commit')
    ap.add_argument('--tests', type=int, default=0,
                    help='synthesize N test history nodes with per-test '
                         'contributions, so merge-with-history is measured '
                         'rather than assumed')
    ap.add_argument('--test-hit-frac', type=float, default=0.05,
                    help='fraction of bins each synthetic test contributes to')
    ap.add_argument('--test-pattern', choices=('clustered', 'random'),
                    default='clustered',
                    help='hit locality: clustered is realistic, random is the '
                         'pessimistic bound for the delta encoding')
    ap.add_argument('--oo-merge', action='store_true',
                    help='also time the generic object-API merge (db_merger); '
                         'two orders of magnitude slower, so it is opt-in')
    ap.add_argument('--merge-sweep', default='',
                    help='comma-separated run counts to sweep, e.g. 1,4,16 '
                         '(replays the inputs to reach each count)')
    ap.add_argument('--label', default='',
                    help='tag for the metrics record (e.g. Design:config:test)')
    ap.add_argument('--json-line',
                    help='append one JSON metrics record to this file')
    args = ap.parse_args()

    t0 = time.perf_counter()

    # One ingest, shared by every emitter below.
    items_per_run = [parse_coverage_dat(p) for p in args.dat]
    dbs = [build_db(items) for items in items_per_run]
    if args.tests:
        for i, db in enumerate(dbs):
            add_test_history(db, args.tests, args.test_hit_frac,
                             pattern=args.test_pattern, seed=i + 1)
    points = sum(len(i) for i in items_per_run)
    by_type = {}
    for items in items_per_run:
        for item in items:
            by_type[item['type']] = by_type.get(item['type'], 0) + 1
    instances = len({i['hier'] for items in items_per_run for i in items})
    dat_bytes = sum(os.path.getsize(p) for p in args.dat)
    dat_gz_bytes = 0
    for path in args.dat:
        with open(path, 'rb') as fp:
            dat_gz_bytes += len(gzip.compress(fp.read(), 9, mtime=0))

    print("\nparsed %s coverage points from %d file(s)"
          % (format(points, ','), len(args.dat)))
    print("  by type: " + ", ".join("%s=%s" % (k, format(v, ','))
                                    for k, v in sorted(by_type.items())))
    print("  instances (hierarchies): %s" % format(instances, ','))

    keep = args.out is not None
    tmp = tempfile.mkdtemp(prefix="cov2pq-")
    try:
        snappy_path = args.out if keep else os.path.join(tmp, "snappy.parquet")
        zstd_path = os.path.join(tmp, "zstd.parquet")

        # Both codecs, so the size comparison is not cherry-picked: snappy for
        # speed, zstd-19 as the like-for-like row against deflated NCDB.
        try:
            run_ids, snappy_s, snappy_bytes = write_parquet(
                dbs, snappy_path, "snappy")
        except Exception as exc:
            if type(exc).__name__ != "DefinitionMismatch":
                raise
            sys.exit(
                "error: the .dat files given are not runs of the same design "
                "(%s).\nRuns of one dataset must share bin definitions; pass "
                "several tests of ONE design, or pass a single .dat and use "
                "--merge-sweep to vary the run count." % exc)
        _, zstd_s, zstd_bytes = write_parquet(dbs, zstd_path, "zstd",
                                              compression_level=19)

        db = ParquetUCIS(snappy_path)
        with _Timer() as timer:
            scopes, bins, total = _full_scan(db)
        parquet_scan_s = timer.elapsed

        record = {
            "label": args.label,
            "dat": [os.path.abspath(p) for p in args.dat],
            "runs": len(dbs),
            "run_ids": run_ids,
            "points": points,
            "by_type": by_type,
            "instances": instances,
            "tests_per_run": args.tests,
            "test_hit_frac": args.test_hit_frac if args.tests else None,
            "test_pattern": args.test_pattern if args.tests else None,
            "scopes": scopes,
            "bins": bins,
            "total_count": total,
            "bytes": {
                "dat": dat_bytes,
                "dat_gz": dat_gz_bytes,
                "parquet_snappy": snappy_bytes,
                "parquet_zstd19": zstd_bytes,
            },
            "write_s": {"parquet_snappy": round(snappy_s, 3),
                        "parquet_zstd19": round(zstd_s, 3)},
            "read_s": {"parquet_full_scan": round(parquet_scan_s, 3)},
            "parquet_columns": parquet_columns(snappy_path),
            "peak_rss_mb": round(_peak_rss_mb(), 1),
            # Reserved for a manual ClickHouse run (decision 2): the columns
            # exist so that run drops into this table without a schema change.
            "clickhouse": {"ingest_s": None, "merge_s": None,
                           "bytes": None, "note": "manual run; see "
                                                  "duckdb_adapter."
                                                  "clickhouse_ddl()"},
        }

        ncdb_paths = []
        if args.compare_ncdb:
            ncdb_dir = os.path.join(tmp, "ncdb")
            ncdb_paths, ncdb_s, ncdb_bytes = write_ncdb_runs(dbs, ncdb_dir)
            from covsight.core.ncdb.ncdb_reader import NcdbReader
            with _Timer() as timer:
                for path in ncdb_paths:
                    _full_scan(NcdbReader().read(path))
            record["bytes"]["ncdb_deflated"] = ncdb_bytes
            record["write_s"]["ncdb"] = round(ncdb_s, 3)
            record["read_s"]["ncdb_full_scan"] = round(timer.elapsed, 3)
            record["ncdb_members"] = ncdb_members(ncdb_paths[0])

        record["query"] = measure_query(snappy_path)
        record["merge"] = measure_merge(snappy_path, ncdb_paths, tmp)
        if args.iceberg:
            record["iceberg"] = measure_iceberg(snappy_path, tmp)
            if record["iceberg"]:
                record["bytes"]["iceberg"] = record["iceberg"]["bytes"]

        if args.merge_sweep:
            record["merge_sweep"] = _merge_sweep(
                dbs, [int(n) for n in args.merge_sweep.split(',') if n], tmp,
                compare_ncdb=args.compare_ncdb, oo_merge=args.oo_merge)

        record["convert_s"] = round(time.perf_counter() - t0, 1)
        _report(record)

        if args.json_line:
            with open(args.json_line, 'a') as fp:
                fp.write(json.dumps(record) + "\n")
            print("  metrics appended to %s" % args.json_line)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def _merge_sweep(dbs, counts, tmp, compare_ncdb=False, oo_merge=False):
    """Merge cost as a function of run count — N appends, not N rewrites.

    Inputs are replayed to reach each N, so the sweep is honest about what it
    measures: the *shape* of merge cost against run count, on real coverage,
    not N genuinely different regressions.

    When *compare_ncdb* is set, the same N runs are also merged the local
    binary way (``db_merger``), which is the decisive architectural row: local
    merge produces an artifact and discards per-run data, query-time merge
    produces a view and keeps it.
    """
    out = {}
    for n in counts:
        path = os.path.join(tmp, "sweep-%d.parquet" % n)
        shutil.rmtree(path, ignore_errors=True)
        writer = ParquetWriter(path)
        with _Timer() as timer:
            for i in range(n):
                writer.write(dbs[i % len(dbs)], run_id="run-%04d" % i)
        ingest_s = timer.elapsed

        with _Timer() as timer:
            pq_merge.virtual(path)
        virtual_s = timer.elapsed

        snapshot = os.path.join(tmp, "sweep-%d-merged.parquet" % n)
        shutil.rmtree(snapshot, ignore_errors=True)
        with _Timer() as timer:
            pq_merge.materialize(path, snapshot)
        materialized_s = timer.elapsed

        entry = {"ingest_s": round(ingest_s, 3),
                 "virtual_merge_s": round(virtual_s, 3),
                 "materialized_merge_s": round(materialized_s, 3),
                 "bytes": _dir_size(path)}

        if compare_ncdb and n > 1:
            entry.update(_ncdb_local_merge(
                [dbs[i % len(dbs)] for i in range(n)], tmp, n,
                with_oo_merge=oo_merge) or {})

        out[str(n)] = entry
        print("  merge sweep N=%-3d ingest %.2fs  virtual %.3fs  "
              "materialized %.2fs  %s%s%.1f MB"
              % (n, ingest_s, virtual_s, materialized_s,
                 "ncdb %.3fs  " % entry["ncdb_merge_s"]
                 if entry.get("ncdb_merge_s") else "",
                 "oo-api %.2fs  " % entry["oo_api_merge_s"]
                 if entry.get("oo_api_merge_s") else "",
                 entry["bytes"] / (1024 * 1024)))
    return out


def _ncdb_local_merge(dbs, tmp, n, with_oo_merge=False):
    """Time NCDB's own merge of the same N runs.

    Two very different things get called "the local merge", and using the wrong
    one makes the comparison meaningless:

    * ``NcdbMerger`` is the real path.  When the sources share a ``schema_hash``
      it element-wise-adds the counts arrays and copies the schema members
      verbatim -- no object graph is built at all.  This is the number to
      compare against.
    * ``DbMerger`` is the generic object-API merge: it rebuilds every scope and
      bin through ``createScope``/``createNextCover``.  It works across
      *different* schemas, which is why it exists, but it is two orders of
      magnitude slower and is not what a same-design regression merge uses.

    Returns a dict of timings; ``oo_merge_s`` is only measured when asked,
    because at large N it dominates the whole benchmark run.
    """
    from covsight.core.ncdb.ncdb_merger import NcdbMerger
    from covsight.core.ncdb.ncdb_writer import NcdbWriter

    directory = os.path.join(tmp, "ncdb-sweep-%d" % n)
    shutil.rmtree(directory, ignore_errors=True)
    os.makedirs(directory, exist_ok=True)
    result = {}
    try:
        paths = []
        for i, db in enumerate(dbs):
            out = os.path.join(directory, "run-%04d.cdb" % i)
            NcdbWriter().write(db, out)
            paths.append(out)

        merged = os.path.join(directory, "merged.cdb")
        with _Timer() as timer:
            NcdbMerger().merge(paths, merged)
        result["ncdb_merge_s"] = round(timer.elapsed, 3)
        result["ncdb_merged_bytes"] = os.path.getsize(merged)

        if with_oo_merge:
            from covsight.core.mem import MemFactory
            from covsight.core.merge.db_merger import DbMerger
            from covsight.core.ncdb.ncdb_reader import NcdbReader
            sources = [NcdbReader().read(p) for p in paths]
            with _Timer() as timer:
                DbMerger().merge(MemFactory.create(), sources)
            result["oo_api_merge_s"] = round(timer.elapsed, 3)
        return result
    except Exception as exc:                                # pragma: no cover
        print("  !! NCDB merge at N=%d failed: %s" % (n, exc))
        return result or None
    finally:
        shutil.rmtree(directory, ignore_errors=True)


def _report(record):
    sizes = record["bytes"]
    baseline = sizes["dat"] or 1
    baseline_gz = sizes["dat_gz"] or 1
    labels = [
        ("Verilator coverage.dat (raw)", "dat"),
        ("coverage.dat + gzip-9", "dat_gz"),
        ("Parquet (snappy)", "parquet_snappy"),
        ("Parquet (zstd-19)", "parquet_zstd19"),
        ("Iceberg (incl. metadata)", "iceberg"),
        ("NCDB (ZIP_DEFLATED)", "ncdb_deflated"),
    ]
    print("\n  %-30s  %11s  %8s  %8s  %10s"
          % ('Format', 'Bytes', 'KB', 'vs .dat', 'vs .dat.gz'))
    print("  %s  %s  %s  %s  %s"
          % ('-' * 30, '-' * 11, '-' * 8, '-' * 8, '-' * 10))
    for label, key in labels:
        if key not in sizes or sizes[key] is None:
            continue
        size = sizes[key]
        print("  %-30s  %11s  %8.1f  %7.3fx  %9.3fx"
              % (label, format(size, ','), size / 1024,
                 size / baseline, size / baseline_gz))

    if "ncdb_deflated" in sizes:
        ratio = sizes["parquet_zstd19"] / (sizes["ncdb_deflated"] or 1)
        print("\n  like-for-like size: zstd-19 Parquet / deflated NCDB = "
              "%.2fx" % ratio)
        print("  (NCDB's shape-aware members -- tiered associations, "
              "vector-toggle -- have no Parquet equivalent.)")

    print("\n  write: " + ", ".join("%s %.2fs" % (k, v)
                                    for k, v in record["write_s"].items()))
    print("  read:  " + ", ".join("%s %.2fs" % (k, v)
                                  for k, v in record["read_s"].items()))
    merge_info = record.get("merge") or {}
    if merge_info:
        print("  merge: " + ", ".join(
            "%s %.3fs" % (k, v) for k, v in merge_info.items()
            if k.endswith("_s")))
    query = record.get("query")
    if query:
        print("  query: coverage_by_du %.3fs over %d DUs, uncovered %.3fs"
              % (query["coverage_by_du_s"], query["num_dus"],
                 query["uncovered_bins_s"]))
    print("  largest Parquet columns: " + ", ".join(
        "%s %.0fK" % (name, size / 1024)
        for name, size in list(record["parquet_columns"].items())[:6]))


if __name__ == '__main__':
    main()
