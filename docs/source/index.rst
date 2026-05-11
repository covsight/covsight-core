covsight-core
=============

**covsight-core** is the foundational Python library for the `covsight` coverage
analysis ecosystem. It provides:

* The **UCIS object-oriented API** — a complete Python interface to the
  Unified Coverage Interoperability Standard (UCIS) data model.
* An **in-memory backend** — fast, zero-dependency coverage database backed
  entirely by Python objects.
* The **NCDB format** — a compact, ZIP-based binary format (``.cdb``) for
  storing and exchanging coverage databases.
* A **format plugin registry** — a ``setuptools`` entry-points mechanism so
  third-party packages can add new database and report formats.

.. rubric:: Where do you want to start?

.. list-table::
   :widths: 40 60

   * - :doc:`getting-started/quickstart`
     - Get up and running in 5 minutes
   * - :doc:`reference/python-api/index`
     - Complete API reference for every class and enum
   * - :doc:`reference/formats/ncdb`
     - NCDB binary format specification
   * - :doc:`reference/migration`
     - Migrating from pyucis

.. toctree::
   :maxdepth: 2
   :hidden:
   :caption: Getting Started

   getting-started/quickstart

.. toctree::
   :maxdepth: 2
   :hidden:
   :caption: Reference

   reference/index
