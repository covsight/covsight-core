#!/usr/bin/env bash
# Drive a real RTLMeter design through the coverage→NCDB benchmark.
#
# Why this works without patching anything:
#   RTLMeter verilates every design with `--main` (see src/rtlmeter/verilator.py:
#   `verilator --cc --main --exe --timing ...`). Verilator's *auto-generated*
#   main writes coverage on exit when built with --coverage:
#       // Write coverage data (since Verilated with --coverage)
#       contextp->coveragep()->write();
#   So passing `--compileArgs="--coverage"` is enough — coverage.dat is emitted
#   into the execute directory automatically; no harness hook is required.
#   (A *custom* harness would need the write() call itself — see
#   example/sim_main.cpp — but RTLMeter doesn't use one.)
#
# Cost note: with --coverage, Verilation + C++ build are markedly slower and
#   toggle coverage on a full SoC is large. OpenTitan:default:hello is the
#   lightest case; expect minutes and a few GB RAM.
#
# Usage:
#   ./run_rtlmeter.sh [CASE]
#   CASE defaults to OpenTitan:default:hello (RTLMeter's smoke case).
# Env:
#   RTLMETER_DIR  where to clone/find rtlmeter (default: ~/projects/rtlmeter)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
CASE="${1:-OpenTitan:default:hello}"
RTLMETER_DIR="${RTLMETER_DIR:-$HOME/projects/rtlmeter}"
WORKROOT="work-cov"
OUTDIR="$HERE/rtlmeter-out"

command -v verilator >/dev/null || { echo "verilator not on PATH"; exit 1; }

if [ ! -d "$RTLMETER_DIR/.git" ]; then
    echo "== Cloning RTLMeter into $RTLMETER_DIR =="
    git clone --depth 1 https://github.com/verilator/rtlmeter "$RTLMETER_DIR"
fi
cd "$RTLMETER_DIR"

if [ ! -x "venv/bin/python3" ]; then
    echo "== Setting up RTLMeter venv (make venv) =="
    make venv
fi

echo "== Available cases (first matching '$CASE' family) =="
./rtlmeter show --cases | grep -i "${CASE%%:*}" || true

echo "== Running '$CASE' with --coverage =="
./rtlmeter run --cases "$CASE" --compileArgs="--coverage" --workRoot "$WORKROOT"

echo "== Locating coverage.dat under $WORKROOT =="
mkdir -p "$OUTDIR"
mapfile -t dats < <(find "$WORKROOT" -name 'coverage.dat' -size +0c 2>/dev/null)
if [ "${#dats[@]}" -eq 0 ]; then
    cat >&2 <<EOF

No coverage.dat found under $WORKROOT. Expected one (RTLMeter's --main build
auto-writes it under --coverage). Check that:
  - the case actually executed (not just compiled), and reached \$finish, and
  - --compileArgs="--coverage" was applied to the *verilate* step.
The converter (coverage_to_ncdb.py) works on any coverage.dat once produced.
EOF
    exit 2
fi

i=0
for dat in "${dats[@]}"; do
    cp "$dat" "$OUTDIR/coverage_$i.dat"
    echo "== coverage_$i.dat -> NCDB + size comparison =="
    python3 "$HERE/coverage_to_ncdb.py" "$OUTDIR/coverage_$i.dat" \
        --out "$OUTDIR/coverage_$i.cdb"
    i=$((i + 1))
done
