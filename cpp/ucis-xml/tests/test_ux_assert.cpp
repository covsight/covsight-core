// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// T-5, second half: with UCIS_XML_ASSERT defined, a contract violation aborts
// loudly instead of latching. That is how the test suite is built (design
// §5.5), and it is what makes a violation impossible to miss during
// development while keeping production silent.
//
// Each case runs in a forked child, because the first abort ends the process.
#include <cstdio>
#include <cstdlib>

static void uxAbort(const char* message) {
    std::fprintf(stderr, "ucis_xml contract violation: %s\n", message);
    std::abort();
}

#define UCIS_XML_ASSERT(msg) uxAbort(msg)

#include "ux_points.hpp"
#include "ux_test.hpp"

#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#define UX_CAN_FORK 1
#else
#define UX_CAN_FORK 0
#endif

namespace ux = ucisxml;

namespace {

void violate() {
    ux::MemorySink out;
    ux::CoverageWriter cov;
    cov.open(out.sink(), ux::WriterOptions().writtenTime("2026-01-01T00:00:00"));
    cov.tool(ux::Tool().name("assert").version("1.0").vendorId("CVST"));
    cov.test(ux::Test().name("t"));
    cov.sources({"a.sv"});
    {
        ux::Scope s = cov.scope("top");
        s.line("never-declared.sv", 1, 1);  // LateSourceFile
    }
    cov.close();
}

}  // namespace

int main() {
#if UX_CAN_FORK
    pid_t pid = fork();
    if (pid == 0) {
        violate();
        std::_Exit(0);  // reached only if the assert did not fire
    }
    int status = 0;
    waitpid(pid, &status, 0);
    CHECK_MSG(WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status) != 0),
              "a contract violation did not abort under UCIS_XML_ASSERT");
#else
    // Without fork there is no way to observe an abort and keep testing; the
    // build itself still proves the macro hook compiles.
    (void)&violate;
    std::fprintf(stderr, "test_ux_assert: fork unavailable, compile-only\n");
#endif
    return uxtest::finish("test_ux_assert");
}
