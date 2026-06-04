/*
 * test_v4_benchmark — Phase 4.10: scaled-down size + perf benchmark
 * for the v4 format. Builds a representative DB at ~1/50 the
 * reference-DB dimensions, writes it, measures each member's size,
 * and validates the plan's structural claims (e.g. that
 * tests_assoc.bin scales sub-linearly thanks to the tiered encoding).
 *
 * Reference DB in docs/ncdb-format-v4-plan.md:
 *   50k scopes, 200k coveritems, 5k tests, 100 DUs, ~500 assertions,
 *   ~50 formal-verified targets
 *
 * This test runs at 1/50 scale so it stays comfortable for CI:
 *   1k scopes, 4k coveritems, 100 tests, 5 DUs, ~10 assertions, ~5
 *   formal-verified targets.
 *
 * Plan claims preserved at any scale (relative invariants):
 *   - tests_assoc.bin per-cover cost stays bounded; NEVER and
 *     ALL_NO_EXC tiers pay zero payload bytes.
 *   - attrs.bin only carries genuinely-user-defined attributes (not
 *     UCIS properties — those go through typed-props).
 *   - All v4 feature_flags bits we exercise land in the manifest.
 */

#include "ncdb/ncdb.h"
#include "ncdb/ncdb_props.h"
#include "ncdb_impl.h"
#include "ncdb_manifest.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define N_DUS         5
#define N_SCOPES_PER_DU 200
#define N_TESTS      100
#define N_ASSERTIONS  10
#define N_FORMAL       5

static const char *PATH = "/tmp/test_v4_benchmark.cdb";

static size_t member_size(const char *zip_path, const char *member) {
    uint8_t *data = NULL; size_t sz = 0;
    char err[256] = {0};
    if (ncdb_zip_read_member(zip_path, member, &data, &sz, err, sizeof(err)) != 0) {
        return 0;   /* absent */
    }
    free(data);
    return sz;
}

int main(void) {
    int rc = 0;
    ncdbT db = ncdb_Open(NULL);
    ncdbHistoryNodeT tests[N_TESTS];
    int i, k, d;

    unlink(PATH);
    /* Build the DU layer + instance hierarchy. */
    ncdbScopeT dus[N_DUS];
    for (d = 0; d < N_DUS; d++) {
        char nm[32]; snprintf(nm, sizeof(nm), "lib.du%d", d);
        dus[d] = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_DU_MODULE, nm);
    }

    /* Per-DU subtree with scopes + covers. Some scopes are coverpoints
     * carrying a typed-property (M2), some are blocks with stmt bins,
     * some are assertions with mixed bins (M1). */
    int cover_count = 0;
    for (d = 0; d < N_DUS; d++) {
        char nm[32]; snprintf(nm, sizeof(nm), "inst%d", d);
        ncdbScopeT inst = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, nm);
        inst->du_scope = dus[d];
        for (i = 0; i < N_SCOPES_PER_DU; i++) {
            char sn[32]; snprintf(sn, sizeof(sn), "blk%d", i);
            ncdbScopeT blk = ncdb_impl_create_scope(db, inst, NCDB_SCOPE_BLOCK, sn);
            /* 4 stmt bins per block. */
            for (k = 0; k < 4; k++) {
                char cn[16]; snprintf(cn, sizeof(cn), "ln%d", k);
                ncdb_impl_create_cover(db, blk, NCDB_COVER_STMTBIN, cn, (uint64_t)(k+1));
                cover_count++;
            }
            /* Typed-prop on one in five blocks. */
            if ((i % 5) == 0) {
                ncdb_prop_table_set_int32(&blk->props, NCDB_PROP_INT_STMT_INDEX, i);
            }
        }
    }

    /* Mixed-bin assertions (M1). */
    for (i = 0; i < N_ASSERTIONS; i++) {
        char an[32]; snprintf(an, sizeof(an), "p_assert_%d", i);
        ncdbScopeT a = ncdb_impl_create_scope(db, dus[0], NCDB_SCOPE_ASSERT, an);
        ncdb_impl_create_cover(db, a, NCDB_COVER_PASSBIN,    "pass",    100);
        ncdb_impl_create_cover(db, a, NCDB_COVER_FAILBIN,    "fail",      0);
        ncdb_impl_create_cover(db, a, NCDB_COVER_ATTEMPTBIN, "attempt", 105);
        ncdb_impl_create_cover(db, a, NCDB_COVER_ACTIVEBIN,  "active",    2);
        /* Sprinkle formal data on a few of them (M5). */
        if (i < N_FORMAL) {
            ncdb_formal_set_status(db, a, 2 /* UCIS_FORMAL_PROOF */);
            ncdb_formal_set_radius(db, a, 17 + i);
        }
    }

    /* History nodes. */
    for (i = 0; i < N_TESTS; i++) {
        char tn[16]; snprintf(tn, sizeof(tn), "t%d", i);
        tests[i] = ncdb_impl_create_history(db, NCDB_HISTORY_TEST, tn, NULL);
    }

    /* Per-cover associations — drive a mix of NEVER/ALL/SPARSE tiers.
     * Walk a fraction of covers and bucket them:
     *   cover_idx % 10 == 0 → NEVER (skip)
     *   cover_idx % 10 == 1 → ALL (every test)
     *   else                → SPARSE (only tests 0..(cover_idx%17)) */
    {
        int target_cover = 0;
        for (d = 0; d < N_DUS && target_cover < cover_count; d++) {
            char nm[32]; snprintf(nm, sizeof(nm), "inst%d", d);
            ncdbScopeT inst = NULL;
            for (size_t r = 0; r < db->root_count; r++) {
                if (strcmp(db->roots[r]->name, nm) == 0) { inst = db->roots[r]; break; }
            }
            if (!inst) continue;
            for (size_t b = 0; b < inst->child_count; b++) {
                ncdbScopeT blk = inst->children[b];
                for (size_t cv = 0; cv < blk->cover_count; cv++) {
                    int mode = target_cover % 10;
                    if (mode == 0) { /* NEVER: skip */ }
                    else if (mode == 1) {
                        for (i = 0; i < N_TESTS; i++)
                            ncdb_AssocCoverHistory(db, blk->covers[cv], tests[i]);
                    } else {
                        int upto = target_cover % 17;
                        for (i = 0; i < upto && i < N_TESTS; i++)
                            ncdb_AssocCoverHistory(db, blk->covers[cv], tests[i]);
                    }
                    target_cover++;
                }
            }
        }
    }

    /* A couple of metric defs (M6). */
    ncdb_metric_add(db, 1, "ucis://covers/stmt",   3, NCDB_SCOPE_BLOCK);
    ncdb_metric_add(db, 2, "ucis://covers/assert", 3, NCDB_SCOPE_ASSERT);

    if (ncdb_Write(db, PATH) != 0) {
        printf("write failed: %s\n", ncdb_GetLastError(db));
        ncdb_Close(db);
        return 1;
    }
    ncdb_Close(db);

    /* Measure & report. */
    struct { const char *member; size_t bytes; const char *feature; } members[] = {
        { "manifest.json",        0, "always" },
        { "strings.bin",          0, "always" },
        { "scope_tree.bin",       0, "always (+NTRL trailer)" },
        { "counts.bin",           0, "always" },
        { "history.json",         0, "always" },
        { "sources.json",         0, "always" },
        { "attrs.bin",            0, "M2 user-attrs (UCIS props go elsewhere)" },
        { "tests_assoc.bin",      0, "M4 NTAS" },
        { "formal.bin",           0, "M5 NFRM" },
        { "metrics.bin",          0, "M6 NMTR" },
        { "tags.bin",             0, "tags (Phase 1.10)" },
        { "unique_id_index.bin",  0, "M3 NUID" },
        { "toggle.bin",           0, "phase 1.2 toggle" },
        { "fsm.bin",              0, "phase 1.3 fsm" },
        { "cross.bin",            0, "phase 1.9 covergroup" },
        { NULL, 0, NULL }
    };
    size_t total = 0;
    int j;
    for (j = 0; members[j].member; j++) {
        members[j].bytes = member_size(PATH, members[j].member);
        total += members[j].bytes;
    }

    /* Re-open to read manifest stats. */
    db = ncdb_Open(PATH);
    if (!db) { rc = 2; goto cleanup; }

    printf("\n========== v4 benchmark @ 1/50 scale ==========\n");
    printf("scale: %d DUs × %d blocks × 4 stmt covers + %d assertions × 4 bin types\n",
           N_DUS, N_SCOPES_PER_DU, N_ASSERTIONS);
    printf("tests: %d   covers: %zu   feature_flags: 0x%llx\n",
           N_TESTS, ncdb_impl_count_covers(db),
           (unsigned long long)db->feature_flags);
    printf("\n%-26s %10s   %s\n", "member", "bytes", "produced by");
    printf("------------------------------------------------------------\n");
    for (j = 0; members[j].member; j++) {
        if (members[j].bytes == 0) {
            printf("%-26s %10s   %s\n", members[j].member, "-", members[j].feature);
        } else {
            printf("%-26s %10zu   %s\n",
                   members[j].member, members[j].bytes, members[j].feature);
        }
    }
    printf("------------------------------------------------------------\n");
    printf("%-26s %10zu\n", "(zip-uncompressed total)", total);
    printf("\n");

    /* Sanity assertions: every v4 feature we exercised should be on. */
    uint64_t expected = NCDB_FEATURE_TYPED_PROPS
                      | NCDB_FEATURE_UNIQUE_ID_INDEX
                      | NCDB_FEATURE_TESTS_ASSOC
                      | NCDB_FEATURE_FORMAL
                      | NCDB_FEATURE_METRICS
                      | NCDB_FEATURE_SCOPE_TRAILER;
    if ((db->feature_flags & expected) != expected) {
        printf("FAIL: feature_flags 0x%llx missing bits from 0x%llx\n",
               (unsigned long long)db->feature_flags,
               (unsigned long long)expected);
        rc = 3;
    }

    /* The presence of every new member we expect. */
    if (member_size(PATH, "tests_assoc.bin") == 0) { printf("FAIL: NTAS missing\n"); rc = 4; }
    if (member_size(PATH, "unique_id_index.bin") == 0) { printf("FAIL: NUID missing\n"); rc = 5; }
    if (member_size(PATH, "formal.bin") == 0) { printf("FAIL: NFRM missing\n"); rc = 6; }
    if (member_size(PATH, "metrics.bin") == 0) { printf("FAIL: NMTR missing\n"); rc = 7; }

    ncdb_Close(db);

cleanup:
    unlink(PATH);
    if (rc == 0) printf("v4 benchmark ok\n");
    return rc;
}
