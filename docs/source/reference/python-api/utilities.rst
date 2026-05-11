############################
Utilities
############################

.. contents::
   :local:
   :depth: 2

Visitor / Traversal
===================

Use the visitor pattern to walk the entire scope hierarchy without writing
manual recursive loops.

UCISVisitor
-----------

Override any ``visit_*`` / ``leave_*`` method you care about.  All have
default no-op implementations.

.. code-block:: python

   from covsight.core.visitors import UCISVisitor, traverse
   from covsight.core.api import CoverTypeT

   class HitCounter(UCISVisitor):
       def __init__(self):
           self.hits = 0

       def visit_cover_item(self, idx):
           data = self._scope.getCoverData(idx)
           self.hits += data.hits

   counter = HitCounter()
   traverse(db, counter)
   print("Total hits:", counter.hits)

.. autoclass:: covsight.core.visitors.visitor.UCISVisitor
   :members:
   :member-order: bysource
   :undoc-members:

traverse
--------

.. autofunction:: covsight.core.visitors.traverse.traverse

-----

Database Merge
==============

DbMerger
--------

Merges the scope trees and cover-item hit counts from a list of source
databases into a single destination database.

.. code-block:: python

   from covsight.core.mem import MemFactory
   from covsight.core.merge import DbMerger

   merged = MemFactory.create()
   DbMerger().merge(merged, [db_a, db_b, db_c])

.. autoclass:: covsight.core.merge.db_merger.DbMerger
   :members:
   :member-order: bysource
   :undoc-members:

-----

Conversion Framework
====================

The conversion framework provides a listener-based API for importing coverage
data from external formats.  Importers drive ``ConversionListener`` callbacks
to populate a UCIS database without having to know about the underlying backend.

ConversionContext
-----------------

.. autoclass:: covsight.core.conversion.context.ConversionContext
   :members:
   :member-order: bysource
   :undoc-members:

ConversionListener
------------------

.. autoclass:: covsight.core.conversion.listener.ConversionListener
   :members:
   :member-order: bysource
   :undoc-members:

-----

Format Registry
===============

FormatRegistry
--------------

Discovers database and report format plugins registered via the
``covsight.formats.db`` and ``covsight.formats.rpt`` entry-point groups.

.. code-block:: python

   from covsight.core.ext import FormatRegistry

   reg = FormatRegistry()

   # List all installed database formats
   print(list(reg.db_formats().keys()))   # ['ncdb', ...]

   # Read a database using a named format plugin
   desc = reg.get_db_format("ncdb")
   db   = desc.fmt_if.read("coverage.cdb")

.. autoclass:: covsight.core.ext.registry.FormatRegistry
   :members:
   :member-order: bysource
   :undoc-members:

FormatDescDb / FormatIfDb
-------------------------

Descriptor and interface protocol for database format plugins.

.. autoclass:: covsight.core.ext.format_db.FormatDescDb
   :members:
   :member-order: bysource
   :undoc-members:

.. autoclass:: covsight.core.ext.format_db.FormatIfDb
   :members:
   :member-order: bysource
   :undoc-members:

FormatDescRpt / FormatIfRpt
---------------------------

Descriptor and interface protocol for report format plugins.

.. autoclass:: covsight.core.ext.format_rpt.FormatDescRpt
   :members:
   :member-order: bysource
   :undoc-members:

.. autoclass:: covsight.core.ext.format_rpt.FormatIfRpt
   :members:
   :member-order: bysource
   :undoc-members:
