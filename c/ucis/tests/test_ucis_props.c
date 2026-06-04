/*
 * test_props — Phase 4.2 / M2: typed-property table.
 *
 * Cases:
 *   1. In-memory set/get of int32/int64/double/string by prop_id.
 *   2. End-to-end round trip: write a DB with typed props on a scope
 *      and a cover, reopen, verify values survive.
 *   3. Presence-bit gating: a DB with no typed props writes zero
 *      typed-prop bytes (verified by feature_flags staying clear).
 *   4. UCIS shim integration: ucis_SetIntProperty / SetStringProperty
 *      for long-tail enums lands in the typed-prop table, and
 *      ucis_GetIntProperty / GetStringProperty reads it back.
 */

#include "ncdb/ncdb.h"
#include "ncdb/ncdb_props.h"
#include "ncdb_impl.h"
#include "ncdb_manifest.h"

#include "ucis.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern ncdbT ucis_internal_get_ncdb(ucisT);

static const char *PATH = "/tmp/test_props.cdb";

static int test_in_memory_table(void) {
    ncdb_prop_table_t t;
    int64_t i; double d; const char *s;
    ncdb_prop_table_init(&t);

    assert(ncdb_prop_table_set_int32 (&t, NCDB_PROP_INT_CVG_AUTOBINMAX,     64) == 0);
    assert(ncdb_prop_table_set_int64 (&t, NCDB_PROP_INT_FSM_STATEVAL, 0x1FFFFFFFFLL) == 0);
    assert(ncdb_prop_table_set_double(&t, NCDB_PROP_REAL_CVG_INST_AVERAGE,  87.5) == 0);
    assert(ncdb_prop_table_set_string(&t, NCDB_PROP_STR_TEST_HOSTNAME, "host42") == 0);
    assert(t.count == 4);

    assert(ncdb_prop_table_get_int   (&t, NCDB_PROP_INT_CVG_AUTOBINMAX, &i) == 0 && i == 64);
    assert(ncdb_prop_table_get_int   (&t, NCDB_PROP_INT_FSM_STATEVAL,   &i) == 0 && i == 0x1FFFFFFFFLL);
    assert(ncdb_prop_table_get_double(&t, NCDB_PROP_REAL_CVG_INST_AVERAGE, &d) == 0 && fabs(d - 87.5) < 1e-9);
    s = ncdb_prop_table_get_string   (&t, NCDB_PROP_STR_TEST_HOSTNAME);
    assert(s && strcmp(s, "host42") == 0);

    /* Setting same id overwrites (and frees old string). */
    assert(ncdb_prop_table_set_string(&t, NCDB_PROP_STR_TEST_HOSTNAME, "host99") == 0);
    s = ncdb_prop_table_get_string(&t, NCDB_PROP_STR_TEST_HOSTNAME);
    assert(s && strcmp(s, "host99") == 0);
    assert(t.count == 4);

    /* Type mismatch on getter → -1 / NULL. */
    assert(ncdb_prop_table_get_int(&t, NCDB_PROP_STR_TEST_HOSTNAME, &i) == -1);

    /* Remove + re-check. */
    assert(ncdb_prop_table_remove(&t, NCDB_PROP_STR_TEST_HOSTNAME) == 0);
    assert(ncdb_prop_table_get_string(&t, NCDB_PROP_STR_TEST_HOSTNAME) == NULL);
    assert(t.count == 3);

    ncdb_prop_table_free(&t);
    return 0;
}

static int test_e2e_roundtrip(void) {
    unlink(PATH);

    ncdbT db = ncdb_Open(NULL);
    ncdbScopeT inst = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "top");
    ncdbScopeT cg   = ncdb_impl_create_scope(db, inst, NCDB_SCOPE_COVERGROUP, "cg");
    ncdbCoverT bin0 = ncdb_impl_create_cover(db, cg, NCDB_COVER_CVGBIN, "bin0", 7);

    /* Scope-level typed props. */
    assert(ncdb_prop_table_set_int32(&cg->props, NCDB_PROP_INT_CVG_AUTOBINMAX,    64) == 0);
    assert(ncdb_prop_table_set_int32(&cg->props, NCDB_PROP_INT_CVG_DETECTOVERLAP, 1)  == 0);
    assert(ncdb_prop_table_set_string(&cg->props, NCDB_PROP_STR_FSM_STATEVAR,
                                      "state_q") == 0);

    /* Cover-level typed props. */
    assert(ncdb_prop_table_set_int32(&bin0->props, NCDB_PROP_INT_STMT_INDEX, 42) == 0);

    if (ncdb_Write(db, PATH) != 0) {
        printf("write failed: %s\n", ncdb_GetLastError(db));
        return 1;
    }
    ncdb_Close(db);

    /* Re-open and confirm. */
    db = ncdb_Open(PATH);
    assert(db);
    assert(db->root_count == 1);
    ncdbScopeT inst2 = db->roots[0];
    assert(strcmp(inst2->name, "top") == 0);
    assert(inst2->child_count == 1);
    ncdbScopeT cg2 = inst2->children[0];
    assert(strcmp(cg2->name, "cg") == 0);

    int64_t i;
    const char *s;
    assert(ncdb_prop_table_get_int(&cg2->props, NCDB_PROP_INT_CVG_AUTOBINMAX, &i) == 0);
    assert(i == 64);
    assert(ncdb_prop_table_get_int(&cg2->props, NCDB_PROP_INT_CVG_DETECTOVERLAP, &i) == 0);
    assert(i == 1);
    s = ncdb_prop_table_get_string(&cg2->props, NCDB_PROP_STR_FSM_STATEVAR);
    assert(s && strcmp(s, "state_q") == 0);

    assert(cg2->cover_count == 1);
    ncdbCoverT bin0_2 = cg2->covers[0];
    assert(ncdb_prop_table_get_int(&bin0_2->props, NCDB_PROP_INT_STMT_INDEX, &i) == 0);
    assert(i == 42);

    /* feature_flags must reflect the typed-prop bit being set. */
    assert((db->feature_flags & NCDB_FEATURE_TYPED_PROPS) != 0);

    ncdb_Close(db);
    unlink(PATH);
    return 0;
}

static int test_presence_gating(void) {
    /* A DB with no typed props must not set the feature_flags bit on
     * write. */
    unlink(PATH);
    ncdbT db = ncdb_Open(NULL);
    ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "top");
    if (ncdb_Write(db, PATH) != 0) return 1;
    ncdb_Close(db);

    db = ncdb_Open(PATH);
    assert(db);
    assert((db->feature_flags & NCDB_FEATURE_TYPED_PROPS) == 0);
    ncdb_Close(db);
    unlink(PATH);
    return 0;
}

static int test_ucis_shim_routing(void) {
    /* ucis_SetIntProperty for a long-tail enum should land in the
     * typed-prop block, not attrs.bin. */
    unlink(PATH);
    ucisT db = ucis_Open(NULL);
    ucisScopeT du   = ucis_CreateScope(db, NULL, "lib.m", NULL, -1, UCIS_VLOG, UCIS_DU_MODULE, 0);
    ucisScopeT inst = ucis_CreateInstance(db, NULL, "tb.dut", NULL, -1, UCIS_VLOG, UCIS_INSTANCE, du, 0);
    ucisScopeT cg   = ucis_CreateScope(db, inst, "cg", NULL, 1, UCIS_SV, UCIS_COVERGROUP, 0);

    assert(ucis_SetIntProperty   (db, cg, -1, UCIS_INT_CVG_AUTOBINMAX, 128) == 0);
    assert(ucis_SetIntProperty   (db, cg, -1, UCIS_INT_CVG_STROBE,       1) == 0);
    assert(ucis_SetStringProperty(db, cg, -1, UCIS_STR_FSM_STATEVAR, "q") == 0);

    assert(ucis_GetIntProperty   (db, cg, -1, UCIS_INT_CVG_AUTOBINMAX) == 128);
    assert(ucis_GetIntProperty   (db, cg, -1, UCIS_INT_CVG_STROBE)       == 1);
    {
        const char *s = ucis_GetStringProperty(db, cg, -1, UCIS_STR_FSM_STATEVAR);
        assert(s && strcmp(s, "q") == 0);
    }

    /* The values should now live in cg->props, not in cg->attrs. */
    ncdbT core = ucis_internal_get_ncdb(db);
    ncdbScopeT cg_n = (ncdbScopeT)cg;
    assert(cg_n->props.count >= 3);

    assert(ucis_Write(db, PATH, NULL, 1, -1) == 0);
    assert(ucis_Close(db) == 0);

    /* Reopen via UCIS shim, query again. */
    ucisT db2 = ucis_Open(PATH);
    assert(db2);
    ncdbT core2 = ucis_internal_get_ncdb(db2);
    /* Find the covergroup scope. */
    ncdbScopeT cg2 = NULL;
    for (size_t i = 0; i < core2->root_count && !cg2; i++) {
        ncdbScopeT r = core2->roots[i];
        for (size_t j = 0; j < r->child_count && !cg2; j++) {
            if (r->children[j]->type == NCDB_SCOPE_COVERGROUP) cg2 = r->children[j];
        }
    }
    assert(cg2);
    assert(ucis_GetIntProperty(db2, (ucisScopeT)cg2, -1, UCIS_INT_CVG_AUTOBINMAX) == 128);
    {
        const char *s = ucis_GetStringProperty(db2, (ucisScopeT)cg2, -1, UCIS_STR_FSM_STATEVAR);
        assert(s && strcmp(s, "q") == 0);
    }
    assert(ucis_Close(db2) == 0);
    unlink(PATH);
    return 0;
}

int main(void) {
    if (test_in_memory_table())     { printf("FAIL: in_memory_table\n");    return 1; }
    if (test_e2e_roundtrip())       { printf("FAIL: e2e_roundtrip\n");      return 2; }
    if (test_presence_gating())     { printf("FAIL: presence_gating\n");    return 3; }
    if (test_ucis_shim_routing())   { printf("FAIL: ucis_shim_routing\n");  return 4; }
    printf("props M2 ok\n");
    return 0;
}
