// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// 01_hello - the smallest complete UCIS-XML document.
#include "ex_common.hpp"

// --8<-- [start:body]
namespace ux = ucisxml;

int main(int argc, char** argv) {
    ux::CoverageWriter cov;
    if (!cov.openFile(ex::outPath(argc, argv, "01_hello.xml"), ex::options()))
        return 1;

    // Rule 1 of the contract: the tool, the test and the source files come
    // first. The file list may be a superset, so it is the compile file list
    // you already have.
    cov.tool(ux::Tool().name("hello").version("1.0").vendorId("CVST"));
    cov.test(ux::Test().name("run1").passed(true));
    cov.sources({"rtl/alu.sv"});

    {
        ux::Scope s = cov.scope("top.u_alu", "alu");
        // Where the instance itself is declared. Without this the scope points
        // at the synthetic "(unknown)" file, and a reporting tool has nothing
        // to link back to.
        s.declaredAt("rtl/alu.sv", 12);

        s.line("rtl/alu.sv", 42, 17);
    }  // the scope renders when it closes

    return ex::finish(cov, "01_hello");
}
// --8<-- [end:body]
