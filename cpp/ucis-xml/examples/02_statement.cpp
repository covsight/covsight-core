// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// 02_statement - line coverage, the shape almost every code-coverage tool has.
#include "ex_common.hpp"

// --8<-- [start:body]
namespace ux = ucisxml;

struct Hit { const char* file; unsigned line; unsigned long long count; const char* what; };

int main(int argc, char** argv) {
    static const Hit kHits[] = {
        {"rtl/alu.sv", 42, 17, "if (a)"},
        {"rtl/alu.sv", 44, 0, "else"},
        {"rtl/fifo.sv", 8, 1024, "always_ff"},
    };

    ux::CoverageWriter cov;
    if (!cov.openFile(ex::outPath(argc, argv, "02_statement.xml"), ex::options()))
        return 1;
    cov.tool(ux::Tool().name("linecov").version("2.1").vendorId("CVST"));
    cov.test(ux::Test().name("smoke").passed(true).seed("12345"));
    cov.sources({"rtl/alu.sv", "rtl/fifo.sv", "rtl/unused.sv"});

    {
        ux::Scope s = cov.scope("top.u_alu", "alu");
        s.declaredAt("rtl/alu.sv", 12);

        for (const Hit& h : kHits)
            s.line(h.file, h.line, h.count, h.what);

        // Uncommon bin attributes stay chainable rather than cluttering the
        // common-case signature.
        s.line("rtl/alu.sv", 99, 0, "unreachable")
            .exclude("waived: tied off in this configuration");
    }

    return ex::finish(cov, "02_statement");
}
// --8<-- [end:body]
