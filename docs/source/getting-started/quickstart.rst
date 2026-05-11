##########
Quickstart
##########

This page shows you how to create a coverage database, populate it with
coverage data, and save it to the NCDB format — all in a few lines of Python.

Installation
============

.. code-block:: bash

   pip install covsight-core

Creating an In-Memory Database
================================

Use :class:`~covsight.core.mem.mem_factory.MemFactory` to create a fresh
in-memory UCIS database:

.. code-block:: python

   from covsight.core.mem import MemFactory
   from covsight.core.api import (
       ScopeTypeT, CoverTypeT, CoverData, SourceT, FlagsT,
       HistoryNodeKind,
   )

   db = MemFactory.create()

Recording a Test Run
====================

Before storing coverage data, register the test run that produced it:

.. code-block:: python

   from covsight.core.api import TestStatusT

   hn = db.createHistoryNode(
       None,                    # parent (None = root)
       "my_test",               # logical name
       "my_test.cdb",           # physical name / path
       HistoryNodeKind.TEST,
   )
   hn.setTestStatus(TestStatusT.OK)

Creating a Design Hierarchy
============================

Coverage data is attached to design hierarchy scopes.  Create a design unit
and then instantiate it:

.. code-block:: python

   du = db.createScope(
       "top",           # name
       None,            # source info
       1,               # weight
       SourceT.SV,      # HDL language
       ScopeTypeT.DU_MODULE,
       FlagsT.INST_ONCE,
   )
   inst = db.createInstance("top", None, 1, SourceT.SV,
                             ScopeTypeT.INSTANCE, du, 0)

Adding Functional Coverage (Covergroup / Coverpoint)
=====================================================

.. code-block:: python

   cg  = inst.createCovergroup("packet_cg", None, 1, SourceT.SV)
   cgi = cg.createCoverInstance("packet_cg", None, 1, SourceT.SV)
   cp  = cgi.createCoverpoint("length_cp", None, 1, SourceT.SV)

   # Create a bin and record 3 hits
   idx = cp.createBin("short", None, 1, 3, CoverData(3, 1))

Reading Back Coverage
=====================

Iterate scopes and cover items using the standard API:

.. code-block:: python

   from covsight.core.api import ScopeTypeT, CoverTypeT

   for scope in db.scopes(ScopeTypeT.ALL):
       print("scope:", scope.getScopeName())
       for ci in scope.coverItems(CoverTypeT.ALL):
           print("  bin:", ci.getName(), "hits:", ci.getData().hits)

Saving to NCDB
==============

.. code-block:: python

   from covsight.core.ncdb.ncdb_writer import NcdbWriter

   NcdbWriter().write(db, "coverage.cdb")

Loading from NCDB
=================

.. code-block:: python

   from covsight.core.ncdb.ncdb_reader import NcdbReader

   db2 = NcdbReader().read("coverage.cdb")

Or use the lazy-loading wrapper, which defers parsing until data is accessed:

.. code-block:: python

   from covsight.core.ncdb.ncdb_ucis import NcdbUCIS

   db3 = NcdbUCIS("coverage.cdb")
   # File is not parsed until the first access below
   for hn in db3.historyNodes(HistoryNodeKind.TEST):
       print(hn.getLogicalName())

Merging Databases
=================

.. code-block:: python

   from covsight.core.mem import MemFactory
   from covsight.core.merge import DbMerger
   from covsight.core.ncdb.ncdb_reader import NcdbReader

   a = NcdbReader().read("run_a.cdb")
   b = NcdbReader().read("run_b.cdb")

   merged = MemFactory.create()
   DbMerger().merge(merged, [a, b])

Using the Format Registry
=========================

Third-party format plugins are discovered automatically via
``setuptools`` entry points.  Use :class:`~covsight.core.ext.registry.FormatRegistry`
to list or retrieve installed formats:

.. code-block:: python

   from covsight.core.ext import FormatRegistry

   reg = FormatRegistry()
   print("DB formats:", list(reg.db_formats().keys()))    # ['ncdb', ...]

   desc = reg.get_db_format("ncdb")
   db   = desc.fmt_if.read("coverage.cdb")

Next Steps
==========

* :doc:`../reference/python-api/index` — complete API reference
* :doc:`../reference/formats/ncdb` — NCDB binary format details
* :doc:`../reference/migration` — migrating from pyucis
