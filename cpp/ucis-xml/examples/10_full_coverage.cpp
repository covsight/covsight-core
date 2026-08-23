// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// 10_full_coverage - every complex type in ucis.xsd in one document, including
// the ones no current producer emits: PROCESS_BLOCK, nested BLOCK, DIMENSION,
// NAME_VALUE, USER_ATTR, METRIC_MODE, and a nested scope.
//
// This is the completeness fixture: design section 6 is the checklist, and a
// type with no fixture is not supported.
#include "ex_common.hpp"

// --8<-- [start:body]
namespace ux = ucisxml;

int main(int argc, char** argv) {
    ux::CoverageWriter cov;
    if (!cov.openFile(ex::outPath(argc, argv, "10_full_coverage.xml"), ex::options()))
        return 1;
    cov.tool(ux::Tool().name("everything").version("1.0").vendorId("CVST")
                       .category("UCIS:simulator"));
    cov.test(ux::Test().name("full").passed(true).seed("42").cmd("sim")
                       .args("+UVM_TESTNAME=t").cwd("/work").userName("ci")
                       .comment("completeness fixture").kind("simulation")
                       .simTime(1234.5, "ns").cpuTime(9.25));
    cov.sources({"rtl/top.sv", "rtl/core.sv", "tb/env.sv"});

    // A scope that carries design parameters, a metric mode, user attributes,
    // and a nested child scope.
    {
        ux::Scope top = cov.scope("top", "top_m");
        top.declaredAt("rtl/top.sv", 4);
        top.parameter("WIDTH", "32");
        top.parameter("DEPTH", "16");
        top.attr("build", "release");
        top.attr("jobs", "8", ux::AttrType::Int);
        top.metricMode(ux::CovKind::Toggle, "UCIS:toggle_enum");
        top.weight(ux::CovKind::Toggle, 2);

        top.signal("bus", "rtl/top.sv", 6, 31, 0);
        top.toggle("bus", 0u, ux::Edge::Rise, 5);
        top.toggle("bus", 0u, ux::Edge::Fall, 4);

        // Real block coverage under a process, with a nested block and its own
        // statement ids -- the BLOCK/PROCESS_BLOCK arm of the xsd:choice.
        ux::Block proc = top.process("always_ff", "rtl/top.sv", 10, 900, "seq");
        top.blockStatement(proc, "rtl/top.sv", 11);
        top.blockStatement(proc, "rtl/top.sv", 12);
        ux::Block nested = top.childBlock(proc, "rtl/top.sv", 14, 41, "inner");
        top.blockStatement(nested, "rtl/top.sv", 15);

        {
            // A nested scope: @parentInstanceId is built for you.
            ux::Scope core = top.child("u_core", "core_m");
            core.declaredAt("rtl/core.sv", 3);
            core.line("rtl/core.sv", 7, 33, "assign");
            core.line("rtl/core.sv", 8, 0, "unreachable")
                .alias("core.8")
                .goal(10)
                .weight(3)
                .attr("column", "12", ux::AttrType::Int);

            ux::Branch b = core.branch("rtl/core.sv", 20, "if", "en");
            b.arm("true", 30);
            b.arm("false", 3);

            ux::Expr e = core.condition("rtl/core.sv", 20, "c_en", "en && rdy");
            e.subExpr("en").subExpr("rdy");
            e.bin("11", 30);
            e.bin("10", 3);

            ux::Fsm f = core.fsm("st", "st_e", 2);
            f.state("A", 10, "0");
            f.state("B", 20, "1");
            f.transition("A", "B", 5);

            ux::Assertion a = core.assertion("a_x", "assert");
            a.attempts(100);
            a.passes(99);
            a.fails(1);
            a.vacuous(0);
            a.disabled(0);
            a.active(2);
            a.peakActive(4);
            a.covers(7);

            ux::Covergroup cg = core.covergroup("cg", "cg_t", "work.pkg");
            cg.instanceAt("tb/env.sv", 31).typeAt("tb/env.sv", 12);
            cg.options(ux::Options().weight(2).goal(90).atLeast(3)
                                    .autoBinMax(32).detectOverlap(true)
                                    .perInstance(true).mergeInstances(true)
                                    .crossNumPrintMissing(5).comment("all set"));
            cg.parameter("N", "4");
            cg.attr("origin", "handwritten");

            ux::Coverpoint cp = cg.coverpoint("cp_a");
            cp.options(ux::Options().atLeast(2));
            cp.bin("lo", 0, 3, 11).alias("low");
            cp.bin("hi", 4, 7, 0).exclude("waived");
            cp.illegal("bad", 8, 15, 0);
            cp.ignore("dontcare", 16, 31, 0);
            cp.defaultBin("rest", 2);
            static const int64_t kSeq[] = {0, 1, 2};
            cp.sequenceBin("ramp", kSeq, 3, 4);

            ux::Coverpoint cp2 = cg.coverpoint("cp_b");
            cp2.bin("x", 0, 5);
            cp2.bin("y", 1, 6);

            ux::Cross x = cg.cross("x_ab", {"cp_a", "cp_b"});
            x.options(ux::Options().goal(80));
            static const int64_t kIdx[] = {0, 1};
            x.bin("<lo,y>", kIdx, 2, 3).attr("note", "sampled");
        }
    }

    return ex::finish(cov, "10_full_coverage");
}
// --8<-- [end:body]
