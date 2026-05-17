############################
UCIS Object-Oriented API
############################

The ``api`` module defines the abstract UCIS object model.  Every backend
(in-memory, NCDB) implements these interfaces.

Import from the package root:

.. code-block:: typescript

   import { UCIS, Scope, Covergroup, Coverpoint, Cross,
            CoverItem, HistoryNode, ScopeTypeT, CoverTypeT } from '@covsight/core';

.. contents::
   :local:
   :depth: 2

Core Classes
============

.. js:autoclass:: UCIS
   :members:

.. js:autoclass:: Scope
   :members:

.. js:autoclass:: Obj
   :members:

.. js:autoclass:: HistoryNode
   :members:

.. js:autoclass:: CoverItem
   :members:

Coverage Scopes
===============

.. js:autoclass:: Covergroup
   :members:

.. js:autoclass:: Coverpoint
   :members:

.. js:autoclass:: Cross
   :members:

Value Objects
=============

.. js:autoclass:: CoverData
   :members:

.. js:autoclass:: CoverIndex
   :members:

.. js:autoclass:: SourceInfo
   :members:

.. js:autoclass:: FileHandle
   :members:

.. js:autoclass:: TestData
   :members:

Enumerations
============

The TypeScript package exports two styles of enum-like constant groups:

- **Bitmask constants** (``const Object.freeze({...})``): auto-documented below
- **TypeScript native enums** (``enum``): ``HistoryNodeKind``, ``SourceT``,
  ``TestStatusT``, ``IntProperty``, ``ToggleMetricT``, ``ToggleTypeT``,
  ``ToggleDirT`` — see the TypeScript source in ``ts/src/api/enums/`` or the
  :doc:`Python API enumerations <../python-api/oo-api>` for full value listings.

.. js:autoattribute:: ScopeTypeT

.. js:autoattribute:: CoverTypeT

.. js:autoattribute:: CoverFlagsT

.. js:autoattribute:: FlagsT
