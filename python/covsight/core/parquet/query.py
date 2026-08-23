"""Access mapping: UCIS iterators/handles over columnar batch reads.

The UCIS object API is handle-based and row-at-a-time; a columnar store wants
set-at-a-time reads.  The rule this module enforces is the one the mapping doc
states: **never one query per object**.  Each table is read at most once per
open, on first use, and indexed in memory; iterators then advance a cursor over
an already-materialized batch.

:attr:`ParquetDataset.read_count` counts physical table reads and exists for
the access-pattern regression test -- a full scope walk must issue a number of
reads that depends on the dataset's *shape* (tables × selected run partitions),
never on how many objects the walk visits.

Current granularity is whole-table: bounded, but it materializes a table on
first touch.  Row-group / subtree windowing is the next step and would only
lower the constant, not change the bound -- which is why the instrumentation
lives here rather than in the test.
"""

import json
import os

import pyarrow as pa
import pyarrow.parquet as pq

from covsight.core.parquet import schema as sch


class DatasetError(Exception):
    """The path is not a readable covsight Parquet dataset."""


class RunSelection:
    """Which runs a reader sees.

    A dataset holds N runs; a UCIS view over it is the merge of a *subset*.
    That subset is chosen here, once, at open time -- so ``run_id`` predicates
    reach the scan as partition pruning rather than post-filtering.
    """

    def __init__(self, run_ids):
        self.run_ids = tuple(run_ids)

    def __iter__(self):
        return iter(self.run_ids)

    def __len__(self):
        return len(self.run_ids)

    def __repr__(self):
        return "RunSelection(%r)" % (list(self.run_ids),)

    @classmethod
    def all(cls, dataset):
        return cls(dataset.run_ids)


class ParquetDataset:
    """Read access to a run-partitioned Parquet dataset.

    Args:
        path: Dataset directory.
        runs: Run ids to expose -- a single id, a sequence, or None for all.

    Raises:
        DatasetError: If *path* holds no dataset manifest, or names runs that
            are not present.
    """

    def __init__(self, path, runs=None):
        self.path = str(path)
        self.manifest = self._load_manifest()
        self.run_ids = tuple(r["run_id"] for r in self.manifest.get("runs", []))

        if runs is None:
            selected = self.run_ids
        elif isinstance(runs, str):
            selected = (runs,)
        else:
            selected = tuple(runs)
        missing = [r for r in selected if r not in self.run_ids]
        if missing:
            raise DatasetError(
                "run(s) %s not in dataset %s (has %s)"
                % (", ".join(map(repr, missing)), self.path,
                   ", ".join(map(repr, self.run_ids)) or "no runs"))
        self.selection = RunSelection(selected)

        self.read_count = 0
        self._tables = {}
        self._indexes = {}

    # -- manifest ---------------------------------------------------------

    def _load_manifest(self) -> dict:
        manifest_path = os.path.join(self.path, sch.MANIFEST_NAME)
        try:
            with open(manifest_path) as fp:
                manifest = json.load(fp)
        except OSError as exc:
            raise DatasetError(
                "no dataset manifest at %s" % manifest_path) from exc
        except ValueError as exc:
            raise DatasetError(
                "corrupt dataset manifest %s" % manifest_path) from exc
        if manifest.get("format") != sch.DATASET_FORMAT:
            raise DatasetError(
                "%s is not a %s dataset" % (self.path, sch.DATASET_FORMAT))
        return manifest

    @property
    def schema_version(self) -> str:
        return self.manifest.get("schema_version", "unknown")

    @property
    def db_metadata(self) -> dict:
        return self.manifest.get("db", {}) or {}

    # -- raw table access -------------------------------------------------

    def table(self, name: str) -> pa.Table:
        """Read table *name*, honouring the run selection.  Cached.

        Definition tables are read whole.  Measurement tables read only the
        selected ``run_id`` partitions and gain a ``run_id`` column, so a
        caller can tell which run a row came from (the provenance that
        query-time merge keeps and a destructive merge throws away).
        """
        if name in self._tables:
            return self._tables[name]
        if name in sch.DEFINITION_TABLES:
            table = self._read_definition(name)
        elif name in sch.MEASUREMENT_TABLES:
            table = self._read_measurement(name)
        else:
            raise KeyError("unknown table %r" % name)
        self._tables[name] = table
        return table

    def _read_definition(self, name) -> pa.Table:
        path = os.path.join(self.path, name, "data.parquet")
        if not os.path.exists(path):
            return sch.empty_table(name)
        self.read_count += 1
        return pq.read_table(path)

    def _read_measurement(self, name) -> pa.Table:
        parts = []
        for run_id in self.selection:
            path = os.path.join(
                self.path, name, "%s=%s" % (sch.RUN_PARTITION_COL, run_id),
                "data.parquet")
            if not os.path.exists(path):
                continue
            self.read_count += 1
            part = pq.read_table(path)
            run_col = pa.array([run_id] * part.num_rows, type=pa.string())
            parts.append(part.append_column(
                pa.field(sch.RUN_PARTITION_COL, pa.string(), nullable=False),
                run_col))
        if not parts:
            return sch.empty_table(name, with_run_id=True)
        return pa.concat_tables(parts)

    # -- derived indexes ---------------------------------------------------

    def _index(self, key, build):
        if key not in self._indexes:
            self._indexes[key] = build()
        return self._indexes[key]

    def scope_rows(self):
        """Scope rows as dicts in ``dfs_ordinal`` order.

        Materialized once: ``ucis_ScopeIterate`` order is defined, and this is
        where the persisted ``dfs_ordinal`` is spent instead of re-deriving it.
        """
        return self._index("scope_rows", self._build_scope_rows)

    def _build_scope_rows(self):
        table = self.table("scopes")
        rows = table.to_pylist()
        rows.sort(key=lambda r: r["dfs_ordinal"])
        return rows

    def scope_by_uid(self):
        return self._index("scope_by_uid", lambda: {
            r["unique_id"]: r for r in self.scope_rows()})

    def children_of(self):
        """``parent_id`` -> child rows, in ``dfs_ordinal`` order."""
        def build():
            out = {}
            for row in self.scope_rows():
                out.setdefault(row["parent_id"], []).append(row)
            return out
        return self._index("children", build)

    def coveritems_of(self):
        """``scope_id`` -> cover-bin rows, in ``local_index`` order."""
        def build():
            out = {}
            for row in self.table("coveritems").to_pylist():
                out.setdefault(row["scope_id"], []).append(row)
            for rows in out.values():
                rows.sort(key=lambda r: r["local_index"])
            return out
        return self._index("coveritems", build)

    def coveritem_types(self):
        """``coveritem_id`` -> ``cover_type``.  Needed to merge type-aware."""
        def build():
            table = self.table("coveritems")
            return dict(zip(table.column("coveritem_id").to_pylist(),
                            table.column("cover_type").to_pylist()))
        return self._index("coveritem_types", build)

    def counts(self):
        """``coveritem_id`` -> merged count over the selected runs.

        This *is* the virtual merge -- ``GROUP BY coveritem_id`` over the
        selected ``run_id`` partitions -- and it is type-aware: additive bins
        sum, but a peak-active count takes the maximum, because a high-water
        mark summed over 64 runs is a plausible-looking lie.

        With one run selected every op is an identity, so a single-run read and
        a merged read go down the same path.  There is no separate "merged
        database" to build.
        """
        def build():
            merged = self.merged_counts_table()
            return dict(zip(merged.column("coveritem_id").to_pylist(),
                            merged.column("count").to_pylist()))
        return self._index("counts", build)

    def merged_counts_table(self):
        """The merged counts as an Arrow table -- the aggregation itself.

        Done with Arrow's native ``group_by``, in C++, rather than a Python
        loop over the raw rows.  The difference is not cosmetic: merging 16
        runs of a 1.2M-bin design touches ~20M rows, and doing that
        row-at-a-time in Python throws away the one thing a columnar store is
        for.  Python then touches only the *aggregated* result, one entry per
        distinct bin.
        """
        def build():
            table = self.table("counts")
            if table.num_rows == 0:
                return table.select(["coveritem_id", "count"])

            peak_ids = self._non_additive_ids()
            if not peak_ids:
                return _group_sum(table)

            # Split once, aggregate each side with its own op, concatenate.
            # Non-additive bins are rare, so the fast path above is the one
            # that normally runs.
            mask = pa.compute.is_in(table.column("coveritem_id"),
                                    value_set=pa.array(sorted(peak_ids),
                                                       type=pa.int64()))
            additive = _group_sum(table.filter(pa.compute.invert(mask)))
            peaked = _group_max(table.filter(mask))
            return pa.concat_tables([additive, peaked])
        return self._index("merged_counts_table", build)

    def _non_additive_ids(self):
        """Cover bins whose merge op is not ``SUM``."""
        def build():
            coveritems = self.table("coveritems")
            ids = coveritems.column("coveritem_id").to_pylist()
            types = coveritems.column("cover_type").to_pylist()
            return {cid for cid, ctype in zip(ids, types)
                    if sch.merge_op(ctype) != sch.MERGE_SUM}
        return self._index("non_additive_ids", build)

    def formal_data(self):
        """``coveritem_id`` -> merged formal result over the selected runs.

        Status merges by the UCIS conflict rules, not by simple precedence: a
        bin proved in one run and failed in another becomes ``CONFLICT``, since
        reporting a clean proof for an assertion some run observed failing
        would be the worst available answer.  Radius takes the maximum (the
        deepest proof reached) and the witness follows the winning status.
        """
        def build():
            from covsight.core.api.enums import FormalStatusT

            by_item = {}
            for row in self.table("formal").to_pylist():
                by_item.setdefault(row["coveritem_id"], []).append(row)

            out = {}
            for cid, rows in by_item.items():
                status = sch.merge_formal_statuses(r["status"] for r in rows)
                radius = max((r["radius"] or 0) for r in rows)
                if status == int(FormalStatusT.CONFLICT):
                    # No single run won, so keep the first witness for triage.
                    witness = next((r["witness"] for r in rows
                                    if r["witness"]), None)
                else:
                    witness = next((r["witness"] for r in rows
                                    if r["status"] == status), None)
                out[cid] = {"status": status, "radius": radius or None,
                            "witness": witness}
            return out
        return self._index("formal", build)

    def counts_by_run(self):
        """``(run_id, coveritem_id)`` -> count -- per-run provenance."""
        def build():
            table = self.table("counts")
            out = {}
            runs = table.column(sch.RUN_PARTITION_COL).to_pylist()
            ids = table.column("coveritem_id").to_pylist()
            values = table.column("count").to_pylist()
            for run_id, cid, value in zip(runs, ids, values):
                out[(run_id, cid)] = value
            return out
        return self._index("counts_by_run", build)

    def properties_of(self):
        """``(object_kind, object_id)`` -> {(namespace, prop_id): row}.

        Read lazily: a walk that only touches promoted columns never pays for
        the EAV table at all.
        """
        def build():
            out = {}
            for row in self.table("properties").to_pylist():
                key = (row["object_kind"], row["object_id"])
                out.setdefault(key, {})[
                    (row["namespace"], row["prop_id"])] = row
            return out
        return self._index("properties", build)

    def history_properties_of(self):
        """Same shape as :meth:`properties_of`, for history nodes."""
        def build():
            out = {}
            for row in self.table("history_props").to_pylist():
                key = (row["object_kind"], row["object_id"])
                out.setdefault(key, {})[
                    (row["namespace"], row["prop_id"])] = row
            return out
        return self._index("history_props", build)

    def history_rows(self):
        """History-node rows, ordered by run then by in-run index."""
        def build():
            rows = self.table("history_nodes").to_pylist()
            order = {run_id: i for i, run_id in enumerate(self.selection)}
            rows.sort(key=lambda r: (order.get(r.get(sch.RUN_PARTITION_COL), 0),
                                     r["local_index"]))
            return rows
        return self._index("history_rows", build)

    def cross_operands_of(self):
        """``cross_id`` -> operand rows, in ``operand_index`` order."""
        def build():
            out = {}
            for row in self.table("cross_operands").to_pylist():
                out.setdefault(row["cross_id"], []).append(row)
            for rows in out.values():
                rows.sort(key=lambda r: r["operand_index"])
            return out
        return self._index("cross_operands", build)

    def source_path(self, du_id, file_id):
        """Resolve a DU-local file id to a path (ADR 0001)."""
        def build():
            out = {}
            for row in self.table("source_files").to_pylist():
                out[(row["du_id"], row["file_id"])] = row["path"]
            return out
        index = self._index("source_files", build)
        if file_id is None:
            return None
        return index.get((du_id or "", file_id))

    def source_paths(self):
        """Every distinct source path in the dataset, first-seen order."""
        seen, out = set(), []
        for row in self.table("source_files").to_pylist():
            if row["path"] not in seen:
                seen.add(row["path"])
                out.append(row["path"])
        return out

    def assoc_rows(self):
        """SPARSE test↔cover association rows over the selected runs."""
        return self._index(
            "assoc", lambda: self.table("test_cover_assoc").to_pylist())


def _group_sum(table: pa.Table) -> pa.Table:
    return _rename_agg(
        table.group_by("coveritem_id").aggregate([("count", "sum")]),
        "count_sum")


def _group_max(table: pa.Table) -> pa.Table:
    return _rename_agg(
        table.group_by("coveritem_id").aggregate([("count", "max")]),
        "count_max")


def _rename_agg(table: pa.Table, aggregated: str) -> pa.Table:
    """Normalize Arrow's ``count_sum``/``count_max`` back to ``count``."""
    counts = table.column(aggregated).cast(pa.int64())
    return pa.table({"coveritem_id": table.column("coveritem_id"),
                     "count": counts})


class MaskedCursor:
    """Cursor over pre-materialized rows, filtered by a UCIS type mask.

    ``ucis_NextScope`` / ``ucis_NextCover`` advance this cursor; neither
    issues a query.  Mask semantics match the in-memory backend's iterators
    (bitwise-AND against the type field).
    """

    def __init__(self, rows, mask, type_key, factory):
        self._rows = rows
        self._mask = int(mask)
        self._type_key = type_key
        self._factory = factory
        self._idx = 0

    def __iter__(self):
        return self

    def __next__(self):
        while self._idx < len(self._rows):
            row = self._rows[self._idx]
            self._idx += 1
            if int(row[self._type_key] or 0) & self._mask:
                return self._factory(row)
        raise StopIteration
