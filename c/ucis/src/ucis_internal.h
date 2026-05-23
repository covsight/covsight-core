/*
 * Internal wrapper types shared by the UCIS shim translation units.
 * Not part of the public ABI.
 */

#ifndef UCIS_INTERNAL_H
#define UCIS_INTERNAL_H

#include "ucis.h"
#include "ncdb/ncdb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A ucisFileHandleT is a pointer to one of these. The list is anchored on
 * the owning ucis_ctx and freed at ucis_Close. */
typedef struct ucis_file_handle_s {
    char                       *filename;
    char                       *fileworkdir;
    ncdbScopeT                  du_scope;       /* non-NULL for src-file handles */
    int                         du_local_index; /* -1 for plain file handles    */
    struct ucis_file_handle_s  *next;
} ucis_file_handle_t;

typedef enum {
    UCIS_MODE_INMEM = 0,
    UCIS_MODE_WRITESTREAM
} ucis_mode_t;

typedef struct ucis_ctx_s {
    ncdbT                ncdb;
    ucis_mode_t          mode;
    char                *stream_path;       /* set in write-streaming mode */
    ucis_file_handle_t  *file_handles;      /* singly-linked, head insert  */
} ucis_ctx_t;

static inline ucis_ctx_t* ucis_ctx_of(ucisT db) { return (ucis_ctx_t*)db; }

/* Look up an existing file handle by filename, or allocate a fresh one and
 * append it to the ctx's handle list. Used by GetScope/CoverSourceInfo so
 * callers don't need to recreate filehandles after a write/reopen cycle. */
ucis_file_handle_t *ucis_internal_handle_for_path(ucis_ctx_t *ctx, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* UCIS_INTERNAL_H */
