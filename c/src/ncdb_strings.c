#include <stdio.h>

#include "ncdb_strings.h"

void ncdb_strings_init(ncdbStringTable *tab) {
    memset(tab, 0, sizeof(*tab));
}

void ncdb_strings_free(ncdbStringTable *tab) {
    uint32_t i;
    if (!tab) {
        return;
    }
    for (i = 0; i < tab->count; i++) {
        free(tab->items[i]);
    }
    free(tab->items);
    memset(tab, 0, sizeof(*tab));
}

int ncdb_strings_add(ncdbStringTable *tab, const char *s) {
    uint32_t i;
    char **tmp;
    if (!s) {
        s = "";
    }
    for (i = 0; i < tab->count; i++) {
        if (strcmp(tab->items[i], s) == 0) {
            return (int)i;
        }
    }
    if (tab->count >= tab->cap) {
        uint32_t new_cap = tab->cap ? (tab->cap * 2U) : 8U;
        tmp = (char **)realloc(tab->items, new_cap * sizeof(char *));
        if (!tmp) {
            return -1;
        }
        tab->items = tmp;
        tab->cap = new_cap;
    }
    tab->items[tab->count] = ncdb_impl_strdup(s);
    if (!tab->items[tab->count]) {
        return -1;
    }
    tab->count++;
    return (int)(tab->count - 1U);
}

const char *ncdb_strings_get(const ncdbStringTable *tab, uint64_t idx) {
    if (!tab || idx >= tab->count) {
        return "";
    }
    return tab->items[idx];
}

int ncdb_strings_serialize(const ncdbStringTable *tab, ncdbBuf *out) {
    uint32_t i;
    if (ncdb_varint_encode_uint64(tab->count, out) != 0) {
        return -1;
    }
    for (i = 0; i < tab->count; i++) {
        size_t len = strlen(tab->items[i]);
        if (ncdb_varint_encode_uint64((uint64_t)len, out) != 0) {
            return -1;
        }
        if (ncdb_impl_buf_append(out, tab->items[i], len) != 0) {
            return -1;
        }
    }
    return 0;
}

int ncdb_strings_deserialize(const uint8_t *data, size_t size, ncdbStringTable *tab, char *errbuf, size_t errbuf_sz) {
    size_t off = 0;
    uint64_t count = 0;
    uint64_t i;
    ncdb_strings_init(tab);
    if (ncdb_varint_decode_uint64(data, size, &off, &count) != 0) {
        snprintf(errbuf, errbuf_sz, "%s", "invalid string table count");
        return -1;
    }
    for (i = 0; i < count; i++) {
        uint64_t len = 0;
        char *s;
        if (ncdb_varint_decode_uint64(data, size, &off, &len) != 0 || off + len > size) {
            snprintf(errbuf, errbuf_sz, "%s", "invalid string table entry");
            ncdb_strings_free(tab);
            return -1;
        }
        s = (char *)malloc((size_t)len + 1U);
        if (!s) {
            snprintf(errbuf, errbuf_sz, "%s", "out of memory");
            ncdb_strings_free(tab);
            return -1;
        }
        memcpy(s, data + off, (size_t)len);
        s[len] = 0;
        off += (size_t)len;
        if (ncdb_strings_add(tab, s) < 0) {
            free(s);
            snprintf(errbuf, errbuf_sz, "%s", "out of memory");
            ncdb_strings_free(tab);
            return -1;
        }
        free(s);
    }
    return 0;
}
