#include <stdio.h>
#include <string.h>
#include <time.h>

#include "mz.h"
#include "mz_strm.h"
#include "mz_zip.h"
#include "mz_zip_rw.h"

#include "ncdb_zip.h"

static void set_err(char *errbuf, size_t errbuf_sz, const char *msg) {
    if (errbuf && errbuf_sz) {
        snprintf(errbuf, errbuf_sz, "%s", msg ? msg : "zip error");
    }
}

int ncdb_zip_read_member(const char *path, const char *member, uint8_t **data, size_t *size, char *errbuf, size_t errbuf_sz) {
    void *reader = NULL;
    int32_t rc;
    int32_t len;
    uint8_t *buf = NULL;
    *data = NULL;
    *size = 0;

    reader = mz_zip_reader_create();
    if (!reader) {
        set_err(errbuf, errbuf_sz, "failed to create zip reader");
        return -1;
    }
    rc = mz_zip_reader_open_file(reader, path);
    if (rc != MZ_OK) {
        set_err(errbuf, errbuf_sz, "failed to open zip file");
        mz_zip_reader_delete(&reader);
        return -1;
    }
    rc = mz_zip_reader_locate_entry(reader, member, 0);
    if (rc != MZ_OK) {
        set_err(errbuf, errbuf_sz, "missing zip member");
        mz_zip_reader_close(reader);
        mz_zip_reader_delete(&reader);
        return -1;
    }
    rc = mz_zip_reader_entry_open(reader);
    if (rc != MZ_OK) {
        set_err(errbuf, errbuf_sz, "failed to open zip member");
        mz_zip_reader_close(reader);
        mz_zip_reader_delete(&reader);
        return -1;
    }
    len = mz_zip_reader_entry_save_buffer_length(reader);
    if (len < 0) {
        set_err(errbuf, errbuf_sz, "failed to get zip member size");
        mz_zip_reader_entry_close(reader);
        mz_zip_reader_close(reader);
        mz_zip_reader_delete(&reader);
        return -1;
    }
    buf = (uint8_t *)malloc((size_t)len + 1U);
    if (!buf) {
        set_err(errbuf, errbuf_sz, "out of memory");
        mz_zip_reader_entry_close(reader);
        mz_zip_reader_close(reader);
        mz_zip_reader_delete(&reader);
        return -1;
    }
    rc = mz_zip_reader_entry_save_buffer(reader, buf, len);
    if (rc < 0) {
        free(buf);
        set_err(errbuf, errbuf_sz, "failed to read zip member");
        mz_zip_reader_entry_close(reader);
        mz_zip_reader_close(reader);
        mz_zip_reader_delete(&reader);
        return -1;
    }
    buf[len] = 0;
    *data = buf;
    *size = (size_t)len;
    mz_zip_reader_entry_close(reader);
    mz_zip_reader_close(reader);
    mz_zip_reader_delete(&reader);
    return 0;
}

int ncdb_zip_write_archive(const char *path, const ncdbZipMember *members, size_t count, char *errbuf, size_t errbuf_sz) {
    void *writer = NULL;
    size_t i;
    int32_t rc;

    writer = mz_zip_writer_create();
    if (!writer) {
        set_err(errbuf, errbuf_sz, "failed to create zip writer");
        return -1;
    }
    rc = mz_zip_writer_open_file(writer, path, 0, 0);
    if (rc != MZ_OK) {
        set_err(errbuf, errbuf_sz, "failed to open output zip file");
        mz_zip_writer_delete(&writer);
        return -1;
    }
    for (i = 0; i < count; i++) {
        static const uint8_t empty_buf[1] = {0};
        mz_zip_file file_info;
        memset(&file_info, 0, sizeof(file_info));
        file_info.filename = members[i].name;
        file_info.modified_date = time(NULL);
        file_info.compression_method = members[i].store ? MZ_COMPRESS_METHOD_STORE : MZ_COMPRESS_METHOD_DEFLATE;
        /* mz_zip_writer_add_buffer requires a non-NULL data pointer even for 0-byte members */
        rc = mz_zip_writer_add_buffer(writer,
                                      members[i].data ? members[i].data : empty_buf,
                                      (int32_t)members[i].size, &file_info);
        if (rc != MZ_OK) {
            set_err(errbuf, errbuf_sz, "failed to write zip member");
            mz_zip_writer_close(writer);
            mz_zip_writer_delete(&writer);
            return -1;
        }
    }
    mz_zip_writer_close(writer);
    mz_zip_writer_delete(&writer);
    return 0;
}
