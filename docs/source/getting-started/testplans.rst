####################
Using Testplans
####################

This guide walks you through creating a testplan, attaching it to a coverage
database, computing closure, and publishing results to CI.

.. contents::
   :local:
   :depth: 2


Step 1 — Write a testplan
=========================

Create a YAML file (e.g. ``uart.yaml``) that describes your testpoints:

.. code-block:: yaml

   # uart.yaml
   $schema: "https://schema.covsight.io/testplan/v1"
   name: uart
   description: "UART block testplan"
   owner: alice

   substitutions:
     baud: [9600, 115200]

   testpoints:
     - name: uart_smoke
       stage: V1
       desc: "Basic TX/RX at each baud rate"
       tests: [uart_smoke_{baud}]
       priority: high
       coverage:
         - type: covergroup
           path: "top.dut.cg_baud*"

     - name: uart_overflow
       stage: V2
       tests: [uart_overflow_test]
       coverage:
         - type: assertion
           path: "top.chk.assert_overflow"

The ``tests`` list supports ``{key}`` substitution tokens.  Here
``uart_smoke_{baud}`` expands to ``uart_smoke_9600`` and ``uart_smoke_115200``.


Step 2 — Validate the testplan
==============================

Check the file against the JSON Schema before running any simulations:

.. code-block:: python

   import yaml
   from covsight.core.ncdb.testplan_yaml import validate_testplan

   with open("uart.yaml") as f:
       raw = yaml.safe_load(f)

   errors = validate_testplan(raw)
   for e in errors:
       print("SCHEMA ERROR:", e)

Requires ``pip install 'covsight-core[validate]'``.


Step 3 — Attach the testplan to a coverage database
====================================================

After simulation, attach the testplan to the merged coverage database:

.. code-block:: python

   from covsight.core.ncdb.testplan_yaml import load_testplan
   from covsight.core.ncdb.ncdb_ucis import NcdbUCIS

   plan = load_testplan("uart.yaml")
   db   = NcdbUCIS("merged.cdb")
   db.setTestplan(plan)
   db.flush()           # write the updated .cdb back to disk

The testplan is serialised inside the ``.cdb`` ZIP file and round-trips
without data loss.


Step 4 — Compute closure
=========================

Compare testpoints against the test history in the database:

.. code-block:: python

   from covsight.core.ncdb.testplan_closure import compute_closure, TPStatus

   results = compute_closure(plan, db)

   for r in results:
       print(f"{r.testpoint.name:30s}  {r.status.value}")

Testpoints nested inside ``goals`` are included automatically (the
:func:`~covsight.core.ncdb.testplan.iter_testpoints` helper traverses the
goal tree depth-first).


Step 5 — Evaluate a stage gate
================================

A *stage gate* passes when all testpoints up to and including the target stage
are CLOSED or N/A:

.. code-block:: python

   from covsight.core.ncdb.testplan_closure import stage_gate_status

   gate = stage_gate_status(results, "V2", plan)
   print(gate["message"])
   # "Stage V2 gate PASSED" or "Stage V2 gate FAILED — blocking: …"

   if not gate["passed"]:
       raise SystemExit(1)


Step 6 — Publish results
=========================

JUnit XML (for Jenkins / GitLab CI)
------------------------------------

.. code-block:: python

   from covsight.core.ncdb.testplan_export import export_junit_xml

   export_junit_xml(results, "testplan_closure.xml")

GitHub Actions annotations
---------------------------

.. code-block:: python

   from covsight.core.ncdb.testplan_export import export_github_annotations

   export_github_annotations(results)  # writes ::error:: / ::warning:: to stdout

GitHub Actions Job Summary
---------------------------

.. code-block:: python

   import os
   from covsight.core.ncdb.testplan_export import export_summary_markdown

   md = export_summary_markdown(results, stage_gate=gate)
   with open(os.environ.get("GITHUB_STEP_SUMMARY", "/dev/null"), "a") as f:
       f.write(md)


Importing from vendor tools
============================

If you have an existing testplan in a vendor format, import it and optionally
save it as YAML for future editing:

.. code-block:: python

   from covsight.core.ncdb.testplan_vpf import import_vpf
   from covsight.core.ncdb.testplan_yaml import load_testplan

   # Cadence vManager (VPF)
   plan = import_vpf("uart.vpf.xml")

   # Synopsys Verdi VC Planner (CSV or XML)
   from covsight.core.ncdb.testplan_vc_planner import import_vc_planner
   plan = import_vc_planner("uart_vcp.csv")

   # Siemens Questa Visualizer (XML or CSV)
   from covsight.core.ncdb.testplan_questa import import_questa
   plan = import_questa("uart_questa.xml")

   # OpenTitan Hjson
   from covsight.core.ncdb.testplan_hjson import import_hjson
   plan = import_hjson("uart_testplan.hjson",
                        substitutions={"name": "uart"})


Organising with goals
=====================

For large designs, organise testpoints in a goal hierarchy:

.. code-block:: yaml

   goals:
     - id: chip-level
       title: "Chip-level verification"
       goals:
         - id: uart-block
           title: "UART block"
           testpoints:
             - name: uart_smoke
               stage: V1
               tests: [uart_smoke_test]

Use ``require_goals_closed=True`` in
:func:`~covsight.core.ncdb.testplan_closure.stage_gate_status` to require
*all* goal-nested testpoints to be closed (not just the top-level stage
filter):

.. code-block:: python

   gate = stage_gate_status(results, "V1", plan, require_goals_closed=True)


Next steps
==========

* :doc:`../reference/formats/testplan` — full field reference
* :doc:`../reference/python-api/index` — complete API documentation
