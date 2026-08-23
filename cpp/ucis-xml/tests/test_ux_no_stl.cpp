// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// T-12: the no-std::-member invariant (design §9.4).
//
// Built with UCIS_XML_NO_STL=1, so <string> is never included and none of the
// std:: conveniences exist. If the core ever grows a std::vector or
// std::string member, this stops compiling -- which is the point. It is gated
// from phase 1 rather than phase 4, because the invariant is unrecoverable if
// it is allowed to rot.
#include "ux_points.hpp"

#include <cstdio>

static_assert(UCIS_XML_NO_STL == 1, "this test must be built with NO_STL");

int main() {
    ucisxml::MemorySink out;
    ucisxml::CoverageWriter cov;
    if (!cov.open(out.sink(),
                  ucisxml::WriterOptions().writtenTime("2026-01-01T00:00:00")))
        return 1;
    cov.tool(ucisxml::Tool().name("nostl").version("1.0").vendorId("CVST"));
    cov.test(ucisxml::Test().name("t"));

    // The array form, not the initializer-list one -- a caller without the
    // standard library has an array.
    static const ucisxml::Text kFiles[] = {"a.sv", "b.sv"};
    cov.sources(kFiles, 2);

    {
        ucisxml::Scope s = cov.scope("top", "top_m");
        s.line("a.sv", 1, 5, "stmt");
        s.toggle("d", "0", ucisxml::Edge::Rise, 3);
        ucisxml::Branch b = s.branch("b.sv", 9);
        b.arm("true", 1);
        ucisxml::Covergroup cg = s.covergroup("cg");
        ucisxml::Coverpoint cp = cg.coverpoint("cp");
        cp.bin("x", 0, 1);
        static const ucisxml::Text kCps[] = {"cp"};
        ucisxml::Cross x = cg.cross("xr", kCps, 1);
        static const int64_t kIdx[] = {0};
        x.bin("<x>", kIdx, 1, 1);
    }

    if (!cov.close()) {
        std::fprintf(stderr, "%s\n", cov.error());
        return 1;
    }
    return cov.warnings() == 0 && out.size() > 0 ? 0 : 1;
}
