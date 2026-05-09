#include "unity.h"
#include "ncdb_impl.h"

static void test_string_table_roundtrip(void) {
    ncdbStringTable in, out;
    ncdbBuf buf;
    char err[128] = {0};
    ncdb_strings_init(&in);
    ncdb_strings_init(&out);
    ncdb_impl_buf_init(&buf);
    TEST_ASSERT(ncdb_strings_add(&in, "") == 0);
    TEST_ASSERT(ncdb_strings_add(&in, "alpha") == 1);
    TEST_ASSERT(ncdb_strings_add(&in, "beta") == 2);
    TEST_ASSERT(ncdb_strings_add(&in, "héllo") == 3);
    TEST_ASSERT(ncdb_strings_serialize(&in, &buf) == 0);
    TEST_ASSERT(ncdb_strings_deserialize(buf.data, buf.size, &out, err, sizeof(err)) == 0);
    TEST_ASSERT_EQUAL_STRING("", ncdb_strings_get(&out, 0));
    TEST_ASSERT_EQUAL_STRING("alpha", ncdb_strings_get(&out, 1));
    TEST_ASSERT_EQUAL_STRING("beta", ncdb_strings_get(&out, 2));
    TEST_ASSERT_EQUAL_STRING("héllo", ncdb_strings_get(&out, 3));
    ncdb_impl_buf_free(&buf);
    ncdb_strings_free(&in);
    ncdb_strings_free(&out);
}

int main(void) {
    RUN_TEST(test_string_table_roundtrip);
    return 0;
}
