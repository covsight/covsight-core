################
Troubleshooting
################

Every message the writer can produce, with its cause and its fix. The strings
are asserted in ``cpp/ucis-xml/tests/test_ux_contract.cpp``, so this page cannot
quietly drift from what the code says.

How errors behave
=================

A coverage write must never take a simulation down, so there are no exceptions,
no ``abort``, and no writes to ``stderr``. The writer latches the **first**
error — the one that explains the others — turns every subsequent operation into
a no-op, and reports at ``close()``:

.. code-block:: cpp

   if (!cov.close()) fprintf(stderr, "%s\n", cov.error());

``cov.errorCode()`` gives the same thing as a ``ux::Err`` enumerator if you want
to react programmatically.

During development, define ``UCIS_XML_ASSERT(msg)`` before including the header
and each of these aborts loudly instead. That is how the test suite is built.

Errors
======

``source file "..." first seen after scope "..."; pass it to cov.sources(), or construct with Sources::Deferred``
    ``Err::LateSourceFile``. The schema puts ``sourceFiles+`` before everything,
    so under the default ``Sources::UpFront`` the file table is closed when the
    first scope opens.

    **Fix:** pass the path to ``cov.sources()`` first. The list may be a
    superset, so the compile file list you already have will do — you do not
    need to pre-scan your coverage data. If you genuinely cannot enumerate
    files, construct with ``Sources::Deferred`` and read
    :doc:`contract` for what that costs.

``scope "..." was opened again after being closed; scopes must be contiguous``
    ``Err::ScopeReopened``. Rule 2. This does not produce an invalid document —
    it quietly splits one instance's coverage across several
    ``instanceCoverages`` elements, which is why the writer objects rather than
    letting it pass.

    **Fix:** emit all of a scope's coverage before moving on.
    ``ux::sortByScope()`` groups a flat record list for you.

``scope "..." closed while "..." is still open; finish one scope before opening the next``
    ``Err::ScopeReopened``, from closing an outer scope while a nested one is
    still open.

    **Fix:** close children before parents, or let the RAII handles do it by
    scoping them.

``scope "..." was never closed``
    ``Err::ScopeNotClosed``. A ``Scope`` handle was still alive at ``close()``.

    **Fix:** let the handle go out of scope, or call ``s.close()``, before
    ``cov.close()``.

``scope "..." mixes statement, block and process coverage; blockCoverage is an xsd:choice``
    ``Err::MixedBlockForms``. ``BLOCK_COVERAGE`` admits ``process+``, ``block+``
    or ``statement+`` — one of the three.

    **Fix:** use only one of ``s.line()``, ``s.block()`` and ``s.process()`` in
    a given scope.

``fsm "..." has a transition through state "...", which was never declared with state()``
    ``Err::UnknownState``.

    **Fix:** declare every state with ``f.state()``. The order does not matter —
    transitions may precede the states they name.

``cross "..." names coverpoint "...", which is not in covergroup "..."``
    ``Err::UnknownCoverpoint``. A ``crossExpr`` naming a coverpoint that does
    not exist produces a document that validates but describes nothing.

    **Fix:** check the name, or add the coverpoint with ``cg.coverpoint()``.

``document has no coverage scopes``
    ``Err::EmptyDocument``. ``UCIS`` requires ``instanceCoverages+``.

``tool() and test() must both be set``
    ``Err::MissingHistory``. ``HISTORY_NODE`` has nine required attributes and
    six of them come from these two calls.

``sink write failed``
    ``Err::SinkFailed``. The output sink returned non-zero, or the file could
    not be opened. Once the sink has failed the writer stops calling it.

``out of memory``
    ``Err::OutOfMemory``. Allocation failed while staging. Coverage records may
    have been dropped; the writer does not abort, because taking a simulation
    down is worse.

``writer is already closed``
    ``Err::ClosedWriter``. A call arrived after ``close()``.

Warnings
========

Warnings never stop the write. ``cov.warnings()`` is the total and
``cov.keyCollisions()`` is one component of it. The fixture suite asserts the
total is zero, which is the standard to hold your own output to.

**Key collisions.** Two things inside one container carrying one name. The
second gets ``#2``, the third ``#3``, and so on. ``@key`` is merge identity
downstream, so silently folding them together would be worse than distinguishing
them — but a collision usually means the name is not as unique as you thought.

**Clamped integers.** ``@file``, ``@line``, ``@inlineCount`` and ``FSM/@width``
are ``xsd:positiveInteger``, which admits no zero. A tool reporting line 0 for a
generated construct would produce an invalid document, so the value is raised to
1 and counted.

**Sanitized characters.** XML 1.0 forbids most control characters *even as
numeric references*, so escaping ``& < > "`` is not sufficient for names
carrying arbitrary bytes. ``\x00``–``\x08``, ``\x0B``, ``\x0C`` and
``\x0E``–``\x1F`` are replaced with ``?`` and counted; malformed UTF-8 likewise
when ``UCIS_XML_SANITIZE_UTF8`` is on. Tab, newline and carriage return are
legal and come out as numeric references, losing nothing, so they are not
counted.

The count is of repairs *performed*, not of distinct inputs repaired: a name
appearing as both ``@name`` and ``@key`` is escaped twice. It is zero exactly
when nothing needed fixing, which is what matters.

**Dropped constructs.** Where the schema requires a container to be non-empty —
``EXPR`` needs ``bin+``, ``COVERPOINT`` needs ``coverpointBin+``,
``FSM_TRANSITION`` needs two states, ``CROSS_BIN`` needs an index — an empty one
is dropped with a warning rather than filled with a fabricated zero-count bin.
Inventing coverage data to satisfy a cardinality rule would be worse than
omitting a construct you gave no data for.

**A ``.gz`` path without zlib.** ``openFile("x.xml.gz")`` without
``UCIS_XML_ENABLE_ZLIB`` writes plain XML under a misleading name. Silently
writing 500 MB because the extension was ignored is a bad default, so it warns.

**Staging above the limit.** A scope larger than ``WriterOptions::stagingLimit``
(64 MiB by default) counts a warning. Nothing is dropped and nothing fails;
``cov.stagingPeak()`` reports the high-water mark.
