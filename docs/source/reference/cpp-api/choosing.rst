############################################
Which writer: C++ vocabulary or C UCIS API
############################################

covsight has two UCIS-XML emitters. They are aimed at different people, and
picking the wrong one costs more work than it saves.

.. list-table::
   :header-rows: 1
   :widths: 20 40 40

   * -
     - ``cpp/ucis-xml`` (this)
     - ``c/ucis-writer``
   * - API
     - Coverage vocabulary: lines, toggles, branches, coverpoints
     - UCIS 1.0 C API, plus a descriptor layer
   * - Language
     - C++17, header-only
     - C99, single header or static library
   * - You supply
     - Coverage facts, in any order within a scope
     - UCIS scopes, coveritems and properties, in the API's order
   * - The writer supplies
     - Keys, ids, ordering, grouping, ``<options/>``, file ids
     - The XML serialization of the UCIS model
   * - Portability of caller code
     - covsight-specific
     - Ports to any conforming UCIS implementation
   * - Licence
     - Apache-2.0 OR MIT
     - Apache-2.0

**Use the C++ writer** if you have coverage data and want UCIS-XML out of it. A
producer with a flat list of ``(scope, file, line, count)`` has no UCIS
semantics and should not have to acquire any.

**Use the C writer** if you have, or want, UCIS semantics: UCIS 1.0 Chapter 9
defines the XML schema *as a serialization of the UCIS data model*, so writing
against the UCIS API means the standard owns the correspondence, and your call
sequence ports to any conforming implementation. It is also the one to use from
C, and the one with an NCDB backend behind the same calls.

The two coexist deliberately. They share a conformance corpus: both validate
against the same ``ucis.xsd``, and the fixtures are compared where the
constructs correspond.
