// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// 08_cross - a cross of two coverpoints. The writer emits coverpoints before
// crosses whichever order you declare them in.
#include "ex_common.hpp"

// --8<-- [start:body]
namespace ux = ucisxml;

int main(int argc, char** argv) {
    ux::CoverageWriter cov;
    if (!cov.openFile(ex::outPath(argc, argv, "08_cross.xml"), ex::options()))
        return 1;
    cov.tool(ux::Tool().name("cgcov").version("1.0").vendorId("CVST"));
    cov.test(ux::Test().name("run1"));
    cov.sources({"tb/fifo_cg.sv"});

    {
        ux::Scope s = cov.scope("tb.u_env.fifo_cg", "fifo_env");
        s.declaredAt("tb/fifo_cg.sv", 5);
        ux::Covergroup cg = s.covergroup("cg_fifo", "fifo_cg", "work.fifo_pkg");
        cg.instanceAt("tb/fifo_cg.sv", 22);

        // Declared cross-first on purpose: the writer sorts, so this is legal.
        ux::Cross x = cg.cross("x_len_kind", {"cp_len", "cp_kind"});
        x.options(ux::Options().crossNumPrintMissing(4));

        ux::Coverpoint len = cg.coverpoint("cp_len");
        len.bin("small", 0, 3, 41);
        len.bin("large", 4, 15, 60);

        ux::Coverpoint kind = cg.coverpoint("cp_kind");
        kind.bin("rd", 0, 120);
        kind.bin("wr", 1, 118);

        // The index tuple lines up positionally with the cross's coverpoint
        // list, so its order is preserved.
        static const int64_t kRdSmall[] = {0, 0};
        static const int64_t kRdLarge[] = {1, 0};
        static const int64_t kWrSmall[] = {0, 1};
        static const int64_t kWrLarge[] = {1, 1};
        x.bin("<small,rd>", kRdSmall, 2, 12);
        x.bin("<large,rd>", kRdLarge, 2, 29);
        x.bin("<small,wr>", kWrSmall, 2, 29);
        x.bin("<large,wr>", kWrLarge, 2, 31);
    }

    return ex::finish(cov, "08_cross");
}
// --8<-- [end:body]
