#!/usr/bin/env bash
# Run a SUITE of RTLMeter cases through the coverage→NCDB benchmark and
# aggregate the results into a JSONL file for comparison across designs.
#
# Re-runnable: compiles are cached by RTLMeter under the shared workRoot, so
# re-running a case (or running several tests on the same config) reuses the
# build. Per-case failures are logged and skipped, not fatal.
#
# Usage:
#   ./run_suite.sh [SUITE_FILE]
#     SUITE_FILE: text file of `Design:config:test` lines (# comments allowed).
#                 Default: suites/smoke.txt
# Env:
#   RTLMETER_DIR  rtlmeter clone (default: ~/projects/rtlmeter)
#   RESULTS       output JSONL (default: <here>/results.jsonl)
#   FORCE=1       re-run even if a coverage.dat already exists for the case
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SUITE_FILE="${1:-$HERE/suites/smoke.txt}"
# Absolutize before we cd into the rtlmeter dir below.
[ -f "$SUITE_FILE" ] && SUITE_FILE="$(cd "$(dirname "$SUITE_FILE")" && pwd)/$(basename "$SUITE_FILE")"
RTLMETER_DIR="${RTLMETER_DIR:-$HOME/projects/rtlmeter}"
RESULTS="${RESULTS:-$HERE/results.jsonl}"
WORKROOT="work-cov"

command -v verilator >/dev/null || { echo "verilator not on PATH"; exit 1; }
[ -f "$SUITE_FILE" ] || { echo "suite file not found: $SUITE_FILE"; exit 1; }

if [ ! -d "$RTLMETER_DIR/.git" ]; then
    echo "== Cloning RTLMeter into $RTLMETER_DIR =="
    git clone --depth 1 https://github.com/verilator/rtlmeter "$RTLMETER_DIR"
fi
cd "$RTLMETER_DIR"
[ -x "venv/bin/python3" ] || { echo "== make venv =="; make venv; }

echo "== Suite: $SUITE_FILE  ->  $RESULTS =="
: > "$RESULTS.tmp"          # fresh run-log marker; RESULTS itself is appended

while IFS= read -r case || [ -n "$case" ]; do
    case="${case%%#*}"; case="$(echo "$case" | xargs)"   # strip comments/space
    [ -z "$case" ] && continue

    D="${case%%:*}"; rest="${case#*:}"; CFG="${rest%%:*}"; TEST="${case##*:}"
    dat="$WORKROOT/$D/$CFG/execute-"*"/$TEST/coverage.dat"

    if [ -z "${FORCE:-}" ] && compgen -G "$dat" >/dev/null; then
        echo "-- $case: coverage.dat present, reusing (FORCE=1 to redo)"
    else
        echo "== $case: rtlmeter run --coverage =="
        if ! ./rtlmeter run --cases "$case" --compileArgs="--coverage" \
                --workRoot "$WORKROOT"; then
            echo "!! $case: rtlmeter run failed, skipping" >&2
            continue
        fi
    fi

    found="$(compgen -G "$dat" | head -1)"
    if [ -z "$found" ]; then
        echo "!! $case: no coverage.dat found at $dat, skipping" >&2
        continue
    fi

    echo "== $case: convert + measure =="
    if ! python3 "$HERE/coverage_to_ncdb.py" "$found" \
            --label "$case" --json-line "$RESULTS"; then
        echo "!! $case: conversion failed, skipping" >&2
    fi
done < "$SUITE_FILE"
rm -f "$RESULTS.tmp"

echo "== Summary =="
python3 "$HERE/summarize.py" "$RESULTS"
