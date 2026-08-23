"""L0: the catalog, the registry and the mapping document's claims agree.

Three files describe the same feature space in three ways. Each pair can drift,
and every drift is silent: a catalog row with no registry entry is never scored,
a claim naming a dead ID quietly un-documents a feature, and a renamed heading
turns every link in the matrix into a 404.
"""

from __future__ import annotations

import pytest

from covsight.core.conformance import claims as claims_mod
from covsight.core.conformance import ids

SYNTHETIC_SECTIONS = {90, 91}


def test_every_catalog_id_has_a_registry_entry(reg, catalog_sections):
    missing = [
        e for s in catalog_sections for e in s.entries if e.id not in reg
    ]
    assert not missing, "catalog IDs with no registry entry: " + ", ".join(
        f"{e.id} (§{e.section_number} line {e.line})" for e in missing[:10]
    )


def test_every_catalog_sourced_registry_entry_is_still_in_the_catalog(
    reg, catalog_sections
):
    catalog_ids = {e.id for s in catalog_sections for e in s.entries}
    orphans = [
        f
        for f in reg
        if f.section_number not in SYNTHETIC_SECTIONS and f.id not in catalog_ids
    ]
    assert not orphans, "registry entries missing from the catalog: " + ", ".join(
        f.id for f in orphans[:10]
    )


def test_catalog_ids_are_unique(catalog_sections):
    from covsight.core.conformance import catalog

    dupes = catalog.find_duplicates(catalog_sections)
    assert not dupes, (
        "an ID used twice in the catalog makes the key space ambiguous: "
        + ", ".join(sorted(dupes))
    )


def test_claims_resolve_to_registry_ids(reg, doc_claims):
    bad = claims_mod.unknown_ids(doc_claims, reg.ids)
    assert not bad, "mapping document claims unknown feature IDs: " + ", ".join(
        f"{pattern} (line {c[0].line})" for pattern, c in sorted(bad.items())
    )


def test_globs_expand_to_something(reg, doc_claims):
    """A glob matching nothing is the dangerous case.

    `ST.*` silently expanding to zero IDs after a prefix rename would
    un-document 39 features while the document still looks like it covers them.
    """
    for claim in doc_claims:
        for pattern in claim.patterns:
            if ids.is_glob(pattern):
                assert ids.expand_glob(pattern, reg.ids), (
                    f"{claim.document}:{claim.line}: {pattern} matches no "
                    f"registry ID"
                )


def test_claim_anchors_are_unique(doc_claims):
    seen: dict[str, int] = {}
    for claim in doc_claims:
        assert claim.anchor not in seen, (
            f"two claiming sections share the anchor #{claim.anchor} "
            f"(lines {seen[claim.anchor]} and {claim.line}); the matrix would "
            f"link to the wrong one"
        )
        seen[claim.anchor] = claim.line


def test_claim_headings_exist_in_the_document(doc_claims):
    """Guards the rename case: a claim orphaned from its heading.

    parse() ties each claim to the heading above it, so this can only fail if
    a claim block drifts away from its section -- which is exactly what happens
    when someone moves a heading and leaves the comment behind.
    """
    path = claims_mod.default_path()
    text = path.read_text(encoding="utf-8")
    for claim in doc_claims:
        assert claim.heading in text
        assert claim.anchor == claims_mod.slugify(claim.heading)


def test_slugify_matches_github_conventions():
    assert claims_mod.slugify("`scopes`") == "scopes"
    assert claims_mod.slugify("Identity & ordering rules") == "identity--ordering-rules"
    assert (
        claims_mod.slugify("Merge is type-aware — not everything is `SUM`")
        == "merge-is-type-aware--not-everything-is-sum"
    )


@pytest.mark.parametrize(
    "text,expected",
    [("S4.4", ("S4", 4, "")), ("ST.17", ("ST", 17, "")), ("X.1", ("X", 1, ""))],
)
def test_id_parsing(text, expected):
    assert tuple(ids.parse(text)) == expected


def test_ids_sort_numerically_not_lexically():
    assert sorted(["S4.10", "S4.2"], key=ids.sort_key) == ["S4.2", "S4.10"]
