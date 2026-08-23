// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// A test harness small enough not to be a dependency. The library under test
// has none, and a test framework would be the only thing in the directory that
// did.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace uxtest {

inline int& failures() {
    static int n = 0;
    return n;
}

inline void report(const char* file, int line, const char* what) {
    std::fprintf(stderr, "%s:%d: FAIL %s\n", file, line, what);
    ++failures();
}

inline bool contains(const char* hay, size_t n, const char* needle) {
    size_t m = std::strlen(needle);
    if (m > n) return false;
    for (size_t i = 0; i + m <= n; ++i)
        if (std::memcmp(hay + i, needle, m) == 0) return true;
    return false;
}

inline int finish(const char* name) {
    if (failures()) {
        std::fprintf(stderr, "%s: %d failure(s)\n", name, failures());
        return 1;
    }
    std::fprintf(stderr, "%s: ok\n", name);
    return 0;
}

}  // namespace uxtest

#define CHECK(cond) \
    do { if (!(cond)) uxtest::report(__FILE__, __LINE__, #cond); } while (0)

#define CHECK_MSG(cond, msg) \
    do { if (!(cond)) uxtest::report(__FILE__, __LINE__, msg); } while (0)

// Substring assertions against a MemorySink's contents. Renderer tests care
// about what appears in the document, not about byte offsets.
#define CHECK_HAS(sink, needle)                                            \
    do {                                                                   \
        if (!uxtest::contains((sink).begin(), (sink).size(), needle))       \
            uxtest::report(__FILE__, __LINE__, "missing: " needle);        \
    } while (0)

#define CHECK_LACKS(sink, needle)                                          \
    do {                                                                   \
        if (uxtest::contains((sink).begin(), (sink).size(), needle))        \
            uxtest::report(__FILE__, __LINE__, "unexpected: " needle);     \
    } while (0)
