// Verilator harness for the coverage example. Drives the design for a fixed
// number of cycles with varied stimulus, then writes coverage.dat. The
// VerilatedCov::write() call is what actually produces the coverage file —
// Verilator never dumps it automatically (this is exactly the hook RTLMeter's
// perf harnesses lack; see ../README.md).

#include "Vtop.h"
#include "verilated.h"
#include "verilated_cov.h"

int main(int argc, char** argv) {
    VerilatedContext context;
    context.commandArgs(argc, argv);
    Vtop top{&context};

    const int kCycles = 5000;
    top.rst_n = 0;
    top.stim = 0;
    for (int i = 0; i < 4; ++i) {              // reset pulse
        top.clk = !top.clk; top.eval();
    }
    top.rst_n = 1;

    unsigned lfsr = 0xACE1u;                   // cheap varied stimulus
    for (int c = 0; c < kCycles; ++c) {
        lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u);
        top.stim = lfsr & 0xFF;
        top.clk = 0; top.eval();
        top.clk = 1; top.eval();
    }

    const char* out = (argc > 1 && argv[1][0] != '+') ? argv[1] : "coverage.dat";
#if VM_COVERAGE
    context.coveragep()->write(out);
#endif
    top.final();
    return 0;
}
