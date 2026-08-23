// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// T-6: sanitization. Everything design §5.5 promises the caller never has to
// think about:
//   * xsd:positiveInteger cannot be zero, and tools do report line 0 for
//     generated constructs -- this bit the Python prototype;
//   * XML 1.0 forbids most control characters *even as numeric references*, so
//     escaping & < > " is not enough for names carrying arbitrary bytes;
//   * counts are uint64_t all the way through, so UINT64_MAX must survive.
#include "ux_under_test.hpp"
#include "ux_test.hpp"

namespace ux = ucisxml;

namespace {

void header(ux::CoverageWriter& cov) {
    cov.tool(ux::Tool().name("sanitize").version("1.0").vendorId("CVST"));
    cov.test(ux::Test().name("t"));
    cov.sources({"a.sv"});
}

void testLineZero() {
    ux::MemorySink out;
    ux::CoverageWriter cov;
    cov.open(out.sink(), ux::WriterOptions().writtenTime("2026-01-01T00:00:00"));
    header(cov);
    { ux::Scope s = cov.scope("top"); s.line("a.sv", 0, 3); }
    CHECK(cov.close());
    CHECK_HAS(out, "line=\"1\"");
    CHECK_LACKS(out, "line=\"0\"");
    CHECK_MSG(cov.warnings() >= 1, "clamping line 0 should be counted");
}

void testControlChars() {
    ux::MemorySink out;
    ux::CoverageWriter cov;
    cov.open(out.sink(), ux::WriterOptions().writtenTime("2026-01-01T00:00:00"));
    header(cov);
    {
        ux::Scope s = cov.scope("top");
        // \x01 is Verilator's own field separator, so a comment carrying one is
        // not hypothetical. \x0B and \x1F are equally illegal.
        s.toggle(ux::Text("sig\x01" "bad", 7), "0", ux::Edge::Rise, 1);
        s.line("a.sv", 5, 1, ux::Text("a\x0B" "b\x1F" "c", 5));
        // & < > " are legal, and must come out as entity references.
        s.line("a.sv", 6, 1, "a & b < c > d \"e\"");
        // Tab, newline and carriage return are legal XML but must be numeric
        // references to survive attribute-value normalization.
        s.line("a.sv", 7, 1, ux::Text("a\tb\nc\rd", 7));
    }
    CHECK(cov.close());
    CHECK_HAS(out, "sig?bad");
    CHECK_HAS(out, "a?b?c");
    CHECK_HAS(out, "a &amp; b &lt; c &gt; d &quot;e&quot;");
    CHECK_HAS(out, "a&#x9;b&#xA;c&#xD;d");
    // Four replacements: \x0B and \x1F once each in the statement comment, and
    // \x01 twice in the toggle signal, which is rendered as both @name and
    // @key. The counter counts repairs *performed*, not distinct inputs -- a
    // name that appears in two attributes is escaped twice. What matters for
    // the fixture rule is that it is zero exactly when nothing was repaired.
    //
    // Tab, newline and carriage return do not count: they come out as numeric
    // references with nothing lost, so there is nothing to warn about.
    CHECK_MSG(cov.warnings() == 4, "exactly the lossy replacements are counted");
}

void testBadUtf8() {
    ux::MemorySink out;
    ux::CoverageWriter cov;
    cov.open(out.sink(), ux::WriterOptions().writtenTime("2026-01-01T00:00:00"));
    header(cov);
    {
        ux::Scope s = cov.scope("top");
        // A lone continuation byte, and a truncated two-byte sequence.
        s.line("a.sv", 5, 1, ux::Text("x\x80y", 3));
        s.line("a.sv", 6, 1, ux::Text("p\xC3", 2));
        // Valid UTF-8 must pass through untouched.
        s.line("a.sv", 7, 1, ux::Text("caf\xC3\xA9", 5));
    }
    CHECK(cov.close());
    CHECK_HAS(out, "x?y");
    CHECK_HAS(out, "caf\xC3\xA9");
}

void testHugeCount() {
    ux::MemorySink out;
    ux::CoverageWriter cov;
    cov.open(out.sink(), ux::WriterOptions().writtenTime("2026-01-01T00:00:00"));
    header(cov);
    { ux::Scope s = cov.scope("top"); s.line("a.sv", 1, UINT64_MAX); }
    CHECK(cov.close());
    CHECK_HAS(out, "coverageCount=\"18446744073709551615\"");
}

void testEmptyAndUnknownFile() {
    // A fact with no path still needs a @file, and STATEMENT_ID/@file is a
    // required positiveInteger. That is what file id 1 exists for.
    ux::MemorySink out;
    ux::CoverageWriter cov;
    cov.open(out.sink(), ux::WriterOptions().writtenTime("2026-01-01T00:00:00"));
    header(cov);
    { ux::Scope s = cov.scope("top"); s.line(ux::Text(), 4, 1); }
    CHECK(cov.close());
    CHECK_HAS(out, "fileName=\"(unknown)\" id=\"1\"");
    CHECK_HAS(out, "<id file=\"1\" line=\"4\"");
}

}  // namespace

int main() {
    testLineZero();
    testControlChars();
    testBadUtf8();
    testHugeCount();
    testEmptyAndUnknownFile();
    return uxtest::finish("test_ux_sanitize");
}
