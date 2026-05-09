#include "ncdb_varint.h"

int ncdb_varint_encode_uint64(uint64_t value, ncdbBuf *buf) {
    do {
        uint8_t byte = (uint8_t)(value & 0x7FU);
        value >>= 7;
        if (value != 0) {
            byte |= 0x80U;
        }
        if (ncdb_impl_buf_append(buf, &byte, 1) != 0) {
            return -1;
        }
    } while (value != 0);
    return 0;
}

int ncdb_varint_decode_uint64(const uint8_t *data, size_t size, size_t *offset, uint64_t *value) {
    uint64_t result = 0;
    unsigned shift = 0;
    while (1) {
        if (*offset >= size || shift >= 70) {
            return -1;
        }
        result |= (uint64_t)(data[*offset] & 0x7FU) << shift;
        if ((data[*offset] & 0x80U) == 0) {
            (*offset)++;
            *value = result;
            return 0;
        }
        (*offset)++;
        shift += 7;
    }
}

int ncdb_varint_encode_many(const uint64_t *values, size_t count, ncdbBuf *buf) {
    size_t i;
    for (i = 0; i < count; i++) {
        if (ncdb_varint_encode_uint64(values[i], buf) != 0) {
            return -1;
        }
    }
    return 0;
}

int ncdb_varint_decode_many(const uint8_t *data, size_t size, size_t *offset, size_t count, uint64_t *values) {
    size_t i;
    for (i = 0; i < count; i++) {
        if (ncdb_varint_decode_uint64(data, size, offset, &values[i]) != 0) {
            return -1;
        }
    }
    return 0;
}
