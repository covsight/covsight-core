/* uw_tables.c - source-file table and history nodes.
 * SPDX-License-Identifier: Apache-2.0 */

#include "uw_tables.h"
#include "uw_stack.h"
#include "uw_text.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- source files ----------------------------------------------------- */

#define UW_FILETAB_INIT_BUCKETS 64

UW_INTERNAL void uw_filetab_init(uw_filetab_t* t)
{
    t->buckets  = NULL;
    t->nbuckets = 0;
    t->byid     = NULL;
    t->byid_cap = 0;
    t->count    = 0;
    t->sealed   = 0;
}

UW_INTERNAL void uw_filetab_free(uw_filetab_t* t)
{
    uint32_t i;
    for (i = 0; i < t->count; ++i) {
        free(t->byid[i]);
    }
    free(t->byid);
    free(t->buckets);
    uw_filetab_init(t);
}

static size_t uw_hash(const char* s, size_t n)
{
    size_t h = (size_t)1469598103u;   /* FNV-1a, 32-bit basis */
    size_t i;
    for (i = 0; i < n; ++i) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    return h;
}

static int uw_filetab_grow(uw_db_t* db, uw_filetab_t* t)
{
    size_t      newn = t->nbuckets ? t->nbuckets * 2 : UW_FILETAB_INIT_BUCKETS;
    uw_file_t** nb   = (uw_file_t**)calloc(newn, sizeof(uw_file_t*));
    uint32_t    i;

    if (nb == NULL) {
        return uw_fail(db, UCIS_WRITER_ERR_ALLOC, "file table", NULL);
    }
    for (i = 0; i < t->count; ++i) {
        uw_file_t* f = t->byid[i];
        size_t     b = uw_hash(f->name, f->len) & (newn - 1);
        f->hnext = nb[b];
        nb[b] = f;
    }
    free(t->buckets);
    t->buckets  = nb;
    t->nbuckets = newn;
    return UCIS_WRITER_OK;
}

UW_INTERNAL uw_file_t* uw_filetab_by_id(uw_filetab_t* t, uint32_t id)
{
    if (id == 0 || id > t->count) {
        return NULL;
    }
    return t->byid[id - 1];
}

UW_INTERNAL uw_file_t* uw_filetab_intern(uw_db_t* db, const char* filename,
                                         const char* workdir)
{
    uw_filetab_t* t = &db->files;
    size_t        wlen = 0;
    size_t        flen;
    size_t        total;
    size_t        bucket;
    uw_file_t*    f;
    char*         p;

    if (filename == NULL) {
        uw_fail(db, UCIS_WRITER_ERR_USAGE, "file name is NULL", NULL);
        return NULL;
    }
    /* A relative path is only meaningful next to the directory it was
     * relative to, and SOURCE_FILE has nowhere to record that separately. */
    if (workdir != NULL && workdir[0] != '\0' && filename[0] != '/') {
        wlen = strlen(workdir);
        if (wlen > 0 && workdir[wlen - 1] == '/') {
            wlen--;    /* we add exactly one separator below */
        }
    }
    flen  = strlen(filename);
    total = wlen ? wlen + 1 + flen : flen;

    if (t->nbuckets == 0 && uw_filetab_grow(db, t) != UCIS_WRITER_OK) {
        return NULL;
    }
    /* Build the entry first: the hash is over the joined name, so there is no
     * cheaper way to look up a path that came in as workdir + relative name. */
    f = (uw_file_t*)malloc(sizeof(uw_file_t) + total);
    if (f == NULL) {
        uw_fail(db, UCIS_WRITER_ERR_ALLOC, "file table entry", filename);
        return NULL;
    }
    p = f->name;
    if (wlen) {
        memcpy(p, workdir, wlen);
        p += wlen;
        *p++ = '/';
    }
    memcpy(p, filename, flen);
    f->name[total] = '\0';
    f->len = total;

    bucket = uw_hash(f->name, total) & (t->nbuckets - 1);
    {
        uw_file_t* cur;
        for (cur = t->buckets[bucket]; cur != NULL; cur = cur->hnext) {
            if (cur->len == total && memcmp(cur->name, f->name, total) == 0) {
                free(f);
                return cur;      /* dedup: the same path gets the same id */
            }
        }
    }

    if (t->sealed) {
        free(f);
        uw_fail(db, UCIS_WRITER_ERR_SEALED,
                "source file created after the first instance", filename);
        return NULL;
    }

    if ((size_t)t->count == t->byid_cap) {
        size_t      newcap = t->byid_cap ? t->byid_cap * 2 : UW_FILETAB_INIT_BUCKETS;
        uw_file_t** nid    = (uw_file_t**)realloc(t->byid, newcap * sizeof(uw_file_t*));
        if (nid == NULL) {
            free(f);
            uw_fail(db, UCIS_WRITER_ERR_ALLOC, "file table index", NULL);
            return NULL;
        }
        t->byid    = nid;
        t->byid_cap = newcap;
    }

    f->id = ++t->count;               /* 1-based: xsd:positiveInteger */
    t->byid[f->id - 1] = f;
    f->hnext = t->buckets[bucket];
    t->buckets[bucket] = f;

    if (t->count * 2 > t->nbuckets) {
        uw_filetab_grow(db, t);       /* failure here only costs us speed */
    }
    return f;
}

/* ---- history nodes ---------------------------------------------------- */

UW_INTERNAL uw_hist_t* uw_hist_create(uw_db_t* db, const char* logicalname,
                                      const char* physicalname,
                                      ucisHistoryNodeKindT kind)
{
    uw_hist_t* h = (uw_hist_t*)calloc(1, sizeof(uw_hist_t));
    if (h == NULL) {
        uw_fail(db, UCIS_WRITER_ERR_ALLOC, "history node", NULL);
        return NULL;
    }
    h->id           = db->hist_count++;
    h->kind         = kind;
    h->teststatus   = UCIS_TESTSTATUS_OK;
    h->logicalname  = uw_strdup(db, logicalname);
    h->physicalname = uw_strdup(db, physicalname);

    if (db->hist_tail) {
        db->hist_tail->next = h;
    } else {
        db->hist_head = h;
    }
    db->hist_tail = h;
    return h;
}

UW_INTERNAL void uw_hist_free_all(uw_db_t* db)
{
    uw_hist_t* h = db->hist_head;
    while (h != NULL) {
        uw_hist_t* next = h->next;
        free(h->logicalname);
        free(h->physicalname);
        free(h->timeunit);
        free(h->runcwd);
        free(h->seed);
        free(h->cmd);
        free(h->args);
        free(h->date);
        free(h->username);
        free(h->toolcategory);
        free(h->vendor_id);
        free(h->vendor_tool);
        free(h->vendor_version);
        free(h->comment);
        free(h);
        h = next;
    }
    db->hist_head  = NULL;
    db->hist_tail  = NULL;
    db->hist_count = 0;
}

/* ---- time ------------------------------------------------------------- */

UW_INTERNAL void uw_now_iso8601(char* out, size_t cap)
{
    time_t     now = time(NULL);
    struct tm* g   = gmtime(&now);

    if (g == NULL || cap < 21) {
        if (cap > 0) {
            out[0] = '\0';
        }
        return;
    }
    /* Hand-formatted rather than strftime: strftime's %F/%T are C99-optional
     * in practice on some platforms, and this avoids any locale involvement. */
    out[0]  = (char)('0' + ((g->tm_year + 1900) / 1000) % 10);
    out[1]  = (char)('0' + ((g->tm_year + 1900) / 100) % 10);
    out[2]  = (char)('0' + ((g->tm_year + 1900) / 10) % 10);
    out[3]  = (char)('0' + ((g->tm_year + 1900) % 10));
    out[4]  = '-';
    out[5]  = (char)('0' + ((g->tm_mon + 1) / 10));
    out[6]  = (char)('0' + ((g->tm_mon + 1) % 10));
    out[7]  = '-';
    out[8]  = (char)('0' + (g->tm_mday / 10));
    out[9]  = (char)('0' + (g->tm_mday % 10));
    out[10] = 'T';
    out[11] = (char)('0' + (g->tm_hour / 10));
    out[12] = (char)('0' + (g->tm_hour % 10));
    out[13] = ':';
    out[14] = (char)('0' + (g->tm_min / 10));
    out[15] = (char)('0' + (g->tm_min % 10));
    out[16] = ':';
    out[17] = (char)('0' + ((g->tm_sec > 59 ? 59 : g->tm_sec) / 10));
    out[18] = (char)('0' + ((g->tm_sec > 59 ? 59 : g->tm_sec) % 10));
    out[19] = 'Z';
    out[20] = '\0';
}

UW_INTERNAL int uw_is_datetime(const char* s)
{
    int i;
    if (s == NULL) {
        return 0;
    }
    for (i = 0; i < 19; ++i) {
        if (s[i] == '\0') {
            return 0;
        }
    }
    return s[4] == '-' && s[7] == '-' && s[10] == 'T' &&
           s[13] == ':' && s[16] == ':';
}

/* ---- emission --------------------------------------------------------- */

static const char* uw_hist_kind_name(ucisHistoryNodeKindT kind)
{
    switch (kind) {
        case UCIS_HISTORYNODE_TEST:  return "TEST";
        case UCIS_HISTORYNODE_MERGE: return "MERGE";
        case UCIS_HISTORYNODE_ALL:   return "ALL";
        default:                     return NULL;
    }
}

static int uw_emit_source_files(uw_db_t* db)
{
    uint32_t i;

    if (db->files.count == 0) {
        /* sourceFiles has minOccurs="1". A caller that recorded no source
         * files still deserves a document a reader will accept, so we emit a
         * placeholder and say so through the warning counter. */
        db->warnings++;
        if (uw_el_begin(db, "sourceFiles", 0) != UCIS_WRITER_OK) {
            return db->buf.status;
        }
        uw_text_attr(&db->buf, "fileName", "(unknown)", &db->warnings);
        uw_text_attr_u64(&db->buf, "id", 1);
        return uw_el_end(db);
    }

    for (i = 0; i < db->files.count; ++i) {
        uw_file_t* f = db->files.byid[i];
        if (uw_el_begin(db, "sourceFiles", 0) != UCIS_WRITER_OK) {
            return db->buf.status;
        }
        if (uw_buf_write(&db->buf, " fileName=\"", 11) != UCIS_WRITER_OK) {
            return db->buf.status;
        }
        uw_text_escape(&db->buf, f->name, f->len, &db->warnings);
        uw_buf_putc(&db->buf, '"');
        uw_text_attr_u64(&db->buf, "id", f->id);
        if (uw_el_end(db) != UCIS_WRITER_OK) {
            return db->buf.status;
        }
    }
    return db->buf.status;
}

static int uw_emit_one_history(uw_db_t* db, const uw_hist_t* h)
{
    uw_buf_t*   b    = &db->buf;
    const char* kind = uw_hist_kind_name(h->kind);
    const char* date;

    if (uw_el_begin(db, "historyNodes", 0) != UCIS_WRITER_OK) {
        return b->status;
    }
    uw_text_attr_u64(b, "historyNodeId", h->id);
    if (h->has_parent) {
        uw_text_attr_u64(b, "parentId", h->parent_id);
    }
    uw_text_attr(b, "logicalName",
                 h->logicalname ? h->logicalname : "(unnamed)", &db->warnings);
    uw_text_attr(b, "physicalName", h->physicalname, &db->warnings);
    uw_text_attr(b, "kind", kind, &db->warnings);

    /* testStatus is xsd:boolean in the schema even though the API models six
     * states. Anything short of a clean run is reported as false, and the
     * detail survives as UCIS_INT_TEST_STATUS on the reading side. */
    uw_text_attr(b, "testStatus",
                 h->teststatus == UCIS_TESTSTATUS_OK ? "true" : "false", NULL);

    if (h->has_testdata) {
        uw_text_attr_double(b, "simtime", h->simtime);
        uw_text_attr(b, "timeunit", h->timeunit, &db->warnings);
        uw_text_attr(b, "runCwd", h->runcwd, &db->warnings);
        uw_text_attr_double(b, "cpuTime", h->cputime);
        uw_text_attr(b, "seed", h->seed, &db->warnings);
        uw_text_attr(b, "cmd", h->cmd, &db->warnings);
        uw_text_attr(b, "args", h->args, &db->warnings);
        if (h->compulsory) {
            uw_text_attr(b, "compulsory", "1", NULL);
        }
    }

    date = h->date;
    if (!uw_is_datetime(date)) {
        if (date != NULL) {
            db->warnings++;      /* caller supplied something unparseable */
        }
        date = db->written_time;
    }
    uw_text_attr(b, "date", date, &db->warnings);
    uw_text_attr(b, "userName", h->username, &db->warnings);
    if (h->has_testdata) {
        uw_text_attr_double(b, "cost", h->cost);
    }
    uw_text_attr(b, "toolCategory",
                 h->toolcategory ? h->toolcategory : UCIS_SIM_TOOL, &db->warnings);
    uw_text_attr(b, "ucisVersion", UCIS_WRITER_UCIS_VERSION, NULL);
    /* Per-node override, then the database-wide default, then ours. */
    uw_text_attr(b, "vendorId",
                 h->vendor_id ? h->vendor_id
                              : db->vendor_id ? db->vendor_id
                                              : UCIS_WRITER_VENDOR_ID, &db->warnings);
    uw_text_attr(b, "vendorTool",
                 h->vendor_tool ? h->vendor_tool
                                : db->vendor_tool ? db->vendor_tool
                                                  : UCIS_WRITER_VENDOR_TOOL, &db->warnings);
    uw_text_attr(b, "vendorToolVersion",
                 h->vendor_version ? h->vendor_version
                                   : db->vendor_version ? db->vendor_version
                                                        : UCIS_WRITER_VERSION, &db->warnings);
    uw_text_attr(b, "comment", h->comment, &db->warnings);

    return uw_el_end(db);
}

static int uw_emit_history_nodes(uw_db_t* db)
{
    const uw_hist_t* h;

    if (db->hist_head == NULL) {
        /* historyNodes has minOccurs="1"; same repair as sourceFiles. */
        uw_hist_t synth;
        memset(&synth, 0, sizeof(synth));
        synth.teststatus = UCIS_TESTSTATUS_OK;
        db->warnings++;
        return uw_emit_one_history(db, &synth);
    }
    for (h = db->hist_head; h != NULL; h = h->next) {
        if (uw_emit_one_history(db, h) != UCIS_WRITER_OK) {
            return db->buf.status;
        }
    }
    return db->buf.status;
}

UW_INTERNAL int uw_placeholder_instance(uw_db_t* db)
{
    /* instanceCoverages is minOccurs="1" too, so a run that recorded nothing
     * still needs one. Emitting a named placeholder keeps the document
     * readable by any conforming tool and makes the omission obvious to a
     * human, which silently writing an invalid file would not. */
    db->warnings++;
    if (uw_el_begin(db, "instanceCoverages", UCIS_INSTANCE) != UCIS_WRITER_OK) {
        return db->buf.status;
    }
    uw_text_attr(&db->buf, "name", "(none)", NULL);
    uw_text_attr(&db->buf, "key", "(none)", NULL);
    uw_el_set_id(db, 1, 1, 1, 0);
    return uw_el_end(db);
}

UW_INTERNAL int uw_tables_flush(uw_db_t* db)
{
    if (db->phase != UW_PHASE_TABLES) {
        return db->buf.status;
    }
    db->phase        = UW_PHASE_BODY;
    db->files.sealed = 1;

    if (uw_emit_source_files(db) != UCIS_WRITER_OK) {
        return db->buf.status;
    }
    return uw_emit_history_nodes(db);
}
