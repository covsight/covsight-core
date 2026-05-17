####################
C API Types
####################

.. contents::
   :local:
   :depth: 2

Handle Types
============

All database objects are represented as opaque pointer typedefs.  The underlying
structs are private to the implementation.

.. c:type:: ncdbT

   Handle to an open NCDB coverage database.  Obtained from :c:func:`ncdb_Open`
   and released with :c:func:`ncdb_Close`.

.. c:type:: ncdbScopeT

   Handle to a scope node in the coverage hierarchy (design unit, instance,
   covergroup, coverpoint, etc.).

.. c:type:: ncdbCoverT

   Handle to a cover item (bin) within a scope.

.. c:type:: ncdbHistoryNodeT

   Handle to a history node recording one test run or merge operation.

.. c:type:: ncdbIssueT

   Handle to a coverage issue record.

-----

Scope Type Mask
===============

Bit-mask constants used to filter scope iteration.  Pass one or more ORed
together as the ``type_mask`` argument to :c:func:`ncdb_ScopeIterate`.

.. c:macro:: NCDB_SCOPE_TOGGLE
.. c:macro:: NCDB_SCOPE_BRANCH
.. c:macro:: NCDB_SCOPE_EXPR
.. c:macro:: NCDB_SCOPE_COND
.. c:macro:: NCDB_SCOPE_INSTANCE
.. c:macro:: NCDB_SCOPE_PROCESS
.. c:macro:: NCDB_SCOPE_BLOCK
.. c:macro:: NCDB_SCOPE_FUNCTION
.. c:macro:: NCDB_SCOPE_FORKJOIN
.. c:macro:: NCDB_SCOPE_GENERATE
.. c:macro:: NCDB_SCOPE_GENERIC
.. c:macro:: NCDB_SCOPE_CLASS
.. c:macro:: NCDB_SCOPE_COVERGROUP
.. c:macro:: NCDB_SCOPE_COVERINSTANCE
.. c:macro:: NCDB_SCOPE_COVERPOINT
.. c:macro:: NCDB_SCOPE_CROSS
.. c:macro:: NCDB_SCOPE_COVER
.. c:macro:: NCDB_SCOPE_ASSERT
.. c:macro:: NCDB_SCOPE_PROGRAM
.. c:macro:: NCDB_SCOPE_PACKAGE
.. c:macro:: NCDB_SCOPE_TASK
.. c:macro:: NCDB_SCOPE_INTERFACE
.. c:macro:: NCDB_SCOPE_FSM
.. c:macro:: NCDB_SCOPE_DU_MODULE
.. c:macro:: NCDB_SCOPE_DU_ARCH
.. c:macro:: NCDB_SCOPE_DU_PACKAGE
.. c:macro:: NCDB_SCOPE_DU_PROGRAM
.. c:macro:: NCDB_SCOPE_DU_INTERFACE
.. c:macro:: NCDB_SCOPE_FSM_STATES
.. c:macro:: NCDB_SCOPE_FSM_TRANS
.. c:macro:: NCDB_SCOPE_COVBLOCK
.. c:macro:: NCDB_SCOPE_CVGBINSCOPE
.. c:macro:: NCDB_SCOPE_ILLEGALBINSCOPE
.. c:macro:: NCDB_SCOPE_IGNOREBINSCOPE

.. c:macro:: NCDB_SCOPE_ALL

   Convenience mask that matches every scope type.

-----

Cover Type Mask
===============

Bit-mask constants used to classify cover items.  Returned by
:c:func:`ncdb_GetCoverType`.

.. c:macro:: NCDB_COVER_CVGBIN
.. c:macro:: NCDB_COVER_COVERBIN
.. c:macro:: NCDB_COVER_ASSERTBIN
.. c:macro:: NCDB_COVER_STMTBIN
.. c:macro:: NCDB_COVER_BRANCHBIN
.. c:macro:: NCDB_COVER_EXPRBIN
.. c:macro:: NCDB_COVER_CONDBIN
.. c:macro:: NCDB_COVER_TOGGLEBIN
.. c:macro:: NCDB_COVER_PASSBIN
.. c:macro:: NCDB_COVER_FSMBIN
.. c:macro:: NCDB_COVER_USERBIN
.. c:macro:: NCDB_COVER_COUNT
.. c:macro:: NCDB_COVER_FAILBIN
.. c:macro:: NCDB_COVER_VACUOUSBIN
.. c:macro:: NCDB_COVER_DISABLEDBIN
.. c:macro:: NCDB_COVER_ATTEMPTBIN
.. c:macro:: NCDB_COVER_ACTIVEBIN
.. c:macro:: NCDB_COVER_IGNOREBIN
.. c:macro:: NCDB_COVER_ILLEGALBIN
.. c:macro:: NCDB_COVER_DEFAULTBIN
.. c:macro:: NCDB_COVER_PEAKACTIVEBIN
.. c:macro:: NCDB_COVER_BLOCKBIN

.. c:macro:: NCDB_COVER_ALL

   Convenience mask that matches every cover type.

-----

Source Language
===============

HDL source language identifier.  Returned by scope source-info accessors.

.. c:macro:: NCDB_SOURCE_VHDL
.. c:macro:: NCDB_SOURCE_VLOG
.. c:macro:: NCDB_SOURCE_SV
.. c:macro:: NCDB_SOURCE_SYSTEMC
.. c:macro:: NCDB_SOURCE_PSL_VHDL
.. c:macro:: NCDB_SOURCE_PSL_VLOG
.. c:macro:: NCDB_SOURCE_PSL_SV
.. c:macro:: NCDB_SOURCE_PSL_SYSTEMC
.. c:macro:: NCDB_SOURCE_E
.. c:macro:: NCDB_SOURCE_VERA
.. c:macro:: NCDB_SOURCE_NONE
.. c:macro:: NCDB_SOURCE_OTHER
.. c:macro:: NCDB_SOURCE_ERROR

-----

History Kind
============

Used as the ``kind_mask`` argument to :c:func:`ncdb_HistoryIterate` and
returned by :c:func:`ncdb_GetHistoryKind`.

.. c:macro:: NCDB_HISTORY_NONE

   No-match sentinel (``(uint32_t)-1``).

.. c:macro:: NCDB_HISTORY_ALL

   Match all history node kinds.

.. c:macro:: NCDB_HISTORY_TEST

   History node records a single test run.

.. c:macro:: NCDB_HISTORY_MERGE

   History node records a merge of multiple test runs.

-----

Issue Severity
==============

.. c:macro:: NCDB_ISSUE_SEV_INFO
.. c:macro:: NCDB_ISSUE_SEV_LOW
.. c:macro:: NCDB_ISSUE_SEV_MEDIUM
.. c:macro:: NCDB_ISSUE_SEV_HIGH
.. c:macro:: NCDB_ISSUE_SEV_CRITICAL

Issue Kind
==========

.. c:macro:: NCDB_ISSUE_KIND_DESIGN_BUG
.. c:macro:: NCDB_ISSUE_KIND_TEST_BUG
.. c:macro:: NCDB_ISSUE_KIND_INFRA
.. c:macro:: NCDB_ISSUE_KIND_SPEC_GAP

Issue State
===========

.. c:macro:: NCDB_ISSUE_STATE_OPEN
.. c:macro:: NCDB_ISSUE_STATE_IN_PROGRESS
.. c:macro:: NCDB_ISSUE_STATE_RESOLVED
.. c:macro:: NCDB_ISSUE_STATE_CLOSED
.. c:macro:: NCDB_ISSUE_STATE_WONTFIX

Issue Resolution
================

.. c:macro:: NCDB_ISSUE_RES_NONE
.. c:macro:: NCDB_ISSUE_RES_FIXED
.. c:macro:: NCDB_ISSUE_RES_WONT_FIX
.. c:macro:: NCDB_ISSUE_RES_DUPLICATE
.. c:macro:: NCDB_ISSUE_RES_NOT_A_BUG

-----

Link Kind
=========

Used in link-iteration callbacks.

.. c:macro:: NCDB_LINK_BLOCKED_BY
.. c:macro:: NCDB_LINK_CAUSED_BY
.. c:macro:: NCDB_LINK_RELATED
