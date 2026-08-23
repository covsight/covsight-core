"""Iceberg target for the Parquet dataset -- including a disposable catalog.

Tuning and testing want a database with no server to stand up.  Three tiers,
lightest first (mapping doc, "Local & ephemeral testing"):

1. Raw Parquet, no catalog -- :mod:`covsight.core.parquet.writer`.  Tightest
   inner loop; what the default test suite and the benchmark use.
2. **Ephemeral Iceberg** -- :func:`ephemeral_catalog`: a PyIceberg
   ``SqlCatalog`` on SQLite with a ``file://`` warehouse.  Real Iceberg table
   semantics, no service, teardown is deleting a directory.  This is the tier
   CI runs.
3. REST catalog (``apache/iceberg-rest-fixture`` in Docker) --
   :func:`rest_catalog`, used only for the REST-catalog path (e.g. DuckDB
   *writes*).  Gated behind a marker so the default suite needs no Docker.

The SQLite catalog is single-writer: fine for tests, not for concurrency
benchmarks.

One portability note the round-trip has to handle: Iceberg has no 8-bit
integer, so ``int8`` columns come back as ``int32``.  :func:`to_dataset`
normalizes them back to the declared schema, which is why an
Iceberg round-trip compares equal to a raw-Parquet one rather than
"equal except for two column types".
"""

import os
import warnings

import pyarrow as pa

from covsight.core.parquet import schema as sch
from covsight.core.parquet.query import ParquetDataset
from covsight.core.parquet.writer import ParquetWriter


def _require_pyiceberg():
    try:
        import pyiceberg  # noqa: F401
    except ImportError as exc:
        raise ImportError(
            "the Iceberg target needs pyiceberg: pip install "
            "'covsight-core[iceberg]'") from exc


def ephemeral_catalog(warehouse, name="covsight"):
    """A throwaway Iceberg catalog: SQLite metadata, ``file://`` warehouse.

    Args:
        warehouse: Directory for both the catalog database and the table data.
            A pytest ``tmp_path`` is the intended argument.
        name: Catalog name.

    Returns:
        A ``pyiceberg.catalog.sql.SqlCatalog``.
    """
    _require_pyiceberg()
    from pyiceberg.catalog.sql import SqlCatalog

    warehouse = str(warehouse)
    os.makedirs(warehouse, exist_ok=True)
    return SqlCatalog(name, **{
        "uri": "sqlite:///%s" % os.path.join(warehouse, "catalog.db"),
        "warehouse": "file://%s" % warehouse,
    })


def rest_catalog(uri="http://localhost:8181", name="covsight", **properties):
    """Attach an Iceberg REST catalog (the Docker fixture, in tests)."""
    _require_pyiceberg()
    from pyiceberg.catalog.rest import RestCatalog
    return RestCatalog(name, uri=uri, **properties)


def ensure_namespace(catalog, namespace):
    """Create *namespace* if it does not exist."""
    try:
        catalog.create_namespace(namespace)
    except Exception:
        # Already present -- pyiceberg raises a catalog-specific error type.
        pass


# --------------------------------------------------------------------------
# Parquet dataset <-> Iceberg tables
# --------------------------------------------------------------------------

def to_iceberg(dataset_path, catalog, namespace="covsight", runs=None):
    """Load the dataset at *dataset_path* into Iceberg tables.

    Measurement tables are written with an explicit ``run_id`` column: Iceberg
    partitioning is engine-side tuning, and the logical schema keeps the run
    identity as data so it survives however a given engine chooses to lay it
    out.

    Args:
        dataset_path: Source Parquet dataset.
        catalog: An Iceberg catalog.
        namespace: Iceberg namespace to create the tables in.
        runs: Runs to load; None for all.

    Returns:
        ``{table name: iceberg table identifier}``.
    """
    _require_pyiceberg()
    dataset = ParquetDataset(dataset_path, runs=runs)
    ensure_namespace(catalog, namespace)

    created = {}
    for name in sch.TABLES:
        with_run = name in sch.MEASUREMENT_TABLES
        table = dataset.table(name)
        target = sch.with_metadata(
            sch.table_schema(name, with_run_id=with_run), name)
        table = _conform(table, target)

        identifier = "%s.%s" % (namespace, name)
        try:
            iceberg_table = catalog.create_table(identifier,
                                                 schema=table.schema)
        except Exception:
            iceberg_table = catalog.load_table(identifier)
        iceberg_table.append(table)
        created[name] = identifier
    return created


def to_dataset(catalog, out_path, namespace="covsight", *,
               compression="snappy"):
    """Read Iceberg tables back out as a Parquet dataset.

    Materializing back to the on-disk layout means the *same* backend reads
    both -- so "does an Iceberg round-trip lose anything" is answered by the
    same golden comparison used for raw Parquet, not by a second reader with
    its own bugs.

    Args:
        catalog: An Iceberg catalog holding tables written by :func:`to_iceberg`.
        out_path: Destination Parquet dataset.
        namespace: Iceberg namespace to read from.
        compression: Parquet codec for the output.

    Returns:
        The run ids written.
    """
    _require_pyiceberg()
    writer = ParquetWriter(out_path, compression=compression)
    os.makedirs(str(out_path), exist_ok=True)

    tables = {}
    for name in sch.TABLES:
        iceberg_table = catalog.load_table("%s.%s" % (namespace, name))
        with_run = name in sch.MEASUREMENT_TABLES
        target = sch.with_metadata(
            sch.table_schema(name, with_run_id=with_run), name)
        tables[name] = _conform(iceberg_table.scan().to_arrow(), target)

    writer.write_definitions(
        {name: tables[name].select(
            [f.name for f in sch.table_schema(name)])
         for name in sch.DEFINITION_TABLES})

    run_ids = []
    for run_id in _distinct_runs(tables):
        measures = {}
        for name in sch.MEASUREMENT_TABLES:
            table = tables[name]
            mask = pa.compute.equal(table.column(sch.RUN_PARTITION_COL),
                                    run_id)
            part = table.filter(mask).drop([sch.RUN_PARTITION_COL])
            measures[name] = _conform(
                part, sch.with_metadata(sch.table_schema(name), name))
        writer.write_run(run_id, measures)
        run_ids.append(run_id)
    return tuple(run_ids)


def _distinct_runs(tables):
    """Run ids present in the measurement tables, in first-seen order."""
    seen, ordered = set(), []
    for name in sch.MEASUREMENT_TABLES:
        column = tables[name].column(sch.RUN_PARTITION_COL)
        for run_id in column.to_pylist():
            if run_id is not None and run_id not in seen:
                seen.add(run_id)
                ordered.append(run_id)
    return ordered


def _conform(table: pa.Table, target: pa.Schema) -> pa.Table:
    """Cast *table* to *target*, column by column, ignoring column order.

    Iceberg widens ``int8`` to ``int32`` and drops schema metadata; conforming
    on the way in and out keeps those engine details from leaking into the
    logical schema (a neutrality rule, not a cosmetic one).
    """
    columns = []
    for field in target:
        if field.name in table.schema.names:
            column = table.column(field.name)
            if column.type != field.type:
                column = column.cast(field.type)
        else:
            column = pa.nulls(table.num_rows, type=field.type)
        columns.append(column)
    return pa.table(columns, schema=target)


def compact(catalog, runs, into_run_id, namespace="covsight", *,
            drop_sources=True, expire_snapshots=True):
    """Roll several runs up into one, inside the Iceberg tables.

    The workflow this serves: individual simulations ``append`` their complete
    per-run rows as they finish, and a periodic job rolls the old ones down
    into a single rollup partition.  Storage then tracks *retained* runs
    rather than every run ever uploaded.

    Iceberg is a good fit for this because it already has the two primitives
    the job needs -- an atomic ``delete`` by predicate, and snapshot isolation
    so readers keep seeing a consistent table while the rollup happens.

    .. warning::

       Iceberg is MVCC: ``delete`` writes a new snapshot but the old data files
       stay on disk, pinned by the previous snapshots, so **nothing is
       reclaimed until snapshots expire**.  This is why *expire_snapshots*
       defaults to True -- without it a compaction job increases storage.

    Args:
        catalog: An Iceberg catalog holding tables written by :func:`to_iceberg`.
        runs: Run ids to roll up.
        into_run_id: Run id for the rollup partition.
        namespace: Iceberg namespace.
        drop_sources: Delete the source runs after the rollup is committed.
        expire_snapshots: Expire old snapshots so the files are actually freed.

    Returns:
        ``{table name: rows written}`` for the rollup.

    Raises:
        ValueError: If *into_run_id* is one of the runs being rolled up.
    """
    _require_pyiceberg()
    from pyiceberg.expressions import In, EqualTo

    runs = [str(r) for r in runs]
    into_run_id = str(into_run_id)
    if into_run_id in runs:
        raise ValueError(
            "compaction target %r is one of the runs being rolled up"
            % into_run_id)

    rollup = _aggregate_runs(catalog, runs, into_run_id, namespace)

    written = {}
    for name, table in rollup.items():
        iceberg_table = catalog.load_table("%s.%s" % (namespace, name))
        if table.num_rows:
            iceberg_table.append(table)
        if drop_sources:
            # One predicate over the run column: Iceberg turns a whole-partition
            # predicate into a metadata operation rather than a rewrite.
            predicate = (EqualTo(sch.RUN_PARTITION_COL, runs[0])
                         if len(runs) == 1
                         else In(sch.RUN_PARTITION_COL, runs))
            with warnings.catch_warnings():
                # A table with no rows for these runs (an empty `formal`, say)
                # is normal, not a problem worth warning about.
                warnings.filterwarnings(
                    "ignore", message="Delete operation did not match any "
                                      "records", category=UserWarning)
                iceberg_table.delete(predicate)
        if expire_snapshots:
            iceberg_table.maintenance.expire_snapshots()
        written[name] = table.num_rows
    return written


def _aggregate_runs(catalog, runs, into_run_id, namespace):
    """Compute the rollup rows for *runs*, tagged with *into_run_id*."""
    import tempfile

    from covsight.core.parquet import merge as pq_merge

    staging = tempfile.mkdtemp(prefix="covsight-rollup-")
    try:
        to_dataset(catalog, staging, namespace=namespace)
        merged = os.path.join(staging, "_merged")
        pq_merge.materialize(staging, merged, runs=runs, run_id=into_run_id,
                             merge_name="rollup")

        rolled = ParquetDataset(merged, runs=into_run_id)
        out = {}
        for name in sch.MEASUREMENT_TABLES:
            target = sch.with_metadata(
                sch.table_schema(name, with_run_id=True), name)
            out[name] = _conform(rolled.table(name), target)
        return out
    finally:
        import shutil
        shutil.rmtree(staging, ignore_errors=True)


def write_ucis(db, catalog, namespace="covsight", run_id=None, *,
               staging=None):
    """Write a UCIS database straight into Iceberg tables as one run.

    Staged through a raw-Parquet dataset rather than built twice: the writer
    stays the single implementation of the UCIS→columnar mapping, so the
    Iceberg target cannot drift from the Parquet one.

    Args:
        db: Any UCIS database.
        catalog: An Iceberg catalog.
        namespace: Iceberg namespace.
        run_id: Run id for this write.
        staging: Directory for the intermediate dataset.  A temporary
            directory is used, and removed, when omitted.

    Returns:
        The ``run_id`` written.
    """
    import shutil
    import tempfile

    temporary = staging is None
    staging = tempfile.mkdtemp(prefix="covsight-iceberg-") if temporary \
        else str(staging)
    try:
        written = ParquetWriter(staging).write(db, run_id=run_id)
        to_iceberg(staging, catalog, namespace=namespace, runs=written)
        return written
    finally:
        if temporary:
            shutil.rmtree(staging, ignore_errors=True)
