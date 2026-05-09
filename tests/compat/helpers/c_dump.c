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
    indent(depth + 1); printf("\"type\": %llu\n", (unsigned long long)ncdb_GetScopeType(db, scope));
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
    struct ncdb_history_node_s *n = (struct ncdb_history_node_s *)node;
    indent(ctx->depth);
    printf("{\n");
    indent(ctx->depth + 1); printf("\"kind\": "); jstr(kind_name(n->kind)); printf(",\n");
    indent(ctx->depth + 1); printf("\"name\": "); jstr(ncdb_GetHistoryLogicalName(db, node)); printf("\n");
    indent(ctx->depth); printf("}");
    (void)db;
    return 0;
}

static int cmd_read(const char *path) {
    char errbuf[256] = {0};
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
    printf("]\n}\n");

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

/* source_info: C doesn't yet support attaching source info to scopes — skip */
static void write_source_info(ncdbT db) {
    /* Minimal stand-in so the scenario doesn't crash; not full C source info support */
    ncdbScopeT cg = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_COVERGROUP, "cg_src");
    ncdbScopeT cp = ncdb_impl_create_scope(db, cg, NCDB_SCOPE_COVERPOINT, "cp0");
    make_cover(db, cp, NCDB_COVER_CVGBIN, "b0", 1, 1);
}

static void write_history(ncdbT db) {
    ncdbScopeT cg = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_COVERGROUP, "cg_h");
    ncdbScopeT cp = ncdb_impl_create_scope(db, cg, NCDB_SCOPE_COVERPOINT, "cp0");
    make_cover(db, cp, NCDB_COVER_CVGBIN, "b0", 1, 1);
    ncdb_impl_create_history(db, NCDB_HISTORY_TEST, "smoke", NULL);
    ncdb_impl_create_history(db, NCDB_HISTORY_TEST, "regression", NULL);
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

typedef struct {
    const char *name;
    void (*fn)(ncdbT);
} Scenario;

static const Scenario SCENARIOS[] = {
    { "empty",       write_empty       },
    { "minimal",     write_minimal     },
    { "basic",       write_basic       },
    { "at_least",    write_at_least    },
    { "toggle",      write_toggle      },
    { "source_info", write_source_info },
    { "history",     write_history     },
    { "cross",       write_cross       },
    { "deep",        write_deep        },
    { "full",        write_full        },
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
