#ifndef INCLUDED_MINI_UNITY_H
#define INCLUDED_MINI_UNITY_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); } } while (0)
#define TEST_ASSERT_EQUAL_UINT64(exp, act) do { unsigned long long _e=(unsigned long long)(exp), _a=(unsigned long long)(act); if (_e != _a) { fprintf(stderr, "FAIL %s:%d: expected %llu got %llu\n", __FILE__, __LINE__, _e, _a); exit(1); } } while (0)
#define TEST_ASSERT_EQUAL_STRING(exp, act) do { if (strcmp((exp), (act)) != 0) { fprintf(stderr, "FAIL %s:%d: expected '%s' got '%s'\n", __FILE__, __LINE__, (exp), (act)); exit(1); } } while (0)
#define RUN_TEST(fn) do { fn(); printf("PASS: %s\n", #fn); } while (0)

#endif
