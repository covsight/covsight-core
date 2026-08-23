// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// T-7 (writer half): the two source modes.
//
// The claim in design §3.1 is that Sources::Deferred exists to remove an ask
// from the caller, not to change the document. So: the same facts through both
// modes, with the same declared file list, must produce byte-identical output.
//
// The other half of T-7 -- that a superset file list validates, and that
// concatenated gzip members are recovered by three different readers -- is in
// tests/ucis_xml/test_sources.py, because both are facts about tooling outside
// this header.
#include "ux_under_test.hpp"
#include "ux_test.hpp"

namespace ux = ucisxml;

namespace {

void emit(ux::CoverageWriter& cov, bool declareAll) {
    cov.tool(ux::Tool().name("sources").version("1.0").vendorId("CVST"));
    cov.test(ux::Test().name("t"));
    if (declareAll) cov.sources({"a.sv", "b.sv"});
    {
        ux::Scope s = cov.scope("top.u_a", "a");
        s.line("a.sv", 1, 5);
        s.line("b.sv", 2, 6);
    }
    {
        ux::Scope s = cov.scope("top.u_b", "b");
        s.line("b.sv", 3, 7);
    }
}

void testModesAgree() {
    ux::MemorySink up, def;

    {
        ux::CoverageWriter cov;
        cov.open(up.sink(), ux::WriterOptions().writtenTime("2026-01-01T00:00:00"));
        emit(cov, true);
        CHECK(cov.close());
        CHECK(cov.warnings() == 0);
        CHECK_MSG(cov.spoolBytes() == 0, "UpFront must not spool anything");
    }
    {
        ux::CoverageWriter cov;
        cov.open(def.sink(), ux::WriterOptions()
                                 .writtenTime("2026-01-01T00:00:00")
                                 .sources(ux::Sources::Deferred));
        emit(cov, true);
        CHECK(cov.close());
        CHECK(cov.warnings() == 0);
        CHECK_MSG(cov.spoolBytes() > 0, "Deferred must spool the scope bodies");
        CHECK_MSG(!cov.spoolSpilled(), "a document this size stays in memory");
    }

    CHECK_MSG(up.size() == def.size() &&
                  std::memcmp(up.begin(), def.begin(), up.size()) == 0,
              "UpFront and Deferred produced different documents");
}

// Deferred with nothing declared: paths are interned as they are seen, and the
// table still comes out ahead of the first scope.
void testDeferredWithoutDeclarations() {
    ux::MemorySink out;
    ux::CoverageWriter cov;
    cov.open(out.sink(), ux::WriterOptions()
                             .writtenTime("2026-01-01T00:00:00")
                             .sources(ux::Sources::Deferred));
    emit(cov, false);
    CHECK(cov.close());
    CHECK(cov.warnings() == 0);
    CHECK_HAS(out, "fileName=\"a.sv\"");
    CHECK_HAS(out, "fileName=\"b.sv\"");

    // The header, including the whole file table, precedes the first scope.
    const char* p = out.begin();
    size_t n = out.size();
    size_t files = 0, firstScope = n;
    for (size_t i = 0; i + 12 <= n; ++i) {
        if (std::memcmp(p + i, "<sourceFiles", 12) == 0) files = i;
        if (firstScope == n && std::memcmp(p + i, "<instanceCov", 12) == 0)
            firstScope = i;
    }
    CHECK_MSG(files < firstScope, "sourceFiles must precede instanceCoverages");
}

// The spool spills to a temp file above its threshold and replays byte for
// byte -- the property the gzip-member passthrough depends on.
void testSpoolSpills() {
    ux::MemorySink small, spilled;
    for (int pass = 0; pass < 2; ++pass) {
        ux::MemorySink& out = pass ? spilled : small;
        ux::CoverageWriter cov;
        ux::WriterOptions o;
        o.writtenTime("2026-01-01T00:00:00").sources(ux::Sources::Deferred);
        // Second pass: a threshold low enough that the body must go to disk.
        o.spoolThreshold(pass ? 1024 : 8u * 1024u * 1024u);
        cov.open(out.sink(), o);
        cov.tool(ux::Tool().name("spool").version("1.0").vendorId("CVST"));
        cov.test(ux::Test().name("t"));
        {
            ux::Scope s = cov.scope("top");
            for (uint32_t i = 1; i <= 2000; ++i) s.line("a.sv", i, i);
        }
        CHECK(cov.close());
        CHECK_MSG(cov.spoolSpilled() == (pass == 1), "spill threshold ignored");
    }
    CHECK_MSG(small.size() == spilled.size() &&
                  std::memcmp(small.begin(), spilled.begin(), small.size()) == 0,
              "a spilled spool did not replay byte for byte");
}

}  // namespace

int main() {
    testModesAgree();
    testDeferredWithoutDeclarations();
    testSpoolSpills();
    return uxtest::finish("test_ux_sources");
}
