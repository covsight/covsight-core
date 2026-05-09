#include <stdint.h>
#include "unity.h"
#include "ncdb_impl.h"

static void test_varint_counts(void) {
    uint64_t in[] = {0, 1, 2, 3, 127};
    uint64_t *out = NULL;
    size_t count = 0;
    ncdbBuf buf;
    char err[128] = {0};
    ncdb_impl_buf_init(&buf);
    TEST_ASSERT(ncdb_counts_serialize(in, sizeof(in)/sizeof(in[0]), &buf) == 0);
    TEST_ASSERT(buf.data[0] == NCDB_COUNTS_MODE_VARINT);
    TEST_ASSERT(ncdb_counts_deserialize(buf.data, buf.size, &out, &count, err, sizeof(err)) == 0);
    TEST_ASSERT_EQUAL_UINT64(5, count);
    TEST_ASSERT_EQUAL_UINT64(127, out[4]);
    free(out);
    ncdb_impl_buf_free(&buf);
}

static void test_uint32_counts(void) {
    uint64_t in[] = {0xFFFFFFFFULL, 0xFFFFFFFFULL};
    uint64_t *out = NULL;
    size_t count = 0;
    ncdbBuf buf;
    char err[128] = {0};
    ncdb_impl_buf_init(&buf);
    TEST_ASSERT(ncdb_counts_serialize(in, sizeof(in)/sizeof(in[0]), &buf) == 0);
    TEST_ASSERT(buf.data[0] == NCDB_COUNTS_MODE_UINT32);
    TEST_ASSERT(ncdb_counts_deserialize(buf.data, buf.size, &out, &count, err, sizeof(err)) == 0);
    TEST_ASSERT_EQUAL_UINT64(2, count);
    TEST_ASSERT_EQUAL_UINT64(0xFFFFFFFFULL, out[0]);
    TEST_ASSERT_EQUAL_UINT64(0xFFFFFFFFULL, out[1]);
    free(out);
    ncdb_impl_buf_free(&buf);
}

int main(void) {
    RUN_TEST(test_varint_counts);
    RUN_TEST(test_uint32_counts);
    return 0;
}
