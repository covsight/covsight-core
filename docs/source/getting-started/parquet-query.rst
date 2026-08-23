####################################
Querying Coverage with Parquet
####################################

This guide converts coverage into a columnar dataset, loads several runs into
it, queries it with SQL, and merges without rewriting anything.

Install the extra first:

.. code-block:: console

   $ pip install 'covsight-core[parquet]'          # raw Parquet
   $ pip install 'covsight-core[parquet,iceberg,duckdb]'   # + Iceberg + SQL

.. contents::
   :local:
   :depth: 2

Convert a database
==================

The writer walks any UCIS backend through the object API, so an NCDB archive,
an in-memory database or another Parquet dataset all convert the same way:

.. code-block:: python

   from covsight.core.ncdb.ncdb_reader import NcdbReader
   from covsight.core.parquet import ParquetWriter

   db = NcdbReader().read("cov.cdb")
   ParquetWriter("cov.parquet").write(db, run_id="nightly-001")

A dataset is a directory, and it holds a *database*, not a run.

Load several runs
=================

Each write appends one ``run_id`` partition.  The definition tables are written
once; a later run verifies them and leaves them untouched:

.. code-block:: python

   writer = ParquetWriter("cov.parquet")
   for i, path in enumerate(nightly_runs):
       writer.write(NcdbReader().read(path), run_id="nightly-%03d" % i)

Two guarantees fall out of this:

* Re-loading a run is **refused**, not silently double-counted — measurements
  are keyed on ``(run_id, coveritem_id)``.
* A run whose bin definitions disagree raises ``DefinitionMismatch`` instead of
  merging drifted schemas together.

Read one run, or all of them
============================

.. code-block:: python

   from covsight.core.parquet import ParquetUCIS
   from covsight.core.api import CoverTypeT, ScopeTypeT

   merged = ParquetUCIS("cov.parquet")                       # every run
   one    = ParquetUCIS("cov.parquet", runs="nightly-003")   # just one
   subset = ParquetUCIS("cov.parquet", runs=["nightly-001", "nightly-007"])

   merged.run_ids        # every run in the dataset
   merged.selected_runs  # the ones this view merges

With more than one run selected the counts read as the **merge** of those runs.
There is no merge step and no merged artifact: the merge *is* the read.  A
single-run view goes down the same path, and selecting one run prunes the other
partitions rather than reading and filtering them.

From here the plain UCIS API works unchanged:

.. code-block:: python

   for scope in merged.scopes(ScopeTypeT.ALL):
       for item in scope.coverItems(CoverTypeT.ALL):
           print(scope.getScopeName(), item.getName(),
                 item.getCoverData().data)

Query it with SQL
=================

The DuckDB adapter reads the Parquet files where they lie — no load step, no
second copy — so a rollup becomes predicate and column pushdown instead of a
full walk:

.. code-block:: python

   from covsight.core.parquet.duckdb_adapter import DuckDbAdapter

   with DuckDbAdapter("cov.parquet") as engine:
       engine.coverage_percent()
       engine.coverage_by_du()            # (du_id, total, covered, pct)
       engine.uncovered_bins(scope_prefix="/top/dut", limit=50)

The interesting queries are the ones a merged artifact can no longer answer,
because query-time merge keeps every run:

.. code-block:: python

   with DuckDbAdapter("cov.parquet") as engine:
       engine.per_run_counts(coveritem_id)     # which run hit this bin
       engine.contributing_tests(coveritem_id) # which test, across all runs

Anything else is reachable with SQL over the same views
(``scopes``, ``coveritems``, ``counts``, ``merged_counts``, …):

.. code-block:: python

   engine.sql("SELECT s.name, count(*) FROM coveritems i "
              "JOIN scopes s ON s.unique_id = i.scope_id "
              "GROUP BY s.name ORDER BY 2 DESC LIMIT 10")

Merge
=====

.. code-block:: python

   from covsight.core.parquet import merge

   # Query-time: nothing is written, every run stays queryable.
   result = merge.virtual("cov.parquet")
   result.num_runs, result.num_bins, result.total_count

   # Materialized: a physical snapshot, for a tool that wants one merged run.
   merge.materialize("cov.parquet", "snapshot.parquet", run_id="milestone-1")

   # Collect runs from separate datasets into one.  Merging N runs is N
   # appends, not N rewrites.
   merge.ingest("all.parquet", ["site-a.parquet", "site-b.parquet"])

Merging is type-aware: counts sum, but a peak-active assertion count takes the
maximum and formal status merges by precedence — a failure in any run is a
failure.  Bin definitions are carried through untouched.

Compact old runs down
=====================

Sims upload complete per-run datasets; a periodic job rolls the old ones into
one partition so storage tracks *retained* runs rather than every run ever
uploaded:

.. code-block:: python

   # "keep the last 7 runs individually queryable, roll up everything older"
   old, keep = merge.compaction_plan("cov.parquet", keep_recent=7)
   if old:
       merge.compact("cov.parquet", runs=old, into_run_id="rollup-2026-07")

The rollup is itself just another run partition, so a compacted dataset reads,
merges and compacts again through the same code.

Coverage is exact afterwards, test↔cover associations are carried over, and a
MERGE history node records which runs were consumed.  What a rollup *does* lose
is per-run counts — "how many times did run ``sim-0037`` hit this bin" is no
longer answerable.  Pass ``drop_sources=False`` to keep the sources and lose
nothing.

Measured on a 1.24M-bin design at zstd-19: 16 retained runs occupy 9.53 MB,
compact to **2.71 MB** in 1.9 s, and a "keep 7 + 1 rollup" policy holds flat at
~5.9 MB no matter how many sims have run.

The same thing inside Iceberg:

.. code-block:: python

   iceberg.compact(catalog, runs=old, into_run_id="rollup-2026-07")

.. warning::

   Iceberg is MVCC: ``delete`` writes a new snapshot, but the old data files
   stay pinned by earlier snapshots, so nothing is reclaimed until snapshots
   expire.  A compaction job without ``expire_snapshots()`` *increases*
   storage.  :func:`~covsight.core.parquet.iceberg.compact` defaults it on.

Spin up an ephemeral Iceberg catalog
====================================

For real Iceberg table semantics with no server to run — a SQLite catalog and a
``file://`` warehouse, teardown by deleting the directory:

.. code-block:: python

   from covsight.core.parquet import iceberg

   catalog = iceberg.ephemeral_catalog("/tmp/warehouse")
   iceberg.to_iceberg("cov.parquet", catalog)

   # ...and back, for the same reader to verify nothing was lost.
   iceberg.to_dataset(catalog, "roundtrip.parquet")

Use :func:`~covsight.core.parquet.iceberg.rest_catalog` when you need the REST
catalog path (DuckDB *writes* require one).

Choosing a codec
================

The writer takes any Parquet codec.  On a 1.2M-bin design, measured:

.. list-table::
   :header-rows: 1
   :widths: 28 22 22 28

   * - Codec
     - Dataset size
     - Write time
     - Use for
   * - ``snappy`` (default)
     - 9.5 MB
     - 5.8 s
     - Working datasets, fast turnaround
   * - ``zstd`` level 19
     - 2.8 MB
     - 12.5 s
     - At-rest and cloud storage

.. code-block:: python

   ParquetWriter("cov.parquet", compression="zstd",
                 compression_level=19).write(db)

.. note::

   Identity columns are stored ``DELTA_BINARY_PACKED`` rather than
   dictionary-encoded.  They are dense ascending integers, and dictionary
   encoding them cost 1.9 MB per run against 327 KB delta-encoded — more than
   the counts they identify.  This is handled automatically by the writer.

How it compares to NCDB
=======================

See :doc:`../reference/formats/ncdb` for the full comparison.  In short: for a
single run NCDB is ~3.7× smaller; from roughly eight runs onward a Parquet
dataset that keeps *every* run is both smaller than the equivalent pile of
``.cdb`` files and faster to merge, while staying queryable per run.
