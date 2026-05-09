#ifndef INCLUDED_NCDB_CROSS_H
#define INCLUDED_NCDB_CROSS_H

#include "ncdb_impl.h"

int ncdb_cross_serialize(ncdbT db, ncdbBuf *out);
int ncdb_cross_deserialize(ncdbT db, const uint8_t *data, size_t size, char *errbuf, size_t errbuf_sz);

#endif
