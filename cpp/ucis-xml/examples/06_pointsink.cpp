// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// 06_pointsink - the flat path, for a tool whose input is already a flat list
// of records (Verilator's coverage.dat, or any converter's input).
#include "ex_common.hpp"

// --8<-- [start:body]
namespace ux = ucisxml;

int main(int argc, char** argv) {
    // A flat record list, as a converter would have it. PointSink opens and
    // closes scopes as the scope field changes; all it asks is that records for
    // one scope are contiguous, and sortByScope() is there if they are not.
    // declaredAt() is where the *instance* is declared, which is not the same
    // as where this record's coverage item is -- and a converter that does not
    // know it should leave it unset rather than guess.
    //
    // On a toggle record, file/line is the signal's declaration site. It is the
    // only source of toggleObject/id, so it is worth carrying through.
    ux::Point pts[5];
    pts[0].kind(ux::Kind::Line).scope("top.u_a").declaredAt("a.sv", 1)
          .file("a.sv").line(3).count(7);
    pts[1].kind(ux::Kind::Toggle).scope("top.u_b").declaredAt("b.sv", 2)
          .file("b.sv").line(5).signal("d").bit("0")
          .edge(ux::Edge::Rise).count(12);
    pts[2].kind(ux::Kind::Line).scope("top.u_a").declaredAt("a.sv", 1)
          .file("a.sv").line(4).count(0);
    pts[3].kind(ux::Kind::Branch).scope("top.u_b").declaredAt("b.sv", 2)
          .file("b.sv").line(9).name("true").count(3);
    pts[4].kind(ux::Kind::Toggle).scope("top.u_b").declaredAt("b.sv", 2)
          .file("b.sv").line(5).signal("d").bit("0")
          .edge(ux::Edge::Fall).count(11);

    ux::sortByScope(pts, 5);

    ux::CoverageWriter cov;
    if (!cov.openFile(ex::outPath(argc, argv, "06_pointsink.xml"), ex::options()))
        return 1;
    cov.tool(ux::Tool().name("convert").version("1.0").vendorId("CVST"));
    cov.test(ux::Test().name("run1"));
    cov.sources({"a.sv", "b.sv"});

    {
        ux::PointSink sink(cov);
        for (const ux::Point& p : pts) sink.add(p);
    }

    return ex::finish(cov, "06_pointsink");
}
// --8<-- [end:body]
