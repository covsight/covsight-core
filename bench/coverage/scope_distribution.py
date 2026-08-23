#!/usr/bin/env python3
"""Measure the per-scope coverage-point distribution of a Verilator coverage.dat.

Task I-0.1 of docs/ucis-xml-cpp-impl-plan.md. The C++ writer stages one scope at
a time (design §5), so its peak memory is set by the largest single scope, not by
the design. That premise was established on OpenTitan alone; this script is how it
gets checked against any other design.

Gate: if a design's largest scope exceeds ~500 K points (~50 MB staged), decision
D-1 reopens -- that is the case Ordering::Prescribed existed for.

Record format (Verilator SystemC::Coverage-3):
    C '<\\x01key\\x02value><\\x01key\\x02value>...' <count>
with keys: f=file, l=line, n=column, t=type, page, o=comment, h=hierarchy.

Usage:
    python3 bench/coverage/scope_distribution.py bench/coverage/rtlmeter-out/coverage_0.dat
    python3 bench/coverage/scope_distribution.py --json out.json --label OpenTitan FILE...
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

# ~40 bytes of POD record per fact plus its strings; design §5.1 measured ~4 MB
# for 37,404 points, i.e. ~112 bytes/point all in. Used only for the human-facing
# "staged" column -- the gate is on point count.
BYTES_PER_POINT = 112

GATE_POINTS = 500_000


def parse(path: Path):
    """Yield (hierarchy, kind) for every coverage point in `path`."""
    with path.open("rb") as f:
        for raw in f:
            if not raw.startswith(b"C "):
                continue
            # The record body is single-quoted and the trailing field is the
            # count. Fields inside are \x01key\x02value; a quote can legally
            # appear inside a comment, so bound the body by the last quote.
            start = raw.find(b"'")
            end = raw.rfind(b"'")
            if start < 0 or end <= start:
                continue
            body = raw[start + 1:end]
            hier = kind = None
            for field in body.split(b"\x01"):
                if not field:
                    continue
                key, _, value = field.partition(b"\x02")
                if key == b"h":
                    hier = value
                elif key == b"t":
                    kind = value
            yield (hier or b"<none>").decode("utf-8", "replace"), \
                  (kind or b"<none>").decode("utf-8", "replace")


def percentile(sorted_values, q):
    if not sorted_values:
        return 0
    # Nearest-rank; the values are integer point counts, so interpolation would
    # only add spurious precision.
    i = max(0, min(len(sorted_values) - 1, int(round(q * len(sorted_values))) - 1))
    return sorted_values[i]


def measure(path: Path, label: str) -> dict:
    per_scope = Counter()
    per_scope_kind = Counter()
    per_kind = Counter()

    for hier, kind in parse(path):
        per_scope[hier] += 1
        per_scope_kind[(hier, kind)] += 1
        per_kind[kind] += 1

    counts = sorted(per_scope.values())
    total = sum(counts)
    largest_scope = max(per_scope.items(), key=lambda kv: kv[1], default=("", 0))
    largest_pair = max(per_scope_kind.items(), key=lambda kv: kv[1],
                       default=((None, None), 0))

    return {
        "label": label,
        "file": str(path),
        "points": total,
        "scopes": len(per_scope),
        "max_scope_points": largest_scope[1],
        "max_scope_name": largest_scope[0],
        "max_scope_kind_points": largest_pair[1],
        "mean": round(total / len(per_scope)) if per_scope else 0,
        "median": percentile(counts, 0.50),
        "p90": percentile(counts, 0.90),
        "p99": percentile(counts, 0.99),
        "kind_mix": dict(per_kind.most_common()),
        "staged_peak_bytes": largest_scope[1] * BYTES_PER_POINT,
        "gate_exceeded": largest_scope[1] > GATE_POINTS,
    }


def report(r: dict) -> str:
    mb = r["staged_peak_bytes"] / (1024 * 1024)
    mix = " * ".join(f"{k} {v:,}" for k, v in r["kind_mix"].items())
    verdict = ("GATE EXCEEDED - reopen D-1" if r["gate_exceeded"]
               else f"within gate ({GATE_POINTS:,})")
    return (
        f"{r['label']}  ({r['file']})\n"
        f"  total points      {r['points']:>12,}\n"
        f"  scopes            {r['scopes']:>12,}\n"
        f"  largest scope     {r['max_scope_points']:>12,}   {verdict}\n"
        f"  largest (sc,kind) {r['max_scope_kind_points']:>12,}\n"
        f"  mean              {r['mean']:>12,}\n"
        f"  median            {r['median']:>12,}\n"
        f"  p90               {r['p90']:>12,}\n"
        f"  p99               {r['p99']:>12,}\n"
        f"  staged peak       {mb:>12.2f} MB\n"
        f"  kind mix          {mix}\n"
        f"  largest scope is  {r['max_scope_name']}\n"
    )


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+", type=Path)
    ap.add_argument("--label", action="append", default=[],
                    help="design label per file, in order (default: file stem)")
    ap.add_argument("--json", type=Path, help="also write results as JSON")
    args = ap.parse_args(argv)

    results = []
    exceeded = False
    for i, path in enumerate(args.files):
        label = args.label[i] if i < len(args.label) else path.parent.name or path.stem
        r = measure(path, label)
        results.append(r)
        print(report(r))
        exceeded |= r["gate_exceeded"]

    if args.json:
        args.json.write_text(json.dumps(results, indent=2) + "\n")

    # Non-zero so a CI or batch caller notices without reading the text.
    return 2 if exceeded else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
