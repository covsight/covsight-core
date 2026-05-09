#ifndef INCLUDED_NCDB_H
#define INCLUDED_NCDB_H

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

size_t       ncdb_GetCrossPointCount(ncdbT db, ncdbScopeT scope);
ncdbScopeT   ncdb_GetCrossPoint(ncdbT db, ncdbScopeT scope, size_t idx);

const char  *ncdb_GetLastError(ncdbT db);

#ifdef __cplusplus
}
#endif

#endif
