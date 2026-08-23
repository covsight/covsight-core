// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// 03_branch - branch coverage, including a branch nested inside an arm.
#include "ex_common.hpp"

// --8<-- [start:body]
namespace ux = ucisxml;

int main(int argc, char** argv) {
    ux::CoverageWriter cov;
    if (!cov.openFile(ex::outPath(argc, argv, "03_branch.xml"), ex::options()))
        return 1;
    cov.tool(ux::Tool().name("brcov").version("1.0").vendorId("CVST"));
    cov.test(ux::Test().name("run1"));
    cov.sources({"rtl/ctrl.sv"});

    {
        ux::Scope s = cov.scope("top.u_ctrl", "ctrl");
        s.declaredAt("rtl/ctrl.sv", 4);

        // Arms of one branch are grouped into a BRANCH_STATEMENT for you; you
        // never see the grouping, only the branch and its arms.
        ux::Branch b = s.branch("rtl/ctrl.sv", 21, "if", "valid && ready");
        b.arm("true", 812);
        b.arm("false", 44);

        // A branch inside the "true" arm of the one above.
        ux::Branch inner = s.nestedBranch(b.lastArm(), "rtl/ctrl.sv", 23, "if");
        inner.arm("true", 800);
        inner.arm("false", 12);

        ux::Branch c = s.branch("rtl/ctrl.sv", 40, "case", "state");
        c.arm("IDLE", 900);
        c.arm("RUN", 42);
        c.arm("default", 0);
    }

    return ex::finish(cov, "03_branch");
}
// --8<-- [end:body]
