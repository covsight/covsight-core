####################
In-Memory Backend
####################

The in-memory backend is the default implementation of the UCIS API.  It stores
all coverage data as Python objects in RAM, providing fast random access with no
I/O overhead.  It is the backend used internally by
:class:`~covsight.core.ncdb.ncdb_reader.NcdbReader` when loading an NCDB file.

.. contents::
   :local:
   :depth: 2

MemFactory
==========

The entry point for creating in-memory databases.

.. code-block:: python

   from covsight.core.mem import MemFactory

   db = MemFactory.create()       # new, empty database
   db2 = MemFactory.clone(other)  # deep copy of an existing database

.. autoclass:: covsight.core.mem.mem_factory.MemFactory
   :members:
   :member-order: bysource
   :undoc-members:

MemUCIS
=======

Concrete UCIS implementation backed by Python lists and dicts.  Returned by
:meth:`MemFactory.create` and used as the in-memory representation after
loading an NCDB file.

.. autoclass:: covsight.core.mem.mem_ucis.MemUCIS
   :members:
   :member-order: bysource
   :undoc-members:

Internal Implementation Classes
================================

The following classes implement the abstract API interfaces for the in-memory
backend.  You normally interact with these through the abstract API (e.g.
:class:`~covsight.core.api.scope.Scope`,
:class:`~covsight.core.api.cover_item.CoverItem`), but they are documented here
for completeness.

.. autoclass:: covsight.core.mem.mem_scope.MemScope
   :members:
   :member-order: bysource
   :undoc-members:

.. autoclass:: covsight.core.mem.mem_history_node.MemHistoryNode
   :members:
   :member-order: bysource
   :undoc-members:

.. autoclass:: covsight.core.mem.mem_cover_item.MemCoverItem
   :members:
   :member-order: bysource
   :undoc-members:

.. autoclass:: covsight.core.mem.mem_du_scope.MemDUScope
   :members:
   :member-order: bysource
   :undoc-members:

.. autoclass:: covsight.core.mem.mem_instance_scope.MemInstanceScope
   :members:
   :member-order: bysource
   :undoc-members:

.. autoclass:: covsight.core.mem.mem_covergroup.MemCovergroup
   :members:
   :member-order: bysource
   :undoc-members:

.. autoclass:: covsight.core.mem.mem_coverpoint.MemCoverpoint
   :members:
   :member-order: bysource
   :undoc-members:

.. autoclass:: covsight.core.mem.mem_cross.MemCross
   :members:
   :member-order: bysource
   :undoc-members:

.. autoclass:: covsight.core.mem.mem_toggle_scope.MemToggleScope
   :members:
   :member-order: bysource
   :undoc-members:

.. autoclass:: covsight.core.mem.mem_fsm_scope.MemFSMScope
   :members:
   :member-order: bysource
   :undoc-members:

.. autoclass:: covsight.core.mem.mem_file_handle.MemFileHandle
   :members:
   :member-order: bysource
   :undoc-members:
