#!/bin/sh
# Regenerate the Verilator coverage fixtures and the verilator_coverage
# summaries the tests use as an oracle.  Fixtures were captured with
# Verilator 5.052; run with that version (or newer) on PATH.
set -e
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
FLAGS="--binary --timing -Wno-fatal --coverage-user --coverage-line --coverage-expr --coverage-toggle --coverage-fsm"

build_run() {  # <name> <top> <src> [extra flags]
  name=$1 top=$2 src=$3; shift 3
  mkdir -p "$work/$name"
  cp "$here/$src" "$work/$name/"
  (cd "$work/$name" &&
   verilator $FLAGS "$@" "$src" --top-module "$top" -o sim >build.log &&
   ./obj_dir/sim >/dev/null)
  # Neutralize the install path of Verilator's std package sources.
  python3 -c 'import re,sys; sys.stdout.write(re.sub("\x02[^\x01]*/share/verilator/include/", "\x02/opt/verilator/share/verilator/include/", open(sys.argv[1]).read()))' \
    "$work/$name/coverage.dat" > "$here/$name.dat"
  verilator_coverage --report summary "$here/$name.dat" > "$here/$name.summary.txt"
}

build_run cov_top      cov_top  cov_top.sv
build_run hier_top     hier_top hier_top.sv
build_run hier_top_pi  hier_top hier_top.sv --coverage-per-instance
verilator --version > "$here/VERSION"
