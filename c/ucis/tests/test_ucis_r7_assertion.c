/*
 * test_ucis_r7_assertion — exercises M1 (Phase 4.1): mixed-cover-type
 * bins under a single UCIS_ASSERT scope must (1) all survive write+read
 * round-trip, (2) be addressable as a flat sequence via the UCIS API
 * regardless of how the writer split them, and (3) round-trip without
 * the v3 R7 bug (silent collapse to first cover_type).
 *
 * UCIS assertion scopes legitimately hold:
 *   UCIS_PASSBIN, UCIS_FAILBIN, UCIS_ATTEMPTBIN, UCIS_ACTIVEBIN,
 *   UCIS_PEAKACTIVEBIN, UCIS_DISABLEDBIN, UCIS_VACUOUSBIN
 * as siblings. Pre-M1, scope_tree.bin stored one child_cover_type per
 * parent, so these would collapse to whichever type the first cover
 * happened to carry.
 */

#include "ucis.h"
#include "ncdb/ncdb.h"
#include "ncdb_impl.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern ncdbT ucis_internal_get_ncdb(ucisT);

static const char *TMP = "/tmp/test_ucis_r7_assertion.ncdb";

static int create_cover(ucisT db, ucisScopeT parent, const char *name,
                        ucisCoverTypeT type, uint64_t count)
{
    ucisCoverDataT d;
    memset(&d, 0, sizeof(d));
    d.type = type;
    d.flags = UCIS_IS_64BIT | UCIS_HAS_COUNT;
    d.data.int64 = count;
    return ucis_CreateNextCover(db, parent, name, &d, NULL);
}

/* Five assertion-style bins under one UCIS_ASSERT scope. Verify each
 * survives serialization with its name, type, and count. */
static void test_assertion_mixed_bins_roundtrip(void)
{
    unlink(TMP);

    ucisT db = ucis_Open(NULL);
    ucisScopeT du = ucis_CreateScope(db, NULL, "lib.m", NULL, -1,
                                     UCIS_VLOG, UCIS_DU_MODULE, 0);
    ucisScopeT inst = ucis_CreateInstance(db, NULL, "tb.dut", NULL, -1,
                                          UCIS_VLOG, UCIS_INSTANCE, du, 0);
    ucisScopeT asrt = ucis_CreateScope(db, inst, "my_assert", NULL, 1,
                                       UCIS_SV, UCIS_ASSERT, 0);
    assert(asrt);

    /* Five different bin types as siblings of `asrt`. */
    int i_pass    = create_cover(db, asrt, "pass",       UCIS_PASSBIN,       100);
    int i_fail    = create_cover(db, asrt, "fail",       UCIS_FAILBIN,         2);
    int i_attempt = create_cover(db, asrt, "attempt",    UCIS_ATTEMPTBIN,    150);
    int i_active  = create_cover(db, asrt, "active",     UCIS_ACTIVEBIN,      30);
    int i_peak    = create_cover(db, asrt, "peakactive", UCIS_PEAKACTIVEBIN,   5);

    /* Logical indices stay 0..4 in insertion order, regardless of which
     * synthetic child each cover landed in. */
    assert(i_pass    == 0);
    assert(i_fail    == 1);
    assert(i_attempt == 2);
    assert(i_active  == 3);
    assert(i_peak    == 4);

    /* Logical-view count exposes all 5. */
    assert(ncdb_scope_logical_cover_count((ncdbScopeT)asrt) == 5);

    /* Increment via logical index works for any of them. */
    assert(ucis_IncrementCover(db, asrt, i_fail, 3) == 0);  /* fail: 2 → 5 */

    assert(ucis_Write(db, TMP, NULL, 1, -1) == 0);
    assert(ucis_Close(db) == 0);

    /* Re-open and walk. */
    ucisT db2 = ucis_Open(TMP);
    assert(db2);
    ncdbT core = ucis_internal_get_ncdb(db2);

    /* Locate the reloaded assertion scope (its hierarchy is
     * inst→asrt, plus synthetic R7 children we don't care about here). */
    assert(core->root_count >= 2);  /* du and inst as roots */
    ncdbScopeT inst2 = NULL;
    for (size_t i = 0; i < core->root_count; i++) {
        if (strcmp(core->roots[i]->name, "tb.dut") == 0) { inst2 = core->roots[i]; break; }
    }
    assert(inst2);
    ncdbScopeT asrt2 = NULL;
    for (size_t i = 0; i < inst2->child_count; i++) {
        if (strcmp(inst2->children[i]->name, "my_assert") == 0) {
            asrt2 = inst2->children[i];
            break;
        }
    }
    assert(asrt2);

    /* Through the logical view: all 5 covers present, in insertion
     * order, with their original types and counts. */
    assert(ncdb_scope_logical_cover_count(asrt2) == 5);

    static const struct { const char *name; uint64_t type; uint64_t count; } expected[5] = {
        { "pass",       NCDB_COVER_PASSBIN,       100 },
        { "fail",       NCDB_COVER_FAILBIN,         5 },  /* post-Increment */
        { "attempt",    NCDB_COVER_ATTEMPTBIN,    150 },
        { "active",     NCDB_COVER_ACTIVEBIN,      30 },
        { "peakactive", NCDB_COVER_PEAKACTIVEBIN,   5 },
    };
    for (int k = 0; k < 5; k++) {
        ncdbCoverT c = ncdb_scope_logical_cover_at(asrt2, (size_t)k);
        assert(c);
        if (strcmp(c->name, expected[k].name) != 0) {
            printf("idx %d: name=%s expected=%s\n", k, c->name, expected[k].name);
            assert(0);
        }
        if (c->type != expected[k].type) {
            printf("idx %d (%s): type=0x%llx expected=0x%llx\n",
                   k, c->name,
                   (unsigned long long)c->type,
                   (unsigned long long)expected[k].type);
            assert(0);
        }
        assert(c->count == expected[k].count);
    }

    assert(ucis_Close(db2) == 0);
    unlink(TMP);
}

/* Single-type case must not introduce a synthetic child. */
static void test_single_type_no_synthetic(void)
{
    ucisT db = ucis_Open(NULL);
    ucisScopeT du = ucis_CreateScope(db, NULL, "lib.m", NULL, -1,
                                     UCIS_VLOG, UCIS_DU_MODULE, 0);
    ucisScopeT inst = ucis_CreateInstance(db, NULL, "tb.dut", NULL, -1,
                                          UCIS_VLOG, UCIS_INSTANCE, du, 0);
    ucisScopeT asrt = ucis_CreateScope(db, inst, "single", NULL, 1,
                                       UCIS_SV, UCIS_ASSERT, 0);
    create_cover(db, asrt, "pass", UCIS_PASSBIN, 1);
    create_cover(db, asrt, "pass_more", UCIS_PASSBIN, 2);

    /* Both covers on the parent; no synthetic children. */
    ncdbScopeT s = (ncdbScopeT)asrt;
    assert(s->cover_count == 2);
    assert(s->child_count == 0);
    assert(ncdb_scope_logical_cover_count(s) == 2);

    assert(ucis_Close(db) == 0);
}

/* The R7 synthetic children must carry UCIS_SCOPE_INTERNAL so any
 * spec-compliant reader filters them from default iteration. */
static void test_synthetics_carry_internal_flag(void)
{
    ucisT db = ucis_Open(NULL);
    ucisScopeT du = ucis_CreateScope(db, NULL, "lib.m", NULL, -1,
                                     UCIS_VLOG, UCIS_DU_MODULE, 0);
    ucisScopeT inst = ucis_CreateInstance(db, NULL, "tb.dut", NULL, -1,
                                          UCIS_VLOG, UCIS_INSTANCE, du, 0);
    ucisScopeT asrt = ucis_CreateScope(db, inst, "a", NULL, 1,
                                       UCIS_SV, UCIS_ASSERT, 0);
    create_cover(db, asrt, "pass", UCIS_PASSBIN, 1);
    create_cover(db, asrt, "fail", UCIS_FAILBIN, 1);

    ncdbScopeT s = (ncdbScopeT)asrt;
    assert(s->child_count == 1);   /* one synthetic for FAILBIN */
    ncdbScopeT synth = s->children[0];
    assert((synth->flags & NCDB_SCOPE_FLAG_INTERNAL) == NCDB_SCOPE_FLAG_INTERNAL);
    assert((synth->flags & NCDB_SCOPE_FLAG_R7_SYNTHETIC) != 0);
    /* Synthetic inherits parent's scope type. */
    assert(synth->type == NCDB_SCOPE_ASSERT);
    /* Synthetic name reflects the bin-type group it holds. */
    assert(strcmp(synth->name, "<fail>") == 0);

    assert(ucis_Close(db) == 0);
}

int main(void)
{
    test_assertion_mixed_bins_roundtrip();
    test_single_type_no_synthetic();
    test_synthetics_carry_internal_flag();
    printf("ucis r7 assertion: M1 mixed-bin assertion scope OK\n");
    return 0;
}
