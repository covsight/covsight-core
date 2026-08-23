################
The contract
################

This is the page to read before adopting the writer. It is short, which is the
result the design was aiming at.

The three rules
===============

1. **Describe the tool, the test and the source files before emitting
   coverage.** The file list may be a superset, so it is the one you already
   have from compilation.
2. **Finish one scope before opening the next**, or nest them with
   ``parent.child(...)``.
3. **Within a scope, emit anything in any order.**

That is all of them. Every one is a statement about coverage collection, not
about XML.

For contrast, here is what the schema requires and the writer handles for you:
coverage kinds in ``INSTANCE_COVERAGE``'s nine-element sequence; coverpoints
before crosses; FSM states before transitions; ``statement``, ``block`` and
``process`` never mixed; ``blockBin`` after nested blocks; ``branchBin`` after
nested branches; assertion bins in one exact order; ``@key`` everywhere;
``@inlineCount``; ``@instanceId`` and ``@parentInstanceId``; the required
``<options/>`` child; ``EXPR``'s ``@index`` and ``@width``; source-file ids; and
``HISTORY_NODE``'s nine required attributes.

Why rule 3 is real
==================

Every scope is staged: each fact becomes a small fixed-size record plus its
strings in a bump arena, and when the scope closes the writer sorts them and
renders one complete element.

The sort is on **content** — names, source locations, edges — and never on
arrival. So the document depends only on *what* you emitted, not on *when*.
``tests/test_ux_order.cpp`` builds the same document six ways, with the
containers and the facts in five different permutations plus full reversal, and
requires the output to be byte-identical.

The exception, and it is not really one: sequences whose order *is* the
information keep the order you gave them. An expression's ``subExpr`` list, a
cross bin's index tuple, the state path of an FSM transition, and a covergroup's
parameter list are all positional. Reordering those would change their meaning,
so the writer does not.

What rule 2 costs
=================

Peak memory is bounded by the largest single scope, not by the design. Measured
on OpenTitan — 1,241,667 points across 1,520 scopes:

.. list-table::
   :header-rows: 1

   * - Metric
     - Points
   * - Total
     - 1,241,667
   * - Scopes
     - 1,520
   * - **Largest scope**
     - **37,404**
   * - Mean
     - 817
   * - Median
     - 232
   * - p99
     - 9,117

Replaying that capture through the writer gives a staging peak of about
**5.8 MB** against a **122 MB** document. Reproduce the distribution with
``bench/coverage/scope_distribution.py`` and the replay with
``cpp/ucis-xml/tests/scale_replay.cpp``.

``WriterOptions::stagingLimit`` (64 MiB by default, roughly ten times the
measured peak) is a tripwire rather than a hard cap: exceeding it counts a
warning and reports through ``CoverageWriter::stagingPeak()``. It does not drop
coverage, and it does not fail.

Violating rule 2 is detected. Reopening a scope after closing it does not
produce an invalid document — it quietly splits one instance's coverage across
several ``instanceCoverages`` elements — so the writer says so rather than
letting it pass.

The source-file table
=====================

The schema puts ``sourceFiles+`` before everything, so file ids must exist
before the first scope is written. This is the one genuine "you must do a pass
first" constraint in the format, and rather than hide it, the writer makes it a
named choice.

Two facts set what that choice actually costs. Both are pinned by
``tests/ucis_xml/test_sources.py`` rather than left as claims.

**The table may list files nothing references.** ``ucis.xsd`` declares
``sourceFiles`` with ``fileName`` and ``id`` and has no ``xsd:key`` or
``xsd:keyref`` anywhere, so unreferenced entries validate. So "register your
files first" does **not** mean "pre-scan your coverage data". It means "hand
over the compile file list you already have", superset and all — which almost
every producer has for free.

**Deferring does not prevent compression.** A gzip file is a sequence of
independently compressed members (:rfc:`1952` §2.2), and concatenated members
decompress transparently. So a writer that owns its compression can emit the
header as one complete member at ``close()`` and copy an already-compressed
spool through byte for byte, with no recompression at all.

.. list-table::
   :header-rows: 1

   * - Mode
     - Caller must
     - Spool
     - Extra I/O (OpenTitan)
   * - ``Sources::UpFront``
     - pass a file list (superset fine) before the first scope
     - none
     - **0**
   * - ``Sources::Deferred`` + writer-owned gzip
     - nothing
     - compressed body
     - **3.1 MiB**
   * - ``Sources::Deferred`` + plain sink
     - nothing
     - raw body
     - **122 MiB**

So: **if you defer, compress.** The combination a naive reading would steer you
toward — defer *and* write plain XML — is the one genuinely expensive cell, and
uncompressed XML is not a viable per-test artifact at these sizes anyway.

``Sources::UpFront`` is the default, because the first fact makes it nearly free
to satisfy and it is the only mode with no overhead at all. A caller that cannot
enumerate its files gets a precise error naming both fixes rather than a silent
mode switch.

.. note::

   The writer-owned gzip path is not implemented yet, so today
   ``Sources::Deferred`` spools whatever bytes the sink is given — raw XML if
   the sink is a plain one. The row above describes what the mode costs once
   ``UCIS_XML_ENABLE_ZLIB`` lands; until then, deferred mode against a large
   design spools the uncompressed body and spills it to a temp file.

One caveat on multi-member gzip. It is standard and handled by ``gzip``,
``zcat``, zlib's ``gzread`` and Python's ``gzip``, but a consumer that calls raw
``inflate()`` once and stops sees only the first member. That is asserted as a
test rather than left as a worry, and it is why the multi-member path is kept
off the default.

Errors
======

No exceptions, no ``abort``, no writes to ``stderr``. A coverage write must never
take a simulation down. The writer latches the first error, every subsequent
operation becomes a no-op, and ``close()`` reports.

Defining ``UCIS_XML_ASSERT(msg)`` before including the header turns contract
violations into loud aborts instead. That is how the test suite is built, and it
is what you want during development.

See :doc:`troubleshooting` for every message the writer can produce.
