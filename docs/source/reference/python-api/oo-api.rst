################################
UCIS Object-Oriented API
################################

The UCIS object-oriented API provides access to coverage databases through a
hierarchy of Python abstract classes.  Every concrete backend (in-memory, NCDB,
future SQLite) implements these interfaces so that code written against the API
works with any backend.

Import all public types from the top-level package:

.. code-block:: python

   from covsight.core.api import (
       UCIS, Scope, HistoryNode, CovScope, CvgScope,
       DUScope, InstanceScope, Covergroup, Coverpoint, Cross,
       CoverItem, CoverData, SourceInfo, FileHandle, TestData,
       ScopeTypeT, CoverTypeT, CoverFlagsT, FlagsT, SourceT,
       HistoryNodeKind, TestStatusT, IntProperty, StrProperty,
       ToggleMetricT, ToggleTypeT, ToggleDirT,
   )

.. contents::
   :local:
   :depth: 2

Class Hierarchy
===============

.. code-block:: text

    Obj
    ├── UCIS (Scope)               ← database root
    │
    ├── Scope (Obj)
    │   ├── CovScope               ← code coverage base
    │   │   └── FuncCovScope       ← functional coverage base
    │   │       └── CvgScope       ← covergroup/coverpoint/cross base
    │   │           ├── Covergroup
    │   │           ├── Coverpoint
    │   │           │   └── Cross
    │   │           ├── CvgBinScope
    │   │           ├── IgnoreBinScope
    │   │           └── IllegalBinScope
    │   ├── DUScope                ← design unit definition
    │   └── InstanceScope          ← design hierarchy instance
    │
    ├── HistoryNode                ← test-run / merge record
    └── CoverItem                  ← bin (leaf coverage measurement)

    CoverData    — hit count + goal + flags for one bin
    SourceInfo   — (file, line, token) location tuple
    FileHandle   — reference to a source file
    TestData     — test metadata container
    CoverIndex   — typed index into the cover-item array

-----

Core Classes
============

UCIS
----

The database root.  Returned by :meth:`MemFactory.create()` or
:class:`~covsight.core.ncdb.ncdb_ucis.NcdbUCIS`.
Inherits all :class:`~covsight.core.api.scope.Scope` operations and adds
database-level management.

.. autoclass:: covsight.core.api.ucis.UCIS
   :members:
   :member-order: bysource
   :undoc-members:

Scope
-----

Base class for every hierarchical node.  Provides scope creation, iteration,
and cover-item management.

.. autoclass:: covsight.core.api.scope.Scope
   :members:
   :member-order: bysource
   :undoc-members:

Obj
---

Thin base class providing property accessors (``getIntProperty``,
``setIntProperty``, ``getCoverData``, etc.) shared by all UCIS objects.

.. autoclass:: covsight.core.api.obj.Obj
   :members:
   :member-order: bysource
   :undoc-members:

HistoryNode
-----------

Records one test run or merge operation.
Create via :meth:`~covsight.core.api.ucis.UCIS.createHistoryNode`;
iterate via :meth:`~covsight.core.api.ucis.UCIS.historyNodes`.

.. autoclass:: covsight.core.api.history_node.HistoryNode
   :members:
   :member-order: bysource
   :undoc-members:

-----

Coverage Scope Hierarchy
========================

CovScope
--------

Base class for generic code-coverage scopes (toggle, branch, expression,
condition, statement, FSM).

.. autoclass:: covsight.core.api.cov_scope.CovScope
   :members:
   :member-order: bysource
   :undoc-members:

CvgScope
--------

Base class for covergroup-level scopes.  Provides shared attributes such as
``at_least``, ``auto_bin_max``, and ``comment``.

.. autoclass:: covsight.core.api.cvg_scope.CvgScope
   :members:
   :member-order: bysource
   :undoc-members:

Covergroup
----------

A SystemVerilog or SystemC covergroup type definition.  Contains
coverpoints, crosses, and per-instance coverage children.

Create via :meth:`~covsight.core.api.scope.Scope.createCovergroup`.

.. autoclass:: covsight.core.api.covergroup.Covergroup
   :members:
   :member-order: bysource
   :undoc-members:

Coverpoint
----------

A coverpoint measuring coverage of a single variable or expression.
Bins are created via :meth:`~covsight.core.api.coverpoint.Coverpoint.createBin`.

Create via :meth:`~covsight.core.api.covergroup.Covergroup.createCoverpoint`.

.. autoclass:: covsight.core.api.coverpoint.Coverpoint
   :members:
   :member-order: bysource
   :undoc-members:

Cross
-----

Cross-product coverage of two or more coverpoints.

Create via :meth:`~covsight.core.api.covergroup.Covergroup.createCross`.

.. autoclass:: covsight.core.api.cross.Cross
   :members:
   :member-order: bysource
   :undoc-members:

InstanceCoverage
----------------

Per-instance coverage container returned when iterating coverage instances
within a covergroup.

.. autoclass:: covsight.core.api.instance_coverage.InstanceCoverage
   :members:
   :member-order: bysource
   :undoc-members:

-----

Design Hierarchy Scopes
=======================

DUScope
-------

Design unit (module / entity / package) definition.  Acts as a template for
instances.  Create via :meth:`~covsight.core.api.scope.Scope.createScope` with
:attr:`~covsight.core.api.enums.ScopeTypeT.DU_MODULE` (or another ``DU_*`` type).

.. autoclass:: covsight.core.api.du_scope.DUScope
   :members:
   :member-order: bysource
   :undoc-members:

InstanceScope
-------------

A design hierarchy instance.  Points back to its design unit via
:meth:`~covsight.core.api.instance_scope.InstanceScope.getInstanceDu`.
Create via :meth:`~covsight.core.api.scope.Scope.createInstance`.

.. autoclass:: covsight.core.api.instance_scope.InstanceScope
   :members:
   :member-order: bysource
   :undoc-members:

-----

Cover Items
===========

CoverItem
---------

Leaf-level coverage measurement (a bin) within a scope.

.. autoclass:: covsight.core.api.cover_item.CoverItem
   :members:
   :member-order: bysource
   :undoc-members:

CoverType
---------

Provides cover-type classification helpers on a cover item.

.. autoclass:: covsight.core.api.cover_type.CoverType
   :members:
   :member-order: bysource
   :undoc-members:

-----

Value Objects
=============

CoverData
---------

Holds the hit count, goal, and flags for one cover item.  Passed to bin
creation methods and returned by ``getCoverData()``.

.. autoclass:: covsight.core.api.cover_data.CoverData
   :members:
   :member-order: bysource
   :undoc-members:

CoverIndex
----------

Typed index pointing to a cover item within a scope's item array.

.. autoclass:: covsight.core.api.cover_index.CoverIndex
   :members:
   :member-order: bysource
   :undoc-members:

SourceInfo
----------

Bundles a :class:`~covsight.core.api.file_handle.FileHandle` with a line
number and token offset to identify where a scope or bin was declared.

.. autoclass:: covsight.core.api.source_info.SourceInfo
   :members:
   :member-order: bysource
   :undoc-members:

FileHandle
----------

Reference to a source file, created via
:meth:`~covsight.core.api.ucis.UCIS.createFileHandle`.

.. autoclass:: covsight.core.api.file_handle.FileHandle
   :members:
   :member-order: bysource
   :undoc-members:

SourceFile
----------

Metadata about a source file (name, language, etc.).

.. autoclass:: covsight.core.api.source_file.SourceFile
   :members:
   :member-order: bysource
   :undoc-members:

TestData
--------

Carries test metadata (status, tool, date, seed, …) and is passed to
:meth:`~covsight.core.api.history_node.HistoryNode.setTestData`.

.. autoclass:: covsight.core.api.test_data.TestData
   :members:
   :member-order: bysource
   :undoc-members:

StatementId
-----------

Identifies an HDL statement for statement coverage.

.. autoclass:: covsight.core.api.statement_id.StatementId
   :members:
   :member-order: bysource
   :undoc-members:

-----

Enumerations
============

ScopeTypeT
----------

Identifies the type of every scope in the hierarchy.  Used as a filter mask
in :meth:`~covsight.core.api.scope.Scope.scopes` and as a required argument to
scope creation methods.

.. autoclass:: covsight.core.api.enums.scope_type.ScopeTypeT
   :members:
   :member-order: bysource
   :undoc-members:

CoverTypeT
----------

Identifies the coverage type of every cover item (bin).  Used as a filter
mask in :meth:`~covsight.core.api.scope.Scope.coverItems`.

.. autoclass:: covsight.core.api.enums.cover_type.CoverTypeT
   :members:
   :member-order: bysource
   :undoc-members:

CoverFlagsT
-----------

Bit flags stored in a :class:`CoverData` that control data precision and
indicate which optional fields are valid.

.. autoclass:: covsight.core.api.enums.cover_flags.CoverFlagsT
   :members:
   :member-order: bysource
   :undoc-members:

FlagsT
------

Scope-level flags controlling coverage enablement, exclusion, and other
scope behaviours.  Passed as the ``flags`` argument to scope creation methods.

.. autoclass:: covsight.core.api.enums.flags.FlagsT
   :members:
   :member-order: bysource
   :undoc-members:

SourceT
-------

HDL source language of a scope (``VLOG``, ``SV``, ``VHDL``, etc.).

.. autoclass:: covsight.core.api.enums.source.SourceT
   :members:
   :member-order: bysource
   :undoc-members:

HistoryNodeKind
---------------

Discriminates test-run nodes (``TEST``) from merge nodes (``MERGE``) in the
database history tree.

.. autoclass:: covsight.core.api.enums.history_node_kind.HistoryNodeKind
   :members:
   :member-order: bysource
   :undoc-members:

TestStatusT
-----------

Pass/fail status recorded on a test history node.

.. autoclass:: covsight.core.api.enums.test_status.TestStatusT
   :members:
   :member-order: bysource
   :undoc-members:

FormalStatusT
-------------

Status code for formal verification results.

.. autoclass:: covsight.core.api.enums.formal_status.FormalStatusT
   :members:
   :member-order: bysource
   :undoc-members:

IntProperty
-----------

Integer property keys for ``getIntProperty`` / ``setIntProperty``.

.. autoclass:: covsight.core.api.enums.int_property.IntProperty
   :members:
   :member-order: bysource
   :undoc-members:

StrProperty
-----------

String property keys for ``getStrProperty`` / ``setStrProperty``.

.. autoclass:: covsight.core.api.enums.str_property.StrProperty
   :members:
   :member-order: bysource
   :undoc-members:

RealProperty
------------

Floating-point property keys for ``getRealProperty`` / ``setRealProperty``.

.. autoclass:: covsight.core.api.enums.real_property.RealProperty
   :members:
   :member-order: bysource
   :undoc-members:

HandleProperty
--------------

Handle (object reference) property keys.

.. autoclass:: covsight.core.api.enums.handle_property.HandleProperty
   :members:
   :member-order: bysource
   :undoc-members:

ToggleMetricT
-------------

Metric used for toggle coverage (``TRANSITION``, ``ENUM``, ``ASSERT``).

.. autoclass:: covsight.core.api.enums.toggle.ToggleMetricT
   :members:
   :member-order: bysource
   :undoc-members:

ToggleTypeT
-----------

Toggle type (``NET``, ``REG``, ``INTEGER``, etc.).

.. autoclass:: covsight.core.api.enums.toggle.ToggleTypeT
   :members:
   :member-order: bysource
   :undoc-members:

ToggleDirT
----------

Toggle direction filter (``INTERNAL``, ``IN``, ``OUT``, etc.).

.. autoclass:: covsight.core.api.enums.toggle.ToggleDirT
   :members:
   :member-order: bysource
   :undoc-members:
