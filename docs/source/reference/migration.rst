############################################
Migration Guide: pyucis → covsight-core
############################################

This page summarises the changes required to migrate Python code and shell
scripts from ``pyucis`` to the ``covsight`` ecosystem.

Installation
============

.. code-block:: bash

   # Old
   pip install pyucis

   # New
   pip install covsight-core   # core data model, in-memory backend, NCDB
   pip install covsight        # CLI tools and analysis engine

Import Mapping
==============

Database creation
-----------------

.. code-block:: python

   # OLD
   from ucis.mem.mem_factory import MemFactory
   db = MemFactory.create()

   # NEW
   from covsight.core.mem import MemFactory
   db = MemFactory.create()

Abstract types
--------------

.. code-block:: python

   # OLD
   from ucis import UCIS, Scope
   from ucis.ucis import UCIS
   from ucis.scope import Scope

   # NEW
   from covsight.core.api import UCIS, Scope

Enumerations
------------

.. code-block:: python

   # NEW — all enums available from one place
   from covsight.core.api import (
       ScopeTypeT, CoverTypeT, CoverFlagsT, SourceT, FlagsT,
       TestStatusT, HistoryNodeKind, IntProperty, ToggleMetricT,
   )
   # or specifically from the enums subpackage
   from covsight.core.api.enums import ScopeTypeT, CoverTypeT

Old UCIS\_ constant aliases
----------------------------

The old C-API-style constants are gone.  Use enum members instead:

.. list-table::
   :header-rows: 1
   :widths: 45 55

   * - Old constant
     - New enum member
   * - ``UCIS_COVERGROUP``
     - ``ScopeTypeT.COVERGROUP``
   * - ``UCIS_COVERPOINT``
     - ``ScopeTypeT.COVERPOINT``
   * - ``UCIS_INSTANCE``
     - ``ScopeTypeT.INSTANCE``
   * - ``UCIS_DU_MODULE``
     - ``ScopeTypeT.DU_MODULE``
   * - ``UCIS_TOGGLE``
     - ``ScopeTypeT.TOGGLE``
   * - ``UCIS_CVGBIN``
     - ``CoverTypeT.CVGBIN``
   * - ``UCIS_IGNOREBIN``
     - ``CoverTypeT.IGNOREBIN``
   * - ``UCIS_ILLEGALBIN``
     - ``CoverTypeT.ILLEGALBIN``
   * - ``UCIS_BRANCHBIN``
     - ``CoverTypeT.BRANCHBIN``
   * - ``UCIS_OTHER``
     - ``SourceT.OTHER``
   * - ``UCIS_SV``
     - ``SourceT.SV``
   * - ``UCIS_INST_ONCE``
     - ``FlagsT.INST_ONCE``
   * - ``UCIS_SCOPE_UNDER_DU``
     - ``FlagsT.SCOPE_UNDER_DU``
   * - ``UCIS_ENABLED_STMT``
     - ``FlagsT.ENABLED_STMT``
   * - ``UCIS_ENABLED_BRANCH``
     - ``FlagsT.ENABLED_BRANCH``
   * - ``UCIS_ENABLED_FSM``
     - ``FlagsT.ENABLED_FSM``
   * - ``UCIS_ENABLED_TOGGLE``
     - ``FlagsT.ENABLED_TOGGLE``

NCDB format
-----------

.. code-block:: python

   # OLD
   from ucis.ncdb.ncdb_reader import NcdbReader
   from ucis.ncdb.ncdb_writer import NcdbWriter

   # NEW
   from covsight.core.ncdb.ncdb_reader import NcdbReader
   from covsight.core.ncdb.ncdb_writer import NcdbWriter

Visitors and traversal
-----------------------

.. code-block:: python

   # OLD
   from ucis.visitors import traverse, UCISVisitor

   # NEW
   from covsight.core.visitors import traverse, UCISVisitor

Merge
-----

.. code-block:: python

   # OLD
   from ucis.merge.db_merger import DbMerger

   # NEW
   from covsight.core.merge import DbMerger

Format registry
---------------

.. code-block:: python

   # OLD
   from ucis.rgy import FormatRgy
   rgy = FormatRgy.inst()
   desc = rgy.getDatabaseDesc("ncdb")
   db = desc.fmt_if.read("coverage.cdb")

   # NEW
   from covsight.core.ext import FormatRegistry
   registry = FormatRegistry()
   desc = registry.get_db_format("ncdb")
   db = desc.fmt_if.read("coverage.cdb")

CLI Commands
============

.. code-block:: bash

   # OLD
   pyucis convert -o out.cdb in.xml
   pyucis merge -o merged.cdb a.cdb b.cdb
   pyucis show summary coverage.cdb

   # NEW
   covsight convert -o out.cdb in.xml
   covsight merge -o merged.cdb a.cdb b.cdb
   covsight show summary coverage.cdb

   # Also works as a module
   python -m covsight --help

Format Plugins
==============

Formats are now optional plugins discovered via Python entry points.

**Built-in** (ships with ``covsight-core``):

* ``ncdb`` — NCDB ZIP-based binary format (``.cdb``)

**Future installable plugins**:

* ``covsight-xml`` — UCIS XML format
* ``covsight-sqlite`` — SQLite format

To list installed formats:

.. code-block:: python

   from covsight.core.ext import FormatRegistry
   r = FormatRegistry()
   print("DB formats:", list(r.db_formats().keys()))
   print("Report formats:", list(r.rpt_formats().keys()))

Dropped APIs
============

The following pyucis features are **not** carried forward in covsight:

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Dropped
     - Reason / Alternative
   * - ``FormatRgy.inst()`` singleton
     - Replaced by ``FormatRegistry()``
   * - ``DbFormatRgy``
     - Merged into ``FormatRegistry``
   * - ``UCIS_*`` constant aliases
     - Use enum members (see table above)
   * - ``ucis.__init__`` re-exports
     - Import from ``covsight.core.api`` directly
   * - ``XmlFactory`` / ``XmlFileFactory``
     - Will be ``covsight-xml`` plugin
   * - HTML report formatter
     - Will be ``covsight-report-html`` plugin
   * - Cobertura/JaCoCo/LCOV formatters
     - Future plugin packages
