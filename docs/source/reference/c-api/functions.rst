####################
C API Functions
####################

.. contents::
   :local:
   :depth: 2

Database Lifecycle
==================

.. c:function:: ncdbT ncdb_Open(const char *path)

   Open an NCDB database at *path*.  Returns a valid :c:type:`ncdbT` handle on
   success, or ``NULL`` on failure.  Call :c:func:`ncdb_Read` to load the file
   contents into memory.

.. c:function:: void ncdb_Close(ncdbT db)

   Release all resources associated with *db*.  The handle is invalid after
   this call.

.. c:function:: int ncdb_Read(ncdbT db, const char *path)

   Deserialise the NCDB file at *path* into the already-open database *db*.
   Returns 0 on success, non-zero on error.

.. c:function:: int ncdb_Write(ncdbT db, const char *path)

   Serialise the in-memory database *db* to *path*.
   Returns 0 on success, non-zero on error.

-----

Scope Access
============

.. c:function:: int ncdb_ScopeIterate(ncdbT db, ncdbScopeT parent, uint32_t type_mask, int (*cb)(ncdbT, ncdbScopeT, void *), void *ud)

   Iterate over the direct children of *parent* (pass ``NULL`` for top-level
   scopes) whose type matches *type_mask* (see :doc:`types` for ``NCDB_SCOPE_*``
   constants).  *cb* is called for each matching scope; returning non-zero from
   *cb* stops iteration.  *ud* is passed unchanged to every callback invocation.
   Returns 0 on success.

.. c:function:: const char *ncdb_GetScopeName(ncdbT db, ncdbScopeT scope)

   Return the name of *scope*.  The returned pointer is valid for the lifetime
   of *db*.

.. c:function:: uint32_t ncdb_GetScopeType(ncdbT db, ncdbScopeT scope)

   Return the type bitmask of *scope* (one of the ``NCDB_SCOPE_*`` values).

.. c:function:: ncdbScopeT ncdb_GetParent(ncdbT db, ncdbScopeT scope)

   Return the parent scope of *scope*, or ``NULL`` if *scope* is a top-level
   node.

.. c:function:: uint64_t ncdb_GetScopeWeight(ncdbT db, ncdbScopeT scope)

   Return the weight assigned to *scope*.

.. c:function:: int64_t ncdb_GetScopeGoal(ncdbT db, ncdbScopeT scope)

   Return the coverage goal for *scope*.

.. c:function:: void ncdb_SetScopeWeight(ncdbT db, ncdbScopeT scope, uint64_t weight)

   Set the weight for *scope*.

.. c:function:: void ncdb_SetScopeGoal(ncdbT db, ncdbScopeT scope, int64_t goal)

   Set the coverage goal for *scope*.

.. c:function:: const char *ncdb_GetScopeSourcePath(ncdbT db, ncdbScopeT scope)

   Return the source file path associated with *scope*, or ``NULL`` if not set.

.. c:function:: uint64_t ncdb_GetScopeSourceLine(ncdbT db, ncdbScopeT scope)

   Return the source line number associated with *scope*.

.. c:function:: uint64_t ncdb_GetScopeSourceToken(ncdbT db, ncdbScopeT scope)

   Return the source token offset associated with *scope*.

.. c:function:: int ncdb_SetScopeSourceInfo(ncdbT db, ncdbScopeT scope, const char *path, uint64_t line, uint64_t token)

   Set the source file information for *scope*.  Returns 0 on success.

-----

Cover Item Access
=================

.. c:function:: int ncdb_CoverIterate(ncdbT db, ncdbScopeT scope, int (*cb)(ncdbT, ncdbCoverT, void *), void *ud)

   Iterate over all cover items (bins) within *scope*.  *cb* is called for each
   item; returning non-zero stops iteration.  Returns 0 on success.

.. c:function:: const char *ncdb_GetCoverName(ncdbT db, ncdbCoverT cover)

   Return the name of *cover*.

.. c:function:: uint32_t ncdb_GetCoverType(ncdbT db, ncdbCoverT cover)

   Return the type bitmask of *cover* (one of the ``NCDB_COVER_*`` values).

.. c:function:: uint64_t ncdb_GetCoverCount(ncdbT db, ncdbCoverT cover)

   Return the hit count for *cover*.

-----

History Node Access
===================

.. c:function:: int ncdb_HistoryIterate(ncdbT db, uint32_t kind_mask, int (*cb)(ncdbT, ncdbHistoryNodeT, void *), void *ud)

   Iterate over history nodes matching *kind_mask* (``NCDB_HISTORY_TEST``,
   ``NCDB_HISTORY_MERGE``, or ``NCDB_HISTORY_ALL``).  *cb* is called for each
   matching node.  Returns 0 on success.

.. c:function:: uint32_t ncdb_GetHistoryKind(ncdbT db, ncdbHistoryNodeT node)

   Return the kind of *node* (``NCDB_HISTORY_TEST`` or ``NCDB_HISTORY_MERGE``).

.. c:function:: const char *ncdb_GetHistoryLogicalName(ncdbT db, ncdbHistoryNodeT node)

   Return the logical name of the test or merge recorded by *node*.

.. c:function:: const char *ncdb_GetHistoryPhysicalName(ncdbT db, ncdbHistoryNodeT node)

   Return the physical name (e.g. file path) of *node*.

.. c:function:: const char *ncdb_GetHistoryUserName(ncdbT db, ncdbHistoryNodeT node)

   Return the user name associated with *node*.

.. c:function:: const char *ncdb_GetHistorySeed(ncdbT db, ncdbHistoryNodeT node)

   Return the random seed used in the test recorded by *node*.

.. c:function:: const char *ncdb_GetHistoryToolCategory(ncdbT db, ncdbHistoryNodeT node)

   Return the tool category string for *node*.

.. c:function:: const char *ncdb_GetHistoryComment(ncdbT db, ncdbHistoryNodeT node)

   Return the comment string for *node*.

.. c:function:: uint32_t ncdb_GetHistoryTestStatus(ncdbT db, ncdbHistoryNodeT node)

   Return the test status for *node* (pass, fail, etc.).

.. c:function:: void ncdb_SetHistoryUserName(ncdbT db, ncdbHistoryNodeT node, const char *v)
.. c:function:: void ncdb_SetHistorySeed(ncdbT db, ncdbHistoryNodeT node, const char *v)
.. c:function:: void ncdb_SetHistoryToolCategory(ncdbT db, ncdbHistoryNodeT node, const char *v)
.. c:function:: void ncdb_SetHistoryComment(ncdbT db, ncdbHistoryNodeT node, const char *v)
.. c:function:: void ncdb_SetHistoryTestStatus(ncdbT db, ncdbHistoryNodeT node, uint32_t v)

-----

Cross Coverage
==============

.. c:function:: size_t ncdb_GetCrossPointCount(ncdbT db, ncdbScopeT scope)

   Return the number of cross points (contributing coverpoints) in the cross
   scope *scope*.

.. c:function:: ncdbScopeT ncdb_GetCrossPoint(ncdbT db, ncdbScopeT scope, size_t idx)

   Return the *idx*-th contributing coverpoint scope of the cross *scope*.

-----

Issue Access
============

.. c:function:: int ncdb_IssueIterate(ncdbT db, int (*cb)(ncdbT, ncdbIssueT, void *), void *ud)

   Iterate over all issues in *db*.  Returns 0 on success.

.. c:function:: int ncdb_IssueIterateOpen(ncdbT db, int (*cb)(ncdbT, ncdbIssueT, void *), void *ud)

   Iterate over only the open issues in *db*.  Returns 0 on success.

.. c:function:: int ncdb_IssueIterateBySeverity(ncdbT db, uint8_t severity, int (*cb)(ncdbT, ncdbIssueT, void *), void *ud)

   Iterate over issues with the given *severity* (one of the
   ``NCDB_ISSUE_SEV_*`` constants).  Returns 0 on success.

.. c:function:: ncdbIssueT ncdb_GetIssueById(ncdbT db, const char *issue_id)

   Look up an issue by its string identifier.  Returns ``NULL`` if not found.

.. c:function:: const char *ncdb_GetIssueId(ncdbT db, ncdbIssueT issue)
.. c:function:: const char *ncdb_GetIssueExt(ncdbT db, ncdbIssueT issue)
.. c:function:: uint8_t ncdb_GetIssueSeverity(ncdbT db, ncdbIssueT issue)
.. c:function:: uint8_t ncdb_GetIssueKind(ncdbT db, ncdbIssueT issue)
.. c:function:: uint8_t ncdb_GetIssueState(ncdbT db, ncdbIssueT issue)
.. c:function:: uint8_t ncdb_GetIssueResolution(ncdbT db, ncdbIssueT issue)
.. c:function:: uint32_t ncdb_GetIssueCreatedAt(ncdbT db, ncdbIssueT issue)
.. c:function:: uint32_t ncdb_GetIssueUpdatedAt(ncdbT db, ncdbIssueT issue)
.. c:function:: uint32_t ncdb_GetIssueSyncedAt(ncdbT db, ncdbIssueT issue)

-----

Link Iteration
==============

Link callbacks receive the source and target issue IDs plus link metadata.

.. c:function:: int ncdb_WaiverLinkIterate(ncdbT db, int (*cb)(ncdbT, const char *, const char *, void *), void *ud)

   Iterate over all waiver links.  The callback receives the waiver ID and the
   linked issue ID.

.. c:function:: int ncdb_TestpointLinkIterate(ncdbT db, int (*cb)(ncdbT, const char *, const char *, uint8_t, void *), void *ud)

   Iterate over all testpoint links.  The callback receives the testpoint ID,
   the linked issue ID, and the link kind (``NCDB_LINK_*``).

.. c:function:: int ncdb_CoverageLinkIterate(ncdbT db, int (*cb)(ncdbT, const char *, const char *, const char *, uint8_t, void *), void *ud)

   Iterate over all coverage links.  The callback receives the coverage scope
   path, the linked issue ID, an annotation string, and the link kind.

-----

Issue History
=============

.. c:function:: int ncdb_IssueHistoryIterate(ncdbT db, const char *issue_id, int (*cb)(ncdbT, uint32_t, uint8_t, const char *, void *), void *ud)

   Iterate over the state-change history of the issue identified by *issue_id*.
   The callback receives a timestamp (Unix epoch), the new state
   (``NCDB_ISSUE_STATE_*``), a comment string, and *ud*.

.. c:function:: int ncdb_IssueStateAt(ncdbT db, const char *issue_id, uint32_t ts)

   Return the state of issue *issue_id* at the given Unix timestamp *ts*.

-----

Error Handling
==============

.. c:function:: const char *ncdb_GetLastError(ncdbT db)

   Return a human-readable description of the last error that occurred on *db*,
   or ``NULL`` if no error has occurred.
