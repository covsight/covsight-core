/*
 * bench_c.c — Native C NCDB write + read benchmark.
 * Build: gcc -O2 -o bench_c bench_c.c -I../c/include/ncdb -I../c/src \
 *              -L../c/build -lncdb -Wl,-rpath,../c/build
 * Usage: ./bench_c [N_GROUPS] [N_POINTS] [N_BINS] [N_TESTS]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ncdb/ncdb.h"
#include "ncdb_impl.h"

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static long file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    return sz;
}

int main(int argc, char **argv) {
    int N_GROUPS = argc > 1 ? atoi(argv[1]) : 50;
    int N_POINTS = argc > 2 ? atoi(argv[2]) : 10;
    int N_BINS   = argc > 3 ? atoi(argv[3]) : 20;
    int N_TESTS  = argc > 4 ? atoi(argv[4]) : 5;

    const char *path = "/tmp/bench_c.cdb";
    char name[64];
    double t0, t_build, t_write, t_read;

    /* ---- Build ---- */
    t0 = now_ms();
    ncdbT db = ncdb_Open(NULL);
    if (!db) { fprintf(stderr, "ncdb_Open failed\n"); return 1; }

    ncdb_impl_add_source(db, "bench.sv");

    ncdbScopeT du  = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_DU_MODULE, "bench_du");
    ncdbScopeT top = ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "top");
    (void)du;

    for (int g = 0; g < N_GROUPS; g++) {
        snprintf(name, sizeof(name), "cg_%d", g);
        ncdbScopeT cg = ncdb_impl_create_scope(db, top, NCDB_SCOPE_COVERGROUP, name);
        for (int p = 0; p < N_POINTS; p++) {
            snprintf(name, sizeof(name), "cp_%d", p);
            ncdbScopeT cp = ncdb_impl_create_scope(db, cg, NCDB_SCOPE_COVERPOINT, name);
            for (int b = 0; b < N_BINS; b++) {
                snprintf(name, sizeof(name), "bin_%d", b);
                ncdb_impl_create_cover(db, cp, NCDB_COVER_CVGBIN, name, (uint64_t)(b % 10));
            }
        }
    }
    for (int t = 0; t < N_TESTS; t++) {
        snprintf(name, sizeof(name), "test_%d", t);
        ncdb_impl_create_history(db, NCDB_HISTORY_TEST, name, NULL);
    }
    t_build = now_ms() - t0;

    /* ---- Write ---- */
    t0 = now_ms();
    if (ncdb_Write(db, path) != 0) {
        fprintf(stderr, "ncdb_Write failed: %s\n", ncdb_GetLastError(db));
        ncdb_Close(db);
        return 1;
    }
    t_write = now_ms() - t0;
    ncdb_Close(db);

    long fsize = file_size(path);

    /* ---- Read ---- */
    t0 = now_ms();
    ncdbT db2 = ncdb_Open(path);
    if (!db2) { fprintf(stderr, "ncdb_Open(read) failed\n"); return 1; }
    const char *err = ncdb_GetLastError(db2);
    if (err && err[0]) { fprintf(stderr, "read error: %s\n", err); }
    t_read = now_ms() - t0;
    ncdb_Close(db2);

    remove(path);

    int total_bins = N_GROUPS * N_POINTS * N_BINS;
    printf("C(native) build=%.1fms  write=%.1fms  read=%.1fms  total=%.1fms  file=%ldKB  bins=%d\n",
           t_build, t_write, t_read, t_build + t_write + t_read,
           fsize / 1024, total_bins);
    return 0;
}
