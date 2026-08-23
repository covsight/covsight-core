#!/usr/bin/env python3
"""One-shot: turn the UCIS feature catalog into registry stub files.

    python3 tools/bootstrap_feature_registry.py --out docs/conformance/features

Every entry lands as ``scope: unclassified``, which the matrix generator treats
as a hard error. That is deliberate: the bootstrap converts a *parsing* problem
into a *review* problem (task T5 of docs/ucis-conformance-structure-plan.md),
and the generator refuses to produce a matrix until the review has happened.

The output is ordinary reviewed source from the moment it is written. This tool
is not part of any build; re-running it over an already-classified registry
would discard the review, so it refuses to overwrite unless --force is given.

For ongoing catalog changes use ``tools/sync_catalog.py``, which reports drift
rather than regenerating.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "python"))

from covsight.core.conformance import catalog  # noqa: E402

# File slugs are spelled out rather than derived. A slugifier turns
# "Database lifecycle and I/O" into "database-lifecycle-i" and
# "History-node lists / test<->coveritem association" into something worse; the
# file name is a label a reviewer reads next to the catalog, so 26 explicit
# names are worth more than a clever heuristic.
SLUGS = {
    1: "lifecycle",
    2: "errors",
    3: "versioning",
    4: "scopes",
    5: "source-language",
    6: "coveritems",
    7: "iteration",
    8: "properties",
    9: "attributes",
    10: "source-files",
    11: "history-nodes",
    12: "test-assoc",
    13: "tags",
    14: "toggle",
    15: "fsm",
    16: "branch",
    17: "statement-block",
    18: "condition-expr",
    19: "covergroup",
    20: "assertion",
    21: "user-defined",
    22: "formal",
    23: "net-aliasing",
    24: "metrics",
    25: "xml-interchange",
    26: "database-objects",
}

HEADER = """\
# UCIS conformance registry -- section {number}: {title}
#
# GENERATED SKELETON, HAND-MAINTAINED THEREAFTER.
# Records intent only: what the backend is expected to do with each feature.
# Status ("is it tested?", "is it documented?") is computed by
# tools/gen_conformance_matrix.py and never written here.
#
# See docs/conformance/README.md.
"""


def quote(text: str) -> str:
    """YAML double-quoted scalar. Catalog titles are full of backticks, colons,
    em dashes and § signs, so nothing here can be left bare."""
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def render(section: catalog.CatalogSection, backends: list[str]) -> str:
    out = [HEADER.format(number=section.number, title=section.title), "section:"]
    out.append(f"  number: {section.number}")
    out.append(f"  title: {quote(section.title)}")
    if section.spec_ref:
        out.append(f"  spec_ref: {quote(section.spec_ref)}")
    out.append("")
    out.append("features:")

    for entry in section.entries:
        out.append(f"  - id: {entry.id}")
        out.append(f"    title: {quote(entry.title)}")
        if entry.spec_ref:
            out.append(f"    spec_ref: {quote(entry.spec_ref)}")
        out.append("    kind: feature")
        out.append("    backends:")
        for backend in backends:
            out.append(f"      {backend}:")
            out.append("        scope: unclassified")
        if entry.notes:
            out.append(f"    notes: {quote(entry.notes)}")
        out.append("")

    return "\n".join(out).rstrip() + "\n"


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--catalog", type=Path, default=None,
                    help="path to ucis-feature-catalog.md")
    ap.add_argument("--out", type=Path, default=ROOT / "docs" / "conformance" / "features")
    ap.add_argument("--backend", action="append", default=None,
                    help="backend column to stub out (repeatable; default: parquet)")
    ap.add_argument("--force", action="store_true",
                    help="overwrite existing registry files, discarding classifications")
    args = ap.parse_args(argv)

    backends = args.backend or ["parquet"]
    sections = catalog.parse(args.catalog)

    dupes = catalog.find_duplicates(sections)
    if dupes:
        for fid, entries in sorted(dupes.items()):
            locations = ", ".join(f"§{e.section_number} line {e.line}" for e in entries)
            print(f"error: catalog ID {fid} is used more than once: {locations}",
                  file=sys.stderr)
        print("Feature IDs are the registry's key space and must be unique. "
              "Fix the catalog first.", file=sys.stderr)
        return 1

    args.out.mkdir(parents=True, exist_ok=True)
    written = 0
    total = 0
    for section in sections:
        slug = SLUGS.get(section.number, section.slug)
        path = args.out / f"{section.number:02d}-{slug}.yaml"
        if path.exists() and not args.force:
            print(f"skip (exists): {path.relative_to(ROOT)}")
            continue
        path.write_text(render(section, backends), encoding="utf-8")
        written += 1
        total += len(section.entries)
        print(f"wrote {path.relative_to(ROOT)}  ({len(section.entries)} entries)")

    print(f"\n{written} file(s), {total} entries, all scope: unclassified.")
    print("Next: classify them (T5). The matrix generator fails until none remain.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
