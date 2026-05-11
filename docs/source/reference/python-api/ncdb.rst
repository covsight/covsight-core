################
NCDB Python API
################

The NCDB module provides reader, writer, and lazy-loading classes for the
NCDB ``.cdb`` ZIP-based binary format.

See :doc:`../formats/ncdb` for a complete description of the file format.

.. contents::
   :local:
   :depth: 2

NcdbReader
==========

Reads an NCDB ``.cdb`` file and returns a fully populated
:class:`~covsight.core.mem.mem_ucis.MemUCIS`:

.. code-block:: python

   from covsight.core.ncdb.ncdb_reader import NcdbReader

   db = NcdbReader().read("coverage.cdb")

.. autoclass:: covsight.core.ncdb.ncdb_reader.NcdbReader
   :members:
   :member-order: bysource
   :undoc-members:

NcdbWriter
==========

Serializes a UCIS database to an NCDB ``.cdb`` file:

.. code-block:: python

   from covsight.core.ncdb.ncdb_writer import NcdbWriter

   NcdbWriter().write(db, "output.cdb")

.. autoclass:: covsight.core.ncdb.ncdb_writer.NcdbWriter
   :members:
   :member-order: bysource
   :undoc-members:

NcdbUCIS
========

A lazy-loading UCIS wrapper backed by an NCDB file.  The file is not parsed
until data is first accessed.  This is useful when only history metadata
(test names, pass/fail status) is needed without loading the full scope tree.

.. code-block:: python

   from covsight.core.ncdb.ncdb_ucis import NcdbUCIS
   from covsight.core.api import HistoryNodeKind

   db = NcdbUCIS("coverage.cdb")
   # File is not yet parsed — history is loaded on the first call below
   for hn in db.historyNodes(HistoryNodeKind.TEST):
       print(hn.getLogicalName(), hn.getTestStatus())

   # Scope tree is loaded on first scope access
   for scope in db.scopes(ScopeTypeT.ALL):
       ...

Lazy-loading units:

* **history** — loaded when ``historyNodes()`` is first called (reads
  ``history.json`` only).
* **scopes** — loaded when ``scopes()`` or any scope/coveritem method is
  first called (reads ``scope_tree.bin``, ``counts.bin``, etc.).
* **v2 history** — loaded on demand when any v2 binary-history API method
  is called (reads ``test_registry.bin``, ``test_stats.bin``, etc.).

.. autoclass:: covsight.core.ncdb.ncdb_ucis.NcdbUCIS
   :members:
   :member-order: bysource
   :undoc-members:

NcdbMerger
==========

Merges two or more NCDB files using the fast same-schema path when the
structural layout (``schema_hash``) matches, falling back to the generic
:class:`~covsight.core.merge.db_merger.DbMerger` otherwise.

.. autoclass:: covsight.core.ncdb.ncdb_merger.NcdbMerger
   :members:
   :member-order: bysource
   :undoc-members:

Format Plugin
=============

The ``NcdbFormatPlugin`` registers the NCDB format under the
``covsight.formats.db`` ``setuptools`` entry-point group, making it
available via :class:`~covsight.core.ext.registry.FormatRegistry`.

.. autoclass:: covsight.core.ncdb.format_plugin.NcdbFormatPlugin
   :members:
   :member-order: bysource
   :undoc-members:

Manifest
========

The ``manifest.json`` member of an NCDB archive.  Stores format identity,
version, and aggregate statistics.

.. autoclass:: covsight.core.ncdb.manifest.Manifest
   :members:
   :member-order: bysource
   :undoc-members:
