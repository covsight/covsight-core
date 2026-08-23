"""Server-side coverage merge over a Parquet dataset.

Three models, matching the mapping doc:

1. **Virtual / query-time (primary).**  Never physically merge.  Runs are
   append-only partitions and the "merged database" is a view:
   ``GROUP BY coveritem_id`` over the selected ``run_id`` partitions.  This
   needs no code here at all -- it is what :class:`ParquetUCIS` does when more
   than one run is selected (:meth:`ParquetDataset.counts`).  :func:`virtual`
   just names it and reports what it merged.
2. **Materialized snapshot.**  :func:`materialize` -- the server-side analog of
   NCDB's merged artifact, for a downstream tool that wants one merged run.
3. **Engine-native incremental.**  Merge *is* ingestion: ClickHouse
   ``SummingMergeTree`` sums duplicate keys on compaction, so a run is just an
   ``INSERT``.  Nothing to implement -- :func:`ingest` is the same idea in
   Parquet terms (appending a partition), and the ClickHouse hook lives in the
   benchmark adapter.

Two properties matter more than speed here:

**Idempotency.**  Measurements are keyed on ``(run_id, coveritem_id)``, so
re-loading a run cannot double-count -- appending an existing ``run_id`` is
refused rather than silently added.

**Provenance.**  Query-time merge keeps every run.  A destructive local merge
answers "what is the coverage" and throws away "which test got us there";
this keeps both, which is the capability difference the benchmark should
report alongside the timing.
"""

import json
import os
import shutil

import pyarrow as pa

from covsight.core.api import HistoryNodeKind

from covsight.core.parquet import schema as sch
from covsight.core.parquet.query import ParquetDataset
from covsight.core.parquet.writer import (
    DefinitionMismatch, ParquetWriter,
)


class MergeResult:
    """What a merge produced, and from what.

    Args:
        counts: ``coveritem_id`` -> merged count.
        runs: The run ids merged.
        formal: ``coveritem_id`` -> merged formal result.
        run_id: Output run id, for a materialized merge.
        path: Output dataset path, for a materialized merge.
    """

    def __init__(self, counts, runs, formal=None, run_id=None, path=None):
        self.counts = counts
        self.runs = tuple(runs)
        self.formal = formal or {}
        self.run_id = run_id
        self.path = path

    @property
    def num_runs(self) -> int:
        return len(self.runs)

    @property
    def num_bins(self) -> int:
        return len(self.counts)

    @property
    def total_count(self) -> int:
        return sum(self.counts.values())

    def __repr__(self):
        return ("MergeResult(runs=%d, bins=%d, total=%d%s)"
                % (self.num_runs, self.num_bins, self.total_count,
                   ", run_id=%r" % self.run_id if self.run_id else ""))


# --------------------------------------------------------------------------
# 1. Virtual (query-time) merge
# --------------------------------------------------------------------------

def virtual(path, runs=None) -> MergeResult:
    """Merge *runs* of the dataset at *path* without writing anything.

    The merge is the read: no data moves, nothing is rewritten, and every run
    stays queryable afterwards.  Adding a run later re-merges nothing.
    """
    dataset = ParquetDataset(path, runs=runs)
    return MergeResult(counts=dataset.counts(), runs=dataset.selection,
                       formal=dataset.formal_data(), path=str(path))


# --------------------------------------------------------------------------
# 2. Materialized merge
# --------------------------------------------------------------------------

def materialize(path, out_path, runs=None, run_id="merged",
                *, compression="snappy", merge_name="merge") -> MergeResult:
    """Write the merge of *runs* as a single-run dataset at *out_path*.

    Definition tables are copied verbatim -- bin definitions are carried
    through a merge, never merged -- so only the narrow measurement tables are
    recomputed.  That separation is the whole reason ``counts`` is its own
    table.

    Args:
        path: Source dataset.
        out_path: Destination dataset.  Must not be the source.
        runs: Runs to merge; None for all.
        run_id: Run id of the single output partition.
        compression: Parquet codec for the output.
        merge_name: Logical name of the merge history node.

    Returns:
        A :class:`MergeResult` describing the snapshot.

    Raises:
        ValueError: If *out_path* is the source dataset.
    """
    source = ParquetDataset(path, runs=runs)
    if os.path.abspath(str(out_path)) == os.path.abspath(str(path)):
        raise ValueError(
            "materialize() must write to a different dataset than it reads; "
            "merging in place would destroy the per-run provenance that makes "
            "query-time merge worth having")

    writer = ParquetWriter(out_path, compression=compression)
    os.makedirs(str(out_path), exist_ok=True)

    # Definitions: copy, do not recompute.
    defs = {name: source.table(name) for name in sch.DEFINITION_TABLES}
    writer.write_definitions(defs)

    formal = source.formal_data()

    measures = {
        # Straight from the aggregation, without a Python dict in between.
        "counts": _conform_counts(source.merged_counts_table()),
        "history_nodes": _merged_history(source, run_id, merge_name),
        "history_props": sch.empty_table("history_props"),
        "test_cover_assoc": _merged_assoc(source, run_id),
        "formal": _formal_table(formal),
    }
    writer.write_run(run_id, measures,
                     db_metadata=dict(source.db_metadata,
                                      merged_from=list(source.selection)))

    return MergeResult(counts=source.counts(), runs=source.selection,
                       formal=formal, run_id=run_id, path=str(out_path))


def _conform_counts(table: pa.Table) -> pa.Table:
    """Attach the declared schema (and its metadata) to an aggregate result."""
    schema = sch.with_metadata(sch.table_schema("counts"), "counts")
    return pa.table({"coveritem_id": table.column("coveritem_id").cast(
                         pa.int64()),
                     "count": table.column("count").cast(pa.int64())},
                    schema=schema)


def _formal_table(formal) -> pa.Table:
    schema = sch.with_metadata(sch.table_schema("formal"), "formal")
    items = sorted(formal.items())
    return pa.table({
        "coveritem_id": [k for k, _ in items],
        "status": [v.get("status") for _, v in items],
        "radius": [v.get("radius") for _, v in items],
        "witness": [v.get("witness") for _, v in items],
    }, schema=schema)


def _merged_history(source, run_id, merge_name) -> pa.Table:
    """Every source history node, plus a merge node that parents them.

    "History gains a merge node" is not bookkeeping: it is how a merged
    database records which runs it came from, and the tree is just rows.
    """
    schema = sch.with_metadata(sch.table_schema("history_nodes"),
                               "history_nodes")
    cols = {f.name: [] for f in schema}

    merge_id = "%s/merge:%s" % (run_id, merge_name)
    _append_history_row(cols, schema, {
        "node_id": merge_id,
        "parent_id": None,
        "kind": int(HistoryNodeKind.MERGE),
        "local_index": 0,
        "logical_name": merge_name,
        "physical_name": None,
        "comment": "merge of %d run(s): %s"
                   % (len(source.selection), ", ".join(source.selection)),
    })

    for i, row in enumerate(source.history_rows(), start=1):
        row = dict(row)
        row.pop(sch.RUN_PARTITION_COL, None)
        row["local_index"] = i
        # Re-parent the roots onto the merge node; keep any existing tree.
        if row.get("parent_id") is None:
            row["parent_id"] = merge_id
        _append_history_row(cols, schema, row)

    return pa.table(cols, schema=schema)


def _append_history_row(cols, schema, row):
    for field in schema:
        cols[field.name].append(row.get(field.name))


def _merged_assoc(source, run_id) -> pa.Table:
    """Union the association rows, summing counts for a repeated pair.

    A union, not a replace: two runs of the same test both contributed, and
    dropping one would misreport per-test contribution.
    """
    schema = sch.with_metadata(sch.table_schema("test_cover_assoc"),
                              "test_cover_assoc")
    merged = {}
    for row in source.assoc_rows():
        key = (row["test_id"], row["coveritem_id"])
        merged[key] = merged.get(key, 0) + (row["count"] or 0)
    items = sorted(merged.items())
    return pa.table({
        "test_id": [k[0] for k, _ in items],
        "coveritem_id": [k[1] for k, _ in items],
        "count": [v for _, v in items],
    }, schema=schema)


# --------------------------------------------------------------------------
# 2b. Compaction (rollup in place)
# --------------------------------------------------------------------------

def compact(path, runs=None, into_run_id=None, *, drop_sources=True,
            compression="zstd", compression_level=19,
            merge_name="rollup") -> MergeResult:
    """Roll several runs of a dataset down into one partition, in place.

    The operational mode this enables: individual simulations upload complete
    per-run datasets, and periodically the old ones are rolled up.  Storage
    then tracks *retained* runs rather than total runs ever uploaded, and the
    rollup itself is one more run partition -- so a rolled-up dataset is
    read, merged and rolled up again by exactly the same code paths.

    What survives and what does not:

    * **Coverage is exact.**  The rollup's counts are the type-aware merge of
      the sources, so total coverage is unchanged.  Nothing is approximated.
    * **Test↔cover associations are carried over**, so "which test covered
      this bin" still answers across the rolled-up window.
    * **Per-run count arrays are gone** once *drop_sources* is set.  "How many
      times did run r0037 specifically hit this bin" is no longer answerable.
      That is the whole point of the operation -- pass ``drop_sources=False``
      to write the rollup while keeping the sources, and lose nothing at all.

    Args:
        path: Dataset to compact, in place.
        runs: Runs to roll up; None for all.
        into_run_id: Run id of the rollup partition.  Defaults to
            ``rollup-<first>..<last>`` over the runs consumed, so the id says
            what it contains.
        drop_sources: Remove the source partitions afterwards.
        compression: Codec for the rollup partition -- defaults to zstd,
            since a rollup is by definition the at-rest copy.
        compression_level: Codec level.
        merge_name: Logical name of the merge history node.

    Returns:
        A :class:`MergeResult` for the rollup partition.

    Raises:
        ValueError: If *into_run_id* is one of the runs being rolled up, or if
            fewer than one run is selected.
    """
    source = ParquetDataset(path, runs=runs)
    selected = tuple(source.selection)
    if not selected:
        raise ValueError("no runs selected to compact in %s" % path)

    if into_run_id is None:
        into_run_id = ("rollup-%s..%s" % (selected[0], selected[-1])
                       if len(selected) > 1 else "rollup-%s" % selected[0])
    into_run_id = str(into_run_id)
    if into_run_id in selected:
        raise ValueError(
            "compaction target %r is one of the runs being rolled up; it "
            "would be deleted as a source" % into_run_id)
    existing = set(_existing_runs(path)) - set(selected)
    if into_run_id in existing:
        raise FileExistsError(
            "compaction target %r is an existing run that is not being rolled "
            "up; writing the rollup there would silently replace it"
            % into_run_id)

    formal = source.formal_data()
    measures = {
        "counts": _conform_counts(source.merged_counts_table()),
        "history_nodes": _merged_history(source, into_run_id, merge_name),
        "history_props": sch.empty_table("history_props"),
        "test_cover_assoc": _merged_assoc(source, into_run_id),
        "formal": _formal_table(formal),
    }

    writer = ParquetWriter(path, compression=compression,
                           compression_level=compression_level)
    # Write the rollup *before* dropping anything: a crash in between leaves a
    # dataset with a redundant partition, which is recoverable.  The other
    # order loses data.
    writer.write_run(into_run_id, measures)

    if drop_sources:
        for run_id in selected:
            _drop_run(path, run_id)

    return MergeResult(counts=source.counts(), runs=selected, formal=formal,
                       run_id=into_run_id, path=str(path))


def _drop_run(path, run_id):
    """Remove a run's partitions and its manifest entry."""
    for name in sch.MEASUREMENT_TABLES:
        part = os.path.join(str(path), name,
                            "%s=%s" % (sch.RUN_PARTITION_COL, run_id))
        shutil.rmtree(part, ignore_errors=True)

    manifest_path = os.path.join(str(path), sch.MANIFEST_NAME)
    with open(manifest_path) as fp:
        manifest = json.load(fp)
    manifest["runs"] = [r for r in manifest.get("runs", [])
                        if r["run_id"] != run_id]
    with open(manifest_path, "w") as fp:
        json.dump(manifest, fp, indent=2, sort_keys=True)


def compaction_plan(path, keep_recent=0, runs=None):
    """Which runs a rolling policy would compact, without doing it.

    A retention policy is usually "keep the last N runs queryable per run, roll
    everything older into one partition".  This reports that split so a caller
    can schedule it.

    Returns:
        ``(to_compact, to_keep)`` -- tuples of run ids, in dataset order.
    """
    dataset = ParquetDataset(path, runs=runs)
    selected = tuple(dataset.selection)
    if keep_recent <= 0:
        return selected, ()
    if keep_recent >= len(selected):
        return (), selected
    return selected[:-keep_recent], selected[-keep_recent:]


# --------------------------------------------------------------------------
# 3. Merge as ingestion
# --------------------------------------------------------------------------

def ingest(out_path, sources, *, compression="snappy",
           skip_existing=False) -> tuple:
    """Collect the runs of several datasets into one, as partitions.

    This is the "merge is ingestion" model: nothing is summed at write time,
    the runs simply coexist, and any subset merges on read.  Merging N runs
    costs N appends rather than N rewrites -- which is what the merge-scaling
    sweep measures.

    Args:
        out_path: Destination dataset (created or appended to).
        sources: Dataset paths to pull runs from.
        compression: Parquet codec for the output.
        skip_existing: Skip a run whose id is already present instead of
            raising.  Off by default: a silently skipped run looks exactly
            like a successful merge.

    Returns:
        The tuple of run ids now in the destination.

    Raises:
        DefinitionMismatch: If two sources disagree on bin definitions.
        FileExistsError: If a run id is already present and *skip_existing*
            is False.
    """
    writer = ParquetWriter(out_path, compression=compression)
    first = True
    for source_path in sources:
        source = ParquetDataset(source_path)
        defs = {name: source.table(name) for name in sch.DEFINITION_TABLES}
        existing = _existing_runs(out_path)
        writer.write_definitions(defs, allow_existing=not first or
                                 bool(existing))
        first = False
        for run_id in source.run_ids:
            if run_id in _existing_runs(out_path):
                if skip_existing:
                    continue
                raise FileExistsError(
                    "run_id %r from %s is already in %s -- re-ingesting it "
                    "would double-count" % (run_id, source_path, out_path))
            one = ParquetDataset(source_path, runs=run_id)
            measures = {name: _strip_run_col(one.table(name))
                        for name in sch.MEASUREMENT_TABLES}
            writer.write_run(run_id, measures,
                             db_metadata=source.db_metadata)
    return _existing_runs(out_path)


def _strip_run_col(table: pa.Table) -> pa.Table:
    """Drop the ``run_id`` column: it is the partition, not a data column."""
    if sch.RUN_PARTITION_COL in table.schema.names:
        return table.drop([sch.RUN_PARTITION_COL])
    return table


def _existing_runs(path) -> tuple:
    from covsight.core.parquet.format_detect import dataset_runs
    return dataset_runs(path)


# --------------------------------------------------------------------------
# Consistency
# --------------------------------------------------------------------------

def check_definitions(paths) -> bool:
    """Assert every dataset in *paths* shares the same bin definitions.

    Raises:
        DefinitionMismatch: On the first disagreement, naming the table and
            column -- a merge across drifted definitions is an error, not a
            silent pick of one side.
    """
    reference = None
    reference_path = None
    for path in paths:
        dataset = ParquetDataset(path)
        tables = {name: dataset.table(name) for name in sch.DEFINITION_TABLES}
        if reference is None:
            reference, reference_path = tables, path
            continue
        for name, table in tables.items():
            expected = reference[name]
            if expected.num_rows != table.num_rows:
                raise DefinitionMismatch(
                    "%s: %s has %d rows, %s has %d"
                    % (name, reference_path, expected.num_rows, path,
                       table.num_rows))
            for column in expected.schema.names:
                if expected.column(column).to_pylist() != \
                        table.column(column).to_pylist():
                    raise DefinitionMismatch(
                        "%s.%s differs between %s and %s"
                        % (name, column, reference_path, path))
    return True


def copy_dataset(src, dst):
    """Copy a dataset directory -- convenience for benchmarks and tests."""
    shutil.copytree(str(src), str(dst))
    return str(dst)
