// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// T-5: contract violations.
//
// Design §5.5 is explicit that a coverage write must never take a simulation
// down, so each of these latches an error, turns every later call into a no-op,
// and reports at close() -- and the message has to name the fix, not just the
// symptom. That message text is what the troubleshooting page (X-6) documents,
// so it is asserted here rather than left to drift.
//
// The suite deliberately does NOT define UCIS_XML_ASSERT: this file tests the
// production behaviour. CI builds the same file a second time with the assert
// defined (target ux_contract_assert), where each case must abort instead.
#include "ux_under_test.hpp"
#include "ux_test.hpp"

namespace ux = ucisxml;

namespace {

void header(ux::CoverageWriter& cov) {
    cov.tool(ux::Tool().name("contract").version("1.0").vendorId("CVST"));
    cov.test(ux::Test().name("t"));
}

// A source path first seen after the first scope opened, under the default
// Sources::UpFront. The message must name both fixes.
void testLateSourceFile() {
    ux::MemorySink out;
    ux::CoverageWriter cov;
    cov.open(out.sink(), ux::WriterOptions().writtenTime("2026-01-01T00:00:00"));
    header(cov);
    cov.sources({"a.sv"});
    {
        ux::Scope s = cov.scope("top.u_alu");
        s.line("rtl/alu.sv", 3, 1);  // never declared
    }
    CHECK(!cov.close());
    CHECK(cov.errorCode() == ux::Err::LateSourceFile);
    CHECK(uxtest::contains(cov.error(), std::strlen(cov.error()), "rtl/alu.sv"));
    CHECK(uxtest::contains(cov.error(), std::strlen(cov.error()), "cov.sources()"));
    CHECK(uxtest::contains(cov.error(), std::strlen(cov.error()),
                           "Sources::Deferred"));
}

// The same path under Sources::Deferred is simply interned -- that is the whole
// point of the mode.
void testDeferredAcceptsLateFile() {
    ux::MemorySink out;
    ux::CoverageWriter cov;
    cov.open(out.sink(), ux::WriterOptions()
                             .writtenTime("2026-01-01T00:00:00")
                             .sources(ux::Sources::Deferred));
    header(cov);
    { ux::Scope s = cov.scope("top.u_alu"); s.line("rtl/alu.sv", 3, 1); }
    CHECK(cov.close());
    CHECK_HAS(out, "fileName=\"rtl/alu.sv\"");
}

// Rule 2: finish one scope before opening the next. Closing an outer scope
// while an inner one is still open is the violation the writer can detect.
void testScopeClosedOutOfOrder() {
    ux::MemorySink out;
    ux::CoverageWriter cov;
    cov.open(out.sink(), ux::WriterOptions().writtenTime("2026-01-01T00:00:00"));
    header(cov);
    cov.sources({"a.sv"});
    {
        ux::Scope top = cov.scope("top");
        ux::Scope child = top.child("u_core");
        top.close();  // child is still open
        child.close();
    }
    CHECK(!cov.close());
    CHECK(cov.errorCode() == ux::Err::ScopeReopened);
    CHECK(uxtest::contains(cov.error(), std::strlen(cov.error()),
                           "finish one scope before opening the next"));
}

// blockCoverage is an xsd:choice, so a scope cannot have both.
void testMixedBlockForms() {
    ux::MemorySink out;
    ux::CoverageWriter cov;
    cov.open(out.sink(), ux::WriterOptions().writtenTime("2026-01-01T00:00:00"));
    header(cov);
    cov.sources({"a.sv"});
    {
        ux::Scope s = cov.scope("top");
        s.line("a.sv", 1, 1);
        s.block("a.sv", 2, 1);
    }
    CHECK(!cov.close());
    CHECK(cov.errorCode() == ux::Err::MixedBlockForms);
    CHECK(uxtest::contains(cov.error(), std::strlen(cov.error()), "xsd:choice"));
}

// An FSM transition through a state that was never declared.
void testUnknownState() {
    ux::MemorySink out;
    ux::CoverageWriter cov;
    cov.open(out.sink(), ux::WriterOptions().writtenTime("2026-01-01T00:00:00"));
    header(cov);
    cov.sources({"a.sv"});
    {
        ux::Scope s = cov.scope("top");
        ux::Fsm f = s.fsm("st");
        f.state("IDLE", 1);
        f.transition("IDLE", "GONE", 1);
    }
    CHECK(!cov.close());
    CHECK(cov.errorCode() == ux::Err::UnknownState);
    CHECK(uxtest::contains(cov.error(), std::strlen(cov.error()), "GONE"));
}

// A cross naming a coverpoint that is not in its covergroup.
void testUnknownCoverpoint() {
    ux::MemorySink out;
    ux::CoverageWriter cov;
    cov.open(out.sink(), ux::WriterOptions().writtenTime("2026-01-01T00:00:00"));
    header(cov);
    cov.sources({"a.sv"});
    {
        ux::Scope s = cov.scope("top");
        ux::Covergroup cg = s.covergroup("cg");
        ux::Coverpoint cp = cg.coverpoint("cp_a");
        cp.bin("x", 0, 1);
        ux::Cross x = cg.cross("x_ab", {"cp_a", "cp_missing"});
        static const int64_t kIdx[] = {0, 0};
        x.bin("<x,?>", kIdx, 2, 1);
    }
    CHECK(!cov.close());
    CHECK(cov.errorCode() == ux::Err::UnknownCoverpoint);
    CHECK(uxtest::contains(cov.error(), std::strlen(cov.error()), "cp_missing"));
}

// Duplicate names inside one container are not an error -- they are a real
// ambiguity the caller may have meant -- but they must be distinguished and
// counted, never silently merged (decision D-3).
void testDuplicateKey() {
    ux::MemorySink out;
    ux::CoverageWriter cov;
    cov.open(out.sink(), ux::WriterOptions().writtenTime("2026-01-01T00:00:00"));
    header(cov);
    cov.sources({"a.sv"});
    {
        ux::Scope s = cov.scope("top");
        ux::Covergroup cg = s.covergroup("cg");
        ux::Coverpoint cp = cg.coverpoint("cp");
        cp.bin("dup", 0, 1);
        cp.bin("dup", 5, 2);
    }
    CHECK(cov.close());
    CHECK_MSG(cov.keyCollisions() == 1, "the second 'dup' is a collision");
    CHECK_HAS(out, "key=\"dup\"");
    CHECK_HAS(out, "key=\"dup#2\"");
}

// A document with no scopes at all cannot satisfy instanceCoverages+.
void testEmptyDocument() {
    ux::MemorySink out;
    ux::CoverageWriter cov;
    cov.open(out.sink(), ux::WriterOptions().writtenTime("2026-01-01T00:00:00"));
    header(cov);
    cov.sources({"a.sv"});
    CHECK(!cov.close());
    CHECK(cov.errorCode() == ux::Err::EmptyDocument);
}

// tool() and test() are not optional: HISTORY_NODE has nine required
// attributes and they are where six of them come from.
void testMissingHistory() {
    ux::MemorySink out;
    ux::CoverageWriter cov;
    cov.open(out.sink(), ux::WriterOptions().writtenTime("2026-01-01T00:00:00"));
    cov.sources({"a.sv"});
    { ux::Scope s = cov.scope("top"); s.line("a.sv", 1, 1); }
    CHECK(!cov.close());
    CHECK(cov.errorCode() == ux::Err::MissingHistory);
}

// A scope left open at close() is reported rather than quietly dropped.
void testScopeNeverClosed() {
    ux::MemorySink out;
    ux::CoverageWriter cov;
    cov.open(out.sink(), ux::WriterOptions().writtenTime("2026-01-01T00:00:00"));
    header(cov);
    cov.sources({"a.sv"});
    ux::Scope s = cov.scope("top");
    s.line("a.sv", 1, 1);
    CHECK(!cov.close());
    CHECK(cov.errorCode() == ux::Err::ScopeNotClosed);
    s.close();  // the handle still has to be safe to destroy
}

// The first error is the one kept: it is the one that explains the others.
void testFirstErrorWins() {
    ux::MemorySink out;
    ux::CoverageWriter cov;
    cov.open(out.sink(), ux::WriterOptions().writtenTime("2026-01-01T00:00:00"));
    header(cov);
    cov.sources({"a.sv"});
    {
        ux::Scope s = cov.scope("top");
        s.line("late.sv", 1, 1);   // LateSourceFile
        s.line("a.sv", 1, 1);
        s.block("a.sv", 2, 1);     // would be MixedBlockForms
    }
    CHECK(!cov.close());
    CHECK(cov.errorCode() == ux::Err::LateSourceFile);
}

}  // namespace

int main() {
    testLateSourceFile();
    testDeferredAcceptsLateFile();
    testScopeClosedOutOfOrder();
    testMixedBlockForms();
    testUnknownState();
    testUnknownCoverpoint();
    testDuplicateKey();
    testEmptyDocument();
    testMissingHistory();
    testScopeNeverClosed();
    testFirstErrorWins();
    return uxtest::finish("test_ux_contract");
}
