/*
 * Property API: typed get/set keyed on UCIS_* enum constants, dispatched to
 * the right NCDB field by (object kind, property).
 *
 * Object identification follows the UCIS convention:
 *   obj==NULL                              → database
 *   obj=ucisScopeT, coverindex==-1         → scope
 *   obj=ucisScopeT, coverindex>=0          → coveritem at index
 *   obj=ucisHistoryNodeT                   → history node
 *
 * Read-only properties (UCIS_INT_NUM_TESTS, *_IS_UNDER_*, UCIS_STR_UNIQUE_ID,
 * UCIS_STR_SCOPE_HIER_NAME, UCIS_REAL_CVG_INST_AVERAGE, …) return -1 from
 * setters per spec. Unmodeled long-tail properties fall through to the typed-
 * attribute store keyed by the enum name so nothing is silently dropped.
 *
 * The current writer surface covers everything Verilator/post-#7117 emits and
 * everything the per-cover UCIS metadata exposes. The remaining enums are
 * easy follow-on once Phase 2.6/2.7/2.8 wire them up.
 */

#include "ucis_internal.h"
#include "ncdb_impl.h"
#include "ncdb/ncdb_props.h"

#include <stdlib.h>
#include <string.h>

extern ncdbT ucis_internal_get_ncdb(ucisT db);

/* Phase 4.2 / M2 — mapping from UCIS_INT_* / UCIS_STR_* enum constants
 * to the numeric prop IDs in ncdb_props.h. Returns 0 for properties
 * with no typed-prop ID (caller falls back to attribute storage —
 * which, post-M2, should be empty for known UCIS properties). */
static uint16_t ucis_int_to_prop_id(ucisIntPropertyEnumT p) {
    switch (p) {
    case UCIS_INT_STMT_INDEX:          return NCDB_PROP_INT_STMT_INDEX;
    case UCIS_INT_BRANCH_COUNT:        return NCDB_PROP_INT_BRANCH_COUNT;
    case UCIS_INT_BRANCH_HAS_ELSE:     return NCDB_PROP_INT_BRANCH_HAS_ELSE;
    case UCIS_INT_BRANCH_ISCASE:       return NCDB_PROP_INT_BRANCH_ISCASE;
    case UCIS_INT_FSM_STATEVAL:        return NCDB_PROP_INT_FSM_STATEVAL;
    case UCIS_INT_CVG_AUTOBINMAX:      return NCDB_PROP_INT_CVG_AUTOBINMAX;
    case UCIS_INT_CVG_DETECTOVERLAP:   return NCDB_PROP_INT_CVG_DETECTOVERLAP;
    case UCIS_INT_CVG_NUMPRINTMISSING: return NCDB_PROP_INT_CVG_NUMPRINTMISSING;
    case UCIS_INT_CVG_STROBE:          return NCDB_PROP_INT_CVG_STROBE;
    case UCIS_INT_CVG_PERINSTANCE:     return NCDB_PROP_INT_CVG_PERINSTANCE;
    case UCIS_INT_CVG_GETINSTCOV:      return NCDB_PROP_INT_CVG_GETINSTCOV;
    case UCIS_INT_CVG_MERGEINSTANCES:  return NCDB_PROP_INT_CVG_MERGEINSTANCES;
    default: return 0;
    }
}

static uint16_t ucis_str_to_prop_id(ucisStringPropertyEnumT p) {
    switch (p) {
    case UCIS_STR_GENERIC:           return NCDB_PROP_STR_GENERIC;
    case UCIS_STR_TEST_NAME:         return NCDB_PROP_STR_TEST_NAME;
    case UCIS_STR_TEST_HOSTNAME:     return NCDB_PROP_STR_TEST_HOSTNAME;
    case UCIS_STR_TEST_HOSTOS:       return NCDB_PROP_STR_TEST_HOSTOS;
    case UCIS_STR_DESIGN_VERSION_ID: return NCDB_PROP_STR_DESIGN_VERSION_ID;
    case UCIS_STR_FSM_STATEVAR:      return NCDB_PROP_STR_FSM_STATEVAR;
    case UCIS_STR_UNIQUE_ID_ALIAS:   return NCDB_PROP_STR_UNIQUE_ID_ALIAS;
    default: return 0;
    }
}


/* History-node detection: NCDB stores history nodes in a flat array on the
 * db; we identify obj as a history node iff it's reachable from db->history.
 * That's O(n) per property call but the writer-surface call rate is bounded
 * by the number of history nodes which is small. */
static int is_history_obj(ncdbT core, void *obj)
{
    if (!core || !obj) return 0;
    for (size_t i = 0; i < core->history_count; i++) {
        if ((void*)core->history_nodes[i] == obj) return 1;
    }
    return 0;
}

/* -- Attribute fallback for unmodeled long-tail properties -------------- */

static const char *int_prop_name(ucisIntPropertyEnumT p)
{
    switch (p) {
    case UCIS_INT_STMT_INDEX:           return "UCIS_INT_STMT_INDEX";
    case UCIS_INT_BRANCH_COUNT:         return "UCIS_INT_BRANCH_COUNT";
    case UCIS_INT_FSM_STATEVAL:         return "UCIS_INT_FSM_STATEVAL";
    case UCIS_INT_CVG_AUTOBINMAX:       return "UCIS_INT_CVG_AUTOBINMAX";
    case UCIS_INT_CVG_DETECTOVERLAP:    return "UCIS_INT_CVG_DETECTOVERLAP";
    case UCIS_INT_CVG_NUMPRINTMISSING:  return "UCIS_INT_CVG_NUMPRINTMISSING";
    case UCIS_INT_CVG_STROBE:           return "UCIS_INT_CVG_STROBE";
    case UCIS_INT_CVG_PERINSTANCE:      return "UCIS_INT_CVG_PERINSTANCE";
    case UCIS_INT_CVG_GETINSTCOV:       return "UCIS_INT_CVG_GETINSTCOV";
    case UCIS_INT_CVG_MERGEINSTANCES:   return "UCIS_INT_CVG_MERGEINSTANCES";
    default: return NULL;
    }
}

static const char *str_prop_name(ucisStringPropertyEnumT p)
{
    switch (p) {
    case UCIS_STR_GENERIC:              return "UCIS_STR_GENERIC";
    case UCIS_STR_TEST_NAME:            return "UCIS_STR_TEST_NAME";
    case UCIS_STR_TEST_HOSTNAME:        return "UCIS_STR_TEST_HOSTNAME";
    case UCIS_STR_TEST_HOSTOS:          return "UCIS_STR_TEST_HOSTOS";
    case UCIS_STR_DESIGN_VERSION_ID:    return "UCIS_STR_DESIGN_VERSION_ID";
    case UCIS_STR_FSM_STATEVAR:         return "UCIS_STR_FSM_STATEVAR";
    case UCIS_STR_UNIQUE_ID_ALIAS:      return "UCIS_STR_UNIQUE_ID_ALIAS";
    default: return NULL;
    }
}

static int attr_set_int(ncdbT core, void *obj, int coverindex,
                        const char *key, int64_t val)
{
    ncdbAttrValue a;
    a.type = NCDB_ATTR_INT64;
    a.u.i64 = val;
    if (!obj)                    return ncdb_DbSetAttr(core, key, &a);
    if (coverindex < 0)          return ncdb_ScopeSetAttr(core, (ncdbScopeT)obj, key, &a);
    {
        ncdbScopeT s = (ncdbScopeT)obj;
        if ((size_t)coverindex >= s->cover_count) return -1;
        return ncdb_CoverSetAttr(core, s->covers[coverindex], key, &a);
    }
}

static int attr_set_str(ncdbT core, void *obj, int coverindex,
                        const char *key, const char *val)
{
    ncdbAttrValue a;
    a.type = NCDB_ATTR_STRING;
    a.u.str.s = val ? val : "";
    if (!obj)                    return ncdb_DbSetAttr(core, key, &a);
    if (is_history_obj(core, obj)) return ncdb_HistorySetAttr(core, (ncdbHistoryNodeT)obj, key, &a);
    if (coverindex < 0)          return ncdb_ScopeSetAttr(core, (ncdbScopeT)obj, key, &a);
    {
        ncdbScopeT s = (ncdbScopeT)obj;
        if ((size_t)coverindex >= s->cover_count) return -1;
        return ncdb_CoverSetAttr(core, s->covers[coverindex], key, &a);
    }
}

/* ====================================================================== */
/* Integer property setter                                                */
/* ====================================================================== */

int ucis_SetIntProperty(ucisT                 db,
                        ucisObjT              obj,
                        int                   coverindex,
                        ucisIntPropertyEnumT  property,
                        int                   value)
{
    ncdbT core = ucis_internal_get_ncdb(db);
    if (!core) return -1;

    /* History-node properties */
    if (is_history_obj(core, obj)) {
        ncdbHistoryNodeT h = (ncdbHistoryNodeT)obj;
        switch (property) {
        case UCIS_INT_TEST_STATUS:      ncdb_SetHistoryTestStatus(core, h, (uint32_t)value); return 0;
        case UCIS_INT_TEST_COMPULSORY:  h->compulsory = value; return 0;
        default: break;
        }
        const char *k = int_prop_name(property);
        return k ? attr_set_int(core, h, -1, k, value) : -1;
    }

    /* Coveritem properties */
    if (obj && coverindex >= 0) {
        ncdbScopeT s = (ncdbScopeT)obj;
        ncdbCoverT c = ncdb_scope_logical_cover_at(s, (size_t)coverindex);
        if (!c) return -1;
        switch (property) {
        case UCIS_INT_COVER_GOAL:    return ncdb_SetCoverGoal  (core, c, (int64_t)value);
        case UCIS_INT_COVER_WEIGHT:  return ncdb_SetCoverWeight(core, c, (uint64_t)value);
        case UCIS_INT_COVER_LIMIT:   c->at_least = (uint64_t)value; return 0;
        default: break;
        }
        /* Phase 4.2 / M2: long-tail goes into the typed-property block. */
        {
            uint16_t pid = ucis_int_to_prop_id(property);
            if (pid) return ncdb_prop_table_set_int32(&c->props, pid, value);
        }
        return -1;
    }

    /* Scope properties */
    if (obj && coverindex == -1) {
        ncdbScopeT s = (ncdbScopeT)obj;
        switch (property) {
        case UCIS_INT_SCOPE_WEIGHT:        ncdb_SetScopeWeight(core, s, (uint64_t)value); return 0;
        case UCIS_INT_SCOPE_GOAL:          ncdb_SetScopeGoal  (core, s, (int64_t)value);  return 0;
        case UCIS_INT_SCOPE_SOURCE_TYPE:   s->source_type = value; return 0;
        case UCIS_INT_TOGGLE_TYPE:         ncdb_SetToggleType  (core, s, (uint8_t)value); return 0;
        case UCIS_INT_TOGGLE_DIR:          ncdb_SetToggleDir   (core, s, (uint8_t)value); return 0;
        case UCIS_INT_TOGGLE_METRIC:       ncdb_SetToggleMetric(core, s, (uint8_t)value); return 0;
        case UCIS_INT_CVG_ATLEAST:         s->at_least = (uint64_t)value; return 0;
        default: break;
        }
        {
            uint16_t pid = ucis_int_to_prop_id(property);
            if (pid) return ncdb_prop_table_set_int32(&s->props, pid, value);
        }
        return -1;
    }

    /* DB-level long-tail still goes into the DB attribute table (no
     * per-DB typed-prop block in v4 — M2 covers scope/cover only). */
    {
        const char *k = int_prop_name(property);
        return k ? attr_set_int(core, NULL, -1, k, value) : -1;
    }
}

/* ====================================================================== */
/* Integer property getter                                                */
/* ====================================================================== */

int ucis_GetIntProperty(ucisT                 db,
                        ucisObjT              obj,
                        int                   coverindex,
                        ucisIntPropertyEnumT  property)
{
    ncdbT core = ucis_internal_get_ncdb(db);
    if (!core) return -1;

    if (!obj) {
        if (property == UCIS_INT_NUM_TESTS) return (int)core->history_count;
        return -1;
    }
    if (is_history_obj(core, obj)) {
        ncdbHistoryNodeT h = (ncdbHistoryNodeT)obj;
        switch (property) {
        case UCIS_INT_TEST_STATUS:     return (int)ncdb_GetHistoryTestStatus(core, h);
        case UCIS_INT_TEST_COMPULSORY: return h->compulsory;
        default: return -1;
        }
    }
    if (coverindex >= 0) {
        ncdbScopeT s = (ncdbScopeT)obj;
        ncdbCoverT c = ncdb_scope_logical_cover_at(s, (size_t)coverindex);
        if (!c) return -1;
        switch (property) {
        case UCIS_INT_COVER_GOAL:    return (int)ncdb_GetCoverGoal  (core, c);
        case UCIS_INT_COVER_WEIGHT:  return (int)ncdb_GetCoverWeight(core, c);
        case UCIS_INT_COVER_LIMIT:   return (int)c->at_least;
        default: break;
        }
        {
            uint16_t pid = ucis_int_to_prop_id(property);
            int64_t out;
            if (pid && ncdb_prop_table_get_int(&c->props, pid, &out) == 0)
                return (int)out;
        }
        return -1;
    }
    /* Scope */
    {
        ncdbScopeT s = (ncdbScopeT)obj;
        switch (property) {
        case UCIS_INT_SCOPE_WEIGHT:               return (int)ncdb_GetScopeWeight(core, s);
        case UCIS_INT_SCOPE_GOAL:                 return (int)ncdb_GetScopeGoal  (core, s);
        case UCIS_INT_SCOPE_SOURCE_TYPE:          return s->source_type;
        case UCIS_INT_TOGGLE_TYPE:                return ncdb_GetToggleType  (core, s);
        case UCIS_INT_TOGGLE_DIR:                 return ncdb_GetToggleDir   (core, s);
        case UCIS_INT_TOGGLE_METRIC:              return ncdb_GetToggleMetric(core, s);
        case UCIS_INT_SCOPE_NUM_COVERITEMS:       return (int)s->cover_count;
        case UCIS_INT_NUM_CROSSED_CVPS:           return (int)s->crossed_count;
        case UCIS_INT_SCOPE_IS_UNDER_DU:          return s->du_scope ? 1 : 0;
        case UCIS_INT_CVG_ATLEAST:                return (int)s->at_least;
        default: break;
        }
        {
            uint16_t pid = ucis_int_to_prop_id(property);
            int64_t out;
            if (pid && ncdb_prop_table_get_int(&s->props, pid, &out) == 0)
                return (int)out;
        }
        return -1;
    }
}

/* ====================================================================== */
/* String property setter                                                 */
/* ====================================================================== */

int ucis_SetStringProperty(ucisT                    db,
                           ucisObjT                 obj,
                           int                      coverindex,
                           ucisStringPropertyEnumT  property,
                           const char              *value)
{
    ncdbT core = ucis_internal_get_ncdb(db);
    if (!core) return -1;

    /* DB-level */
    if (!obj) {
        switch (property) {
        case UCIS_STR_VER_VENDOR_ID:        return ncdb_SetVendorId         (core, value);
        case UCIS_STR_VER_VENDOR_TOOL:      return ncdb_SetVendorTool       (core, value);
        case UCIS_STR_VER_VENDOR_VERSION:   return ncdb_SetVendorToolVersion(core, value);
        case UCIS_STR_VER_STANDARD_VERSION: return ncdb_SetUcisStandard     (core, value);
        case UCIS_STR_VER_STANDARD:         return 0;  /* always "UCIS"; accept silently */
        default: break;
        }
        const char *k = str_prop_name(property);
        return k ? attr_set_str(core, NULL, -1, k, value) : -1;
    }

    /* History-node properties */
    if (is_history_obj(core, obj)) {
        ncdbHistoryNodeT h = (ncdbHistoryNodeT)obj;
        switch (property) {
        case UCIS_STR_TEST_USERNAME:    ncdb_SetHistoryUserName    (core, h, value); return 0;
        case UCIS_STR_TEST_SEED:        ncdb_SetHistorySeed        (core, h, value); return 0;
        case UCIS_STR_HIST_TOOLCATEGORY:ncdb_SetHistoryToolCategory(core, h, value); return 0;
        case UCIS_STR_COMMENT:          ncdb_SetHistoryComment     (core, h, value); return 0;
        case UCIS_STR_HIST_CMDLINE:     free(h->cmd);       h->cmd       = ncdb_impl_strdup(value ? value : ""); return 0;
        case UCIS_STR_HIST_RUNCWD:      free(h->run_cwd);   h->run_cwd   = ncdb_impl_strdup(value ? value : ""); return 0;
        case UCIS_STR_TEST_DATE:        free(h->date);      h->date      = ncdb_impl_strdup(value ? value : ""); return 0;
        case UCIS_STR_TEST_SIMARGS:     free(h->args);      h->args      = ncdb_impl_strdup(value ? value : ""); return 0;
        case UCIS_STR_TEST_TIMEUNIT:    free(h->time_unit); h->time_unit = ncdb_impl_strdup(value ? value : ""); return 0;
        default: break;
        }
        const char *k = str_prop_name(property);
        return k ? attr_set_str(core, h, -1, k, value) : -1;
    }

    /* Coveritem */
    if (coverindex >= 0) {
        ncdbScopeT s = (ncdbScopeT)obj;
        ncdbCoverT c = ncdb_scope_logical_cover_at(s, (size_t)coverindex);
        if (!c) return -1;
        if (property == UCIS_STR_COMMENT)
            return ncdb_SetCoverComment(core, c, value);
        {
            uint16_t pid = ucis_str_to_prop_id(property);
            if (pid) return ncdb_prop_table_set_string(&c->props, pid, value);
        }
        return -1;
    }

    /* Scope */
    {
        ncdbScopeT s = (ncdbScopeT)obj;
        switch (property) {
        case UCIS_STR_DU_SIGNATURE:     return ncdb_SetDUSignature        (core, s, value);
        case UCIS_STR_TOGGLE_CANON_NAME:return ncdb_SetToggleCanonicalName(core, s, value);
        case UCIS_STR_EXPR_TERMS:       return ncdb_SetCoverpointExprTerms(core, s, value);
        default: break;
        }
        {
            uint16_t pid = ucis_str_to_prop_id(property);
            if (pid) return ncdb_prop_table_set_string(&s->props, pid, value);
        }
        return -1;
    }
}

/* ====================================================================== */
/* String property getter                                                 */
/* ====================================================================== */

const char *ucis_GetStringProperty(ucisT                    db,
                                   ucisObjT                 obj,
                                   int                      coverindex,
                                   ucisStringPropertyEnumT  property)
{
    ncdbT core = ucis_internal_get_ncdb(db);
    if (!core) return NULL;

    if (!obj) {
        switch (property) {
        case UCIS_STR_FILE_NAME:            return core->path;
        case UCIS_STR_VER_VENDOR_ID:        return ncdb_GetVendorId         (core);
        case UCIS_STR_VER_VENDOR_TOOL:      return ncdb_GetVendorTool       (core);
        case UCIS_STR_VER_VENDOR_VERSION:   return ncdb_GetVendorToolVersion(core);
        case UCIS_STR_VER_STANDARD_VERSION: return ncdb_GetUcisStandard     (core);
        case UCIS_STR_VER_STANDARD:         return "UCIS";
        default: return NULL;
        }
    }
    if (is_history_obj(core, obj)) {
        ncdbHistoryNodeT h = (ncdbHistoryNodeT)obj;
        switch (property) {
        case UCIS_STR_HIST_LOG_NAME:    return ncdb_GetHistoryLogicalName (core, h);
        case UCIS_STR_HIST_PHYS_NAME:   return ncdb_GetHistoryPhysicalName(core, h);
        case UCIS_STR_TEST_USERNAME:    return ncdb_GetHistoryUserName    (core, h);
        case UCIS_STR_TEST_SEED:        return ncdb_GetHistorySeed        (core, h);
        case UCIS_STR_HIST_TOOLCATEGORY:return ncdb_GetHistoryToolCategory(core, h);
        case UCIS_STR_COMMENT:          return ncdb_GetHistoryComment     (core, h);
        case UCIS_STR_HIST_CMDLINE:     return h->cmd;
        case UCIS_STR_HIST_RUNCWD:      return h->run_cwd;
        case UCIS_STR_TEST_DATE:        return h->date;
        case UCIS_STR_TEST_SIMARGS:     return h->args;
        case UCIS_STR_TEST_TIMEUNIT:    return h->time_unit;
        default: return NULL;
        }
    }
    if (coverindex >= 0) {
        ncdbScopeT s = (ncdbScopeT)obj;
        ncdbCoverT c = ncdb_scope_logical_cover_at(s, (size_t)coverindex);
        if (!c) return NULL;
        if (property == UCIS_STR_COMMENT) return ncdb_GetCoverComment(core, c);
        {
            uint16_t pid = ucis_str_to_prop_id(property);
            if (pid) return ncdb_prop_table_get_string(&c->props, pid);
        }
        return NULL;
    }
    {
        ncdbScopeT s = (ncdbScopeT)obj;
        switch (property) {
        case UCIS_STR_SCOPE_NAME:        return ncdb_GetScopeName        (core, s);
        case UCIS_STR_DU_SIGNATURE:      return ncdb_GetDUSignature      (core, s);
        case UCIS_STR_TOGGLE_CANON_NAME: return ncdb_GetToggleCanonicalName(core, s);
        case UCIS_STR_EXPR_TERMS:        return ncdb_GetCoverpointExprTerms(core, s);
        case UCIS_STR_INSTANCE_DU_NAME: {
            ncdbScopeT du = ncdb_GetScopeDU(core, s);
            return du ? ncdb_GetScopeName(core, du) : NULL;
        }
        default: break;
        }
        {
            uint16_t pid = ucis_str_to_prop_id(property);
            if (pid) return ncdb_prop_table_get_string(&s->props, pid);
        }
        return NULL;
    }
}

/* ====================================================================== */
/* Real property setter / getter                                          */
/* ====================================================================== */

int ucis_SetRealProperty(ucisT                  db,
                         ucisObjT               obj,
                         int                    coverindex,
                         ucisRealPropertyEnumT  property,
                         double                 value)
{
    ncdbT core = ucis_internal_get_ncdb(db);
    (void)coverindex;
    if (!core || !obj || !is_history_obj(core, obj)) return -1;
    ncdbHistoryNodeT h = (ncdbHistoryNodeT)obj;
    switch (property) {
    case UCIS_REAL_HIST_CPUTIME: h->cpu_time = value; return 0;
    case UCIS_REAL_TEST_SIMTIME: h->sim_time = value; return 0;
    case UCIS_REAL_TEST_COST:    h->cost     = value; return 0;
    default: return -1;
    }
}

double ucis_GetRealProperty(ucisT                  db,
                            ucisObjT               obj,
                            int                    coverindex,
                            ucisRealPropertyEnumT  property)
{
    ncdbT core = ucis_internal_get_ncdb(db);
    (void)coverindex;
    if (!core || !obj || !is_history_obj(core, obj)) return -1.0;
    ncdbHistoryNodeT h = (ncdbHistoryNodeT)obj;
    switch (property) {
    case UCIS_REAL_HIST_CPUTIME: return h->cpu_time;
    case UCIS_REAL_TEST_SIMTIME: return h->sim_time;
    case UCIS_REAL_TEST_COST:    return h->cost;
    default: return -1.0;
    }
}

/* ====================================================================== */
/* Handle property setter / getter                                        */
/* ====================================================================== */

int ucis_SetHandleProperty(ucisT            db,
                           ucisObjT         obj,
                           ucisHandleEnumT  property,
                           ucisObjT         value)
{
    ncdbT core = ucis_internal_get_ncdb(db);
    if (!core || !obj) return -1;
    switch (property) {
    case UCIS_HANDLE_INSTANCE_DU:
        return ncdb_SetScopeDU(core, (ncdbScopeT)obj, (ncdbScopeT)value);
    case UCIS_HANDLE_HIST_NODE_PARENT:
        return ncdb_SetHistoryParent(core, (ncdbHistoryNodeT)obj,
                                     (ncdbHistoryNodeT)value);
    default:
        return -1;  /* SCOPE_PARENT / SCOPE_TOP / HIST_NODE_ROOT are read-only */
    }
}

/* ucis_GetHandleProperty (polymorphic read) lands in Phase 6 with the full
 * read API. The writer subset only needs the setter (handle-property
 * mirrors the explicit DU-link and history-parent setters). */
