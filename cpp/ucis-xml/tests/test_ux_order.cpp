// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// T-3: order independence -- the load-bearing test of the whole design.
//
// Rule 3 of the caller's contract (design §3) is "within a scope, emit anything
// in any order". Staging exists to make that true. So: build the same document
// several times with the containers created in different orders and the leaf
// facts emitted in different orders, and require the output to be
// byte-identical. Without this the staging design is unverified.
//
// This is why ordering is content-derived rather than arrival-derived
// (decision D-11): a sort keyed on insertion order would make this test pass
// trivially while proving nothing.
#include "ux_under_test.hpp"
#include "ux_test.hpp"

namespace ux = ucisxml;

namespace {

const uint32_t kContainers = 8;
const uint32_t kLeaves = 24;

// Everything the document contains, built with the containers created in
// `cperm` order and the leaf facts emitted in `lperm` order.
void build(ux::MemorySink& out, const uint32_t* cperm, const uint32_t* lperm) {
    ux::CoverageWriter cov;
    cov.open(out.sink(), ux::WriterOptions().writtenTime("2026-01-01T00:00:00"));
    cov.tool(ux::Tool().name("ordertest").version("1.0").vendorId("CVST"));
    cov.test(ux::Test().name("t").passed(true));
    cov.sources({"a.sv", "b.sv"});

    ux::Scope s = cov.scope("top.u_dut", "dut");

    ux::Branch br[2];
    ux::Expr ex[2];
    ux::Fsm fsm;
    ux::Assertion as[2];
    ux::Covergroup cg;
    ux::Coverpoint cp[2];
    ux::Cross xs;

    for (uint32_t i = 0; i < kContainers; ++i) {
        switch (cperm[i]) {
        case 0: br[0] = s.branch("a.sv", 10, "if", "a && b"); break;
        case 1: br[1] = s.branch("b.sv", 20, "case"); break;
        case 2: ex[0] = s.condition("a.sv", 30, "c0", "a || b"); break;
        case 3: ex[1] = s.condition("b.sv", 40, "c1", "!c"); break;
        case 4: fsm = s.fsm("state_q", "reg", 3); break;
        case 5: as[0] = s.assertion("a_no_overflow"); break;
        case 6: as[1] = s.assertion("a_handshake", "cover"); break;
        default:
            cg = s.covergroup("cg_fifo", "fifo_cg", "work.fifo");
            cp[0] = cg.coverpoint("cp_len");
            cp[1] = cg.coverpoint("cp_kind");
            xs = cg.cross("x_len_kind", {"cp_len", "cp_kind"});
            break;
        }
    }

    // The cross's coverpoint list and each expression's sub-expression list are
    // ordered sequences: their order is the information, so they are built here
    // rather than shuffled.
    ex[0].subExpr("a").subExpr("b");
    ex[1].subExpr("c");

    for (uint32_t i = 0; i < kLeaves; ++i) {
        switch (lperm[i]) {
        case 0:  s.line("a.sv", 11, 17, "stmt-a"); break;
        case 1:  s.line("a.sv", 12, 0, "stmt-b"); break;
        case 2:  s.line("b.sv", 21, 4, "stmt-c"); break;
        case 3:  s.toggle("data", 0u, ux::Edge::Rise, 9); break;
        case 4:  s.toggle("data", 0u, ux::Edge::Fall, 8); break;
        case 5:  s.toggle("data", 1u, ux::Edge::Rise, 7); break;
        case 6:  s.toggle("valid", "0", ux::Edge::Rise, 3); break;
        case 7:  s.signal("data", 7, 0); break;
        case 8:  br[0].arm("true", 5); break;
        case 9:  br[0].arm("false", 6); break;
        case 10: br[1].arm("default", 1); break;
        case 11: ex[0].bin("00", 2); break;
        case 12: ex[0].bin("11", 3); break;
        case 13: ex[1].bin("0", 4); break;
        case 14: fsm.state("IDLE", 900, "0"); break;
        case 15: fsm.state("RUN", 42, "1"); break;
        case 16: fsm.transition("IDLE", "RUN", 12); break;
        case 17: as[0].attempts(1292); break;
        case 18: as[0].passes(1204); break;
        case 19: as[0].vacuous(88); break;
        case 20: as[1].covers(7); break;
        case 21: cp[0].bin("small", 0, 3, 41).exclude("waived"); break;
        case 22: cp[1].bin("rd", 0, 12); break;
        default: {
            const int64_t idx[2] = {0, 0};
            xs.bin("<small,rd>", idx, 2, 12);
            break;
        }
        }
    }

    s.close();
    CHECK(cov.close());
    CHECK_MSG(cov.warnings() == 0, "unexpected warnings");
}

// A permutation with stride `step`, which is a permutation exactly when step
// and n are coprime. Cheap, deterministic, and genuinely reorders.
void stride(uint32_t* p, uint32_t n, uint32_t step, uint32_t start) {
    for (uint32_t i = 0; i < n; ++i) p[i] = (start + i * step) % n;
}

bool sameBytes(const ux::MemorySink& a, const ux::MemorySink& b) {
    return a.size() == b.size() &&
           std::memcmp(a.begin(), b.begin(), a.size()) == 0;
}

}  // namespace

int main() {
    uint32_t cid[kContainers], lid[kLeaves];
    for (uint32_t i = 0; i < kContainers; ++i) cid[i] = i;
    for (uint32_t i = 0; i < kLeaves; ++i) lid[i] = i;

    ux::MemorySink base;
    build(base, cid, lid);
    CHECK(base.size() > 0);

    // 5, 7, 11, 13 are all coprime with both 8 and 24, so each of these is a
    // genuine permutation of both lists.
    static const uint32_t kSteps[] = {5, 7, 11, 13};
    for (uint32_t k = 0; k < sizeof(kSteps) / sizeof(kSteps[0]); ++k) {
        uint32_t c[kContainers], l[kLeaves];
        stride(c, kContainers, kSteps[k], k);
        stride(l, kLeaves, kSteps[k], k * 3);

        ux::MemorySink shuffled;
        build(shuffled, c, l);
        CHECK_MSG(sameBytes(base, shuffled),
                  "shuffled emission order changed the output");
    }

    // Reversal, the case a stride permutation is least likely to catch.
    {
        uint32_t c[kContainers], l[kLeaves];
        for (uint32_t i = 0; i < kContainers; ++i) c[i] = kContainers - 1 - i;
        for (uint32_t i = 0; i < kLeaves; ++i) l[i] = kLeaves - 1 - i;
        ux::MemorySink reversed;
        build(reversed, c, l);
        CHECK_MSG(sameBytes(base, reversed),
                  "reversed emission order changed the output");
    }

    return uxtest::finish("test_ux_order");
}
