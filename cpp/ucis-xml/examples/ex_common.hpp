// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// Shared plumbing for the examples: each takes an output path as argv[1] so the
// test suite can validate what it produced, and pins the timestamp so golden
// comparison is possible. Nothing here is part of the API.
#pragma once

#include "ucis_xml.hpp"

#include <cstdio>

namespace ex {

inline ucisxml::WriterOptions options(bool pretty = false) {
    return ucisxml::WriterOptions()
        .writtenTime("2026-01-01T00:00:00")
        .pretty(pretty);
}

inline int finish(ucisxml::CoverageWriter& cov, const char* name) {
    if (!cov.close()) {
        std::fprintf(stderr, "%s: %s\n", name, cov.error());
        return 1;
    }
    if (cov.warnings()) {
        std::fprintf(stderr, "%s: %llu warning(s)\n", name,
                     static_cast<unsigned long long>(cov.warnings()));
        return 1;
    }
    return 0;
}

inline const char* outPath(int argc, char** argv, const char* fallback) {
    return argc > 1 ? argv[1] : fallback;
}

}  // namespace ex
