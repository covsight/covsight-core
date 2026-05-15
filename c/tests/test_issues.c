#include <stdio.h>
#include <string.h>

#include "unity.h"
#include "ncdb_impl.h"

typedef struct {
    int count;
    ncdbIssueT issues[8];
} issue_collect_t;

typedef struct {
    int count;
    const char *waiver_id;
    const char *issue_id;
} waiver_collect_t;

typedef struct {
    int count;
    const char *tp_name;
    const char *issue_id;
    uint8_t link_type;
} tp_collect_t;

typedef struct {
    int count;
    const char *scope_path;
    const char *bin_name;
    const char *issue_id;
    uint8_t link_type;
} cov_collect_t;

typedef struct {
    int count;
    uint32_t ts[8];
    uint8_t state[8];
    char comment[8][32];
} hist_collect_t;

static int collect_issue_cb(ncdbT db, ncdbIssueT issue, void *ud) {
    issue_collect_t *c = (issue_collect_t *)ud;
    (void)db;
    c->issues[c->count++] = issue;
    return 0;
}

static int collect_waiver_cb(ncdbT db, const char *waiver_id, const char *issue_id, void *ud) {
    waiver_collect_t *c = (waiver_collect_t *)ud;
    (void)db;
    c->count++;
    c->waiver_id = waiver_id;
    c->issue_id = issue_id;
    return 0;
}

static int collect_tp_cb(ncdbT db, const char *tp_name, const char *issue_id, uint8_t link_type, void *ud) {
    tp_collect_t *c = (tp_collect_t *)ud;
    (void)db;
    c->count++;
    c->tp_name = tp_name;
    c->issue_id = issue_id;
    c->link_type = link_type;
    return 0;
}

static int collect_cov_cb(ncdbT db, const char *scope_path, const char *bin_name,
        const char *issue_id, uint8_t link_type, void *ud) {
    cov_collect_t *c = (cov_collect_t *)ud;
    (void)db;
    c->count++;
    c->scope_path = scope_path;
    c->bin_name = bin_name;
    c->issue_id = issue_id;
    c->link_type = link_type;
    return 0;
}

static int collect_hist_cb(ncdbT db, uint32_t ts, uint8_t state, const char *comment, void *ud) {
    hist_collect_t *c = (hist_collect_t *)ud;
    (void)db;
    c->ts[c->count] = ts;
    c->state[c->count] = state;
    snprintf(c->comment[c->count], sizeof(c->comment[c->count]), "%s", comment ? comment : "");
    c->count++;
    return 0;
}

static void test_issue_read_api(void) {
    ncdbT db = ncdb_Open("issues_fixture.cdb");
    issue_collect_t all = {0};
    issue_collect_t open = {0};
    issue_collect_t sev_low = {0};
    waiver_collect_t wl = {0};
    tp_collect_t tl = {0};
    cov_collect_t cl = {0};
    hist_collect_t hist = {0};
    ncdbIssueT issue;

    TEST_ASSERT(db != NULL);
    TEST_ASSERT_EQUAL_STRING("", ncdb_GetLastError(db));
    TEST_ASSERT(db->issues != NULL);

    TEST_ASSERT_EQUAL_UINT64(0, ncdb_IssueIterate(db, collect_issue_cb, &all));
    TEST_ASSERT_EQUAL_UINT64(2, all.count);

    issue = ncdb_GetIssueById(db, "I-001");
    TEST_ASSERT(issue != NULL);
    TEST_ASSERT_EQUAL_STRING("I-001", ncdb_GetIssueId(db, issue));
    TEST_ASSERT_EQUAL_STRING("EXT-1", ncdb_GetIssueExt(db, issue));
    TEST_ASSERT_EQUAL_UINT64(NCDB_ISSUE_SEV_HIGH, ncdb_GetIssueSeverity(db, issue));
    TEST_ASSERT_EQUAL_UINT64(NCDB_ISSUE_KIND_DESIGN_BUG, ncdb_GetIssueKind(db, issue));
    TEST_ASSERT_EQUAL_UINT64(NCDB_ISSUE_STATE_OPEN, ncdb_GetIssueState(db, issue));
    TEST_ASSERT_EQUAL_UINT64(NCDB_ISSUE_RES_NONE, ncdb_GetIssueResolution(db, issue));
    TEST_ASSERT_EQUAL_UINT64(1700000000U, ncdb_GetIssueCreatedAt(db, issue));
    TEST_ASSERT_EQUAL_UINT64(1700000001U, ncdb_GetIssueUpdatedAt(db, issue));
    TEST_ASSERT_EQUAL_UINT64(1700000002U, ncdb_GetIssueSyncedAt(db, issue));
    TEST_ASSERT(ncdb_GetIssueById(db, "missing") == NULL);

    TEST_ASSERT_EQUAL_UINT64(0, ncdb_IssueIterateOpen(db, collect_issue_cb, &open));
    TEST_ASSERT_EQUAL_UINT64(1, open.count);
    TEST_ASSERT_EQUAL_STRING("I-001", ncdb_GetIssueId(db, open.issues[0]));

    TEST_ASSERT_EQUAL_UINT64(0, ncdb_IssueIterateBySeverity(db, NCDB_ISSUE_SEV_LOW, collect_issue_cb, &sev_low));
    TEST_ASSERT_EQUAL_UINT64(1, sev_low.count);
    TEST_ASSERT_EQUAL_STRING("I-002", ncdb_GetIssueId(db, sev_low.issues[0]));

    TEST_ASSERT_EQUAL_UINT64(0, ncdb_WaiverLinkIterate(db, collect_waiver_cb, &wl));
    TEST_ASSERT_EQUAL_UINT64(1, wl.count);
    TEST_ASSERT_EQUAL_STRING("W-001", wl.waiver_id);
    TEST_ASSERT_EQUAL_STRING("I-001", wl.issue_id);

    TEST_ASSERT_EQUAL_UINT64(0, ncdb_TestpointLinkIterate(db, collect_tp_cb, &tl));
    TEST_ASSERT_EQUAL_UINT64(1, tl.count);
    TEST_ASSERT_EQUAL_STRING("tp.alpha", tl.tp_name);
    TEST_ASSERT_EQUAL_STRING("I-001", tl.issue_id);
    TEST_ASSERT_EQUAL_UINT64(NCDB_LINK_BLOCKED_BY, tl.link_type);

    TEST_ASSERT_EQUAL_UINT64(0, ncdb_CoverageLinkIterate(db, collect_cov_cb, &cl));
    TEST_ASSERT_EQUAL_UINT64(1, cl.count);
    TEST_ASSERT_EQUAL_STRING("top.block.cp", cl.scope_path);
    TEST_ASSERT_EQUAL_STRING("bin0", cl.bin_name);
    TEST_ASSERT_EQUAL_STRING("I-001", cl.issue_id);
    TEST_ASSERT_EQUAL_UINT64(NCDB_LINK_RELATED, cl.link_type);

    TEST_ASSERT_EQUAL_UINT64(0, ncdb_IssueHistoryIterate(db, "I-001", collect_hist_cb, &hist));
    TEST_ASSERT_EQUAL_UINT64(3, hist.count);
    TEST_ASSERT_EQUAL_UINT64(1700000100U, hist.ts[0]);
    TEST_ASSERT_EQUAL_UINT64(1700000200U, hist.ts[1]);
    TEST_ASSERT_EQUAL_UINT64(1700000300U, hist.ts[2]);
    TEST_ASSERT_EQUAL_UINT64(NCDB_ISSUE_STATE_OPEN, hist.state[0]);
    TEST_ASSERT_EQUAL_UINT64(NCDB_ISSUE_STATE_IN_PROGRESS, hist.state[1]);
    TEST_ASSERT_EQUAL_UINT64(NCDB_ISSUE_STATE_RESOLVED, hist.state[2]);
    TEST_ASSERT_EQUAL_STRING("", hist.comment[0]);
    TEST_ASSERT_EQUAL_STRING("triaged", hist.comment[1]);
    TEST_ASSERT_EQUAL_STRING("fixed", hist.comment[2]);

    TEST_ASSERT_EQUAL_UINT64(NCDB_ISSUE_STATE_IN_PROGRESS, ncdb_IssueStateAt(db, "I-001", 1700000200U));
    TEST_ASSERT_EQUAL_UINT64(NCDB_ISSUE_STATE_OPEN, ncdb_IssueStateAt(db, "I-001", 1700000150U));
    TEST_ASSERT(ncdb_IssueStateAt(db, "I-001", 1700000000U) == -1);
    TEST_ASSERT_EQUAL_UINT64(0, ncdb_IssueHistoryIterate(db, "I-404", collect_hist_cb, &(hist_collect_t){0}));

    ncdb_Close(db);
}

static void test_no_issues_db(void) {
    ncdbT db = ncdb_Open("roundtrip.cdb");
    issue_collect_t all = {0};

    TEST_ASSERT(db != NULL);
    TEST_ASSERT_EQUAL_STRING("", ncdb_GetLastError(db));
    TEST_ASSERT(db->issues == NULL);
    TEST_ASSERT_EQUAL_UINT64(0, ncdb_IssueIterate(db, collect_issue_cb, &all));
    TEST_ASSERT_EQUAL_UINT64(0, all.count);
    TEST_ASSERT(ncdb_GetIssueById(db, "I-001") == NULL);
    TEST_ASSERT_EQUAL_UINT64(0, ncdb_WaiverLinkIterate(db, collect_waiver_cb, &(waiver_collect_t){0}));
    TEST_ASSERT_EQUAL_UINT64(0, ncdb_TestpointLinkIterate(db, collect_tp_cb, &(tp_collect_t){0}));
    TEST_ASSERT_EQUAL_UINT64(0, ncdb_CoverageLinkIterate(db, collect_cov_cb, &(cov_collect_t){0}));
    TEST_ASSERT_EQUAL_UINT64(0, ncdb_IssueHistoryIterate(db, "I-001", collect_hist_cb, &(hist_collect_t){0}));
    TEST_ASSERT(ncdb_IssueStateAt(db, "I-001", 1) == -1);
    ncdb_Close(db);
}

int main(void) {
    RUN_TEST(test_issue_read_api);
    RUN_TEST(test_no_issues_db);
    return 0;
}
