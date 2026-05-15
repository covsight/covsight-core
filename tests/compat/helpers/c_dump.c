/**
 * c_dump.c — C NCDB dump helper for cross-compat tests.
 *
 * Usage:
 *   ./c_dump read <file>              → canonical JSON to stdout
 *   ./c_dump write <scenario> <file>  → write scenario .cdb
 *
 * Links against libncdb (public API) and uses ncdb_impl.h for write paths.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ncdb/ncdb.h"
#include "ncdb_impl.h"

/* -------------------------------------------------------------------------
 * Minimal JSON emission helpers (no external dependency)
 * ---------------------------------------------------------------------- */

static void jstr(const char *s) {
    /* Write a JSON-quoted string (ASCII-safe for our test names). */
    putchar('"');
    if (s) {
        for (; *s; s++) {
            if (*s == '"') { putchar('\\'); putchar('"'); }
            else if (*s == '\\') { putchar('\\'); putchar('\\'); }
            else if (*s == '\n') { putchar('\\'); putchar('n'); }
            else putchar(*s);
        }
    }
    putchar('"');
}

/* -------------------------------------------------------------------------
 * READ: scope/cover/history recursive dump
 * ---------------------------------------------------------------------- */

typedef struct {
    ncdbT db;
    int depth;
} WalkCtx;

static void indent(int depth) {
    for (int i = 0; i < depth * 2; i++) putchar(' ');
}

typedef struct {
    ncdbT db;
    int depth;
    int first;
    int child_mode; /* 0 = scope children, 1 = cover items */
} IterCtx;

/* Forward declaration */
static void dump_scope(ncdbT db, ncdbScopeT scope, int depth);

static int iter_scope_cb(ncdbT db, ncdbScopeT scope, void *ud) {
    IterCtx *ctx = (IterCtx *)ud;
    if (!ctx->first) { printf(",\n"); } else { printf("\n"); }
    ctx->first = 0;
    dump_scope(db, scope, ctx->depth);
    return 0;
}

static int iter_cover_cb(ncdbT db, ncdbCoverT cover, void *ud) {
    IterCtx *ctx = (IterCtx *)ud;
    /* Read at_least from the cover struct (ncdb_impl.h visible) */
    struct ncdb_cover_s *c = (struct ncdb_cover_s *)cover;
    if (!ctx->first) { printf(",\n"); } else { printf("\n"); }
    ctx->first = 0;
    indent(ctx->depth);
    printf("{\n");
    indent(ctx->depth + 1); printf("\"atLeast\": %llu,\n", (unsigned long long)c->at_least);
    indent(ctx->depth + 1); printf("\"count\": %llu,\n", (unsigned long long)ncdb_GetCoverCount(db, cover));
    indent(ctx->depth + 1); printf("\"coverType\": %llu,\n", (unsigned long long)ncdb_GetCoverType(db, cover));
    indent(ctx->depth + 1); printf("\"name\": "); jstr(ncdb_GetCoverName(db, cover)); printf("\n");
    indent(ctx->depth); printf("}");
    (void)db;
    return 0;
}

static void dump_scope(ncdbT db, ncdbScopeT scope, int depth) {
    IterCtx child_ctx = { db, depth + 2, 1, 0 };
    IterCtx item_ctx  = { db, depth + 2, 1, 1 };
    const char *src_path = ncdb_GetScopeSourcePath(db, scope);

    indent(depth);
    printf("{\n");
    indent(depth + 1); printf("\"children\": [");
    ncdb_ScopeIterate(db, scope, (uint32_t)NCDB_SCOPE_ALL, iter_scope_cb, &child_ctx);
    if (!child_ctx.first) { printf("\n"); indent(depth + 1); }
    printf("],\n");

    indent(depth + 1); printf("\"items\": [");
    ncdb_CoverIterate(db, scope, iter_cover_cb, &item_ctx);
    if (!item_ctx.first) { printf("\n"); indent(depth + 1); }
    printf("],\n");

    indent(depth + 1); printf("\"name\": "); jstr(ncdb_GetScopeName(db, scope)); printf(",\n");
    if (src_path) {
        indent(depth + 1); printf("\"source_file\": "); jstr(src_path); printf(",\n");
        indent(depth + 1); printf("\"source_line\": %llu,\n", (unsigned long long)ncdb_GetScopeSourceLine(db, scope));
        indent(depth + 1); printf("\"source_token\": %llu,\n", (unsigned long long)ncdb_GetScopeSourceToken(db, scope));
    } else {
        indent(depth + 1); printf("\"source_file\": null,\n");
        indent(depth + 1); printf("\"source_line\": 0,\n");
        indent(depth + 1); printf("\"source_token\": 0,\n");
    }
    indent(depth + 1); printf("\"type\": %llu,\n", (unsigned long long)ncdb_GetScopeType(db, scope));
    indent(depth + 1); printf("\"weight\": %llu,\n", (unsigned long long)ncdb_GetScopeWeight(db, scope));
    indent(depth + 1); printf("\"goal\": %lld,\n",   (long long)ncdb_GetScopeGoal(db, scope));
    {
        size_t np = ncdb_GetCrossPointCount(db, scope);
        indent(depth + 1); printf("\"crossed_points\": ");
        if (np > 0) {
            size_t ci;
            printf("[");
            for (ci = 0; ci < np; ci++) {
                ncdbScopeT cp = ncdb_GetCrossPoint(db, scope, ci);
                if (ci > 0) printf(", ");
                jstr(ncdb_GetScopeName(db, cp));
            }
            printf("]");
        } else {
            printf("null");
        }
        printf("\n");
    }
    indent(depth);
    printf("}");
}

static int top_scope_cb(ncdbT db, ncdbScopeT scope, void *ud) {
    IterCtx *ctx = (IterCtx *)ud;
    if (!ctx->first) { printf(",\n"); } else { printf("\n"); }
    ctx->first = 0;
    dump_scope(db, scope, ctx->depth);
    return 0;
}

static const char *kind_name(uint32_t kind) {
    switch (kind) {
        case NCDB_HISTORY_TEST:  return "TEST";
        case NCDB_HISTORY_MERGE: return "MERGE";
        default: return "TEST";
    }
}

static int hist_cb(ncdbT db, ncdbHistoryNodeT node, void *ud) {
    IterCtx *ctx = (IterCtx *)ud;
    if (!ctx->first) { printf(",\n"); } else { printf("\n"); }
    ctx->first = 0;
    indent(ctx->depth);
    printf("{\n");
    indent(ctx->depth + 1); printf("\"kind\": "); jstr(kind_name(ncdb_GetHistoryKind(db, node))); printf(",\n");
    indent(ctx->depth + 1); printf("\"name\": "); jstr(ncdb_GetHistoryLogicalName(db, node)); printf(",\n");
    indent(ctx->depth + 1); printf("\"user_name\": "); jstr(ncdb_GetHistoryUserName(db, node)); printf(",\n");
    indent(ctx->depth + 1); printf("\"seed\": "); jstr(ncdb_GetHistorySeed(db, node)); printf(",\n");
    indent(ctx->depth + 1); printf("\"tool_category\": "); jstr(ncdb_GetHistoryToolCategory(db, node)); printf(",\n");
    indent(ctx->depth + 1); printf("\"comment\": "); jstr(ncdb_GetHistoryComment(db, node)); printf("\n");
    indent(ctx->depth); printf("}");
    return 0;
}

/* -------------------------------------------------------------------------
 * Issue iteration callbacks for cmd_read
 * ---------------------------------------------------------------------- */

typedef struct { int first; } IssueCtx;

static int issue_cb(ncdbT db, ncdbIssueT issue, void *ud) {
    IssueCtx *ctx = (IssueCtx *)ud;
    if (!ctx->first) { printf(",\n"); } else { printf("\n"); }
    ctx->first = 0;
    printf("    {\n");
    printf("      \"id\": ");         jstr(ncdb_GetIssueId(db, issue));         printf(",\n");
    printf("      \"ext\": ");        jstr(ncdb_GetIssueExt(db, issue));        printf(",\n");
    printf("      \"severity\": %d,\n", (int)ncdb_GetIssueSeverity(db, issue));
    printf("      \"kind\": %d,\n",     (int)ncdb_GetIssueKind(db, issue));
    printf("      \"state\": %d,\n",    (int)ncdb_GetIssueState(db, issue));
    printf("      \"resolution\": %d\n",(int)ncdb_GetIssueResolution(db, issue));
    printf("    }");
    return 0;
}

static int waiver_link_cb(ncdbT db, const char *waiver_id, const char *issue_id, void *ud) {
    IssueCtx *ctx = (IssueCtx *)ud;
    if (!ctx->first) { printf(",\n"); } else { printf("\n"); }
    ctx->first = 0;
    printf("    {\"waiver_id\": "); jstr(waiver_id);
    printf(", \"issue_id\": "); jstr(issue_id); printf("}");
    (void)db;
    return 0;
}

static int tp_link_cb(ncdbT db, const char *tp_name, const char *issue_id,
                      uint8_t link_type, void *ud) {
    IssueCtx *ctx = (IssueCtx *)ud;
    if (!ctx->first) { printf(",\n"); } else { printf("\n"); }
    ctx->first = 0;
    printf("    {\"tp_name\": "); jstr(tp_name);
    printf(", \"issue_id\": "); jstr(issue_id);
    printf(", \"link_type\": %d}", (int)link_type);
    (void)db;
    return 0;
}

static int cov_link_cb(ncdbT db, const char *scope_path, const char *bin_name,
                       const char *issue_id, uint8_t link_type, void *ud) {
    IssueCtx *ctx = (IssueCtx *)ud;
    if (!ctx->first) { printf(",\n"); } else { printf("\n"); }
    ctx->first = 0;
    printf("    {\"scope_path\": "); jstr(scope_path);
    printf(", \"bin_name\": ");   jstr(bin_name);
    printf(", \"issue_id\": ");   jstr(issue_id);
    printf(", \"link_type\": %d}", (int)link_type);
    (void)db;
    return 0;
}

static int cmd_read(const char *path) {
    ncdbT db = ncdb_Open(NULL);
    if (!db) { fprintf(stderr, "ncdb_Open failed\n"); return 1; }
    if (ncdb_Read(db, path) != 0) {
        fprintf(stderr, "ncdb_Read failed: %s\n", ncdb_GetLastError(db));
        ncdb_Close(db);
        return 1;
    }

    IterCtx scope_ctx = { db, 2, 1, 0 };
    IterCtx hist_ctx  = { db, 2, 1, 0 };

    printf("{\n");
    printf("  \"format\": \"ncdb-dump-v1\",\n");

    printf("  \"history\": [");
    ncdb_HistoryIterate(db, NCDB_HISTORY_ALL, hist_cb, &hist_ctx);
    if (!hist_ctx.first) { printf("\n  "); }
    printf("],\n");

    printf("  \"scopes\": [");
    ncdb_ScopeIterate(db, NULL, (uint32_t)NCDB_SCOPE_ALL, top_scope_cb, &scope_ctx);
    if (!scope_ctx.first) { printf("\n  "); }
    printf("]");

    /* Emit issues if any */
    IssueCtx issue_ctx = { 1 };
    /* Run a trial iteration to check count via side effect; use a counter approach */
    /* Simpler: always emit arrays, empty if no issues */
    {
        IssueCtx ic = { 1 }, wc = { 1 }, tc = { 1 }, cc = { 1 };
        printf(",\n  \"issues\": [");
        ncdb_IssueIterate(db, issue_cb, &ic);
        if (!ic.first) { printf("\n  "); }
        printf("]");
        printf(",\n  \"waiver_links\": [");
        ncdb_WaiverLinkIterate(db, waiver_link_cb, &wc);
        if (!wc.first) { printf("\n  "); }
        printf("]");
        printf(",\n  \"testpoint_links\": [");
        ncdb_TestpointLinkIterate(db, tp_link_cb, &tc);
        if (!tc.first) { printf("\n  "); }
        printf("]");
        printf(",\n  \"coverage_links\": [");
        ncdb_CoverageLinkIterate(db, cov_link_cb, &cc);
        if (!cc.first) { printf("\n  "); }
        printf("]");
    }

    printf("\n}\n");
    (void)issue_ctx;

    ncdb_Close(db);
    return 0;
}

/* -------------------------------------------------------------------------
 * WRITE: scenario builders
 * ---------------------------------------------------------------------- */

static ncdbCoverT make_cover(ncdbT db, ncdbScopeT scope, uint64_t type,
                              const char *name, uint64_t count, uint64_t at_least) {
    ncdbCoverT c = ncdb_impl_create_cover(db, scope, type, name, count);
    if (c) ((struct ncdb_cover_s *)c)->at_least = at_least;
    return c;
}

static void write_empty(ncdbT db) { (void)db; }

static void write_minimal(ncdbT db) {
    ncdbScopeT cg = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_COVERGROUP, "cg_minimal");
    ncdbScopeT cp = ncdb_impl_create_scope(db, cg, NCDB_SCOPE_COVERPOINT, "cp0");
    make_cover(db, cp, NCDB_COVER_CVGBIN, "bin0", 7, 1);
}

static void write_basic(ncdbT db) {
    ncdbScopeT du   = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_DU_MODULE, "top");
    ncdbScopeT inst = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "top");
    (void)du;
    char cg_name[32], cp_name[32], bin_name[32];
    for (int g = 0; g < 2; g++) {
        snprintf(cg_name, sizeof(cg_name), "cg_%d", g);
        ncdbScopeT cg = ncdb_impl_create_scope(db, inst, NCDB_SCOPE_COVERGROUP, cg_name);
        for (int p = 0; p < 3; p++) {
            snprintf(cp_name, sizeof(cp_name), "cp_%d", p);
            ncdbScopeT cp = ncdb_impl_create_scope(db, cg, NCDB_SCOPE_COVERPOINT, cp_name);
            for (int b = 0; b < 5; b++) {
                snprintf(bin_name, sizeof(bin_name), "bin_%d", b);
                make_cover(db, cp, NCDB_COVER_CVGBIN, bin_name, (uint64_t)(g*15+p*5+b), 1);
            }
        }
    }
}

static void write_at_least(ncdbT db) {
    ncdbScopeT cg = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_COVERGROUP, "cg_al");
    ncdbScopeT cp = ncdb_impl_create_scope(db, cg, NCDB_SCOPE_COVERPOINT, "cp0");
    make_cover(db, cp, NCDB_COVER_CVGBIN, "lo", 0, 2);
    make_cover(db, cp, NCDB_COVER_CVGBIN, "hi", 5, 2);
}

static void write_toggle(ncdbT db) {
    ncdbScopeT du   = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_DU_MODULE, "top");
    ncdbScopeT inst = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "top");
    ncdbScopeT br   = ncdb_impl_create_scope(db, inst, NCDB_SCOPE_BRANCH, "sig_valid");
    (void)du;
    make_cover(db, br, NCDB_COVER_TOGGLEBIN, "0 -> 1", 3, 0);
    make_cover(db, br, NCDB_COVER_TOGGLEBIN, "1 -> 0", 2, 0);
}

static void write_source_info(ncdbT db) {
    ncdbScopeT du   = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_DU_MODULE, "foo");
    ncdbScopeT inst = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE,  "foo");
    ncdbScopeT cg   = ncdb_impl_create_scope(db, inst, NCDB_SCOPE_COVERGROUP, "cg_src");
    ncdbScopeT cp   = ncdb_impl_create_scope(db, cg, NCDB_SCOPE_COVERPOINT, "cp0");
    (void)du;
    ncdb_SetScopeSourceInfo(db, cg, "rtl/foo.sv", 10, 0);
    ncdb_SetScopeSourceInfo(db, cp, "rtl/foo.sv", 11, 5);
    make_cover(db, cp, NCDB_COVER_CVGBIN, "b0", 1, 1);
}

static void write_history(ncdbT db) {
    ncdbScopeT cg = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_COVERGROUP, "cg_h");
    ncdbScopeT cp = ncdb_impl_create_scope(db, cg, NCDB_SCOPE_COVERPOINT, "cp0");
    make_cover(db, cp, NCDB_COVER_CVGBIN, "b0", 1, 1);
    {
        ncdbHistoryNodeT h = ncdb_impl_create_history(db, NCDB_HISTORY_TEST, "smoke", NULL);
        ncdb_SetHistoryUserName(db, h, "alice");
        ncdb_SetHistorySeed(db, h, "42");
        ncdb_SetHistoryToolCategory(db, h, "sim");
        ncdb_SetHistoryComment(db, h, "smoke run");
    }
    {
        ncdbHistoryNodeT h = ncdb_impl_create_history(db, NCDB_HISTORY_TEST, "regression", NULL);
        ncdb_SetHistoryUserName(db, h, "bob");
        ncdb_SetHistorySeed(db, h, "99");
        ncdb_SetHistoryToolCategory(db, h, "sim");
        ncdb_SetHistoryComment(db, h, "full regression");
    }
}

static void write_cross(ncdbT db) {
    ncdbScopeT cg  = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_COVERGROUP, "cg_cross");
    ncdbScopeT cpA = ncdb_impl_create_scope(db, cg, NCDB_SCOPE_COVERPOINT, "cp_a");
    make_cover(db, cpA, NCDB_COVER_CVGBIN, "a0", 1, 1);
    make_cover(db, cpA, NCDB_COVER_CVGBIN, "a1", 2, 1);
    ncdbScopeT cpB = ncdb_impl_create_scope(db, cg, NCDB_SCOPE_COVERPOINT, "cp_b");
    make_cover(db, cpB, NCDB_COVER_CVGBIN, "b0", 3, 1);
    make_cover(db, cpB, NCDB_COVER_CVGBIN, "b1", 4, 1);
    ncdbScopeT x   = ncdb_impl_create_scope(db, cg, NCDB_SCOPE_CROSS, "x_ab");
    make_cover(db, x, NCDB_COVER_DEFAULTBIN, "a0_x_b0", 5, 1);
    ncdb_impl_add_cross_point(x, cpA);
    ncdb_impl_add_cross_point(x, cpB);
}

static void write_deep(ncdbT db) {
    ncdbScopeT cur = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_COVERGROUP, "level_0");
    for (int d = 1; d <= 4; d++) {
        char name[32];
        snprintf(name, sizeof(name), "level_%d", d);
        cur = ncdb_impl_create_scope(db, cur, NCDB_SCOPE_COVERPOINT, name);
    }
    make_cover(db, cur, NCDB_COVER_CVGBIN, "leaf_bin", 42, 1);
}

static void write_full(ncdbT db) {
    write_basic(db);
    write_at_least(db);
    write_toggle(db);
    write_history(db);
    write_cross(db);
    write_deep(db);
}

static void write_unicode_names(ncdbT db) {
    ncdbScopeT cg = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_COVERGROUP, "cg_\xce\xb1\xce\xb2\xce\xb3");
    ncdbScopeT cp = ncdb_impl_create_scope(db, cg, NCDB_SCOPE_COVERPOINT, "cp_caf\xc3\xa9");
    make_cover(db, cp, NCDB_COVER_CVGBIN, "bin_\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", 3, 1);
}

static void write_large_count(ncdbT db) {
    ncdbScopeT cg = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_COVERGROUP, "cg_large");
    ncdbScopeT cp = ncdb_impl_create_scope(db, cg, NCDB_SCOPE_COVERPOINT, "cp0");
    make_cover(db, cp, NCDB_COVER_CVGBIN, "big_bin", UINT64_C(0x100000000), 1);
}

static void write_weight_goal(ncdbT db) {
    ncdbScopeT cg = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_COVERGROUP, "cg_wg");
    ncdb_SetScopeWeight(db, cg, 2);
    ncdb_SetScopeGoal(db, cg, 80);
    ncdbScopeT cp = ncdb_impl_create_scope(db, cg, NCDB_SCOPE_COVERPOINT, "cp0");
    make_cover(db, cp, NCDB_COVER_CVGBIN, "b0", 1, 1);
}

typedef struct {
    const char *name;
    void (*fn)(ncdbT);
} Scenario;

static const Scenario SCENARIOS[] = {
    { "empty",         write_empty         },
    { "minimal",       write_minimal       },
    { "basic",         write_basic         },
    { "at_least",      write_at_least      },
    { "toggle",        write_toggle        },
    { "source_info",   write_source_info   },
    { "history",       write_history       },
    { "cross",         write_cross         },
    { "deep",          write_deep          },
    { "full",          write_full          },
    { "unicode_names", write_unicode_names },
    { "large_count",   write_large_count   },
    { "weight_goal",   write_weight_goal   },
    { NULL, NULL }
};

static int cmd_write(const char *scenario_name, const char *path) {
    const Scenario *s;
    for (s = SCENARIOS; s->name; s++) {
        if (strcmp(s->name, scenario_name) == 0) break;
    }
    if (!s->name) {
        fprintf(stderr, "Unknown scenario: %s\n", scenario_name);
        fprintf(stderr, "Available:");
        for (s = SCENARIOS; s->name; s++) fprintf(stderr, " %s", s->name);
        fprintf(stderr, "\n");
        return 1;
    }

    ncdbT db = ncdb_Open(NULL);
    if (!db) { fprintf(stderr, "ncdb_Open failed\n"); return 1; }
    s->fn(db);
    if (ncdb_Write(db, path) != 0) {
        fprintf(stderr, "ncdb_Write failed: %s\n", ncdb_GetLastError(db));
        ncdb_Close(db);
        return 1;
    }
    ncdb_Close(db);
    return 0;
}

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: c_dump <read|write> ...\n");
        return 1;
    }
    if (strcmp(argv[1], "read") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: c_dump read <file>\n"); return 1; }
        return cmd_read(argv[2]);
    } else if (strcmp(argv[1], "write") == 0) {
        if (argc < 4) { fprintf(stderr, "Usage: c_dump write <scenario> <file>\n"); return 1; }
        return cmd_write(argv[2], argv[3]);
    } else {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        return 1;
    }
}
