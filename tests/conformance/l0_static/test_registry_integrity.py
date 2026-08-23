"""L0: the registry itself is well formed.

These run before anything touches a database. If the registry is broken, every
downstream number is meaningless, so these failures should read as ordinary test
failures with file/line context rather than as a generator crash in CI.

Note on what is *not* here: whether the committed matrix is current. That check
needs test evidence, which only exists after the suite has run, so it lives in
CI as a step after pytest (see .github/workflows/ci.yml). A test that regenerated
the matrix from a partial run would be exactly the failure mode `collected_full`
exists to prevent.
"""

from __future__ import annotations

import json

import pytest

from covsight.core.conformance import ids, registry
from covsight.core.conformance.marker import LEVELS, ucis_feature

BACKEND = "parquet"


def test_registry_loads(reg):
    assert len(reg) > 0
    assert reg.sections


def test_ids_are_unique_and_well_formed(reg):
    seen = set()
    for feature in reg:
        assert ids.is_id(feature.id), f"{feature.id} is not a well-formed feature ID"
        assert feature.id not in seen, f"{feature.id} defined twice"
        seen.add(feature.id)


def test_aliases_do_not_shadow_live_ids(reg):
    """An alias that is also a live ID is ambiguous.

    This is not hypothetical: the catalog used ST.1-ST.4 for both scope types
    and statement coverage. The statement IDs were renamed to SB.* at bootstrap
    rather than aliased, precisely because the alias would have been ambiguous.
    """
    live = {f.id for f in reg}
    for feature in reg:
        for alias in feature.aliases:
            assert alias not in live, (
                f"{feature.id} claims alias {alias}, which is a live feature ID"
            )


def test_exclusions_carry_a_reason(reg):
    for feature in reg:
        for name, scope in feature.backends.items():
            if scope.scope in registry.NEEDS_REASON:
                assert scope.reason and scope.reason.strip(), (
                    f"{feature.id} is {scope.scope} for {name} with no reason. "
                    f"An undocumented exclusion is the one thing a third-party "
                    f"implementer cannot work around."
                )


def test_deferred_entries_are_re_reviewable(reg):
    for feature in reg:
        for name, scope in feature.backends.items():
            if scope.scope == "deferred":
                assert scope.adr or scope.issue, (
                    f"{feature.id} is deferred for {name} with no ADR or issue; "
                    f"nothing would bring it back up for review"
                )


def test_nothing_is_unclassified(reg):
    """The check that stops the registry growing an unreviewed tail."""
    stragglers = reg.unclassified(BACKEND)
    assert not stragglers, (
        f"{len(stragglers)} entries have no {BACKEND} scope: "
        + ", ".join(f.id for f in stragglers[:10])
    )


def test_combinations_compose_known_features(reg):
    combos = [f for f in reg if f.kind == "combination"]
    assert combos, "the combination table is the point of the registry's 90 section"
    for feature in combos:
        assert len(feature.composes) >= 2
        for cid in feature.composes:
            assert cid in reg, f"{feature.id} composes unknown {cid}"


def test_superseded_entries_point_forward(reg):
    for feature in reg:
        if not feature.active:
            assert feature.superseded_by in reg


def test_no_hand_written_status_fields():
    """The design rule, enforced.

    A PR that adds `status: tested` to a registry file must fail, not merge and
    then quietly disagree with the generated matrix.
    """
    root = registry.default_root()
    for path in sorted(root.glob("*.yaml")):
        text = path.read_text(encoding="utf-8")
        for lineno, line in enumerate(text.splitlines(), start=1):
            stripped = line.strip()
            if stripped.startswith("#"):
                continue
            key = stripped.split(":", 1)[0].strip("- ")
            assert key not in registry.FORBIDDEN_FIELDS, (
                f"{path.name}:{lineno}: {key!r} -- "
                f"{registry.FORBIDDEN_FIELDS[key]}"
            )


def test_json_schema_agrees_with_the_loader():
    """The published contract and the code that enforces it must not diverge.

    The JSON Schema is what a third-party tool validates against; the loader is
    what we validate against. If only one of them rejects a file, one of the two
    audiences is being lied to.
    """
    jsonschema = pytest.importorskip("jsonschema")

    import yaml

    schema = json.loads(registry.schema_path().read_text(encoding="utf-8"))
    validator = jsonschema.Draft202012Validator(schema)

    for path in sorted(registry.default_root().glob("*.yaml")):
        doc = yaml.safe_load(path.read_text(encoding="utf-8"))
        errors = sorted(validator.iter_errors(doc), key=lambda e: list(e.path))
        assert not errors, (
            f"{path.name} fails features.schema.json:\n"
            + "\n".join(f"  {list(e.path)}: {e.message}" for e in errors[:5])
        )


def test_marker_rejects_a_bad_level():
    with pytest.raises(ValueError):
        ucis_feature("S4.1", level="L9")


def test_marker_requires_at_least_one_id():
    with pytest.raises(ValueError):
        ucis_feature(level="L1")


def test_levels_are_the_documented_set():
    assert LEVELS == ("L0", "L1", "L2", "L3", "L4", "L5")
