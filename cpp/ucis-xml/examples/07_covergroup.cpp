// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// 07_covergroup - coverpoints, bin kinds, and a sequence (transition) bin.
#include "ex_common.hpp"

// --8<-- [start:body]
namespace ux = ucisxml;

int main(int argc, char** argv) {
    ux::CoverageWriter cov;
    if (!cov.openFile(ex::outPath(argc, argv, "07_covergroup.xml"), ex::options()))
        return 1;
    cov.tool(ux::Tool().name("cgcov").version("1.0").vendorId("CVST"));
    cov.test(ux::Test().name("run1"));
    cov.sources({"tb/fifo_cg.sv", "tb/fifo_pkg.sv"});

    {
        ux::Scope s = cov.scope("tb.u_env.fifo_cg", "fifo_env");
        s.declaredAt("tb/fifo_cg.sv", 5);

        ux::Covergroup cg = s.covergroup("cg_fifo", "fifo_cg", "work.fifo_pkg");
        // CG_ID carries two locations, and they are usually different files:
        // the instance is in the testbench that samples it, the type in the
        // package that declares it.
        cg.instanceAt("tb/fifo_cg.sv", 22);
        cg.typeAt("tb/fifo_pkg.sv", 140);
        cg.options(ux::Options().atLeast(2).comment("per-run instance"));
        cg.parameter("DEPTH", "16");

        ux::Coverpoint len = cg.coverpoint("cp_len");
        len.exprString("txn.len");
        len.bin("small", 0, 3, 41);         // a range bin
        len.bin("exactly_seven", 7, 12);    // a single-value bin
        len.illegal("overflow", 16, 255, 0);
        len.ignore("reserved", 14, 15, 0);
        len.defaultBin("other", 3);

        // A transition bin: the value order is the information, so it is kept
        // exactly as given.
        static const int64_t kSeq[] = {0, 1, 2};
        len.sequenceBin("ramp", kSeq, 3, 9);

        ux::Coverpoint kind = cg.coverpoint("cp_kind");
        kind.bin("rd", 0, 120);
        kind.bin("wr", 1, 118);
    }

    return ex::finish(cov, "07_covergroup");
}
// --8<-- [end:body]
