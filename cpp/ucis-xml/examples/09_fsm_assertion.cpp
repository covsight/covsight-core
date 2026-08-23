// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// 09_fsm_assertion - FSM and assertion coverage. Both are cases where the
// schema demands a fixed order the caller should not have to know: states
// before transitions, and ASSERTION's eight optional bins in one exact
// sequence.
#include "ex_common.hpp"

// --8<-- [start:body]
namespace ux = ucisxml;

int main(int argc, char** argv) {
    ux::CoverageWriter cov;
    if (!cov.openFile(ex::outPath(argc, argv, "09_fsm_assertion.xml"), ex::options()))
        return 1;
    cov.tool(ux::Tool().name("fcov").version("1.0").vendorId("CVST"));
    cov.test(ux::Test().name("run1").passed(true).cpuTime(12.5));
    cov.sources({"rtl/ctrl.sv"});

    {
        ux::Scope s = cov.scope("top.u_ctrl", "ctrl");
        s.declaredAt("rtl/ctrl.sv", 4);

        ux::Fsm f = s.fsm("state_q", "ctrl_state_e", 2);
        // Transitions may precede the states they name.
        f.transition("IDLE", "RUN", 42);
        f.transition("RUN", "DONE", 41);
        f.state("IDLE", 900, "0");
        f.state("RUN", 42, "1");
        f.state("DONE", 41, "2");

        // A three-state path.
        static const ux::Text kPath[] = {"IDLE", "RUN", "DONE"};
        f.transition(kPath, 3, 40);

        // Called in whatever order the tool has them; emitted in the schema's.
        ux::Assertion a = s.assertion("a_no_overflow");
        a.vacuous(88);
        a.attempts(1292);
        a.passes(1204);
        a.fails(0);

        ux::Assertion c = s.assertion("c_handshake", "cover");
        c.covers(77);
        c.peakActive(3);
    }

    return ex::finish(cov, "09_fsm_assertion");
}
// --8<-- [end:body]
