############################
NCDB Reader / Writer
############################

The ``ncdb`` module implements reading and writing the binary NCDB coverage
database format.

.. code-block:: typescript

   import { NcdbReader, NcdbWriter } from '@covsight/core/ncdb';

.. contents::
   :local:
   :depth: 1

Top-Level API
=============

.. js:autoclass:: NcdbReader
   :members:

.. js:autoclass:: NcdbWriter
   :members:

Scope Tree
==========

.. js:autoclass:: ScopeTreeReader
   :members:

.. js:autoclass:: ScopeTreeWriter
   :members:

Coverage Counts
===============

.. js:autoclass:: CountsReader
   :members:

.. js:autoclass:: CountsWriter
   :members:

History
=======

.. js:autoclass:: HistoryReader
   :members:

.. js:autoclass:: HistoryWriter
   :members:

Sources
=======

.. js:autoclass:: SourcesReader
   :members:

.. js:autoclass:: SourcesWriter
   :members:

Cross Coverage
==============

.. js:autoclass:: CrossReader
   :members:

.. js:autoclass:: CrossWriter
   :members:

Design Units
============

.. js:autoclass:: DesignUnitsReader
   :members:

.. js:autoclass:: DesignUnitsWriter
   :members:

Issues
======

.. js:autoclass:: IssueSet
   :members:

.. js:autoclass:: IssuesHistoryReader
   :members:

.. js:autoclass:: IssuesHistoryWriter
   :members:

.. js:autoclass:: IssuesMeta
   :members:

Utilities
=========

.. js:autoclass:: StringTable
   :members:

.. js:autofunction:: dfsScopes

.. js:autofunction:: readVarint

.. js:autofunction:: writeVarint
