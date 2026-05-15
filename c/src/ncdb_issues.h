#ifndef INCLUDED_NCDB_ISSUES_H
#define INCLUDED_NCDB_ISSUES_H

#include "ncdb_impl.h"

typedef struct ncdb_issue_s {
    const char *id;
    const char *ext;
    uint8_t     severity;
    uint8_t     kind;
    uint8_t     state;
    uint8_t     resolution;
    uint32_t    created_at;
    uint32_t    updated_at;
    uint32_t    synced_at;
} ncdb_issue_t;

typedef struct ncdb_waiver_link_s {
    const char *waiver_id;
    const char *issue_id;
} ncdb_waiver_link_t;

typedef struct ncdb_tp_link_s {
    const char *tp_name;
    const char *issue_id;
    uint8_t     link_type;
} ncdb_tp_link_t;

typedef struct ncdb_cov_link_s {
    const char *scope_path;
    const char *bin_name;
    const char *issue_id;
    uint8_t     link_type;
} ncdb_cov_link_t;

typedef struct ncdb_issues_s {
    ncdbStringTable strings;

    ncdb_issue_t *issues;
    size_t        num_issues;

    ncdb_waiver_link_t *wl;
    size_t              num_wl;

    ncdb_tp_link_t *tl;
    size_t          num_tl;

    ncdb_cov_link_t *cl;
    size_t           num_cl;
} ncdb_issues_t;

int  ncdb_issues_parse(ncdb_issues_t **out,
                       const uint8_t *data, size_t len,
                       char *errbuf, size_t errbuf_sz);
void ncdb_issues_free(ncdb_issues_t *issues);

#endif
