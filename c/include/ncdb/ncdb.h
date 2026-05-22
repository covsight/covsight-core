#ifndef INCLUDED_NCDB_H
#define INCLUDED_NCDB_H

#include <stddef.h>
#include "ncdb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

ncdbT     ncdb_Open(const char *path);
void      ncdb_Close(ncdbT db);
int       ncdb_Read(ncdbT db, const char *path);
int       ncdb_Write(ncdbT db, const char *path);

int  ncdb_ScopeIterate(ncdbT db, ncdbScopeT parent,
                       uint32_t type_mask,
                       int (*cb)(ncdbT, ncdbScopeT, void *), void *ud);

const char     *ncdb_GetScopeName(ncdbT db, ncdbScopeT scope);
uint32_t        ncdb_GetScopeType(ncdbT db, ncdbScopeT scope);
ncdbScopeT      ncdb_GetParent(ncdbT db, ncdbScopeT scope);
uint64_t        ncdb_GetScopeWeight(ncdbT db, ncdbScopeT scope);
int64_t         ncdb_GetScopeGoal(ncdbT db, ncdbScopeT scope);
void            ncdb_SetScopeWeight(ncdbT db, ncdbScopeT scope, uint64_t weight);
void            ncdb_SetScopeGoal(ncdbT db, ncdbScopeT scope, int64_t goal);
const char     *ncdb_GetScopeSourcePath(ncdbT db, ncdbScopeT scope);
uint64_t        ncdb_GetScopeSourceLine(ncdbT db, ncdbScopeT scope);
uint64_t        ncdb_GetScopeSourceToken(ncdbT db, ncdbScopeT scope);
int             ncdb_SetScopeSourceInfo(ncdbT db, ncdbScopeT scope,
                                        const char *path, uint64_t line, uint64_t token);

int  ncdb_CoverIterate(ncdbT db, ncdbScopeT scope,
                       int (*cb)(ncdbT, ncdbCoverT, void *), void *ud);
const char  *ncdb_GetCoverName(ncdbT db, ncdbCoverT cover);
uint32_t     ncdb_GetCoverType(ncdbT db, ncdbCoverT cover);
uint64_t     ncdb_GetCoverCount(ncdbT db, ncdbCoverT cover);

int  ncdb_HistoryIterate(ncdbT db, uint32_t kind_mask,
                         int (*cb)(ncdbT, ncdbHistoryNodeT, void *), void *ud);
uint32_t     ncdb_GetHistoryKind(ncdbT db, ncdbHistoryNodeT node);
const char  *ncdb_GetHistoryLogicalName(ncdbT db, ncdbHistoryNodeT node);
const char  *ncdb_GetHistoryPhysicalName(ncdbT db, ncdbHistoryNodeT node);
const char  *ncdb_GetHistoryUserName(ncdbT db, ncdbHistoryNodeT node);
const char  *ncdb_GetHistorySeed(ncdbT db, ncdbHistoryNodeT node);
const char  *ncdb_GetHistoryToolCategory(ncdbT db, ncdbHistoryNodeT node);
const char  *ncdb_GetHistoryComment(ncdbT db, ncdbHistoryNodeT node);
uint32_t     ncdb_GetHistoryTestStatus(ncdbT db, ncdbHistoryNodeT node);

void ncdb_SetHistoryUserName(ncdbT db, ncdbHistoryNodeT node, const char *v);
void ncdb_SetHistorySeed(ncdbT db, ncdbHistoryNodeT node, const char *v);
void ncdb_SetHistoryToolCategory(ncdbT db, ncdbHistoryNodeT node, const char *v);
void ncdb_SetHistoryComment(ncdbT db, ncdbHistoryNodeT node, const char *v);
void ncdb_SetHistoryTestStatus(ncdbT db, ncdbHistoryNodeT node, uint32_t v);
int  ncdb_SetHistoryParent(ncdbT db, ncdbHistoryNodeT node, ncdbHistoryNodeT parent);
ncdbHistoryNodeT ncdb_GetHistoryParent(ncdbT db, ncdbHistoryNodeT node);

size_t       ncdb_GetCrossPointCount(ncdbT db, ncdbScopeT scope);
ncdbScopeT   ncdb_GetCrossPoint(ncdbT db, ncdbScopeT scope, size_t idx);

/* ── Issue iteration ────────────────────────────────────────────────────── */
int ncdb_IssueIterate(ncdbT db,
                      int (*cb)(ncdbT, ncdbIssueT, void *), void *ud);
int ncdb_IssueIterateOpen(ncdbT db,
                          int (*cb)(ncdbT, ncdbIssueT, void *), void *ud);
int ncdb_IssueIterateBySeverity(ncdbT db, uint8_t severity,
                                int (*cb)(ncdbT, ncdbIssueT, void *), void *ud);
ncdbIssueT ncdb_GetIssueById(ncdbT db, const char *issue_id);

/* ── Issue field accessors ──────────────────────────────────────────────── */
const char *ncdb_GetIssueId(ncdbT db, ncdbIssueT issue);
const char *ncdb_GetIssueExt(ncdbT db, ncdbIssueT issue);
uint8_t     ncdb_GetIssueSeverity(ncdbT db, ncdbIssueT issue);
uint8_t     ncdb_GetIssueKind(ncdbT db, ncdbIssueT issue);
uint8_t     ncdb_GetIssueState(ncdbT db, ncdbIssueT issue);
uint8_t     ncdb_GetIssueResolution(ncdbT db, ncdbIssueT issue);
uint32_t    ncdb_GetIssueCreatedAt(ncdbT db, ncdbIssueT issue);
uint32_t    ncdb_GetIssueUpdatedAt(ncdbT db, ncdbIssueT issue);
uint32_t    ncdb_GetIssueSyncedAt(ncdbT db, ncdbIssueT issue);

/* ── Link iteration ─────────────────────────────────────────────────────── */
int ncdb_WaiverLinkIterate(ncdbT db,
    int (*cb)(ncdbT, const char *, const char *, void *), void *ud);
int ncdb_TestpointLinkIterate(ncdbT db,
    int (*cb)(ncdbT, const char *, const char *, uint8_t, void *), void *ud);
int ncdb_CoverageLinkIterate(ncdbT db,
    int (*cb)(ncdbT, const char *, const char *, const char *, uint8_t, void *),
    void *ud);

/* ── Issue history ──────────────────────────────────────────────────────── */
int ncdb_IssueHistoryIterate(ncdbT db, const char *issue_id,
    int (*cb)(ncdbT, uint32_t, uint8_t, const char *, void *), void *ud);
int ncdb_IssueStateAt(ncdbT db, const char *issue_id, uint32_t ts);

/* ── Coverpoint / Cross UCIS metadata ──────────────────────────────────── */
int         ncdb_SetCoverpointExprTerms(ncdbT db, ncdbScopeT s, const char *expr);
const char *ncdb_GetCoverpointExprTerms(ncdbT db, ncdbScopeT s);
size_t      ncdb_GetCrossArity(ncdbT db, ncdbScopeT cross);  /* == GetCrossPointCount */

/* ── Per-cover UCIS metadata ────────────────────────────────────────────── */
int         ncdb_SetCoverSource (ncdbT db, ncdbCoverT c, const char *path, uint64_t line, uint64_t token);
int         ncdb_SetCoverWeight (ncdbT db, ncdbCoverT c, uint64_t weight);
int         ncdb_SetCoverGoal   (ncdbT db, ncdbCoverT c, int64_t goal);
int         ncdb_SetCoverComment(ncdbT db, ncdbCoverT c, const char *comment);
int         ncdb_SetCoverAtLeast(ncdbT db, ncdbCoverT c, uint64_t at_least);
const char *ncdb_GetCoverSourcePath(ncdbT db, ncdbCoverT c);
uint64_t    ncdb_GetCoverSourceLine(ncdbT db, ncdbCoverT c);
uint64_t    ncdb_GetCoverSourceToken(ncdbT db, ncdbCoverT c);
uint64_t    ncdb_GetCoverWeight (ncdbT db, ncdbCoverT c);
int64_t     ncdb_GetCoverGoal   (ncdbT db, ncdbCoverT c);
const char *ncdb_GetCoverComment(ncdbT db, ncdbCoverT c);
uint64_t    ncdb_GetCoverAtLeast(ncdbT db, ncdbCoverT c);

/* ── UCIS DU↔instance linking ───────────────────────────────────────────── */
int         ncdb_SetScopeDU       (ncdbT db, ncdbScopeT instance, ncdbScopeT du);
ncdbScopeT  ncdb_GetScopeDU       (ncdbT db, ncdbScopeT instance);
int         ncdb_SetDUSignature   (ncdbT db, ncdbScopeT du, const char *sig);
const char *ncdb_GetDUSignature   (ncdbT db, ncdbScopeT du);

/* Per-DU file table (UCIS file numbering is per-DU). Returns DU-local file num
 * (0-based) on success, -1 on error. The path is added to the global sources
 * table if not already present; the DU-local index references it. */
int         ncdb_DuAddFile        (ncdbT db, ncdbScopeT du, const char *path);
size_t      ncdb_DuFileCount      (ncdbT db, ncdbScopeT du);
const char *ncdb_DuGetFile        (ncdbT db, ncdbScopeT du, size_t du_local_idx);

/* ── Scope tag set (UCIS string labels) ─────────────────────────────────── */
int         ncdb_ScopeAddTag(ncdbT db, ncdbScopeT s, const char *tag);
size_t      ncdb_ScopeTagCount(ncdbT db, ncdbScopeT s);
const char *ncdb_ScopeGetTag(ncdbT db, ncdbScopeT s, size_t idx);

/* ── DB-level vendor / UCIS standard identification ────────────────────── */
int         ncdb_SetVendorId         (ncdbT db, const char *v);
int         ncdb_SetVendorTool       (ncdbT db, const char *v);
int         ncdb_SetVendorToolVersion(ncdbT db, const char *v);
int         ncdb_SetUcisStandard     (ncdbT db, const char *v);
const char *ncdb_GetVendorId         (ncdbT db);
const char *ncdb_GetVendorTool       (ncdbT db);
const char *ncdb_GetVendorToolVersion(ncdbT db);
const char *ncdb_GetUcisStandard     (ncdbT db);

/* ── FSM scope metadata (only valid when type == NCDB_SCOPE_FSM) ────────── */
int    ncdb_FsmAddState(ncdbT db, ncdbScopeT fsm, const char *name, uint32_t value);
size_t ncdb_FsmStateCount(ncdbT db, ncdbScopeT fsm);
int    ncdb_FsmGetState(ncdbT db, ncdbScopeT fsm, size_t idx, const char **name, uint32_t *value);

/* ── TOGGLE scope metadata (only valid when type == NCDB_SCOPE_TOGGLE) ──── */
const char *ncdb_GetToggleCanonicalName(ncdbT db, ncdbScopeT s);
uint8_t     ncdb_GetToggleMetric(ncdbT db, ncdbScopeT s);
uint8_t     ncdb_GetToggleType(ncdbT db, ncdbScopeT s);
uint8_t     ncdb_GetToggleDir(ncdbT db, ncdbScopeT s);
int         ncdb_SetToggleCanonicalName(ncdbT db, ncdbScopeT s, const char *name);
void        ncdb_SetToggleMetric(ncdbT db, ncdbScopeT s, uint8_t v);
void        ncdb_SetToggleType(ncdbT db, ncdbScopeT s, uint8_t v);
void        ncdb_SetToggleDir(ncdbT db, ncdbScopeT s, uint8_t v);

/* ── Typed attribute API (UCIS-style property bag) ──────────────────────── */
/* Set: returns 0 on success, -1 on error. String/bytes payloads are copied. */
int ncdb_DbSetAttr     (ncdbT db,                       const char *key, const ncdbAttrValue *v);
int ncdb_ScopeSetAttr  (ncdbT db, ncdbScopeT s,         const char *key, const ncdbAttrValue *v);
int ncdb_CoverSetAttr  (ncdbT db, ncdbCoverT c,         const char *key, const ncdbAttrValue *v);
int ncdb_HistorySetAttr(ncdbT db, ncdbHistoryNodeT h,   const char *key, const ncdbAttrValue *v);

/* Get: returns 0 if found (fills *out, pointers borrow into the table), -1 otherwise. */
int ncdb_DbGetAttr     (ncdbT db,                       const char *key, ncdbAttrValue *out);
int ncdb_ScopeGetAttr  (ncdbT db, ncdbScopeT s,         const char *key, ncdbAttrValue *out);
int ncdb_CoverGetAttr  (ncdbT db, ncdbCoverT c,         const char *key, ncdbAttrValue *out);
int ncdb_HistoryGetAttr(ncdbT db, ncdbHistoryNodeT h,   const char *key, ncdbAttrValue *out);

/* Iterate: callback receives (key, value); return non-zero from cb to stop. Returns count visited. */
int ncdb_DbAttrIterate     (ncdbT db,                     int (*cb)(const char *, const ncdbAttrValue *, void *), void *ud);
int ncdb_ScopeAttrIterate  (ncdbT db, ncdbScopeT s,       int (*cb)(const char *, const ncdbAttrValue *, void *), void *ud);
int ncdb_CoverAttrIterate  (ncdbT db, ncdbCoverT c,       int (*cb)(const char *, const ncdbAttrValue *, void *), void *ud);
int ncdb_HistoryAttrIterate(ncdbT db, ncdbHistoryNodeT h, int (*cb)(const char *, const ncdbAttrValue *, void *), void *ud);

const char  *ncdb_GetLastError(ncdbT db);

#ifdef __cplusplus
}
#endif

#endif
