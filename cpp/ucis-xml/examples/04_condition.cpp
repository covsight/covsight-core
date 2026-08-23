// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// 04_condition - expression coverage, including a nested sub-expression.
#include "ex_common.hpp"

// --8<-- [start:body]
namespace ux = ucisxml;

int main(int argc, char** argv) {
    ux::CoverageWriter cov;
    if (!cov.openFile(ex::outPath(argc, argv, "04_condition.xml"), ex::options()))
        return 1;
    cov.tool(ux::Tool().name("condcov").version("1.0").vendorId("CVST"));
    cov.test(ux::Test().name("run1"));
    cov.sources({"rtl/arb.sv"});

    {
        ux::Scope s = cov.scope("top.u_arb", "arb");
        s.declaredAt("rtl/arb.sv", 4);

        // @index and @width are the writer's problem: index is the ordinal
        // within the scope, width falls out of the sub-expression count.
        ux::Expr e = s.condition("rtl/arb.sv", 12, "c_grant", "req && !busy", "if");
        e.subExpr("req").subExpr("!busy");
        e.bin("00", 4);
        e.bin("01", 19);
        e.bin("10", 0);
        e.bin("11", 771);

        ux::Expr inner = s.nestedCondition(e, "rtl/arb.sv", 12, "c_busy", "!busy");
        inner.subExpr("busy");
        inner.bin("0", 771);
        inner.bin("1", 23);
    }

    return ex::finish(cov, "04_condition");
}
// --8<-- [end:body]
