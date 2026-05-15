####################
Testplan Format
####################

The testplan format is a YAML/JSON schema for describing verification goals,
testpoints, coverage bindings, and requirements.  It is the connective tissue
between human-written specifications and machine-executed coverage analysis.

.. contents::
   :local:
   :depth: 2


Overview
========

A testplan file describes *what* you intend to verify (goals and testpoints)
and *how* you measure it (coverage bindings).  At analysis time,
:func:`~covsight.core.ncdb.testplan_closure.compute_closure` compares the
testplan against a coverage database and produces a
:class:`~covsight.core.ncdb.testplan_closure.TestpointResult` for every
testpoint.

Supported file formats:

* **YAML** (``.yaml``, ``.yml``) — recommended for hand-authoring
* **JSON** (``.json``) — recommended for programmatic generation
* **Hjson** (``.hjson``) — accepted for compatibility with OpenTitan flows


Top-level structure
===================

.. code-block:: yaml

   $schema: "https://schema.covsight.io/testplan/v1"
   format_version: 1
   name: uart
   description: "UART block testplan"
   owner: alice
   tags: [uart, regression]

   substitutions:
     baud: [9600, 115200]
     parity: [none, even, odd]

   imports:
     - path: shared/protocol.yaml
     - path: shared/coverage.yaml
       substitutions:
         mod: uart

   testpoints:
     - name: uart_smoke
       stage: V1
       desc: "Basic TX/RX loopback"
       tests: [uart_smoke_{baud}_{parity}]
       owner: bob
       priority: high
       weight: 2
       tags: [smoke]
       requirements:
         - system: JIRA
           project: UART
           item_id: REQ-001
       coverage:
         - type: covergroup
           path: "top.dut.cg_baud*"
           desc: "Baud rate coverage"

   goals:
     - id: uart-functional
       title: "UART Functional Verification"
       owner: alice
       priority: high
       status: in_progress
       goals:
         - id: uart-tx
           title: "TX Path"
           testpoints:
             - name: uart_tx_basic
               stage: V2
               tests: [uart_tx_test]
               coverage:
                 - type: coverpoint
                   path: "top.dut.cg.cp_tx_data"

   covergroups:
     - name: cg_baud
       desc: "Baud rate functional coverage"
       coverpoints:
         - name: cp_baud
           desc: "Baud rate values"
           path: "top.dut.cg_baud.cp_baud"
       custom:
         tool: vcs


Testpoints
==========

A testpoint describes one verification requirement and the tests that satisfy
it.

.. list-table::
   :header-rows: 1
   :widths: 20 10 70

   * - Field
     - Required
     - Description
   * - ``name``
     - ✓
     - Unique identifier within the plan.
   * - ``stage``
     - ✓
     - Verification stage (e.g. ``V1``, ``V2``, ``V3``).
   * - ``desc``
     -
     - Human-readable description (Markdown).
   * - ``tests``
     -
     - List of test names.  May use ``{key}`` substitution tokens and
       ``_*`` wildcard suffix.  An empty list marks the testpoint as
       *unimplemented*.
   * - ``tags``
     -
     - Free-form string labels.
   * - ``na``
     -
     - Boolean.  When ``true``, the testpoint is intentionally excluded from
       closure computation (counts as N/A).
   * - ``owner``
     -
     - Person responsible for this testpoint.
   * - ``priority``
     -
     - ``"high"``, ``"medium"``, or ``"low"``.
   * - ``weight``
     -
     - Positive integer; relative weight for weighted coverage scoring.
   * - ``requirements``
     -
     - List of :ref:`requirement links <requirement-link>`.
   * - ``coverage``
     -
     - List of :ref:`coverage bindings <coverage-binding>`.
   * - ``custom``
     -
     - Arbitrary key/value metadata.


Goals
=====

Goals form a tree that organises testpoints into a hierarchy (chip → subsystem
→ block → feature).

.. code-block:: yaml

   goals:
     - id: uart-functional
       title: "UART Functional Verification"
       desc: "Top-level UART goal"
       owner: alice
       priority: high
       status: in_progress
       tags: [uart]
       goals:          # nested sub-goals (arbitrary depth)
         - id: uart-tx
           title: "TX Path"
           testpoints:
             - name: tx_basic
               stage: V1
               tests: [uart_tx_basic]
       custom:
         jira_epic: UART-10

**Status values**: ``planned``, ``in_progress``, ``complete``, ``waived``.


.. _coverage-binding:

Coverage bindings
=================

A coverage binding connects a testpoint to a specific item in the coverage
database.

.. code-block:: yaml

   coverage:
     - type: covergroup
       path: "top.dut.cg_baud*"
       desc: "Baud rate coverage"
     - type: assertion
       path: "top.chk.assert_overflow"

**Valid types**: ``covergroup``, ``coverpoint``, ``cross``, ``assertion``,
``expression``, ``toggle``, ``line``, ``branch``, ``functional``.

The ``path`` field uses the same dotted-scope hierarchy as the underlying
coverage database.  Glob wildcards (``*``, ``?``) are supported and expanded
at analysis time by
:func:`~covsight.core.ncdb.testplan_closure.compute_coverage_binding`.


.. _requirement-link:

Requirement links
=================

.. code-block:: yaml

   requirements:
     - system: JIRA
       project: UART
       item_id: REQ-001
       url: "https://jira.example.com/browse/REQ-001"

All fields are optional; only ``item_id`` is conventionally required.


Imports
=======

Testplan files can import other testplan files to enable reuse:

.. code-block:: yaml

   imports:
     - path: shared/ip_base.yaml
     - path: shared/coverage_model.yaml
       substitutions:
         mod: uart

Imports are resolved recursively (transitive).  Circular imports are detected
and raise a :class:`~covsight.core.ncdb.testplan_imports.ParseError`.
Duplicate imports (same file via different paths) are deduplicated by resolved
absolute path.


Substitutions
=============

Substitution tokens expand ``{key}`` placeholders in test names:

.. code-block:: yaml

   substitutions:
     baud: [9600, 115200]
     parity: [none, even, odd]

   testpoints:
     - name: uart_test
       stage: V1
       tests: [uart_{baud}_{parity}]

Multiple list-valued substitutions are expanded with Cartesian product, so the
example above yields 6 test names.  Scalar values are also supported.


Covergroups
===========

The top-level ``covergroups`` list documents covergroup declarations
independently of testpoint bindings.  This is useful for tools that need a
manifest of all covergroups without iterating testpoints.

.. code-block:: yaml

   covergroups:
     - name: cg_baud
       desc: "Baud rate coverage"
       coverpoints:
         - name: cp_baud
           path: "top.dut.cg_baud.cp_baud"


Python API
==========

Loading a testplan
------------------

.. code-block:: python

   from covsight.core.ncdb.testplan_yaml import load_testplan

   plan = load_testplan("uart.yaml")

Validating a testplan dict
--------------------------

.. code-block:: python

   import yaml
   from covsight.core.ncdb.testplan_yaml import validate_testplan

   with open("uart.yaml") as f:
       raw = yaml.safe_load(f)

   errors = validate_testplan(raw)
   if errors:
       for e in errors:
           print("ERROR:", e)

Requires ``pip install 'covsight-core[validate]'``.

Computing closure
-----------------

.. code-block:: python

   from covsight.core.ncdb.testplan_yaml import load_testplan
   from covsight.core.ncdb.testplan_closure import compute_closure, stage_gate_status
   from covsight.core.ncdb.ncdb_ucis import NcdbUCIS

   plan = load_testplan("uart.yaml")
   db   = NcdbUCIS("coverage.cdb")

   results = compute_closure(plan, db)
   gate    = stage_gate_status(results, "V2", plan)
   print(gate["message"])

Exporting results
-----------------

.. code-block:: python

   from covsight.core.ncdb.testplan_export import (
       export_junit_xml,
       export_github_annotations,
       export_summary_markdown,
   )

   export_junit_xml(results, "closure.xml")
   export_github_annotations(results)     # writes ::error:: / ::warning:: to stdout
   md = export_summary_markdown(results, stage_gate=gate)


Vendor format import
====================

Cadence vManager (VPF)
----------------------

.. code-block:: python

   from covsight.core.ncdb.testplan_vpf import import_vpf

   plan = import_vpf("uart_testplan.vpf.xml")

Synopsys Verdi VC Planner
-------------------------

.. code-block:: python

   from covsight.core.ncdb.testplan_vc_planner import import_vc_planner

   plan = import_vc_planner("uart.csv")      # CSV variant
   plan = import_vc_planner("uart.xml")      # XML variant

Siemens Questa Visualizer
-------------------------

.. code-block:: python

   from covsight.core.ncdb.testplan_questa import import_questa

   plan = import_questa("uart_questa.xml")   # XML variant
   plan = import_questa("uart_questa.csv")   # CSV variant

OpenTitan Hjson
---------------

.. code-block:: python

   from covsight.core.ncdb.testplan_hjson import import_hjson

   plan = import_hjson("uart_testplan.hjson",
                        substitutions={"name": "uart"})


JSON Schema
===========

The formal JSON Schema (draft-07) is bundled with the package at::

   python/covsight/core/schema/testplan.schema.json

It can be used directly with any ``jsonschema``-compatible validator, or via
the :func:`~covsight.core.ncdb.testplan_yaml.validate_testplan` helper.
