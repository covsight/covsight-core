#!/usr/bin/env python3
"""Generate the UCIS -> Parquet conformance matrix.

    python3 tools/gen_conformance_matrix.py                     # write
    python3 tools/gen_conformance_matrix.py --check             # CI drift gate

Joins three inputs, none of which contains a status:

    docs/conformance/features/*.yaml    intent   -- what each backend must do
    docs/ucis-parquet-mapping.md        claims   -- what the spec documents
    tests/conformance/coverage.json     evidence -- which tests exist

and emits two generated files:

    docs/ucis-parquet-feature-map.md    the matrix
    docs/ucis-conformance-summary.md    the numbers, in a small greppable diff

``--check`` regenerates in memory and compares byte-for-byte, exactly as
``tools/amalgamate.py --check`` does for the single-header artifact. A PR that
changes the mapping without regenerating fails CI, which is the entire
enforcement mechanism -- it needs no reviewer vigilance.

Evidence is optional. Without it every implemented feature reports as untested,
which is the correct reading of "no test run told us otherwise", not a silent
pass.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "python"))

from covsight.core.conformance import claims as claims_mod  # noqa: E402
from covsight.core.conformance import ids, registry  # noqa: E402

DEFAULT_BACKEND = "parquet"
DEFAULT_MATRIX = ROOT / "docs" / "ucis-parquet-feature-map.md"
DEFAULT_SUMMARY = ROOT / "docs" / "ucis-conformance-summary.md"
DEFAULT_EVIDENCE = ROOT / "tests" / "conformance" / "coverage.json"
MAPPING_DOC = "ucis-parquet-mapping.md"

STATUS_MAPPED_TESTED = "mapped-tested"
STATUS_MAPPED_UNTESTED = "mapped-untested"
STATUS_TESTED_UNDOC = "tested-undocumented"
STATUS_UNMAPPED = "unmapped"
STATUS_EXCLUDED = "excluded"

BADGE = {
    STATUS_MAPPED_TESTED: "✅",
    STATUS_MAPPED_UNTESTED: "🟡",
    STATUS_TESTED_UNDOC: "🟠",
    STATUS_UNMAPPED: "❌",
    STATUS_EXCLUDED: "⚪",
}

LABEL = {
    STATUS_MAPPED_TESTED: "mapped + tested",
    STATUS_MAPPED_UNTESTED: "mapped, untested",
    STATUS_TESTED_UNDOC: "tested, undocumented",
    STATUS_UNMAPPED: "unmapped",
    STATUS_EXCLUDED: "out of scope / deferred",
}

ORDER = [
    STATUS_MAPPED_TESTED,
    STATUS_MAPPED_UNTESTED,
    STATUS_TESTED_UNDOC,
    STATUS_UNMAPPED,
    STATUS_EXCLUDED,
]

GENERATED_HEADER = """\
<!-- GENERATED FILE -- DO NOT EDIT.
     Regenerate with:  python3 tools/gen_conformance_matrix.py
     Sources: docs/conformance/features/*.yaml (intent)
              docs/{mapping} (documentation claims)
              {evidence_note}
     See docs/ucis-conformance-structure-plan.md. -->
"""


class GeneratorError(Exception):
    pass


# -- status derivation --------------------------------------------------


def derive(feature, backend: str, documented: bool, tested: bool) -> str:
    scope = feature.backends.get(backend)
    if scope is None or not scope.implemented:
        return STATUS_EXCLUDED
    if documented and tested:
        return STATUS_MAPPED_TESTED
    if documented:
        return STATUS_MAPPED_UNTESTED
    if tested:
        return STATUS_TESTED_UNDOC
    return STATUS_UNMAPPED


# Extras whose absence silently shrinks collection. Several test modules call
# pytest.importorskip at module scope, so a missing extra does not skip tests --
# it means the file yields no items at all, and `collected_full` cannot see the
# difference. Without this check the committed matrix would quietly depend on
# which extras the last person to regenerate happened to have installed.
REQUIRED_EXTRAS = ("parquet", "iceberg", "duckdb")


def load_evidence(path: Path | None, required_extras=REQUIRED_EXTRAS) -> dict:
    if path is None or not path.exists():
        return {"schema": 1, "collected_full": None, "features": {}, "run": {}}
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema") != 1:
        raise GeneratorError(
            f"{path}: evidence schema {data.get('schema')!r} is not 1; "
            f"regenerate with a matching pytest plugin"
        )
    if data.get("collected_full") is False:
        reasons = ", ".join(data.get("incomplete_reasons") or []) or "unknown"
        raise GeneratorError(
            f"{path} came from a partial test run ({reasons}).\n"
            f"The matrix would lose every row whose test was not collected. "
            f"Re-run the whole suite:\n"
            f"    pytest --conformance-json={path}"
        )

    present = set(data.get("run", {}).get("extras") or ())
    missing = [e for e in required_extras if e not in present]
    if missing:
        raise GeneratorError(
            f"{rel(path)} came from a run without {', '.join(missing)} installed.\n"
            f"Several test modules call pytest.importorskip at module scope, so "
            f"those files contributed no collected items at all and the matrix "
            f"would under-report them. Install the extras and re-run:\n"
            f"    pip install -e '.[dev,validate,parquet,iceberg,duckdb]'"
        )
    return data


# -- rendering ----------------------------------------------------------


def rel(path: Path) -> str:
    """Repo-relative, always. Absolute paths in a generated file would make
    --check fail on every machine but the one that last regenerated it."""
    path = Path(path).resolve()
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return path.name


def esc(text: str) -> str:
    """Make a catalog title safe inside a markdown table cell."""
    return text.replace("|", "\\|").replace("\n", " ").strip()


def fmt_tests(entries: list[dict]) -> str:
    if not entries:
        return "—"
    by_level: dict[str, list[str]] = {}
    for entry in entries:
        name = entry["nodeid"].rsplit("::", 1)[-1]
        by_level.setdefault(entry["level"], []).append(name)
    parts = []
    for level in sorted(by_level):
        names = sorted(set(by_level[level]))
        shown = ", ".join(f"`{n}`" for n in names[:3])
        if len(names) > 3:
            shown += f" +{len(names) - 3}"
        parts.append(f"{level}: {shown}")
    return "<br>".join(parts)


def fmt_mapping(section_claims) -> str:
    if not section_claims:
        return "—"
    seen: dict[str, str] = {}
    for claim in section_claims:
        seen.setdefault(claim.anchor, claim.heading)
    return " ".join(
        f"[{esc(heading)}]({MAPPING_DOC}#{anchor})" for anchor, heading in seen.items()
    )


def render_matrix(rows, reg, backend, evidence_note) -> str:
    out = [GENERATED_HEADER.format(mapping=MAPPING_DOC, evidence_note=evidence_note)]
    out.append(f"# UCIS -> {backend} conformance matrix\n")
    out.append(
        "Every UCIS 1.0 feature in [`ucis-feature-catalog.md`](ucis-feature-catalog.md), "
        "scored against the "
        f"`{backend}` backend. **Status is computed**, from three independent inputs: a "
        "human-declared scope in the registry, a documentation claim in the mapping "
        "document, and the existence of a tagged test. Nothing here is hand-written.\n"
    )

    out.append("| Badge | Meaning |")
    out.append("| ----- | ------- |")
    for status in ORDER:
        out.append(f"| {BADGE[status]} | {LABEL[status]} |")
    out.append("")
    out.append(
        "The **Profile** column is a separate axis from status: `core` is what a "
        "conforming implementation must support, `*ext*` is what it MAY omit "
        "while still reporting every coverage number correctly. See the "
        "\"Conformance\" section of the mapping document.\n"
    )
    out.append(
        "🟠 is the state worth reading first: the implementation handles it and a "
        "test proves it, but the specification does not say so — so a third-party "
        "implementation written from the document alone will not interoperate.\n"
    )

    counts = tally(rows)
    total = sum(counts.values())
    out.append("## Totals\n")
    out.append("| Status | Count | Share |")
    out.append("| ------ | ----: | ----: |")
    for status in ORDER:
        n = counts[status]
        out.append(f"| {BADGE[status]} {LABEL[status]} | {n} | {pct(n, total)} |")
    out.append(f"| **total** | **{total}** | |")
    out.append("")

    out.extend(_render_section_totals(rows))

    features = [r for r in rows if r["feature"].kind == "feature"]
    combos = [r for r in rows if r["feature"].kind == "combination"]
    negatives = [r for r in rows if r["feature"].kind == "negative"]

    out.append("## Features\n")
    out.extend(_render_feature_tables(features, reg))

    if combos:
        out.append("## Feature combinations\n")
        out.append(
            "Interactions that are wrong even when every composed feature is "
            "individually right — normally because the merge operator differs per "
            "feature while the rows join on the same key.\n"
        )
        out.append("| ID | Combination | Composes | Mapping | Tests | Status |")
        out.append("| -- | ----------- | -------- | ------- | ----- | ------ |")
        for row in combos:
            f = row["feature"]
            composes = ", ".join(f"`{c}`" for c in f.composes) or "—"
            out.append(
                f"| `{f.id}` | {esc(f.title)} | {composes} | {row['mapping']} | "
                f"{row['tests']} | {BADGE[row['status']]} |"
            )
        out.append("")

    if negatives:
        out.append("## Negative conformance\n")
        out.append(
            "Input the mapping must **reject**. A reader that accepts these produces "
            "a database that is wrong in a way no downstream check can detect.\n"
        )
        out.append("| ID | Must be rejected | Mapping | Tests | Status |")
        out.append("| -- | ---------------- | ------- | ----- | ------ |")
        for row in negatives:
            f = row["feature"]
            out.append(
                f"| `{f.id}` | {esc(f.title)} | {row['mapping']} | {row['tests']} | "
                f"{BADGE[row['status']]} |"
            )
        out.append("")

    out.extend(_render_excluded(rows, backend))
    return "\n".join(out).rstrip() + "\n"


def _render_section_totals(rows) -> list[str]:
    out = ["## By catalog section\n"]
    out.append("| § | Section | " + " | ".join(BADGE[s] for s in ORDER) + " | Total |")
    out.append("| - | ------- | " + " | ".join("--:" for _ in ORDER) + " | ----: |")
    by_section: dict[int, list] = {}
    titles: dict[int, str] = {}
    for row in rows:
        f = row["feature"]
        by_section.setdefault(f.section_number, []).append(row)
        titles[f.section_number] = f.section_title
    for number in sorted(by_section):
        group = by_section[number]
        counts = tally(group)
        cells = " | ".join(str(counts[s]) for s in ORDER)
        out.append(f"| {number} | {esc(titles[number])} | {cells} | {len(group)} |")
    out.append("")
    return out


def _render_feature_tables(rows, reg) -> list[str]:
    out: list[str] = []
    by_section: dict[int, list] = {}
    for row in rows:
        by_section.setdefault(row["feature"].section_number, []).append(row)

    for number in sorted(by_section):
        group = by_section[number]
        section = next(s for s in reg.sections if s.number == number)
        spec = f" ({section.spec_ref})" if section.spec_ref else ""
        out.append(f"### {number}. {esc(section.title)}{spec}\n")
        out.append("| ID | Feature | Spec | Profile | Mapping | Tests | Status |")
        out.append("| -- | ------- | ---- | ------- | ------- | ----- | ------ |")
        for row in group:
            f = row["feature"]
            spec_ref = f.spec_ref or "—"
            note = ""
            if row["status"] == STATUS_EXCLUDED:
                scope = f.backends.get(row["backend"])
                if scope is not None:
                    note = f" — *{scope.scope.replace('_', ' ')}*"
            out.append(
                f"| `{f.id}` | {esc(f.title)}{note} | {spec_ref} | "
                f"{profile(f, row['backend'])} | {row['mapping']} | "
                f"{row['tests']} | {BADGE[row['status']]} |"
            )
        out.append("")
    return out


def _render_excluded(rows, backend: str) -> list[str]:
    excluded = [r for r in rows if r["status"] == STATUS_EXCLUDED]
    out = ["## Out of scope and deferred\n"]
    if not excluded:
        out.append(f"Nothing is excluded for the `{backend}` backend.\n")
        return out
    out.append(
        "The part a third-party implementer needs most, and the part a "
        "hand-written matrix always omits: what this mapping deliberately does "
        "**not** carry, and why.\n"
    )
    out.append("| ID | Feature | Decision | Reason | Reference |")
    out.append("| -- | ------- | -------- | ------ | --------- |")
    for row in excluded:
        f = row["feature"]
        scope = f.backends.get(row["backend"])
        decision = scope.scope.replace("_", " ") if scope else "unclassified"
        reason = esc(scope.reason or "—") if scope else "—"
        ref = "—"
        if scope and scope.adr:
            ref = f"[{Path(scope.adr).name}](../{scope.adr})" if scope.adr.startswith("docs/") else scope.adr
        elif scope and scope.issue:
            ref = scope.issue
        out.append(f"| `{f.id}` | {esc(f.title)} | {decision} | {reason} | {ref} |")
    out.append("")
    return out


def profile(feature, backend: str) -> str:
    """The conformance profile cell.

    core and extended both count as implemented, so they share a status badge;
    the distinction is what a conforming reader MAY omit, which is a separate
    axis and needs its own column.
    """
    scope = feature.backends.get(backend)
    if scope is None:
        return "—"
    return {"core": "core", "extended": "*ext*"}.get(scope.scope, "—")


def tally(rows) -> dict[str, int]:
    counts = {s: 0 for s in ORDER}
    for row in rows:
        counts[row["status"]] += 1
    return counts


def pct(n: int, total: int) -> str:
    return f"{(100.0 * n / total):.1f}%" if total else "—"


def render_summary(rows, backend: str, evidence: dict, evidence_note: str) -> str:
    out = [GENERATED_HEADER.format(mapping=MAPPING_DOC, evidence_note=evidence_note)]
    out.append("# UCIS conformance summary\n")
    out.append(
        "The review surface for [`ucis-parquet-feature-map.md`](ucis-parquet-feature-map.md). "
        "Kept separate so a coverage change is a few visible lines in a diff rather "
        "than a movement inside 280 rows. **A percentage that drops in a PR needs an "
        "explanation in the PR description.**\n"
    )

    in_scope = [r for r in rows if r["status"] != STATUS_EXCLUDED]
    documented = [r for r in in_scope if r["documented"]]
    tested = [r for r in in_scope if r["tested"]]
    both = [r for r in in_scope if r["documented"] and r["tested"]]
    undoc = [r for r in in_scope if r["tested"] and not r["documented"]]

    out.append(f"Backend: `{backend}`\n")
    out.append("| Metric | Value |")
    out.append("| ------ | ----- |")
    out.append(f"| Registry entries | {len(rows)} |")
    out.append(f"| In scope (core + extended) | {len(in_scope)} |")
    out.append(f"| Out of scope / deferred | {len(rows) - len(in_scope)} |")
    out.append(f"| Documentation coverage | {len(documented)}/{len(in_scope)} ({pct(len(documented), len(in_scope))}) |")
    out.append(f"| Test coverage | {len(tested)}/{len(in_scope)} ({pct(len(tested), len(in_scope))}) |")
    out.append(f"| Conformance (documented **and** tested) | {len(both)}/{len(in_scope)} ({pct(len(both), len(in_scope))}) |")
    out.append(f"| Tested but undocumented | {len(undoc)} |")
    out.append("")

    core = [r for r in in_scope
            if r["feature"].backends[backend].scope == "core"]
    ext = [r for r in in_scope
           if r["feature"].backends[backend].scope == "extended"]
    out.append("## By profile\n")
    out.append(
        "`core` is required for conformance; `extended` MAY be omitted. A gap in "
        "core is a different kind of problem from a gap in extended, and a single "
        "coverage percentage hides that.\n"
    )
    out.append("| Profile | Features | Documented | Tested | Both |")
    out.append("| ------- | -------: | ---------: | -----: | ---: |")
    for label, group in (("core", core), ("extended", ext)):
        if not group:
            continue
        d = sum(1 for r in group if r["documented"])
        tst = sum(1 for r in group if r["tested"])
        b = sum(1 for r in group if r["documented"] and r["tested"])
        out.append(
            f"| {label} | {len(group)} | {d} ({pct(d, len(group))}) | "
            f"{tst} ({pct(tst, len(group))}) | {b} ({pct(b, len(group))}) |"
        )
    out.append("")

    out.append("## By kind\n")
    out.append("| Kind | In scope | Documented | Tested | Both |")
    out.append("| ---- | -------: | ---------: | -----: | ---: |")
    for kind in ("feature", "combination", "negative"):
        group = [r for r in in_scope if r["feature"].kind == kind]
        if not group:
            continue
        d = sum(1 for r in group if r["documented"])
        t = sum(1 for r in group if r["tested"])
        b = sum(1 for r in group if r["documented"] and r["tested"])
        out.append(f"| {kind} | {len(group)} | {d} | {t} | {b} |")
    out.append("")

    out.append("## Test coverage by level\n")
    out.append(
        "Depth, not just breadth. 200 IDs covered only at L1 is a different claim "
        "from 200 covered at L2/L3, and these two numbers must not read the same.\n"
    )
    levels: dict[str, set[str]] = {}
    for fid, entries in (evidence.get("features") or {}).items():
        for entry in entries:
            levels.setdefault(entry["level"], set()).add(fid)
    if levels:
        out.append("| Level | What it proves | Features covered |")
        out.append("| ----- | -------------- | ---------------: |")
        descriptions = {
            "L0": "static registry / schema integrity",
            "L1": "mechanical round trip",
            "L2": "cross-backend equivalence against an oracle",
            "L3": "algebraic merge laws",
            "L4": "spec-only reader (no covsight import)",
            "L5": "real-corpus differential",
        }
        for level in sorted(levels):
            out.append(
                f"| {level} | {descriptions.get(level, '—')} | {len(levels[level])} |"
            )
    else:
        out.append("No tagged tests have been collected yet.\n")
    out.append("")

    out.append("## Status counts\n")
    counts = tally(rows)
    out.append("| Status | Count |")
    out.append("| ------ | ----: |")
    for status in ORDER:
        out.append(f"| {BADGE[status]} {LABEL[status]} | {counts[status]} |")
    out.append("")
    return "\n".join(out).rstrip() + "\n"


# -- driver -------------------------------------------------------------


def build(registry_dir, mapping_doc, evidence_path, backend):
    reg = registry.load(registry_dir)

    unclassified = reg.unclassified(backend)
    if unclassified:
        listing = "\n".join(
            f"  {f.source_file}: {f.id}  {f.title[:60]}" for f in unclassified[:20]
        )
        more = f"\n  ... and {len(unclassified) - 20} more" if len(unclassified) > 20 else ""
        raise GeneratorError(
            f"{len(unclassified)} registry entries are still unclassified for "
            f"backend {backend!r}:\n{listing}{more}\n\n"
            f"Every entry needs a scope (core / extended / out_of_scope / deferred). "
            f"This is the check that stops the registry growing an unreviewed tail; "
            f"it is not relaxable."
        )

    doc_claims = claims_mod.parse(mapping_doc)
    known = reg.ids
    bad = claims_mod.unknown_ids(doc_claims, known)
    if bad:
        detail = "\n".join(
            f"  {pattern} claimed at {c[0].document}:{c[0].line} under {c[0].heading!r}"
            for pattern, c in sorted(bad.items())
        )
        raise GeneratorError(
            f"the mapping document claims feature IDs that are not in the "
            f"registry:\n{detail}"
        )
    claim_index = claims_mod.index(doc_claims, known)

    evidence = load_evidence(evidence_path)
    ev_features = evidence.get("features") or {}
    unknown_tags = sorted(set(ev_features) - set(known), key=ids.sort_key)
    if unknown_tags:
        raise GeneratorError(
            f"evidence names feature IDs absent from the registry: "
            f"{', '.join(unknown_tags)}"
        )

    rows = []
    for feature in sorted(reg, key=lambda f: f.sort_key()):
        if not feature.active:
            continue
        section_claims = claim_index.get(feature.id, [])
        test_entries = ev_features.get(feature.id, [])
        documented = bool(section_claims)
        tested = bool(test_entries)
        rows.append(
            {
                "feature": feature,
                "backend": backend,
                "documented": documented,
                "tested": tested,
                "status": derive(feature, backend, documented, tested),
                "mapping": fmt_mapping(section_claims),
                "tests": fmt_tests(test_entries),
            }
        )
    return reg, rows, evidence


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--registry", type=Path, default=None)
    ap.add_argument("--claims", type=Path, default=None,
                    help=f"mapping document (default docs/{MAPPING_DOC})")
    ap.add_argument("--evidence", type=Path, default=DEFAULT_EVIDENCE)
    ap.add_argument("--output", type=Path, default=DEFAULT_MATRIX)
    ap.add_argument("--summary", type=Path, default=DEFAULT_SUMMARY)
    ap.add_argument("--backend", default=DEFAULT_BACKEND)
    ap.add_argument("--check", action="store_true",
                    help="do not write; exit 1 if the committed files differ")
    args = ap.parse_args(argv)

    claims_path = args.claims or claims_mod.default_path()
    evidence_path = args.evidence if args.evidence and args.evidence.exists() else None
    # The note goes into a committed file, so it must not depend on where the
    # tool was invoked from.
    evidence_note = (
        f"{rel(evidence_path)} (test evidence)"
        if evidence_path
        else "no test evidence (run pytest --conformance-json=...)"
    )

    try:
        reg, rows, evidence = build(args.registry, claims_path, evidence_path, args.backend)
    except (GeneratorError, registry.RegistryError, claims_mod.ClaimError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    outputs = {
        args.output: render_matrix(rows, reg, args.backend, evidence_note),
        args.summary: render_summary(rows, args.backend, evidence, evidence_note),
    }

    if args.check:
        stale = []
        for path, text in outputs.items():
            current = path.read_text(encoding="utf-8") if path.exists() else None
            if current != text:
                stale.append(path)
        if stale:
            names = ", ".join(rel(p) for p in stale)
            print(
                f"error: {names} is out of date.\n"
                f"       Regenerate and commit:\n"
                f"           python3 tools/gen_conformance_matrix.py"
                + (f" --evidence {rel(args.evidence)}" if evidence_path else ""),
                file=sys.stderr,
            )
            return 1
        print(f"conformance matrix is current ({len(rows)} entries).")
        return 0

    for path, text in outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        print(f"wrote {rel(path)}")

    counts = tally(rows)
    print(
        "  "
        + "  ".join(f"{BADGE[s]} {counts[s]}" for s in ORDER)
        + f"   ({len(rows)} entries)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
