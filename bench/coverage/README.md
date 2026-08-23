# Code-coverage size benchmark (Verilator → NCDB)

Measures NCDB on-disk size against the native Verilator `coverage.dat` and a
UCIS-XML rendering, using **real** Verilator code coverage (line / branch /
toggle) rather than the synthetic model in `../bench_size_realistic.py`.

## Pipeline

```
Verilator --coverage  →  coverage.dat  →  coverage_to_ncdb.py
                                              ├─ parse (SystemC::Coverage-3)
                                              ├─ build covsight-core MemUCIS
                                              ├─ write NCDB (stored + deflated)
                                              └─ compare vs .dat(.gz) and UCIS-XML(.gz)
```

`coverage_to_ncdb.py` is the reusable core — it ingests any `coverage.dat`.
The `.dat` parser mirrors pyucis `ucis/vltcov` (kept self-contained here).

## Quick start — runs end-to-end today

Requires Verilator (5.024+) and the covsight-core Python package on the path.

```bash
./run_example.sh
```

Builds the small design in `example/` **with a harness that writes coverage**
(`example/sim_main.cpp`), runs it, and prints the size comparison. This is the
validated proof of the pipeline. First local run: ~866 points (mostly toggle),
NCDB-deflated ≈ coverage.dat.gz at this small scale — the binary advantage
opens up at the larger scales the RTLMeter designs reach.

## Scaling up — real RTLMeter designs

```bash
./run_rtlmeter.sh OpenTitan:default:hello
```

RTLMeter (verilator/rtlmeter) ships large real designs — BlackParrot, Caliptra,
NVDLA, OpenPiton, OpenTitan, Vortex, XiangShan, VeeR / XuanTie cores — with real
workloads. The script clones it, runs a case with `--compileArgs="--coverage"`,
finds the resulting `coverage.dat`, and feeds it to the converter.

### Coverage emission — works without patching RTLMeter

`--coverage` only inserts counters; a `coverage.dat` is written only when
something calls `VerilatedCov::write()`. The good news: **RTLMeter verilates
with `--main`** (see `src/rtlmeter/verilator.py`), and Verilator's
auto-generated main already emits coverage on exit when built with coverage:

```cpp
// Write coverage data (since Verilated with --coverage)
contextp->coveragep()->write();
```

So `--compileArgs="--coverage"` is sufficient — `coverage.dat` lands in the
execute directory automatically, no harness change required. (Verified by
generating a `--main` with Verilator 5.041 and inspecting `Vsim__main.cpp`.)
A *custom* harness — like `example/sim_main.cpp` here — must call write()
itself; RTLMeter simply doesn't use one.

## Files

| File | Purpose |
| ---- | ------- |
| `coverage_to_ncdb.py` | parse coverage.dat → NCDB; size comparison + member breakdown |
| `example/top.sv`      | small SV design (ALU + regfile) sized to yield real line/toggle points |
| `example/sim_main.cpp`| harness that drives stimulus and **writes coverage.dat** |
| `run_example.sh`      | build + run the example, then convert (end-to-end, no RTLMeter) |
| `run_rtlmeter.sh`     | clone + run one RTLMeter case with `--coverage`, then convert |
| `run_suite.sh`        | run a suite of cases, aggregate metrics to `results.jsonl` |
| `summarize.py`        | `results.jsonl` → markdown comparison table + CSV |
| `suites/*.txt`        | suite definitions (smoke / large / scaling / counts-stress) |

## Benchmark suites

```bash
./run_suite.sh suites/smoke.txt      # fast pipeline check
./run_suite.sh suites/large.txt      # large-design stress (heavy; see plan)
```

Results accumulate in `results.jsonl` (de-duped by case label on re-run). See
[`../../docs/ncdb-benchmark-plan.md`](../../docs/ncdb-benchmark-plan.md) for the
design prioritization, phasing, and resource expectations.

## Notes

- Code coverage is **toggle-dominated** (in the example, 846 of 866 points) —
  this is the "unique names" regime from the functional-coverage benchmark made
  real: many per-signal-bit points whose names don't dedup.
- The UCIS-XML emitter is a *representative* compact rendering (one element per
  point, same information content), not a byte-perfect UCIS schema document.
- No functional/covergroup coverage here — Verilator covergroups (#7117) aren't
  landed; `../bench_size_realistic.py` still covers that half synthetically.
