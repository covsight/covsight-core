#include "unity.h"
#include "ncdb_impl.h"

static void test_known_values(void) {
    uint64_t values[] = {0, 1, 127, 128, 255, 300, 16384, 0xFFFFFFFFULL, 0x1FFFFFFFFULL};
    size_t i;
    ncdbBuf buf;
    ncdb_impl_buf_init(&buf);
    for (i = 0; i < sizeof(values)/sizeof(values[0]); i++) {
        size_t off = 0;
        uint64_t decoded = 0;
        buf.size = 0;
        TEST_ASSERT(ncdb_varint_encode_uint64(values[i], &buf) == 0);
        TEST_ASSERT(ncdb_varint_decode_uint64(buf.data, buf.size, &off, &decoded) == 0);
        TEST_ASSERT_EQUAL_UINT64(values[i], decoded);
        TEST_ASSERT_EQUAL_UINT64(buf.size, off);
    }
    ncdb_impl_buf_free(&buf);
}

int main(void) {
    RUN_TEST(test_known_values);
    return 0;
}
