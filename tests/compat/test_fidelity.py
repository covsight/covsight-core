"""
test_fidelity.py — Per-feature data integrity checks.

Unlike test_cross_write_read which checks "all readers agree", these tests
check exact expected values for each serializable feature in isolation.

Each test is parametrized over all three writers. Verification reads back with
the Python reader (canonical), then asserts specific field values.
"""

import json
import pytest
from pathlib import Path

from conftest import Helpers, SCENARIO_FNS


WRITERS = ["python", "typescript", "c"]
WRITERS_NO_SOURCE = ["python", "typescript"]  # C lacks source_info write


def _first_scope(doc, name=None):
    """Return the first matching top-level scope (or any if name=None)."""
    for s in doc["scopes"]:
        if name is None or s["name"] == name:
            return s
    return None


def _find_scope(doc_or_scope, name):
    """DFS search for a scope by name."""
    scope = doc_or_scope if isinstance(doc_or_scope, dict) else doc_or_scope
    if scope.get("name") == name:
        return scope
    for child in scope.get("children", []):
        result = _find_scope(child, name)
        if result:
            return result
    for s in scope.get("scopes", []):  # top-level doc
        result = _find_scope(s, name)
        if result:
            return result
    return None


def _all_items(scope):
    """Collect all cover items from a scope and its children (DFS)."""
    items = list(scope.get("items", []))
    for child in scope.get("children", []):
        items.extend(_all_items(child))
    return items


# ---------------------------------------------------------------------------
# minimal scenario: basic structure
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("writer", WRITERS)
def test_minimal_scope_names(writer, helpers: Helpers, tmp_path):
    """Scope names 'cg_minimal' and 'cp0' survive write → read."""
    cdb = tmp_path / "fid.cdb"
    helpers.write(writer, "minimal", cdb)
    doc = helpers.read_python(cdb)

    cg = _first_scope(doc, "cg_minimal")
    assert cg is not None, f"{writer}: top-level 'cg_minimal' not found"
    assert cg["type"] == 4096, f"{writer}: COVERGROUP type should be 4096"

    cp = next((c for c in cg["children"] if c["name"] == "cp0"), None)
    assert cp is not None, f"{writer}: 'cp0' not found under cg_minimal"
    assert cp["type"] == 16384, f"{writer}: COVERPOINT type should be 16384"


@pytest.mark.parametrize("writer", WRITERS)
def test_minimal_bin_count(writer, helpers: Helpers, tmp_path):
    """bin0 count=7 survives write → read."""
    cdb = tmp_path / "fid.cdb"
    helpers.write(writer, "minimal", cdb)
    doc = helpers.read_python(cdb)

    cp = _find_scope(doc, "cp0")
    assert cp is not None
    item = next((i for i in cp["items"] if i["name"] == "bin0"), None)
    assert item is not None, f"{writer}: bin0 not found"
    assert item["count"] == 7, f"{writer}: expected count=7, got {item['count']}"
    assert item["coverType"] == 1, f"{writer}: expected CVGBIN=1"


@pytest.mark.parametrize("writer", WRITERS)
def test_minimal_bin_at_least_default(writer, helpers: Helpers, tmp_path):
    """Default atLeast=1 (CVGBIN) is preserved."""
    cdb = tmp_path / "fid.cdb"
    helpers.write(writer, "minimal", cdb)
    doc = helpers.read_python(cdb)

    cp = _find_scope(doc, "cp0")
    item = next(i for i in cp["items"] if i["name"] == "bin0")
    assert item["atLeast"] == 1, f"{writer}: expected atLeast=1, got {item['atLeast']}"


# ---------------------------------------------------------------------------
# at_least scenario
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("writer", WRITERS)
def test_at_least_non_default_roundtrips(writer, helpers: Helpers, tmp_path):
    """atLeast=2 is preserved for all bins in the at_least scenario."""
    cdb = tmp_path / "fid.cdb"
    helpers.write(writer, "at_least", cdb)
    doc = helpers.read_python(cdb)

    cp = _find_scope(doc, "cp0")
    assert cp is not None, f"{writer}: cp0 not found"
    for item in cp["items"]:
        assert item["atLeast"] == 2, (
            f"{writer}: expected atLeast=2 for {item['name']}, got {item['atLeast']}")


@pytest.mark.parametrize("writer", WRITERS)
def test_at_least_count_values(writer, helpers: Helpers, tmp_path):
    """at_least scenario: count values 0 and 5 are preserved."""
    cdb = tmp_path / "fid.cdb"
    helpers.write(writer, "at_least", cdb)
    doc = helpers.read_python(cdb)

    cp = _find_scope(doc, "cp0")
    counts = {i["name"]: i["count"] for i in cp["items"]}
    assert counts.get("lo") == 0, f"{writer}: expected lo count=0, got {counts.get('lo')}"
    assert counts.get("hi") == 5, f"{writer}: expected hi count=5, got {counts.get('hi')}"


# ---------------------------------------------------------------------------
# basic scenario
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("writer", WRITERS)
def test_basic_du_instance_present(writer, helpers: Helpers, tmp_path):
    """basic scenario has a DU_MODULE (type=16777216) and INSTANCE (type=16)."""
    cdb = tmp_path / "fid.cdb"
    helpers.write(writer, "basic", cdb)
    doc = helpers.read_python(cdb)

    types = {s["type"] for s in doc["scopes"]}
    assert 16777216 in types, f"{writer}: DU_MODULE not found"
    assert 16 in types, f"{writer}: INSTANCE not found"


@pytest.mark.parametrize("writer", WRITERS)
def test_basic_covergroup_count(writer, helpers: Helpers, tmp_path):
    """basic scenario: 2 covergroups under the instance."""
    cdb = tmp_path / "fid.cdb"
    helpers.write(writer, "basic", cdb)
    doc = helpers.read_python(cdb)

    inst = next(s for s in doc["scopes"] if s["type"] == 16)
    cgs = [c for c in inst["children"] if c["type"] == 4096]
    assert len(cgs) == 2, f"{writer}: expected 2 covergroups, got {len(cgs)}"


@pytest.mark.parametrize("writer", WRITERS)
def test_basic_bin_count_varied(writer, helpers: Helpers, tmp_path):
    """basic scenario: bin counts vary correctly across covergroups."""
    cdb = tmp_path / "fid.cdb"
    helpers.write(writer, "basic", cdb)
    doc = helpers.read_python(cdb)

    inst = next(s for s in doc["scopes"] if s["type"] == 16)
    all_counts = set()
    for cg in inst["children"]:
        for cp in cg["children"]:
            for item in cp["items"]:
                all_counts.add(item["count"])
    # counts range from 0 to 29 (g*15 + p*5 + b, g∈[0,1], p∈[0,2], b∈[0,4])
    assert 0 in all_counts, f"{writer}: count=0 not found"
    assert 29 in all_counts, f"{writer}: count=29 not found"


# ---------------------------------------------------------------------------
# toggle scenario
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("writer", WRITERS)
def test_toggle_bin_names(writer, helpers: Helpers, tmp_path):
    """'0 -> 1' and '1 -> 0' bin names survive write → read."""
    cdb = tmp_path / "fid.cdb"
    helpers.write(writer, "toggle", cdb)
    doc = helpers.read_python(cdb)

    br = _find_scope(doc, "sig_valid")
    assert br is not None, f"{writer}: sig_valid branch not found"
    assert br["type"] == 2, f"{writer}: BRANCH type should be 2"
    names = {i["name"] for i in br["items"]}
    assert "0 -> 1" in names, f"{writer}: '0 -> 1' not found"
    assert "1 -> 0" in names, f"{writer}: '1 -> 0' not found"


@pytest.mark.parametrize("writer", WRITERS)
def test_toggle_bin_counts(writer, helpers: Helpers, tmp_path):
    """Toggle bin counts (3, 2) survive write → read."""
    cdb = tmp_path / "fid.cdb"
    helpers.write(writer, "toggle", cdb)
    doc = helpers.read_python(cdb)

    br = _find_scope(doc, "sig_valid")
    counts = {i["name"]: i["count"] for i in br["items"]}
    assert counts.get("0 -> 1") == 3, f"{writer}: expected count=3 for 0->1"
    assert counts.get("1 -> 0") == 2, f"{writer}: expected count=2 for 1->0"


@pytest.mark.parametrize("writer", WRITERS)
def test_toggle_cover_type(writer, helpers: Helpers, tmp_path):
    """Toggle bins have coverType=512 (TOGGLEBIN)."""
    cdb = tmp_path / "fid.cdb"
    helpers.write(writer, "toggle", cdb)
    doc = helpers.read_python(cdb)

    br = _find_scope(doc, "sig_valid")
    for item in br["items"]:
        assert item["coverType"] == 512, (
            f"{writer}: {item['name']} expected TOGGLEBIN=512, got {item['coverType']}")


# ---------------------------------------------------------------------------
# history scenario
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("writer", WRITERS)
def test_history_node_names(writer, helpers: Helpers, tmp_path):
    """History node names 'smoke' and 'regression' survive write → read."""
    cdb = tmp_path / "fid.cdb"
    helpers.write(writer, "history", cdb)
    doc = helpers.read_python(cdb)

    names = {n["name"] for n in doc["history"]}
    assert "smoke" in names, f"{writer}: 'smoke' history node not found"
    assert "regression" in names, f"{writer}: 'regression' history node not found"


@pytest.mark.parametrize("writer", WRITERS)
def test_history_node_kind(writer, helpers: Helpers, tmp_path):
    """History nodes have kind='TEST'."""
    cdb = tmp_path / "fid.cdb"
    helpers.write(writer, "history", cdb)
    doc = helpers.read_python(cdb)

    for node in doc["history"]:
        assert node["kind"] == "TEST", (
            f"{writer}: expected kind=TEST, got {node['kind']}")


# ---------------------------------------------------------------------------
# cross scenario
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("writer", WRITERS)
def test_cross_scope_present(writer, helpers: Helpers, tmp_path):
    """Cross scope 'x_ab' is present with type=32768 (CROSS)."""
    cdb = tmp_path / "fid.cdb"
    helpers.write(writer, "cross", cdb)
    doc = helpers.read_python(cdb)

    x = _find_scope(doc, "x_ab")
    assert x is not None, f"{writer}: cross scope 'x_ab' not found"
    assert x["type"] == 32768, f"{writer}: expected CROSS=32768, got {x['type']}"


@pytest.mark.parametrize("writer", WRITERS)
def test_cross_bin_count(writer, helpers: Helpers, tmp_path):
    """Cross bin 'a0_x_b0' count=5 survives write → read."""
    cdb = tmp_path / "fid.cdb"
    helpers.write(writer, "cross", cdb)
    doc = helpers.read_python(cdb)

    x = _find_scope(doc, "x_ab")
    item = next((i for i in x["items"] if i["name"] == "a0_x_b0"), None)
    assert item is not None, f"{writer}: cross bin 'a0_x_b0' not found"
    assert item["count"] == 5, f"{writer}: expected count=5, got {item['count']}"


@pytest.mark.parametrize("writer", WRITERS)
def test_cross_coverpoint_bins(writer, helpers: Helpers, tmp_path):
    """Coverpoints under cross covergroup retain their bins."""
    cdb = tmp_path / "fid.cdb"
    helpers.write(writer, "cross", cdb)
    doc = helpers.read_python(cdb)

    cpA = _find_scope(doc, "cp_a")
    assert cpA is not None, f"{writer}: cp_a not found"
    names_a = {i["name"] for i in cpA["items"]}
    assert "a0" in names_a and "a1" in names_a, f"{writer}: cp_a bins missing"


# ---------------------------------------------------------------------------
# source_info scenario (Python + TS only)
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("writer", WRITERS_NO_SOURCE)
def test_source_info_scope_exists(writer, helpers: Helpers, tmp_path):
    """source_info scenario: cg_src scope is present."""
    cdb = tmp_path / "fid.cdb"
    helpers.write(writer, "source_info", cdb)
    doc = helpers.read_python(cdb)

    cg = _find_scope(doc, "cg_src")
    assert cg is not None, f"{writer}: cg_src scope not found"


@pytest.mark.parametrize("writer", WRITERS_NO_SOURCE)
def test_source_info_bin_present(writer, helpers: Helpers, tmp_path):
    """source_info scenario: b0 bin is present with count=1."""
    cdb = tmp_path / "fid.cdb"
    helpers.write(writer, "source_info", cdb)
    doc = helpers.read_python(cdb)

    cp = _find_scope(doc, "cp0")
    assert cp is not None, f"{writer}: cp0 not found in source_info"
    item = next((i for i in cp["items"] if i["name"] == "b0"), None)
    assert item is not None, f"{writer}: b0 bin not found"
    assert item["count"] == 1, f"{writer}: expected count=1"


# ---------------------------------------------------------------------------
# deep scenario
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("writer", WRITERS)
def test_deep_nesting(writer, helpers: Helpers, tmp_path):
    """5-level nesting is preserved; leaf bin at level_4."""
    cdb = tmp_path / "fid.cdb"
    helpers.write(writer, "deep", cdb)
    doc = helpers.read_python(cdb)

    # Walk down 5 levels
    level = _first_scope(doc, "level_0")
    assert level is not None, f"{writer}: level_0 not found"
    for d in range(1, 5):
        child = next(
            (c for c in level["children"] if c["name"] == f"level_{d}"), None)
        assert child is not None, f"{writer}: level_{d} not found"
        level = child

    item = next((i for i in level["items"] if i["name"] == "leaf_bin"), None)
    assert item is not None, f"{writer}: leaf_bin not found at level_4"
    assert item["count"] == 42, f"{writer}: expected count=42"


# ---------------------------------------------------------------------------
# scope type values
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("writer", WRITERS)
def test_scope_type_values(writer, helpers: Helpers, tmp_path):
    """ScopeTypeT enum values are correct numeric constants."""
    cdb = tmp_path / "fid.cdb"
    helpers.write(writer, "cross", cdb)
    doc = helpers.read_python(cdb)

    types = {}

    def collect_types(scope):
        types[scope["name"]] = scope["type"]
        for child in scope.get("children", []):
            collect_types(child)

    for s in doc["scopes"]:
        collect_types(s)

    assert types.get("cg_cross") == 4096,  f"{writer}: COVERGROUP != 4096"
    assert types.get("cp_a")     == 16384, f"{writer}: COVERPOINT != 16384"
    assert types.get("x_ab")     == 32768, f"{writer}: CROSS != 32768"
