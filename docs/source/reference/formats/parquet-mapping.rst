####################################
UCIS+ ⇄ Parquet Mapping
####################################

The **Parquet dataset** format stores a UCIS+ coverage database columnar — as
raw Parquet files on disk, or as Apache Iceberg tables — while tools keep
talking to the UCIS object API.  It is a *peer* of :doc:`ncdb`, not a
replacement: NCDB is the compact edge/at-rest format, Parquet is the
queryable/interchange one.

This page is the normative schema.  The design rationale lives in
``docs/ucis-parquet-mapping.md``; the decisions are recorded in
:file:`docs/adr/0003-parquet-backend.md`.

.. contents::
   :local:
   :depth: 3

Overview
========

A dataset is a **directory**, not a file, and it represents a *database*, not a
run.  Measurement tables carry a ``run_id`` and are partitioned by it, so N runs
coexist in one dataset and merging any subset is a query rather than a rewrite.

.. code-block:: text

   cov.parquet/
     _covsight_parquet.json                      dataset manifest
     scopes/data.parquet                         definition tables --
     coveritems/data.parquet                     run-independent, written
     cross_operands/data.parquet                 once, never rewritten when
     properties/data.parquet                     a later run is appended
     source_files/data.parquet
     counts/run_id=<rid>/data.parquet            measurement tables --
     history_nodes/run_id=<rid>/data.parquet     one partition per run
     history_props/run_id=<rid>/data.parquet
     test_cover_assoc/run_id=<rid>/data.parquet
     formal/run_id=<rid>/data.parquet

Identifying a dataset
---------------------

A dataset is recognized by the ``_covsight_parquet.json`` manifest, whose
``format`` field is ``covsight-parquet``.  Magic-byte sniffing does not apply:
the dataset is a directory, and a directory of unrelated Parquet files is not a
coverage database.

.. code-block:: python

   from covsight.core.parquet.format_detect import (
       is_parquet_dataset, dataset_runs, dataset_schema_version)

   is_parquet_dataset("cov.parquet")      # True
   dataset_runs("cov.parquet")            # ('run-0000', 'run-0001')
   dataset_schema_version("cov.parquet")  # '1.1'

Definition vs. measurement
==========================

The split between the two table groups is what makes merge cheap:

**Definition tables** describe what the bins *are* — type, name, goal, weight,
source location, hierarchy.  They are run-independent.  Appending a second run
verifies them and leaves the files untouched; a disagreement raises
``DefinitionMismatch`` rather than silently picking a side.

**Measurement tables** describe what a run *observed* — counts, history,
test↔cover associations, formal results.  A merge touches only these.

.. list-table::
   :header-rows: 1
   :widths: 26 16 58

   * - Table
     - Group
     - Contents
   * - ``scopes``
     - definition
     - The scope tree: design units, instances, coverage scopes
   * - ``coveritems``
     - definition
     - Typed cover bins within a scope
   * - ``cross_operands``
     - definition
     - The coverpoints a cross crosses, in cross order
   * - ``properties``
     - definition
     - Typed-property long tail and UCIS+ extensions (EAV)
   * - ``source_files``
     - definition
     - Per-DU file table (file ids are DU-local)
   * - ``counts``
     - measurement
     - The columnar payload: hit count per bin
   * - ``history_nodes``
     - measurement
     - Per-test-run metadata and merge nodes
   * - ``history_props``
     - measurement
     - History-node property long tail (EAV)
   * - ``test_cover_assoc``
     - measurement
     - Which tests contributed to which bins (SPARSE tier)
   * - ``formal``
     - measurement
     - Formal-verification results per assertion bin

Table schemas
=============

``scopes``
----------

.. list-table::
   :header-rows: 1
   :widths: 24 14 62

   * - Column
     - Type
     - Notes
   * - ``unique_id``
     - STRING
     - **PK** — ``UCIS_STR_UNIQUE_ID``; stable cross-store identity
   * - ``parent_id``
     - STRING
     - FK → ``scopes.unique_id``; null for a top-level scope
   * - ``dfs_ordinal``
     - INT64
     - Canonical ``ucis_ScopeIterate`` order
   * - ``scope_type``
     - INT64
     - UCIS scope-type enum.  64-bit: ``ScopeTypeT`` exceeds 32 bits
   * - ``flags``
     - INT64
     - UCIS scope flags bitfield
   * - ``name``
     - STRING
     - Local scope name, not the full path
   * - ``du_id``
     - STRING
     - Owning design unit; gives file-id locality
   * - ``source_file_id``
     - INT32
     - DU-local file id (nullable)
   * - ``source_line`` / ``source_col``
     - INT32
     - Source position (nullable)
   * - ``weight`` / ``goal``
     - INT32
     - Hot properties promoted to columns
   * - ``source``
     - INT32
     - ``SourceT`` language of the scope
   * - ``instance_du_id``
     - STRING
     - For an INSTANCE: the DU it instantiates
   * - ``promoted_props``
     - INT32
     - Which promoted properties the source backend answered

``coveritems``
--------------

.. list-table::
   :header-rows: 1
   :widths: 24 14 62

   * - Column
     - Type
     - Notes
   * - ``coveritem_id``
     - INT64
     - **PK** — dense surrogate, assigned in definition order
   * - ``scope_id``
     - STRING
     - FK → ``scopes.unique_id``
   * - ``local_index``
     - INT32
     - Position in scope; with ``scope_id`` reproduces ``ucis_CoverIterate`` order
   * - ``cover_type``
     - INT32
     - ``CVGBIN`` / ``STMTBIN`` / ``TOGGLEBIN`` / assertion bins / …
   * - ``flags``
     - INT64
     - Cover flags, including exclusion/waiver
   * - ``name``
     - STRING
     - Bin name (nullable — e.g. toggle)
   * - ``at_least`` / ``weight`` / ``goal`` / ``limit`` / ``bitlen``
     - INT32
     - Hot properties (nullable)
   * - ``source_file_id`` / ``source_line`` / ``source_col``
     - INT32
     - Source position (nullable)

``counts``
----------

Narrow on purpose: it compresses and scans independently of the definitions,
and a merge touches nothing else.

.. list-table::
   :header-rows: 1
   :widths: 24 14 62

   * - Column
     - Type
     - Notes
   * - ``coveritem_id``
     - INT64
     - FK → ``coveritems.coveritem_id``
   * - ``count``
     - INT64
     - Hit count

``cross_operands``
------------------

Which coverpoints a ``UCIS_CROSS`` scope crosses, and in what order.  A table
rather than property rows because the relation is one-to-many *and ordered*:
``operand_index`` is the ``i`` of ``ucis_GetIthCrossedCvp`` and selects which
name ``UCIS_STR_ITH_CROSSED_CVP_NAME`` reports, while ``properties`` is keyed by
``prop_id`` alone and can hold only one value per key.

.. list-table::
   :header-rows: 1
   :widths: 24 14 62

   * - Column
     - Type
     - Notes
   * - ``cross_id``
     - STRING
     - FK → ``scopes.unique_id``, the cross scope
   * - ``operand_index``
     - INT32
     - Dense from 0; the ``i`` of ``ucis_GetIthCrossedCvp``
   * - ``coverpoint_id``
     - STRING
     - FK → ``scopes.unique_id``; null when the source gave a name and no
       handle, in which case a reader must not invent a coverpoint
   * - ``name``
     - STRING
     - The crossed coverpoint's name

``UCIS_INT_NUM_CROSSED_CVPS`` is the count of these rows rather than a stored
number, so the count and the operand list cannot disagree.

``properties`` and ``history_props``
------------------------------------

The full typed-property space and every UCIS+ extension, tall/EAV, typed and
namespaced.  ``properties`` is definition-side (scopes and cover bins);
``history_props`` has the identical shape but is run-partitioned, because a
history node belongs to a run.

.. list-table::
   :header-rows: 1
   :widths: 24 14 62

   * - Column
     - Type
     - Notes
   * - ``object_id``
     - STRING
     - Scope ``unique_id``, ``coveritem_id``, or history ``node_id``
   * - ``object_kind``
     - INT8
     - scope | coveritem | history-node
   * - ``namespace``
     - STRING
     - ``ucis`` for standard properties; ``covsight``/vendor for UCIS+
   * - ``prop_id``
     - STRING
     - Property key.  In the ``ucis`` namespace this is the UCIS property enum
       constant with its ``UCIS_INT_`` / ``UCIS_STR_`` / ``UCIS_REAL_`` /
       ``UCIS_HANDLE_`` prefix removed — ``SCOPE_WEIGHT``, ``CVG_ATLEAST``.
       See "The UCIS property vocabulary" in ``docs/ucis-parquet-mapping.md``
       for where each property lives.
   * - ``prop_type``
     - INT8
     - int | int64 | real | string | handle
   * - ``i64`` / ``f64`` / ``str`` / ``handle``
     - INT64 / DOUBLE / STRING / STRING
     - Exactly one populated per row

``history_nodes``
-----------------

One row per test run or merge node, forming a tree via ``parent_id``.  Carries
the test metadata as columns (``tool_category``, ``vendor_*``, ``cmdline``,
``seed``, ``sim_time``, ``cpu_time``, ``status``, …).

The test date appears as **two** nullable columns, ``date`` (STRING) and
``date_int`` (INT64), exactly one populated: UCIS types the date as a string
property, but a backend may hold an epoch integer, and a type-sniffing
heuristic would eventually guess wrong.

``test_cover_assoc``
--------------------

Only the **SPARSE** tier materializes rows.  ALL and NEVER cost zero rows: they
are recorded as a per-scope ``covsight:assoc_tier`` property instead.

``formal``
----------

Per-assertion formal results — ``status`` (``FormalStatusT``), ``radius`` and
``witness``.  Run-partitioned, because a proof status comes *from* a run.

``source_files``
----------------

Per-DU file table.  UCIS file ids are DU-local, so the key is
``(du_id, file_id)``.

Identity and ordering
=====================

1. **``UCIS_STR_UNIQUE_ID`` is the primary key for scopes.**  UCIS handles are
   process-local pointers; the unique id is the only identity that survives a
   round trip, a merge and a backend swap.
2. **``coveritem_id`` is a dense surrogate**, assigned in definition order — not
   a row autonumber, and not a digest.  A digest of the natural key would also
   be stable, but random integers are incompressible: measured on a 1.2M-bin
   design, hashed ids cost ~20 MB of a 24 MB dataset.  Identity that must
   outlive a *definition change* uses the natural key
   ``(scope_id, local_index)``, whose columns are stored alongside.
3. **Iteration order is persisted, not re-derived.**  Columnar tables are
   unordered sets; ``scopes.dfs_ordinal`` and ``coveritems.local_index``
   reproduce the canonical UCIS iteration order exactly.

Promoted properties
===================

A hot typed property is stored as a column rather than an EAV row.  But a
column cannot express "this backend does not support this property": ``weight =
1`` and ``getIntProperty(SCOPE_WEIGHT)`` raising ``UnimplError`` are different
facts, and a faithful backend has to reproduce both.

Each promoted-property list is therefore paired with a ``promoted_props``
bitmask column — bit *i* is set iff the source backend actually answered
property *i*.  Reading is then exact: promoted column when the bit is set, EAV
row otherwise, raise if neither.  Bit positions are part of the format: append
only, never reorder.

Extensions: data, not schema forks
==================================

New coverage semantics ship as **rows in** ``properties`` **under a non-**
``ucis`` **namespace**, never as new engine-specific columns.  User attributes
become ``covsight:attr:<key>`` rows and tags become ``covsight:tag:<name>``
rows.  Old tools ignore unknown namespaces; new tools read them.

Merge semantics
===============

Merge is ``GROUP BY coveritem_id`` over the selected ``run_id`` partitions plus
a set union of associations.  It is **not** uniformly ``SUM``:

.. list-table::
   :header-rows: 1
   :widths: 46 54

   * - Object / column
     - Merge op
   * - toggle / line / branch / covergroup / FSM counts
     - ``SUM``
   * - peak-active assertion counts
     - ``MAX`` — a high-water mark is not a total
   * - formal / assertion status
     - **precedence** — a failure in any run is a failure
   * - goal / at_least / weight / name / type
     - carried through, never merged; disagreement is an error
   * - test↔cover associations, exclusions, waivers
     - ``UNION``
   * - history nodes
     - append a merge node

Correctness rules
-----------------

* **Idempotency.**  Measurements are keyed on ``(run_id, coveritem_id)``, so
  re-loading a run cannot double-count; appending an existing ``run_id`` is
  refused rather than silently added.
* **Definition consistency.**  Bins sharing an identity must agree on type,
  goal and weight.  A mismatch is tool-version schema drift and raises.
* **Provenance preserved.**  Query-time merge keeps every run, so per-test
  contribution and redundancy analysis remain answerable over data that a
  destructive merge would have discarded.
* **Cost control.**  A query-time scan grows with runs merged; bound it with
  partition pruning and periodic materialized rollups.

Neutrality rules
================

Portable Parquet/Arrow types only — INT8, INT32, INT64, DOUBLE, STRING.  No
engine-specific types (ClickHouse ``LowCardinality``, Snowflake ``VARIANT``
semantics, BigQuery-only types); dictionary encoding gives the low-cardinality
benefit portably.  The test suite asserts every column against this allowlist,
so a non-portable type added for performance fails the build.

Partitioning and sort keys beyond ``run_id`` are per-engine tuning and live in
engine configuration, not in the logical schema.

The schema is versioned (``schema_version`` in table metadata and in the
manifest) and evolves by **adding nullable columns or namespaces** — never by
repurposing a column.

.. note::

   Iceberg has no 8-bit integer, so ``INT8`` columns come back as ``INT32``
   from an Iceberg round trip.  The reader normalizes them to the declared
   schema, so the round trip compares equal rather than "equal except two
   column types".

See also
========

* :doc:`../backends/index` — how the pluggable backends relate
* :doc:`../python-api/parquet-backend` — API reference
* :doc:`../../getting-started/parquet-query` — task guide
* :doc:`ncdb` — the compact at-rest format, and when to use which
