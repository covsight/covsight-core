/*
 * test_metrics — Phase 4.6 / M6: metric definitions table.
 *
 * Cases:
 *   1. Add definitions; count/get reflect them in insertion order.
 *   2. Round-trip via Write/Open.
 *   3. NCDB_FEATURE_METRICS set when used; member absent otherwise.
 */

#include "ncdb/ncdb.h"
#include "ncdb_impl.h"
#include "ncdb_manifest.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *PATH = "/tmp/test_metrics.cdb";

static void test_in_memory(void)
{
    ncdbT db = ncdb_Open(NULL);
    assert(ncdb_metric_count(db) == 0);
    assert(ncdb_metric_add(db, 1, "ucis://covers/stmt",   3,  NCDB_SCOPE_BLOCK) == 0);
    assert(ncdb_metric_add(db, 2, "ucis://covers/branch", 3,  NCDB_SCOPE_BRANCH) == 0);
    assert(ncdb_metric_count(db) == 2);
    {
        const struct ncdb_metric_def_s *m = ncdb_metric_get(db, 0);
        assert(m && m->metric_id == 1 && strcmp(m->name, "ucis://covers/stmt") == 0);
    }
    assert(ncdb_metric_get(db, 99) == NULL);
    ncdb_Close(db);
}

static void test_roundtrip(void)
{
    unlink(PATH);
    ncdbT db = ncdb_Open(NULL);
    ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "top");
    ncdb_metric_add(db, 10, "ucis://covers/toggle", 1, NCDB_SCOPE_TOGGLE);
    ncdb_metric_add(db, 20, "ucis://covers/fsm",    2, NCDB_SCOPE_FSM);
    ncdb_metric_add(db, 30, "ucis://func/cvg",      3, NCDB_SCOPE_COVERGROUP);
    if (ncdb_Write(db, PATH) != 0) exit(1);
    ncdb_Close(db);

    db = ncdb_Open(PATH);
    assert(db);
    assert((db->feature_flags & NCDB_FEATURE_METRICS) != 0);
    assert(ncdb_metric_count(db) == 3);
    {
        const struct ncdb_metric_def_s *m = ncdb_metric_get(db, 2);
        assert(m && m->metric_id == 30);
        assert(strcmp(m->name, "ucis://func/cvg") == 0);
        assert(m->mode == 3);
        assert(m->target_type_mask == NCDB_SCOPE_COVERGROUP);
    }
    ncdb_Close(db);
    unlink(PATH);
}

static void test_no_metrics_no_member(void)
{
    unlink(PATH);
    ncdbT db = ncdb_Open(NULL);
    ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "top");
    if (ncdb_Write(db, PATH) != 0) exit(1);
    ncdb_Close(db);

    db = ncdb_Open(PATH);
    assert((db->feature_flags & NCDB_FEATURE_METRICS) == 0);
    assert(ncdb_metric_count(db) == 0);
    ncdb_Close(db);
    unlink(PATH);
}

int main(void)
{
    test_in_memory();
    test_roundtrip();
    test_no_metrics_no_member();
    printf("metrics M6 ok\n");
    return 0;
}
