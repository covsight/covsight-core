####################################
Parquet Backend
####################################

The :mod:`covsight.core.parquet` package stores a UCIS+ database as a
columnar dataset — raw Parquet, or Apache Iceberg tables — and serves it back
through the UCIS object API.

See :doc:`../formats/parquet-mapping` for the schema and
:doc:`../../getting-started/parquet-query` for a task guide.

.. contents::
   :local:
   :depth: 2

Package
=======

Names re-exported here are documented under their defining module below, so
this section shows only the package overview.

.. automodule:: covsight.core.parquet
   :no-members:

Schema
======

.. automodule:: covsight.core.parquet.schema
   :members:
   :undoc-members:

Identity
========

.. automodule:: covsight.core.parquet.identity
   :members:
   :undoc-members:

Writer
======

.. automodule:: covsight.core.parquet.writer
   :members: ParquetWriter, write_dataset, DefinitionMismatch
   :undoc-members:

Query layer
===========

.. automodule:: covsight.core.parquet.query
   :members: ParquetDataset, RunSelection, MaskedCursor, DatasetError
   :undoc-members:

Backend
=======

.. automodule:: covsight.core.parquet.backend
   :members: ParquetUCIS, ParquetScope, ParquetCoverIndex,
             ParquetHistoryNode, ParquetFileHandle, open_dataset
   :undoc-members:

Merge
=====

.. automodule:: covsight.core.parquet.merge
   :members:
   :undoc-members:

Iceberg
=======

.. automodule:: covsight.core.parquet.iceberg
   :members:
   :undoc-members:

Test coverage
=============

.. automodule:: covsight.core.parquet.test_coverage
   :members:
   :undoc-members:

DuckDB adapter
==============

.. automodule:: covsight.core.parquet.duckdb_adapter
   :members:
   :undoc-members:

Format detection and plugin
===========================

.. automodule:: covsight.core.parquet.format_detect
   :members:
   :undoc-members:

.. automodule:: covsight.core.parquet.format_plugin
   :members:
   :undoc-members:
