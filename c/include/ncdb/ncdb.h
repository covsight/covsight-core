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

int  ncdb_CoverIterate(ncdbT db, ncdbScopeT scope,
                       int (*cb)(ncdbT, ncdbCoverT, void *), void *ud);
const char  *ncdb_GetCoverName(ncdbT db, ncdbCoverT cover);
uint32_t     ncdb_GetCoverType(ncdbT db, ncdbCoverT cover);
uint64_t     ncdb_GetCoverCount(ncdbT db, ncdbCoverT cover);

int  ncdb_HistoryIterate(ncdbT db, uint32_t kind_mask,
                         int (*cb)(ncdbT, ncdbHistoryNodeT, void *), void *ud);
const char  *ncdb_GetHistoryLogicalName(ncdbT db, ncdbHistoryNodeT node);

const char  *ncdb_GetLastError(ncdbT db);

#ifdef __cplusplus
}
#endif

#endif
