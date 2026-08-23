##########
Quickstart
##########

Every example on this page is a file in ``cpp/ucis-xml/examples/``. They are
compiled and run by the test suite and their output is validated against
``ucis.xsd``, so nothing here can drift from code that works.

The smallest complete document
==============================

.. literalinclude:: ../../../../cpp/ucis-xml/examples/01_hello.cpp
   :language: cpp
   :start-after: [start:body]
   :end-before: [end:body]

Three things to notice.

**The descriptors use chained setters.** ``ux::Tool().name(...).version(...)``
rather than ``ux::Tool{.name = ..., .version = ...}``. Designated initializers
are C++20 and the floor here is C++17, so the chained form is the one that
always compiles. If your project is on C++20 the aggregate form works too — the
members are public — but no example uses it.

**The scope closes itself.** ``ux::Scope`` is move-only and RAII; the whole
``instanceCoverages`` element is rendered when the handle goes out of scope,
which is also when the memory it staged is released.

**Locations are paths, and the writer interns them.** ``declaredAt`` is where
the *instance* is declared; the ``line()`` call carries where that statement is.
Source-file ids never appear in caller code. See :doc:`coverage-kinds` for the
four locations that need a call of their own — they are the ones a reporting
tool needs to link back to source.

**Errors are checked once, at the end.** There are no exceptions, no ``abort``,
and no writes to ``stderr``. See :doc:`troubleshooting`.

Line coverage
=============

.. literalinclude:: ../../../../cpp/ucis-xml/examples/02_statement.cpp
   :language: cpp
   :start-after: [start:body]
   :end-before: [end:body]

``rtl/unused.sv`` is in the file list and referenced by nothing. That is legal
and it is the point: ``sourceFiles`` entries need no referent, so rule 1 asks
for the compile file list you already have rather than a pre-scan of your
coverage data.

The comment becomes ``contents/@nameComponent``, which is where a per-point
description belongs — UCIS has no ``@name`` on ``BIN`` itself.

A flat record list
==================

Verilator's ``coverage.dat`` is a flat list, and so is every converter's input.
Those callers should not have to reconstruct a hierarchy first:

.. literalinclude:: ../../../../cpp/ucis-xml/examples/06_pointsink.cpp
   :language: cpp
   :start-after: [start:body]
   :end-before: [end:body]

``PointSink`` opens and closes scopes as the ``scope`` field changes. The only
thing it asks is that records for one scope are contiguous — rule 2 — and
``ux::sortByScope()`` is provided for lists that are not.

Functional coverage
===================

.. literalinclude:: ../../../../cpp/ucis-xml/examples/09_fsm_assertion.cpp
   :language: cpp
   :start-after: [start:body]
   :end-before: [end:body]

Both of these are cases where the schema demands an order the caller should not
have to know: ``FSM`` wants every state before every transition, and
``ASSERTION`` wants its eight optional bins in one exact sequence. Declare them
in whatever order your tool has them.

Where to go next
================

* :doc:`contract` — the three rules, and the one genuine constraint the format
  imposes. **Read this before adopting.**
* :doc:`coverage-kinds` — one section per kind, with the XSD elements produced.
* :doc:`api` — the full reference.
