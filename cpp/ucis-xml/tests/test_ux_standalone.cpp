// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// T-13: the generated header compiles and works in a bare translation unit,
// with nothing included before it and nothing to link. That is exactly the
// situation of a consumer who dropped one file into their tree, so it is the
// only configuration that really has to work.
#include "ucis_xml.hpp"

int main() {
    ucisxml::MemorySink out;
    ucisxml::CoverageWriter cov;
    if (!cov.open(out.sink(),
                  ucisxml::WriterOptions().writtenTime("2026-01-01T00:00:00")))
        return 1;
    cov.tool(ucisxml::Tool().name("standalone").version("1.0").vendorId("CVST"));
    cov.test(ucisxml::Test().name("t"));
    cov.sources({"a.sv"});
    {
        ucisxml::Scope s = cov.scope("top");
        s.line("a.sv", 1, 1);
    }
    if (!cov.close()) return 1;
    return out.size() > 0 ? 0 : 1;
}
