#!/usr/bin/env python3
"""Check that the feature catalog and the conformance registry agree.

    python3 tools/sync_catalog.py            # report
    python3 tools/sync_catalog.py --check    # exit 1 on any disagreement (CI)

The catalog (docs/ucis-feature-catalog.md) is the human-readable narrative; the
registry (docs/conformance/features/) is the authoritative key space. They are
allowed to describe things differently -- the registry has combination and
negative entries the catalog never mentions, and it may rename a feature's title
-- but every catalog ID must have a registry entry, and every registry entry
sourced from the catalog must still be findable there.

Without this check the failure is silent and one-directional: someone adds a row
to the catalog, no registry entry appears, and the ID is simply never scored.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "python"))

from covsight.core.conformance import catalog, ids, registry  # noqa: E402

# Registry sections that legitimately have no catalog counterpart.
SYNTHETIC_SECTIONS = {90, 91}


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--catalog", type=Path, default=None)
    ap.add_argument("--registry", type=Path, default=None)
    ap.add_argument("--check", action="store_true",
                    help="exit non-zero on any disagreement (for CI)")
    args = ap.parse_args(argv)

    try:
        reg = registry.load(args.registry)
    except registry.RegistryError as exc:
        print(f"error: registry is invalid: {exc}", file=sys.stderr)
        return 1

    sections = catalog.parse(args.catalog)

    problems: list[str] = []

    dupes = catalog.find_duplicates(sections)
    for fid, entries in sorted(dupes.items()):
        where = ", ".join(f"§{e.section_number}:{e.line}" for e in entries)
        problems.append(f"catalog ID {fid} appears more than once ({where})")

    catalog_ids = {e.id: e for s in sections for e in s.entries}
    registry_ids = {f.id: f for f in reg}

    for fid in sorted(set(catalog_ids) - set(registry_ids), key=ids.sort_key):
        entry = catalog_ids[fid]
        problems.append(
            f"catalog {fid} (§{entry.section_number}, line {entry.line}) has no "
            f"registry entry -- add it to docs/conformance/features/ with a scope"
        )

    for fid in sorted(set(registry_ids) - set(catalog_ids), key=ids.sort_key):
        feat = registry_ids[fid]
        if feat.section_number in SYNTHETIC_SECTIONS:
            continue
        problems.append(
            f"registry {fid} ({feat.source_file}) is not in the catalog -- either "
            f"restore the catalog row or mark the entry superseded"
        )

    # Section-level drift: a registry file claiming a section number the catalog
    # does not have means one of the two was renumbered.
    catalog_sections = {s.number for s in sections}
    for section in reg.sections:
        if section.number in SYNTHETIC_SECTIONS:
            continue
        if section.number not in catalog_sections:
            problems.append(
                f"registry section {section.number} ({section.source_file}) has no "
                f"matching catalog section"
            )

    # Catalog sections carrying no IDs at all: prose-only, so nothing can be
    # scored against them. Reported, never fatal -- see docs/conformance/README.md.
    prose_only = _prose_only_sections(args.catalog, catalog_sections)

    if prose_only:
        print("note: catalog sections with no feature IDs (nothing to score):")
        for number, title in prose_only:
            print(f"  §{number} {title}")
        print()

    if problems:
        print(f"{len(problems)} catalog/registry disagreement(s):", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 1 if args.check else 0

    print(f"catalog and registry agree: {len(catalog_ids)} catalog IDs, "
          f"{len(registry_ids)} registry entries "
          f"({len(registry_ids) - len(catalog_ids)} synthetic).")
    return 0


def _prose_only_sections(path: Path | None, with_ids: set[int]) -> list[tuple[int, str]]:
    """Catalog `## N.` headings that produced no rows.

    catalog.parse() drops empty sections, so they are invisible to the diff
    above; they still represent spec surface that no ID covers.
    """
    import re

    path = Path(path) if path is not None else catalog.default_path()
    found = []
    for line in path.read_text(encoding="utf-8").splitlines():
        m = catalog.SECTION_RE.match(line)
        if not m:
            continue
        number = int(m["number"])
        if number not in with_ids:
            title = re.sub(r"\s*\(.*\)\s*$", "", m["title"]).strip()
            found.append((number, title))
    return found


if __name__ == "__main__":
    raise SystemExit(main())
