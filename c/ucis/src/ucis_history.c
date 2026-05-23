/*
 * History-node creation. Test-data setter lands in Phase 2.7.
 */

#include "ucis_internal.h"
#include "ncdb_impl.h"

#include <stdlib.h>
#include <string.h>

extern ncdbT ucis_internal_get_ncdb(ucisT db);

static void dup_into(char **slot, const char *v)
{
    free(*slot);
    *slot = ncdb_impl_strdup(v ? v : "");
}

ucisHistoryNodeT ucis_CreateHistoryNode(ucisT                db,
                                        ucisHistoryNodeT     parent,
                                        char                *logicalname,
                                        char                *physicalname,
                                        ucisHistoryNodeKindT kind)
{
    ncdbT core = ucis_internal_get_ncdb(db);
    if (!core || !logicalname) return NULL;
    /* UCIS spec: kind takes the predefined UCIS_HISTORYNODE_* #defines, which
     * have the same integer values as NCDB_HISTORY_*. Pass through. */
    ncdbHistoryNodeT h = ncdb_impl_create_history(core, (uint32_t)kind,
                                                  logicalname, physicalname);
    if (!h) return NULL;
    if (parent) {
        ncdb_SetHistoryParent(core, h, (ncdbHistoryNodeT)parent);
    }
    return (ucisHistoryNodeT)h;
}

int ucis_SetTestData(ucisT db, ucisHistoryNodeT node, ucisTestDataT *data)
{
    ncdbT core = ucis_internal_get_ncdb(db);
    if (!core || !node || !data) return -1;
    ncdbHistoryNodeT h = (ncdbHistoryNodeT)node;

    /* Pack the spec's TEST history-node specialization struct in one shot.
     * Mirrors the per-property setters but cheaper and more idiomatic for
     * the typical "I just finished a sim, dump everything" call site. */
    ncdb_SetHistoryTestStatus(core, h, (uint32_t)data->teststatus);
    h->sim_time   = data->simtime;
    h->cpu_time   = data->cputime;
    h->cost       = data->cost;
    h->compulsory = data->compulsory;
    dup_into(&h->time_unit, data->timeunit);
    dup_into(&h->run_cwd,   data->runcwd);
    dup_into(&h->cmd,       data->cmd);
    dup_into(&h->args,      data->args);
    dup_into(&h->date,      data->date);
    ncdb_SetHistorySeed        (core, h, data->seed);
    ncdb_SetHistoryUserName    (core, h, data->username);
    ncdb_SetHistoryToolCategory(core, h, data->toolcategory);
    return 0;
}

int ucis_GetTestData(ucisT db, ucisHistoryNodeT node, ucisTestDataT *data)
{
    ncdbT core = ucis_internal_get_ncdb(db);
    if (!core || !node || !data) return -1;
    ncdbHistoryNodeT h = (ncdbHistoryNodeT)node;

    memset(data, 0, sizeof(*data));
    data->teststatus   = (ucisTestStatusT)ncdb_GetHistoryTestStatus(core, h);
    data->simtime      = h->sim_time;
    data->cputime      = h->cpu_time;
    data->cost         = h->cost;
    data->compulsory   = h->compulsory;
    data->timeunit     = h->time_unit;
    data->runcwd       = h->run_cwd;
    data->cmd          = h->cmd;
    data->args         = h->args;
    data->date         = h->date;
    data->seed         = ncdb_GetHistorySeed        (core, h);
    data->username     = ncdb_GetHistoryUserName    (core, h);
    data->toolcategory = ncdb_GetHistoryToolCategory(core, h);
    return 0;
}
