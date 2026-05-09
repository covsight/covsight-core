#include <stdio.h>

#include "ncdb_attrs.h"

int ncdb_attrs_serialize_empty(ncdbBuf *out) {
    static const char payload[] = "{\"version\":2,\"scopes\":[],\"coveritems\":[],\"history\":[],\"global\":{}}";
    return ncdb_impl_buf_append(out, payload, sizeof(payload) - 1U);
}

int ncdb_attrs_deserialize(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz) {
    (void)db; (void)data; (void)size; (void)errbuf; (void)errbuf_sz;
    return 0;
}
