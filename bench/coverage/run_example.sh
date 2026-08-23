#!/usr/bin/env bash
# Build + run the local Verilator coverage example, then push its real
# coverage.dat through coverage_to_ncdb.py. Runs end-to-end with just Verilator
# + Python — no RTLMeter clone needed. This is the runnable proof of the
# coverage→NCDB pipeline; run_rtlmeter.sh scales the same converter to real
# designs.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
EX="$HERE/example"
BUILD="$EX/obj_dir"
DAT="$EX/coverage.dat"

command -v verilator >/dev/null || { echo "verilator not on PATH"; exit 1; }

echo "== Verilating + building (with --coverage) =="
verilator --cc --exe --build --coverage -Wno-fatal -Wno-WIDTH \
    --top-module top \
    -CFLAGS "-O2" \
    -Mdir "$BUILD" \
    -o sim \
    "$EX/top.sv" "$EX/sim_main.cpp"

echo "== Running simulation =="
( cd "$EX" && "$BUILD/sim" "$DAT" )

echo "== coverage.dat -> NCDB + size comparison =="
python3 "$HERE/coverage_to_ncdb.py" "$DAT" --out "$EX/coverage.cdb"
