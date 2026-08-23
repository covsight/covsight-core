###############
Coverage kinds
###############

One section per kind: the calls you make, and the XSD elements they produce.
This is the user-facing form of the completeness contract — every complex type
``ucis.xsd`` declares appears below, and ``tests/ucis_xml/test_schema.py`` reads
the types out of the schema and fails if any of them has no fixture.

Source locations
================

Linking coverage back to source is most of what a reporting tool does with a
UCIS-XML document, so it is worth being explicit about where each location comes
from. Every location is a *path plus a line*; the writer interns the path
against the ``sourceFiles`` table and you never see the id.

Most locations arrive with the fact itself — ``s.line(file, line, count)``,
``s.branch(file, line)``, ``s.condition(file, line, ...)``, ``s.block(file,
line, ...)``. Four do not, because they describe a *thing* rather than an
executable point, and each has its own call:

.. list-table::
   :header-rows: 1
   :widths: 30 30 40

   * - XSD element
     - Set with
     - Is the declaration site of
   * - ``instanceCoverages/id``
     - ``s.declaredAt(file, line)``
     - the design instance
   * - ``toggleObject/id``
     - ``s.signal(name, file, line)``
     - the signal
   * - ``CG_ID/cginstSourceId``
     - ``cg.instanceAt(file, line)``
     - the covergroup instance
   * - ``CG_ID/cgSourceId``
     - ``cg.typeAt(file, line)``
     - the covergroup type

``toggleObject/id`` is the one worth calling out: a toggle fact is about a
signal, not about a line, so there is no per-fact location to fall back on.
Without ``s.signal(name, file, line)`` every toggle object in the document
points at the same placeholder.

**File id 1 is always a synthetic** ``(unknown)`` **entry.**
``STATEMENT_ID/@file`` is a required ``positiveInteger``, so a fact with no path
needs somewhere legal to point, and inventing an entry is better than silently
attributing it to whichever file happened to be registered first. Anything
landing there in your output means a location you could have supplied and did
not — with one honest exception: a converter reading a flat record list often
genuinely does not know where an instance is declared, and should leave it
unset rather than guess.

On the flat path, ``ux::Point`` keeps the two apart. ``file``/``line`` is where
*this record's* item is — and, on a toggle record, the signal's declaration site.
``declaredAt(file, line)`` is where the enclosing *instance* is declared.
``PointSink`` declares each signal once rather than once per record, so the cost
is bounded by the signal count and not the point count.

Statement and line coverage
===========================

``s.line(file, line, count, comment)`` → ``blockCoverage/statement``, with a
``STATEMENT_ID`` and a ``BIN``. The comment becomes
``contents/@nameComponent``.

.. literalinclude:: ../../../../cpp/ucis-xml/examples/02_statement.cpp
   :language: cpp
   :start-after: [start:body]
   :end-before: [end:body]

Block and process coverage
==========================

For tools with real block coverage rather than line hits.

* ``s.block(file, line, count, name)`` → ``BLOCK``
* ``s.process(type, file, line, count, name)`` → ``PROCESS_BLOCK`` containing a
  ``BLOCK``
* ``s.childBlock(parent, ...)`` → a nested ``hierarchicalBlock``
* ``s.blockStatement(block, file, line)`` → one of ``BLOCK``'s ``statementId``
  entries

``BLOCK_COVERAGE`` is an ``xsd:choice``, so a scope emits ``process+``,
``block+`` or ``statement+`` — never a mixture. Mixing them is a contract
violation and is reported, not silently resolved.

Branch coverage
===============

``s.branch(file, line, statementType, branchExpr)`` returns a handle whose
``arm(name, count)`` adds one branch. Arms at the same location are grouped into
one ``BRANCH_STATEMENT``; each arm is a ``BRANCH`` with its own ``id`` and
``branchBin``.

``s.nestedBranch(parent.lastArm(), ...)`` nests a branch inside an arm, which
produces ``nestedBranch`` — and the writer holds the parent arm's ``branchBin``
back until after its children, as the schema's sequence requires.

.. literalinclude:: ../../../../cpp/ucis-xml/examples/03_branch.cpp
   :language: cpp
   :start-after: [start:body]
   :end-before: [end:body]

Condition and expression coverage
=================================

``s.condition(file, line, name, exprString, statementType)`` returns a handle
taking ``subExpr(text)`` and ``bin(name, count)``; ``s.nestedCondition(parent,
...)`` produces ``hierarchicalExpr``.

``EXPR`` requires ``@index`` and ``@width``, which you never supply: the index
is the expression's ordinal within the scope, assigned after sorting so it does
not depend on emission order, and the width is the sub-expression count.
``subExpr`` has ``minOccurs="1"``, so an expression with none listed gets its
own expression text as the single entry.

.. literalinclude:: ../../../../cpp/ucis-xml/examples/04_condition.cpp
   :language: cpp
   :start-after: [start:body]
   :end-before: [end:body]

Toggle coverage
===============

``s.toggle(signal, bit, edge, count)`` — with ``bit`` as either a string or an
unsigned index — produces
``toggleCoverage/toggleObject/toggleBit/toggle/bin``. Facts are grouped on
``(signal, bit)``, which is what gives the group-by-bit layout that measured
0.800× gzipped against the alternative. It is not a mode; it is the only layout
this structure can produce.

``s.toggle(signal, bit, from, to, count)`` takes explicit transition strings for
anything beyond a two-state rise and fall.

``s.signal()`` carries the per-signal detail, in three forms: the declaration
site (``name, file, line``), the bit range (``name, left, right, downto``), or
both (``name, file, line, left, right, downto``). The site becomes
``toggleObject/id`` and the range becomes ``DIMENSION``; both are optional and
independent.

.. literalinclude:: ../../../../cpp/ucis-xml/examples/05_toggle.cpp
   :language: cpp
   :start-after: [start:body]
   :end-before: [end:body]

FSM coverage
============

``s.fsm(name, type, width)`` returns a handle taking ``state(name, count,
value)`` and ``transition(...)``, in either order — the writer emits every state
before every transition, as ``FSM``'s sequence requires. A transition may name
any number of states; ``FSM_TRANSITION`` requires at least two, and the state
path's order is preserved because that order is the transition.

A transition through a state that was never declared is reported rather than
written.

Assertion coverage
==================

``s.assertion(name, kind)`` returns a handle with one method per bin:
``covers``, ``passes``, ``fails``, ``vacuous``, ``disabled``, ``attempts``,
``active``, ``peakActive``. Call them in whatever order your tool has them; the
writer emits the ones you set in the one sequence ``ASSERTION`` declares.

.. literalinclude:: ../../../../cpp/ucis-xml/examples/09_fsm_assertion.cpp
   :language: cpp
   :start-after: [start:body]
   :end-before: [end:body]

Covergroups, coverpoints and crosses
====================================

``s.covergroup(name, typeName, moduleName)`` → ``CGINSTANCE`` with its
``CG_ID``. ``cg.instanceAt(file, line)`` and ``cg.typeAt(file, line)`` fill in
``CG_ID``'s two locations, which are usually in different files — the type in a
package, the instance in the testbench that samples it. From there:

* ``cg.coverpoint(name)`` → ``COVERPOINT``
* ``cp.bin(name, from, to, count)`` → a ``COVERPOINT_BIN`` holding a
  ``RANGE_VALUE``
* ``cp.bin(name, value, count)`` — the single-value form
* ``cp.illegal(...)``, ``cp.ignore(...)``, ``cp.defaultBin(...)`` — the other
  three ``@type`` values
* ``cp.sequenceBin(name, values, n, count)`` → a ``SEQUENCE``, whose value order
  is preserved
* ``cg.cross(name, {coverpoints})`` → ``CROSS`` with its ``crossExpr`` list
* ``x.bin(name, indices, n, count)`` → ``CROSS_BIN``, whose index tuple lines up
  positionally with the cross's coverpoint list
* ``cg.parameter(name, value)`` → ``cgParms``

Coverpoints are emitted before crosses whichever order you declare them in, and
a cross naming a coverpoint that is not in its covergroup is reported.

.. literalinclude:: ../../../../cpp/ucis-xml/examples/08_cross.cpp
   :language: cpp
   :start-after: [start:body]
   :end-before: [end:body]

Options
=======

``.options(ux::Options()...)`` on a covergroup, coverpoint or cross. One struct
covers all three because ``CGINST_OPTIONS``, ``COVERPOINT_OPTIONS`` and
``CROSS_OPTIONS`` overlap heavily; each renderer emits only the attributes its
own type declares.

Attributes equal to their schema default are omitted. Note that ``COVERPOINT``,
``CROSS`` and ``CGINSTANCE`` each *require* an ``options`` child, so a fully
defaulted one still costs ten bytes — ``<options/>`` — and is never skipped.

Bin and object attributes
=========================

Every call that creates a bin returns a ``BinRef``, so the uncommon attributes
stay chainable without appearing in the common-case signature:

.. code-block:: cpp

   cp.bin("small", 0, 3, 41).exclude("waived: unreachable in this config");
   s.line("rtl/core.sv", 8, 0).alias("core.8").goal(10).weight(3)
                              .attr("column", "12", ux::AttrType::Int);

``COVERPOINT_BIN`` is the one exception in the schema: its contents live inside
``range``/``sequence`` rather than in a ``BIN``, so it carries neither
``binAttributes`` nor ``objAttributes`` — only ``@alias``. Exclusion, goal and
weight set on a coverpoint bin are therefore carried as ``userAttr``, which is
schema-valid and preserves what you asked for.

Scope-level detail
==================

* ``s.parameter(name, value)`` → ``designParameter``
* ``s.attr(key, value, type)`` → ``userAttr``
* ``s.metricMode(kind, mode)`` and ``s.weight(kind, w)`` → the
  ``metricAttributes`` of a per-kind container
* ``top.child(name, moduleName)`` → a nested scope with ``@parentInstanceId``
  built for you

Everything at once
==================

.. literalinclude:: ../../../../cpp/ucis-xml/examples/10_full_coverage.cpp
   :language: cpp
   :start-after: [start:body]
   :end-before: [end:body]
