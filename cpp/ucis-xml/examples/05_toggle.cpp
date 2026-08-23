// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// 05_toggle - toggle coverage. Grouping on (signal, bit) is automatic, and it
// is what produces the layout that measured 0.800x gzipped against the
// alternative (design section 8.3).
#include "ex_common.hpp"

// --8<-- [start:body]
namespace ux = ucisxml;

int main(int argc, char** argv) {
    ux::CoverageWriter cov;
    if (!cov.openFile(ex::outPath(argc, argv, "05_toggle.xml"), ex::options()))
        return 1;
    cov.tool(ux::Tool().name("togcov").version("1.0").vendorId("CVST"));
    cov.test(ux::Test().name("run1"));
    cov.sources({"rtl/fifo.sv"});

    {
        ux::Scope s = cov.scope("top.u_fifo", "fifo");
        s.declaredAt("rtl/fifo.sv", 3);

        // Once per signal: where it is declared, and its bit range. The
        // declaration site becomes toggleObject/id -- a toggle fact is about a
        // signal rather than a line, so this is the only place that location
        // can come from.
        s.signal("wdata", "rtl/fifo.sv", 17, 7, 0);
        s.signal("wvalid", "rtl/fifo.sv", 18);
        s.signal("reset_n", "rtl/fifo.sv", 9);

        for (unsigned bit = 0; bit < 8; ++bit) {
            s.toggle("wdata", bit, ux::Edge::Rise, 100 + bit);
            s.toggle("wdata", bit, ux::Edge::Fall, 90 + bit);
        }

        s.toggle("wvalid", "0", ux::Edge::Rise, 512);
        s.toggle("wvalid", "0", ux::Edge::Fall, 511);

        // Four-state transitions, for tools that track them.
        s.toggle("reset_n", "0", "x", "1", 1);
    }

    return ex::finish(cov, "05_toggle");
}
// --8<-- [end:body]
