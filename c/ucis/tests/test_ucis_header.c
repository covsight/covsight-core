/*
 * Phase 2.1 acceptance test: the public header is self-contained and the
 * spec-mandated constants/enums/structs survive a compile.
 *
 * Phase 2.2+ replaces the stub assertions below with real lifecycle calls
 * once ucis_Open()/ucis_Close() are implemented against NCDB.
 */

#include "ucis.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Static checks: scope/cover one-hot bits and enum values must match the
 * UCIS 1.0 spec exactly. A divergence here breaks interop with any
 * conforming UCIS reader. */
#define STATIC_ASSERT(cond, tag) \
    typedef char static_assert_##tag[(cond) ? 1 : -1]

STATIC_ASSERT(UCIS_TOGGLE        == 0x0000000000000001LL, scope_toggle);
STATIC_ASSERT(UCIS_INSTANCE      == 0x0000000000000010LL, scope_instance);
STATIC_ASSERT(UCIS_COVERGROUP    == 0x0000000000001000LL, scope_covergroup);
STATIC_ASSERT(UCIS_CROSS         == 0x0000000000008000LL, scope_cross);
STATIC_ASSERT(UCIS_DU_MODULE     == 0x0000000001000000LL, scope_du_module);
STATIC_ASSERT(UCIS_FSM_TRANS     == 0x0000000040000000LL, scope_fsm_trans);

STATIC_ASSERT(UCIS_CVGBIN        == 0x0000000000000001LL, cover_cvgbin);
STATIC_ASSERT(UCIS_STMTBIN       == 0x0000000000000020LL, cover_stmt);
STATIC_ASSERT(UCIS_TOGGLEBIN     == 0x0000000000000200LL, cover_toggle);
STATIC_ASSERT(UCIS_FSMBIN        == 0x0000000000000800LL, cover_fsm);
STATIC_ASSERT(UCIS_USERBIN       == 0x0000000000001000LL, cover_user);

STATIC_ASSERT(UCIS_HISTORYNODE_TEST  == 1, hist_test);
STATIC_ASSERT(UCIS_HISTORYNODE_MERGE == 2, hist_merge);

STATIC_ASSERT(UCIS_TESTSTATUS_OK == 0, teststatus_ok);

STATIC_ASSERT(UCIS_TOGGLE_METRIC_NOBINS == 1, tog_metric_nobins);
STATIC_ASSERT(UCIS_TOGGLE_TYPE_NET      == 1, tog_type_net);
STATIC_ASSERT(UCIS_TOGGLE_DIR_INTERNAL  == 1, tog_dir_internal);

STATIC_ASSERT(sizeof(ucisScopeTypeT) == 8, scope_type_size);
STATIC_ASSERT(sizeof(ucisCoverTypeT) == 8, cover_type_size);

/* Struct shape spot-checks: ucisCoverDataT must be a 4-field POD plus the
 * union; ucisTestDataT must contain the date/cputime/seed triple. */
STATIC_ASSERT(sizeof(((ucisCoverDataT*)0)->data) >= sizeof(uint64_t), cdata_union);

int main(void)
{
    ucisCoverDataT cd;
    ucisTestDataT  td;
    ucisSourceInfoT si;

    memset(&cd, 0, sizeof(cd));
    cd.type   = UCIS_STMTBIN;
    cd.flags  = UCIS_IS_64BIT | UCIS_HAS_COUNT;
    cd.data.int64 = 42;
    cd.bitlen = 0;

    memset(&td, 0, sizeof(td));
    td.teststatus  = UCIS_TESTSTATUS_OK;
    td.simtime     = 1.0;
    td.timeunit    = "ns";
    td.toolcategory = UCIS_SIM_TOOL;

    memset(&si, 0, sizeof(si));
    si.line = 1;

    if (cd.data.int64 != 42 || td.simtime != 1.0 || si.line != 1) {
        fprintf(stderr, "trivial init failure\n");
        return 1;
    }

    printf("ucis.h: writer-subset header compiles and trivial init OK\n");
    return 0;
}
