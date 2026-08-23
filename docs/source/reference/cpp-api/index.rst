###########################
C++ API — UCIS-XML writer
###########################

``ucis_xml.hpp`` is a **header-only C++ writer for UCIS-XML**, meant to be
vendored: a tool that produces coverage copies one file into its tree and emits
schema-valid UCIS-XML by describing the coverage it collected, in the vocabulary
it already uses.

Nothing about XML, and nothing about the UCIS encoding, appears in caller code.

.. code-block:: cpp

   #include "ucis_xml.hpp"
   namespace ux = ucisxml;

   ux::CoverageWriter cov;
   cov.openFile("coverage.xml");
   cov.tool(ux::Tool().name("mytool").version("1.0").vendorId("MINE"));
   cov.test(ux::Test().name("run1").passed(true));
   cov.sources({"rtl/alu.sv"});
   {
       ux::Scope s = cov.scope("top.u_alu", "alu");
       s.line("rtl/alu.sv", 42, 17);
   }
   if (!cov.close()) fprintf(stderr, "%s\n", cov.error());

Who it is for
=============

Any tool that has coverage data and needs to hand it to something else: a
simulator, an emulator, a testbench framework, or a format converter. The
design assumption is that such a tool has a flat list of records, or a design
hierarchy it can walk, and should not have to learn a schema to export either.

If you instead want the **UCIS 1.0 C API** — because you have UCIS semantics
already, or want caller code that ports to any conforming implementation —
use :doc:`../c-api/index` and ``c/ucis-writer`` instead. See
:doc:`choosing` for the one-paragraph version.

Requirements
============

* **C++17.** This is the floor rather than a preference: Verilator 5.041 ships
  ``CFG_CXXFLAGS_STD_NEWEST = -std=gnu++17``, and a header a Verilator-shaped
  consumer cannot compile is not a header that gets vendored. Consumers on
  C++20 can additionally use designated-initializer syntax for the descriptor
  structs; it is never required.
* **No dependencies.** Not zlib, not libxml. Not even ``<string>`` if you define
  ``UCIS_XML_NO_STL``.
* **Nothing to link.** Everything is ``inline``; unused coverage kinds cost zero
  ``.text``.

Vendoring
=========

Copy ``cpp/ucis-xml/include/ucis_xml.hpp`` into your tree and include it. That
is the whole installation. The header is generated from the modules in
``cpp/ucis-xml/src/`` and CI fails if the two ever disagree, so the file you
copy is the one the test suite exercises.

Licence
=======

Dual-licensed **Apache-2.0 OR MIT**, at your option.

The dual licence is deliberate for a header meant to be vendored. MIT removes
Apache-2.0 §4(d)'s ``NOTICE`` obligation — a recurring compliance ask for anyone
carrying the file in their own tree — and unblocks GPLv2-only consumers, which
Apache-2.0 alone cannot. Apache-2.0 keeps its express patent grant available for
anyone who wants it. There is deliberately **no** ``NOTICE`` file in the
directory, and CI fails if one appears.

Contributions to ``cpp/ucis-xml/`` are inbound = outbound under **both**
licences.

.. toctree::
   :maxdepth: 2

   quickstart
   contract
   coverage-kinds
   api
   troubleshooting
   choosing
