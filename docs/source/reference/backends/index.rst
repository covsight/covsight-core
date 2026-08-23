####################################
Backends
####################################

**covsight-core** separates the coverage *API* from coverage *storage*.  Tools
are written once against the UCIS object API and work unchanged whether the
data lives in memory, in an NCDB archive, or in a columnar dataset.

.. contents::
   :local:
   :depth: 2

The model
=========

.. code-block:: text

           Tools  (report, merge, testplan-bind, query)
             │  call only the UCIS+ API
             ▼
      ┌──────────────────────────────┐
      │   UCIS+ API facade           │  stable — tools never see storage
      ├──────────────────────────────┤
      │   Backend interface          │  open · scope walk · cover walk
      │                              │  · properties · history · merge
      ├────────┬──────────┬──────────┤
      │ MemUCIS│  NCDB    │ Parquet /│  peer backends
      │        │          │ Iceberg  │
      └────────┴──────────┴──────────┘

Every backend implements the same interface, so the choice is an operational
one — size, queryability, where the data lives — not an API one.

The three backends
==================

.. list-table::
   :header-rows: 1
   :widths: 18 20 62

   * - Backend
     - Module
     - Use it when
   * - In-memory
     - :mod:`covsight.core.mem`
     - Building a database, or as the reference oracle in tests
   * - NCDB
     - :mod:`covsight.core.ncdb`
     - Storing or shipping a database compactly; the edge/at-rest format
   * - Parquet
     - :mod:`covsight.core.parquet`
     - Querying centrally, merging many runs, interoperating with engines

In-memory (``MemUCIS``)
-----------------------

Fast random access, no persistence, full read/write.  This is where a database
is *constructed*: a simulator front end walks its coverage and calls
``createScope`` / ``createNextCover``.  It is also the oracle the other backends
are tested against.

NCDB
----

A ZIP-based binary format (``.cdb``), read/write/create.  Shape-aware encodings
— tiered test↔cover associations, vector-toggle packing — make it dramatically
the most compact option, which is what an edge format needs.  See
:doc:`../formats/ncdb`.

An archive can also hold **many runs**: the schema is written once and each run
adds only its count array, which is 1.85×–2.46× smaller than the same runs as
separate files.  ``squash()`` folds old runs into the baseline and reclaims
their arrays, with a pass-only policy so a failing run's coverage never props
up a closure number.

.. code-block:: python

   from covsight.core.ncdb.multirun import append_run
   from covsight.core.ncdb.squash import squash, POLICY_PASS_ONLY

   append_run("cov.cdb", db, run_id="nightly-042")
   NcdbReader().read("cov.cdb", runs="nightly-042")   # one run
   NcdbReader().read("cov.cdb")                       # all runs, merged
   squash("cov.cdb", keep_recent=7, policy=POLICY_PASS_ONLY)

Parquet / Iceberg
-----------------

A columnar dataset, **read-only through the object API**, written by a batch
loader.  A dataset holds N runs as ``run_id`` partitions, so merging is a query
over the selected partitions rather than a rewrite.  See
:doc:`../formats/parquet-mapping`.

The write path is deliberately a batch loader rather than the streaming
``ucis_ScopeCreate`` API — matching how coverage is actually produced and
consumed: built at the edge, queried centrally.  The format plugin therefore
declares ``Read | Write`` but **not** ``Create``, so a tool that needs to author
a database fails at open rather than deep inside a hierarchy walk.

Choosing a backend
==================

.. list-table::
   :header-rows: 1
   :widths: 40 30 30

   * - You want to…
     - Backend
     - Why
   * - Build a database from a simulator
     - in-memory, then write
     - Only the writable backends have a create path
   * - Ship a database, or keep it on disk
     - NCDB
     - Measured ~3.7× smaller than zstd Parquet on a 1.2M-bin design
   * - Merge a few runs fast
     - NCDB
     - ``NcdbMerger`` element-wise-adds aligned count arrays: 0.64 s at N=4
   * - Merge many runs
     - Parquet
     - Vectorized aggregation scales better: 1.63 s vs 1.91 s at N=16
   * - Merge *any subset*, repeatedly, with no rewrite
     - Parquet
     - Merge is a query over ``run_id`` partitions; nothing is written
   * - Ask "which run/test hit this bin" after merging
     - either
     - A multi-run `.cdb` and a Parquet dataset both keep per-run counts
   * - Push a rollup down to an engine
     - Parquet
     - Predicate and column pushdown via DuckDB/Trino/ClickHouse

Both formats can hold the same database, and conversion is lossless in both
directions, so this is not a one-way decision.

.. note::

   Two different things get called "the local merge", and they differ by two
   orders of magnitude.  :class:`~covsight.core.ncdb.ncdb_merger.NcdbMerger` is
   the real path: when the sources share a ``schema_hash`` it element-wise-adds
   the count arrays and copies the schema members verbatim, building no object
   graph at all.  :class:`~covsight.core.merge.db_merger.DbMerger` is the
   generic object-API merge that rebuilds every scope and bin — it exists to
   merge *across* schemas, and is not what a same-design regression merge
   should use.

Opening a database
==================

Backends register through the ``covsight.formats.db`` entry-point registry, so
dispatch needs no tool changes:

.. code-block:: python

   from covsight.core.ext.registry import FormatRegistry

   registry = FormatRegistry()
   sorted(registry.db_formats())          # ['ncdb', 'parquet']

   ncdb = registry.get_db_format("ncdb").fmt_if.read("cov.cdb")
   parquet = registry.get_db_format("parquet").fmt_if.read("cov.parquet")

Or directly:

.. code-block:: python

   from covsight.core.parquet import ParquetUCIS

   db = ParquetUCIS("cov.parquet")                    # all runs, merged
   one = ParquetUCIS("cov.parquet", runs="run-0003")  # a single run

Converting between backends
===========================

.. code-block:: python

   from covsight.core.ncdb.ncdb_reader import NcdbReader
   from covsight.core.parquet import ParquetWriter

   db = NcdbReader().read("cov.cdb")
   ParquetWriter("cov.parquet").write(db, run_id="nightly-042")

The writer walks *any* UCIS backend through the public object API, so the same
call converts an in-memory, NCDB or Parquet-backed database.

Capabilities
============

A backend advertises what it can represent, so a tool can check before it
starts:

.. code-block:: python

   caps = FormatRegistry().get_db_format("parquet").capabilities
   caps.lossless             # True
   caps.toggle_coverage      # True
   caps.history_nodes        # True

.. toctree::
   :hidden:

   ../formats/parquet-mapping
