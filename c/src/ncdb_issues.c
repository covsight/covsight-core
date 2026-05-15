#include <stdio.h>
#include <limits.h>
#include <zlib.h>

#include "ncdb_issues.h"

#define NCDB_ISSUES_MAGIC 0x49535342U
#define NCDB_ISSUES_VERSION 1U
#define NCDB_ISSUES_HDR_SIZE 25U
#define NCDB_ISSUE_REC_SIZE 18U
#define NCDB_WL_REC_SIZE 4U
#define NCDB_TL_REC_SIZE 5U
#define NCDB_CL_REC_SIZE 7U

#define NCDB_ISSUE_HIST_MAGIC 0x49535348U
#define NCDB_ISSUE_HIST_VERSION 1U
#define NCDB_ISSUE_HIST_HDR_SIZE 15U
#define NCDB_ISSUE_HIST_NO_COMMENT 0xFFFFU
#define NCDB_ISSUE_HIST_IDX_SIZE 8U

#define UNPACK_SEV(e)   (((e) >> 13) & 0x7U)
#define UNPACK_KIND(e)  (((e) >> 11) & 0x3U)
#define UNPACK_STATE(e) (((e) >> 8) & 0x7U)
#define UNPACK_RES(e)   (((e) >> 5) & 0x7U)

typedef struct ncdb_issue_hist_idx_s {
    uint16_t id_str_idx;
    uint32_t start_row;
    uint16_t count;
} ncdb_issue_hist_idx_t;

typedef struct ncdb_issue_hist_parsed_s {
    uint8_t *raw;
    size_t raw_len;
    char **id_table;
    uint16_t num_id_strings;
    char **comment_table;
    uint16_t num_comments;
    ncdb_issue_hist_idx_t *issue_index;
    uint16_t num_issues;
    uint32_t *ts_deltas;
    uint32_t num_records;
    const uint8_t *state_bytes;
    const uint8_t *comment_idx_bytes;
    uint32_t ts_base;
} ncdb_issue_hist_parsed_t;

static uint16_t read_u16le(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32le(const uint8_t *p) {
    return (uint32_t)(p[0] |
            ((uint32_t)p[1] << 8) |
            ((uint32_t)p[2] << 16) |
            ((uint32_t)p[3] << 24));
}

static int checked_calloc(size_t count, size_t elem_size, void **out) {
    if (count == 0) {
        *out = NULL;
        return 0;
    }
    if (count > SIZE_MAX / elem_size) {
        return -1;
    }
    *out = calloc(count, elem_size);
    return (*out != NULL) ? 0 : -1;
}

static const char *get_string_checked(const ncdbStringTable *strings,
        uint16_t idx, char *errbuf, size_t errbuf_sz) {
    if (!strings || idx >= strings->count) {
        snprintf(errbuf, errbuf_sz, "invalid issues string index");
        return NULL;
    }
    return ncdb_strings_get(strings, idx);
}

static int scan_string_table_end(const uint8_t *data, size_t len, size_t *offset,
        char *errbuf, size_t errbuf_sz) {
    uint64_t count = 0;
    uint64_t i;
    if (ncdb_varint_decode_uint64(data, len, offset, &count) != 0) {
        snprintf(errbuf, errbuf_sz, "invalid string table count");
        return -1;
    }
    for (i = 0; i < count; i++) {
        uint64_t slen = 0;
        if (ncdb_varint_decode_uint64(data, len, offset, &slen) != 0 ||
                slen > (uint64_t)(len - *offset)) {
            snprintf(errbuf, errbuf_sz, "invalid string table entry");
            return -1;
        }
        *offset += (size_t)slen;
    }
    return 0;
}

void ncdb_issues_free(ncdb_issues_t *issues) {
    if (!issues) {
        return;
    }
    ncdb_strings_free(&issues->strings);
    free(issues->issues);
    free(issues->wl);
    free(issues->tl);
    free(issues->cl);
    free(issues);
}

int ncdb_issues_parse(ncdb_issues_t **out, const uint8_t *data, size_t len,
        char *errbuf, size_t errbuf_sz) {
    ncdb_issues_t *obj = NULL;
    size_t offset = NCDB_ISSUES_HDR_SIZE;
    uint32_t magic;
    uint8_t version;
    uint32_t num_issues;
    uint32_t num_wl;
    uint32_t num_tl;
    uint32_t num_cl;
    uint32_t i;

    if (!out || !data) {
        snprintf(errbuf, errbuf_sz, "invalid issues input");
        return -1;
    }
    *out = NULL;
    if (len < NCDB_ISSUES_HDR_SIZE) {
        snprintf(errbuf, errbuf_sz, "issues header truncated");
        return -1;
    }

    magic = read_u32le(data + 0);
    version = data[4];
    num_issues = read_u32le(data + 9);
    num_wl = read_u32le(data + 13);
    num_tl = read_u32le(data + 17);
    num_cl = read_u32le(data + 21);

    if (magic != NCDB_ISSUES_MAGIC) {
        snprintf(errbuf, errbuf_sz, "invalid issues magic");
        return -1;
    }
    if (version != NCDB_ISSUES_VERSION) {
        snprintf(errbuf, errbuf_sz, "unsupported issues version");
        return -1;
    }

    obj = (ncdb_issues_t *)calloc(1, sizeof(*obj));
    if (!obj) {
        snprintf(errbuf, errbuf_sz, "out of memory");
        return -1;
    }
    ncdb_strings_init(&obj->strings);

    if (ncdb_strings_deserialize(data + NCDB_ISSUES_HDR_SIZE,
            len - NCDB_ISSUES_HDR_SIZE, &obj->strings, errbuf, errbuf_sz) != 0 ||
            scan_string_table_end(data, len, &offset, errbuf, errbuf_sz) != 0) {
        ncdb_issues_free(obj);
        return -1;
    }

    if (checked_calloc(num_issues, sizeof(ncdb_issue_t), (void **)&obj->issues) != 0 ||
            checked_calloc(num_wl, sizeof(ncdb_waiver_link_t), (void **)&obj->wl) != 0 ||
            checked_calloc(num_tl, sizeof(ncdb_tp_link_t), (void **)&obj->tl) != 0 ||
            checked_calloc(num_cl, sizeof(ncdb_cov_link_t), (void **)&obj->cl) != 0) {
        snprintf(errbuf, errbuf_sz, "out of memory");
        ncdb_issues_free(obj);
        return -1;
    }
    obj->num_issues = num_issues;
    obj->num_wl = num_wl;
    obj->num_tl = num_tl;
    obj->num_cl = num_cl;

    for (i = 0; i < num_issues; i++) {
        uint16_t id_idx;
        uint16_t ext_idx;
        uint16_t enums;
        ncdb_issue_t *issue;
        if (len - offset < NCDB_ISSUE_REC_SIZE) {
            snprintf(errbuf, errbuf_sz, "issues record truncated");
            ncdb_issues_free(obj);
            return -1;
        }
        id_idx = read_u16le(data + offset + 0);
        ext_idx = read_u16le(data + offset + 2);
        enums = read_u16le(data + offset + 4);
        issue = &obj->issues[i];
        issue->id = get_string_checked(&obj->strings, id_idx, errbuf, errbuf_sz);
        issue->ext = get_string_checked(&obj->strings, ext_idx, errbuf, errbuf_sz);
        if (!issue->id || !issue->ext) {
            ncdb_issues_free(obj);
            return -1;
        }
        issue->severity = (uint8_t)UNPACK_SEV(enums);
        issue->kind = (uint8_t)UNPACK_KIND(enums);
        issue->state = (uint8_t)UNPACK_STATE(enums);
        issue->resolution = (uint8_t)UNPACK_RES(enums);
        issue->created_at = read_u32le(data + offset + 6);
        issue->updated_at = read_u32le(data + offset + 10);
        issue->synced_at = read_u32le(data + offset + 14);
        offset += NCDB_ISSUE_REC_SIZE;
    }

    for (i = 0; i < num_wl; i++) {
        uint16_t waiver_idx;
        uint16_t issue_idx;
        if (len - offset < NCDB_WL_REC_SIZE) {
            snprintf(errbuf, errbuf_sz, "waiver link truncated");
            ncdb_issues_free(obj);
            return -1;
        }
        waiver_idx = read_u16le(data + offset + 0);
        issue_idx = read_u16le(data + offset + 2);
        obj->wl[i].waiver_id = get_string_checked(&obj->strings, waiver_idx, errbuf, errbuf_sz);
        obj->wl[i].issue_id = get_string_checked(&obj->strings, issue_idx, errbuf, errbuf_sz);
        if (!obj->wl[i].waiver_id || !obj->wl[i].issue_id) {
            ncdb_issues_free(obj);
            return -1;
        }
        offset += NCDB_WL_REC_SIZE;
    }

    for (i = 0; i < num_tl; i++) {
        uint16_t tp_idx;
        uint16_t issue_idx;
        if (len - offset < NCDB_TL_REC_SIZE) {
            snprintf(errbuf, errbuf_sz, "testpoint link truncated");
            ncdb_issues_free(obj);
            return -1;
        }
        tp_idx = read_u16le(data + offset + 0);
        issue_idx = read_u16le(data + offset + 2);
        obj->tl[i].tp_name = get_string_checked(&obj->strings, tp_idx, errbuf, errbuf_sz);
        obj->tl[i].issue_id = get_string_checked(&obj->strings, issue_idx, errbuf, errbuf_sz);
        if (!obj->tl[i].tp_name || !obj->tl[i].issue_id) {
            ncdb_issues_free(obj);
            return -1;
        }
        obj->tl[i].link_type = data[offset + 4];
        offset += NCDB_TL_REC_SIZE;
    }

    for (i = 0; i < num_cl; i++) {
        uint16_t scope_idx;
        uint16_t bin_idx;
        uint16_t issue_idx;
        if (len - offset < NCDB_CL_REC_SIZE) {
            snprintf(errbuf, errbuf_sz, "coverage link truncated");
            ncdb_issues_free(obj);
            return -1;
        }
        scope_idx = read_u16le(data + offset + 0);
        bin_idx = read_u16le(data + offset + 2);
        issue_idx = read_u16le(data + offset + 4);
        obj->cl[i].scope_path = get_string_checked(&obj->strings, scope_idx, errbuf, errbuf_sz);
        obj->cl[i].bin_name = get_string_checked(&obj->strings, bin_idx, errbuf, errbuf_sz);
        obj->cl[i].issue_id = get_string_checked(&obj->strings, issue_idx, errbuf, errbuf_sz);
        if (!obj->cl[i].scope_path || !obj->cl[i].bin_name || !obj->cl[i].issue_id) {
            ncdb_issues_free(obj);
            return -1;
        }
        obj->cl[i].link_type = data[offset + 6];
        offset += NCDB_CL_REC_SIZE;
    }

    *out = obj;
    return 0;
}

int ncdb_IssueIterate(ncdbT db, int (*cb)(ncdbT, ncdbIssueT, void *), void *ud) {
    size_t i;
    if (!db || !cb) {
        return -1;
    }
    if (!db->issues) {
        return 0;
    }
    for (i = 0; i < db->issues->num_issues; i++) {
        int stop = cb(db, &db->issues->issues[i], ud);
        if (stop) {
            return stop;
        }
    }
    return 0;
}

int ncdb_IssueIterateOpen(ncdbT db, int (*cb)(ncdbT, ncdbIssueT, void *), void *ud) {
    size_t i;
    if (!db || !cb) {
        return -1;
    }
    if (!db->issues) {
        return 0;
    }
    for (i = 0; i < db->issues->num_issues; i++) {
        ncdbIssueT issue = &db->issues->issues[i];
        if (issue->state == NCDB_ISSUE_STATE_OPEN ||
                issue->state == NCDB_ISSUE_STATE_IN_PROGRESS) {
            int stop = cb(db, issue, ud);
            if (stop) {
                return stop;
            }
        }
    }
    return 0;
}

int ncdb_IssueIterateBySeverity(ncdbT db, uint8_t severity,
        int (*cb)(ncdbT, ncdbIssueT, void *), void *ud) {
    size_t i;
    if (!db || !cb) {
        return -1;
    }
    if (!db->issues) {
        return 0;
    }
    for (i = 0; i < db->issues->num_issues; i++) {
        ncdbIssueT issue = &db->issues->issues[i];
        if (issue->severity == severity) {
            int stop = cb(db, issue, ud);
            if (stop) {
                return stop;
            }
        }
    }
    return 0;
}

ncdbIssueT ncdb_GetIssueById(ncdbT db, const char *issue_id) {
    size_t i;
    if (!db || !db->issues || !issue_id) {
        return NULL;
    }
    for (i = 0; i < db->issues->num_issues; i++) {
        if (strcmp(db->issues->issues[i].id, issue_id) == 0) {
            return &db->issues->issues[i];
        }
    }
    return NULL;
}

const char *ncdb_GetIssueId(ncdbT db, ncdbIssueT issue) { (void)db; return issue ? issue->id : NULL; }
const char *ncdb_GetIssueExt(ncdbT db, ncdbIssueT issue) { (void)db; return issue ? issue->ext : NULL; }
uint8_t ncdb_GetIssueSeverity(ncdbT db, ncdbIssueT issue) { (void)db; return issue ? issue->severity : 0; }
uint8_t ncdb_GetIssueKind(ncdbT db, ncdbIssueT issue) { (void)db; return issue ? issue->kind : 0; }
uint8_t ncdb_GetIssueState(ncdbT db, ncdbIssueT issue) { (void)db; return issue ? issue->state : 0; }
uint8_t ncdb_GetIssueResolution(ncdbT db, ncdbIssueT issue) { (void)db; return issue ? issue->resolution : 0; }
uint32_t ncdb_GetIssueCreatedAt(ncdbT db, ncdbIssueT issue) { (void)db; return issue ? issue->created_at : 0; }
uint32_t ncdb_GetIssueUpdatedAt(ncdbT db, ncdbIssueT issue) { (void)db; return issue ? issue->updated_at : 0; }
uint32_t ncdb_GetIssueSyncedAt(ncdbT db, ncdbIssueT issue) { (void)db; return issue ? issue->synced_at : 0; }

int ncdb_WaiverLinkIterate(ncdbT db,
        int (*cb)(ncdbT, const char *, const char *, void *), void *ud) {
    size_t i;
    if (!db || !cb) {
        return -1;
    }
    if (!db->issues) {
        return 0;
    }
    for (i = 0; i < db->issues->num_wl; i++) {
        int stop = cb(db, db->issues->wl[i].waiver_id, db->issues->wl[i].issue_id, ud);
        if (stop) {
            return stop;
        }
    }
    return 0;
}

int ncdb_TestpointLinkIterate(ncdbT db,
        int (*cb)(ncdbT, const char *, const char *, uint8_t, void *), void *ud) {
    size_t i;
    if (!db || !cb) {
        return -1;
    }
    if (!db->issues) {
        return 0;
    }
    for (i = 0; i < db->issues->num_tl; i++) {
        int stop = cb(db, db->issues->tl[i].tp_name, db->issues->tl[i].issue_id,
                db->issues->tl[i].link_type, ud);
        if (stop) {
            return stop;
        }
    }
    return 0;
}

int ncdb_CoverageLinkIterate(ncdbT db,
        int (*cb)(ncdbT, const char *, const char *, const char *, uint8_t, void *),
        void *ud) {
    size_t i;
    if (!db || !cb) {
        return -1;
    }
    if (!db->issues) {
        return 0;
    }
    for (i = 0; i < db->issues->num_cl; i++) {
        int stop = cb(db, db->issues->cl[i].scope_path, db->issues->cl[i].bin_name,
                db->issues->cl[i].issue_id, db->issues->cl[i].link_type, ud);
        if (stop) {
            return stop;
        }
    }
    return 0;
}

static void ncdb_issue_hist_free(ncdb_issue_hist_parsed_t *hist) {
    uint16_t i;
    if (!hist) {
        return;
    }
    if (hist->id_table) {
        for (i = 0; i < hist->num_id_strings; i++) {
            free(hist->id_table[i]);
        }
    }
    if (hist->comment_table) {
        for (i = 0; i < hist->num_comments; i++) {
            free(hist->comment_table[i]);
        }
    }
    free(hist->id_table);
    free(hist->comment_table);
    free(hist->issue_index);
    free(hist->ts_deltas);
    free(hist->raw);
    memset(hist, 0, sizeof(*hist));
}

static int ncdb_issue_hist_decompress(const uint8_t *data, size_t len,
        uint8_t **out, size_t *out_len, char *errbuf, size_t errbuf_sz) {
    z_stream zs;
    uint8_t *buf = NULL;
    size_t cap = 0;
    int zr;

    *out = NULL;
    *out_len = 0;
    if (!data || len == 0) {
        return 0;
    }
    if (len > UINT_MAX) {
        snprintf(errbuf, errbuf_sz, "issues history too large");
        return -1;
    }

    memset(&zs, 0, sizeof(zs));
    zs.next_in = (Bytef *)data;
    zs.avail_in = (uInt)len;
    zr = inflateInit(&zs);
    if (zr != Z_OK) {
        snprintf(errbuf, errbuf_sz, "failed to initialize zlib");
        return -1;
    }

    cap = len * 4U;
    if (cap < 256U) {
        cap = 256U;
    }
    buf = (uint8_t *)malloc(cap);
    if (!buf) {
        inflateEnd(&zs);
        snprintf(errbuf, errbuf_sz, "out of memory");
        return -1;
    }

    while (1) {
        if (zs.total_out == cap) {
            uint8_t *tmp;
            size_t new_cap;
            if (cap > SIZE_MAX / 2U) {
                free(buf);
                inflateEnd(&zs);
                snprintf(errbuf, errbuf_sz, "issues history too large");
                return -1;
            }
            new_cap = cap * 2U;
            tmp = (uint8_t *)realloc(buf, new_cap);
            if (!tmp) {
                free(buf);
                inflateEnd(&zs);
                snprintf(errbuf, errbuf_sz, "out of memory");
                return -1;
            }
            buf = tmp;
            cap = new_cap;
        }
        zs.next_out = buf + zs.total_out;
        zs.avail_out = (uInt)(cap - zs.total_out);
        zr = inflate(&zs, Z_NO_FLUSH);
        if (zr == Z_STREAM_END) {
            break;
        }
        if (zr != Z_OK) {
            free(buf);
            inflateEnd(&zs);
            snprintf(errbuf, errbuf_sz, "failed to decompress issues history");
            return -1;
        }
    }

    *out_len = (size_t)zs.total_out;
    inflateEnd(&zs);
    *out = buf;
    return 0;
}

static int ncdb_issue_hist_read_u8_string(const uint8_t *data, size_t size, size_t *offset,
        char **out, char *errbuf, size_t errbuf_sz) {
    uint8_t len;
    char *s;
    if (*offset >= size) {
        snprintf(errbuf, errbuf_sz, "issues history string truncated");
        return -1;
    }
    len = data[*offset];
    (*offset)++;
    if ((size_t)len > size - *offset) {
        snprintf(errbuf, errbuf_sz, "issues history string truncated");
        return -1;
    }
    s = (char *)malloc((size_t)len + 1U);
    if (!s) {
        snprintf(errbuf, errbuf_sz, "out of memory");
        return -1;
    }
    memcpy(s, data + *offset, len);
    s[len] = 0;
    *offset += len;
    *out = s;
    return 0;
}

static int ncdb_issue_hist_parse(ncdbT db, ncdb_issue_hist_parsed_t *hist,
        char *errbuf, size_t errbuf_sz) {
    size_t offset = 0;
    uint32_t magic;
    uint8_t version;
    uint16_t num_id_strings;
    uint16_t num_comments;
    uint16_t num_issues;
    uint32_t num_records;
    uint32_t i;

    memset(hist, 0, sizeof(*hist));
    if (!db->issues_hist_data || db->issues_hist_len == 0) {
        return 0;
    }
    if (ncdb_issue_hist_decompress(db->issues_hist_data, db->issues_hist_len,
            &hist->raw, &hist->raw_len, errbuf, errbuf_sz) != 0) {
        return -1;
    }
    if (hist->raw_len < NCDB_ISSUE_HIST_HDR_SIZE) {
        snprintf(errbuf, errbuf_sz, "issues history header truncated");
        ncdb_issue_hist_free(hist);
        return -1;
    }

    magic = read_u32le(hist->raw + 0);
    version = hist->raw[4];
    num_id_strings = read_u16le(hist->raw + 5);
    num_comments = read_u16le(hist->raw + 7);
    num_issues = read_u16le(hist->raw + 9);
    num_records = read_u32le(hist->raw + 11);
    offset = NCDB_ISSUE_HIST_HDR_SIZE;

    if (magic != NCDB_ISSUE_HIST_MAGIC) {
        snprintf(errbuf, errbuf_sz, "invalid issues history magic");
        ncdb_issue_hist_free(hist);
        return -1;
    }
    if (version != NCDB_ISSUE_HIST_VERSION) {
        snprintf(errbuf, errbuf_sz, "unsupported issues history version");
        ncdb_issue_hist_free(hist);
        return -1;
    }

    hist->num_id_strings = num_id_strings;
    hist->num_comments = num_comments;
    hist->num_issues = num_issues;
    hist->num_records = num_records;

    if (checked_calloc(num_id_strings, sizeof(char *), (void **)&hist->id_table) != 0 ||
            checked_calloc(num_comments, sizeof(char *), (void **)&hist->comment_table) != 0 ||
            checked_calloc(num_issues, sizeof(ncdb_issue_hist_idx_t), (void **)&hist->issue_index) != 0 ||
            checked_calloc(num_records, sizeof(uint32_t), (void **)&hist->ts_deltas) != 0) {
        snprintf(errbuf, errbuf_sz, "out of memory");
        ncdb_issue_hist_free(hist);
        return -1;
    }

    for (i = 0; i < num_id_strings; i++) {
        if (ncdb_issue_hist_read_u8_string(hist->raw, hist->raw_len, &offset,
                &hist->id_table[i], errbuf, errbuf_sz) != 0) {
            ncdb_issue_hist_free(hist);
            return -1;
        }
    }
    for (i = 0; i < num_comments; i++) {
        if (ncdb_issue_hist_read_u8_string(hist->raw, hist->raw_len, &offset,
                &hist->comment_table[i], errbuf, errbuf_sz) != 0) {
            ncdb_issue_hist_free(hist);
            return -1;
        }
    }
    for (i = 0; i < num_issues; i++) {
        if (hist->raw_len - offset < NCDB_ISSUE_HIST_IDX_SIZE) {
            snprintf(errbuf, errbuf_sz, "issues history index truncated");
            ncdb_issue_hist_free(hist);
            return -1;
        }
        hist->issue_index[i].id_str_idx = read_u16le(hist->raw + offset + 0);
        hist->issue_index[i].start_row = read_u32le(hist->raw + offset + 2);
        hist->issue_index[i].count = read_u16le(hist->raw + offset + 6);
        if (hist->issue_index[i].id_str_idx >= num_id_strings) {
            snprintf(errbuf, errbuf_sz, "issues history id index out of range");
            ncdb_issue_hist_free(hist);
            return -1;
        }
        if (hist->issue_index[i].start_row > num_records ||
                hist->issue_index[i].count > num_records - hist->issue_index[i].start_row) {
            snprintf(errbuf, errbuf_sz, "issues history row range out of bounds");
            ncdb_issue_hist_free(hist);
            return -1;
        }
        offset += NCDB_ISSUE_HIST_IDX_SIZE;
    }

    if (hist->raw_len - offset < 4U) {
        snprintf(errbuf, errbuf_sz, "issues history missing timestamp base");
        ncdb_issue_hist_free(hist);
        return -1;
    }
    hist->ts_base = read_u32le(hist->raw + offset);
    offset += 4U;

    for (i = 0; i < num_records; i++) {
        uint64_t delta = 0;
        if (ncdb_varint_decode_uint64(hist->raw, hist->raw_len, &offset, &delta) != 0 ||
                delta > UINT32_MAX) {
            snprintf(errbuf, errbuf_sz, "invalid issues history timestamp delta");
            ncdb_issue_hist_free(hist);
            return -1;
        }
        hist->ts_deltas[i] = (uint32_t)delta;
    }

    if (hist->raw_len - offset < (size_t)num_records + ((size_t)num_records * 2U)) {
        snprintf(errbuf, errbuf_sz, "issues history columns truncated");
        ncdb_issue_hist_free(hist);
        return -1;
    }
    hist->state_bytes = hist->raw + offset;
    offset += num_records;
    hist->comment_idx_bytes = hist->raw + offset;
    return 0;
}

int ncdb_IssueHistoryIterate(ncdbT db, const char *issue_id,
        int (*cb)(ncdbT, uint32_t, uint8_t, const char *, void *), void *ud) {
    ncdb_issue_hist_parsed_t hist;
    uint16_t i;
    int rc = 0;
    char errbuf[256] = {0};

    if (!db || !issue_id || !cb) {
        return -1;
    }
    if (!db->issues_hist_data || db->issues_hist_len == 0) {
        return 0;
    }
    if (ncdb_issue_hist_parse(db, &hist, errbuf, sizeof(errbuf)) != 0) {
        ncdb_impl_set_error(db, "%s", errbuf[0] ? errbuf : "failed to parse issues history");
        return -1;
    }

    for (i = 0; i < hist.num_issues; i++) {
        if (strcmp(hist.id_table[hist.issue_index[i].id_str_idx], issue_id) == 0) {
            uint32_t row;
            uint32_t curr_ts = hist.ts_base;
            uint32_t start = hist.issue_index[i].start_row;
            uint32_t count = hist.issue_index[i].count;
            for (row = start; row < start + count; row++) {
                uint16_t cidx;
                const char *comment = "";
                curr_ts += hist.ts_deltas[row];
                cidx = read_u16le(hist.comment_idx_bytes + (row * 2U));
                if (cidx != NCDB_ISSUE_HIST_NO_COMMENT) {
                    if (cidx >= hist.num_comments) {
                        ncdb_issue_hist_free(&hist);
                        ncdb_impl_set_error(db, "%s", "issues history comment index out of range");
                        return -1;
                    }
                    comment = hist.comment_table[cidx];
                }
                rc = cb(db, curr_ts, hist.state_bytes[row], comment, ud);
                if (rc) {
                    ncdb_issue_hist_free(&hist);
                    return rc;
                }
            }
            break;
        }
    }

    ncdb_issue_hist_free(&hist);
    return 0;
}

int ncdb_IssueStateAt(ncdbT db, const char *issue_id, uint32_t ts) {
    ncdb_issue_hist_parsed_t hist;
    uint16_t i;
    int last_state = -1;

    if (!db || !issue_id) {
        return -1;
    }
    if (!db->issues_hist_data || db->issues_hist_len == 0) {
        return -1;
    }
    {
        char errbuf[256] = {0};
        if (ncdb_issue_hist_parse(db, &hist, errbuf, sizeof(errbuf)) != 0) {
            ncdb_impl_set_error(db, "%s", errbuf[0] ? errbuf : "failed to parse issues history");
            return -1;
        }
    }

    for (i = 0; i < hist.num_issues; i++) {
        if (strcmp(hist.id_table[hist.issue_index[i].id_str_idx], issue_id) == 0) {
            uint32_t row;
            uint32_t curr_ts = hist.ts_base;
            uint32_t start = hist.issue_index[i].start_row;
            uint32_t count = hist.issue_index[i].count;
            for (row = start; row < start + count; row++) {
                curr_ts += hist.ts_deltas[row];
                if (curr_ts > ts) {
                    break;
                }
                last_state = hist.state_bytes[row];
            }
            break;
        }
    }

    ncdb_issue_hist_free(&hist);
    return last_state;
}
