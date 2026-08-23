/*
 * bench_c_read.c — Read-only benchmark, N repeated reads of a pre-built .cdb.
 * Build: gcc -O2 -o bench_c_read bench_c_read.c -I../c/include -I../c/src \
 *              -L../c/build -lncdb -Wl,-rpath,../c/build
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "ncdb/ncdb.h"

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* Walk all scopes + cover items so the read is exercised fully */
static int cover_cb(ncdbT db, ncdbCoverT c, void *ud) {
    volatile uint64_t v = ncdb_GetCoverCount(db, c); (void)v; (void)ud; return 0;
}
static int scope_cb(ncdbT db, ncdbScopeT s, void *ud) {
    ncdb_CoverIterate(db, s, cover_cb, NULL);
    ncdb_ScopeIterate(db, s, 0xFFFFFFFFu, scope_cb, ud);
    return 0;
}

int main(int argc, char **argv) {
    const char *path  = argc > 1 ? argv[1] : "/tmp/bench_large.cdb";
    int         reps  = argc > 2 ? atoi(argv[2]) : 5;

    /* warm-up */
    { ncdbT db = ncdb_Open(path); ncdb_Close(db); }

    double total = 0;
    for (int i = 0; i < reps; i++) {
        double t0 = now_ms();
        ncdbT db = ncdb_Open(path);
        if (!db) { fprintf(stderr, "open failed\n"); return 1; }
        ncdb_ScopeIterate(db, NULL, 0xFFFFFFFFu, scope_cb, NULL);
        ncdb_Close(db);
        total += now_ms() - t0;
    }
    printf("C(native) read  avg=%.2fms  (×%d)  file=%s\n",
           total / reps, reps, path);
    return 0;
}
