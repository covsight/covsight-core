/* uw_props.c - ucis_Set{Int,String,Real}Property.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Work item 3.11 of docs/ucis-writer-impl-plan.md.
 *
 * A property is settable only while its target can still absorb it. That is a
 * narrower window than the in-memory UCIS API offers, and it is the price of
 * never buffering an object:
 *
 *   - database properties (obj == NULL) until the first content is written
 *   - history-node properties until the tables flush, since those are resident
 *   - scope properties only while that scope's start tag is still pending
 *   - coveritem properties never; they are emitted atomically from
 *     ucisCoverDataT (see D7)
 *
 * Anything outside its window is UCIS_WRITER_ERR_ORDER, not a silent no-op.
 * A dropped weight or exclusion that nobody reports is a coverage number that
 * is quietly wrong. */

#include "uw_api.h"
#include "uw_cvg.h"
#include "uw_stack.h"
#include "uw_tables.h"
#include "uw_text.h"
#include "uw_xml.h"

#include <stdlib.h>
#include <string.h>

/* Is `obj` one of the resident history nodes? Walking the list is fine: a
 * document has a handful of them, and this only runs on property sets. */
static uw_hist_t* uw_as_hist(uw_db_t* db, ucisObjT obj)
{
    uw_hist_t* h;
    for (h = db->hist_head; h != NULL; h = h->next) {
        if ((ucisObjT)h == obj) {
            return h;
        }
    }
    return NULL;
}

static int uw_set_owned(uw_db_t* db, char** slot, const char* value)
{
    free(*slot);
    *slot = uw_strdup(db, value);
    return (db->err || db->buf.status) ? -1 : 0;
}

/* The innermost element, if it is still accepting attributes. */
static uw_elem_t* uw_pending_scope(uw_db_t* db)
{
    uw_elem_t* top = uw_el_top(db);
    return (top != NULL && top->pending) ? top : NULL;
}

int ucis_SetStringProperty(ucisT dbh, ucisObjT obj, int coverindex,
                           ucisStringPropertyEnumT property, const char* value)
{
    uw_db_t*   db = uw_db_check(dbh);
    uw_hist_t* h;

    if (db == NULL) {
        return -1;
    }
    if (coverindex >= 0) {
        return uw_fail(db, UCIS_WRITER_ERR_ORDER,
                       "coveritem properties must be set in ucisCoverDataT",
                       NULL) ? -1 : 0;
    }

    if (obj == NULL) {
        /* Database-wide defaults, inherited by every history node that does
         * not override them. */
        switch (property) {
            case UCIS_STR_VER_VENDOR_ID:      return uw_set_owned(db, &db->vendor_id, value);
            case UCIS_STR_VER_VENDOR_TOOL:    return uw_set_owned(db, &db->vendor_tool, value);
            case UCIS_STR_VER_VENDOR_VERSION: return uw_set_owned(db, &db->vendor_version, value);
            default: break;
        }
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "unsupported database string property", NULL) ? -1 : 0;
    }

    h = uw_as_hist(db, obj);
    if (h != NULL) {
        if (db->phase != UW_PHASE_TABLES) {
            return uw_fail(db, UCIS_WRITER_ERR_SEALED,
                           "history node modified after it was written", NULL) ? -1 : 0;
        }
        switch (property) {
            case UCIS_STR_HIST_LOG_NAME:      return uw_set_owned(db, &h->logicalname, value);
            case UCIS_STR_HIST_PHYS_NAME:     return uw_set_owned(db, &h->physicalname, value);
            case UCIS_STR_HIST_TOOLCATEGORY:  return uw_set_owned(db, &h->toolcategory, value);
            case UCIS_STR_HIST_CMDLINE:       return uw_set_owned(db, &h->cmd, value);
            case UCIS_STR_HIST_RUNCWD:        return uw_set_owned(db, &h->runcwd, value);
            case UCIS_STR_COMMENT:            return uw_set_owned(db, &h->comment, value);
            case UCIS_STR_TEST_TIMEUNIT:      return uw_set_owned(db, &h->timeunit, value);
            case UCIS_STR_TEST_DATE:          return uw_set_owned(db, &h->date, value);
            case UCIS_STR_TEST_SIMARGS:       return uw_set_owned(db, &h->args, value);
            case UCIS_STR_TEST_USERNAME:      return uw_set_owned(db, &h->username, value);
            case UCIS_STR_TEST_SEED:          return uw_set_owned(db, &h->seed, value);
            case UCIS_STR_VER_VENDOR_ID:      return uw_set_owned(db, &h->vendor_id, value);
            case UCIS_STR_VER_VENDOR_TOOL:    return uw_set_owned(db, &h->vendor_tool, value);
            case UCIS_STR_VER_VENDOR_VERSION: return uw_set_owned(db, &h->vendor_version, value);
            default: break;
        }
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "unsupported history-node string property", NULL) ? -1 : 0;
    }

    /* Otherwise it is meant for the current scope. */
    if (uw_pending_scope(db) == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_ORDER,
                       "scope property set after the scope's attributes were written",
                       NULL) ? -1 : 0;
    }
    if (property == UCIS_STR_ITH_CROSSED_CVP_NAME) {
        if (uw_pending_scope(db)->type != UCIS_CROSS) {
            return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                           "crossed coverpoint named on a scope that is not a cross",
                           value) ? -1 : 0;
        }
        return uw_cross_add_cvp(db, value) == UCIS_WRITER_OK ? 0 : -1;
    }
    switch (property) {
        case UCIS_STR_INSTANCE_DU_NAME:
            uw_text_attr(&db->buf, "moduleName", value, &db->warnings);
            return 0;
        case UCIS_STR_TOGGLE_CANON_NAME:
        case UCIS_STR_UNIQUE_ID:
            uw_text_attr(&db->buf, "key", value, &db->warnings);
            return 0;
        case UCIS_STR_UNIQUE_ID_ALIAS:
            uw_text_attr(&db->buf, "alias", value, &db->warnings);
            return 0;
        case UCIS_STR_EXPR_TERMS:
            /* Not an attribute: this becomes EXPR's subExpr+ children and its
             * @width, both emitted when the start tag is terminated. */
            uw_copy_bounded(db, db->expr_terms, sizeof(db->expr_terms), value);
            db->expr_terms_explicit = 1;
            return 0;
        default: break;
    }
    return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                   "unsupported scope string property", NULL) ? -1 : 0;
}

int ucis_SetIntProperty(ucisT dbh, ucisObjT obj, int coverindex,
                        ucisIntPropertyEnumT property, int value)
{
    uw_db_t*   db = uw_db_check(dbh);
    uw_hist_t* h;

    if (db == NULL) {
        return -1;
    }
    if (coverindex >= 0) {
        return uw_fail(db, UCIS_WRITER_ERR_ORDER,
                       "coveritem properties must be set in ucisCoverDataT",
                       NULL) ? -1 : 0;
    }

    h = obj ? uw_as_hist(db, obj) : NULL;
    if (h != NULL) {
        if (db->phase != UW_PHASE_TABLES) {
            return uw_fail(db, UCIS_WRITER_ERR_SEALED,
                           "history node modified after it was written", NULL) ? -1 : 0;
        }
        switch (property) {
            case UCIS_INT_TEST_STATUS:
                h->teststatus = (ucisTestStatusT)value;
                h->has_testdata = 1;
                return 0;
            case UCIS_INT_TEST_COMPULSORY:
                h->compulsory = value;
                h->has_testdata = 1;
                return 0;
            default: break;
        }
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "unsupported history-node int property", NULL) ? -1 : 0;
    }

    if (obj == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "unsupported database int property", NULL) ? -1 : 0;
    }

    if (uw_pending_scope(db) == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_ORDER,
                       "scope property set after the scope's attributes were written",
                       NULL) ? -1 : 0;
    }
    /* On a covergroup scope these are not attributes at all: they belong in
     * the <options> element, which is emitted when the start tag closes. That
     * includes weight and goal, which COVERPOINT and CROSS carry only there. */
    if (uw_pending_scope(db)->needs_options != UW_OPT_NONE) {
        if (uw_opts_set(db, uw_pending_scope(db)->needs_options, property,
                        value)) {
            return 0;
        }
    }
    switch (property) {
        case UCIS_INT_SCOPE_WEIGHT:
            /* @weight defaults to 1 throughout the schema; emitting the
             * default would add bytes to every element for no information. */
            if (value != 1) {
                uw_text_attr_i64(&db->buf, "weight", value);
            }
            return 0;
        default: break;
    }
    return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                   "unsupported scope int property", NULL) ? -1 : 0;
}

int ucis_SetRealProperty(ucisT dbh, ucisObjT obj, int coverindex,
                         ucisRealPropertyEnumT property, double value)
{
    uw_db_t*   db = uw_db_check(dbh);
    uw_hist_t* h;

    if (db == NULL) {
        return -1;
    }
    if (coverindex >= 0) {
        return uw_fail(db, UCIS_WRITER_ERR_ORDER,
                       "coveritem properties must be set in ucisCoverDataT",
                       NULL) ? -1 : 0;
    }
    h = obj ? uw_as_hist(db, obj) : NULL;
    if (h == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                       "real properties apply to history nodes only", NULL) ? -1 : 0;
    }
    if (db->phase != UW_PHASE_TABLES) {
        return uw_fail(db, UCIS_WRITER_ERR_SEALED,
                       "history node modified after it was written", NULL) ? -1 : 0;
    }
    h->has_testdata = 1;
    switch (property) {
        case UCIS_REAL_HIST_CPUTIME:  h->cputime = value; return 0;
        case UCIS_REAL_TEST_SIMTIME:  h->simtime = value; return 0;
        case UCIS_REAL_TEST_COST:     h->cost    = value; return 0;
        default: break;
    }
    return uw_fail(db, UCIS_WRITER_ERR_USAGE,
                   "unsupported real property", NULL) ? -1 : 0;
}
