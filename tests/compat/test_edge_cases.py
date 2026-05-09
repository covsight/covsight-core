"""
test_edge_cases.py — Edge cases and boundary conditions.

These tests exercise extreme or unusual inputs to validate robustness:
  - Empty database
  - Zero-count bins
  - Large count values
  - Deep nesting (already in test_fidelity; verified here for all writers)
  - Many siblings under one scope
  - Empty coverpoint (no bins)
"""

import json
import pytest
from pathlib import Path

from conftest import Helpers, SCENARIO_FNS, SCENARIOS_C


WRITERS = ["python", "typescript", "c"]


def _find_scope(doc_or_scope, name):
    if isinstance(doc_or_scope, dict) and "scopes" in doc_or_scope:
        # top-level doc
        for s in doc_or_scope["scopes"]:
            result = _find_scope(s, name)
            if result:
                return result
        return None
    scope = doc_or_scope
    if scope.get("name") == name:
        return scope
    for child in scope.get("children", []):
        result = _find_scope(child, name)
        if result:
            return result
    return None


# ---------------------------------------------------------------------------
# Empty database
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("writer", WRITERS)
def test_empty_database(writer, helpers: Helpers, tmp_path):
    """DB with no scopes and no history serializes and reads back cleanly."""
    cdb = tmp_path / "empty.cdb"
    helpers.write(writer, "empty", cdb)
    doc = helpers.read_python(cdb)

    assert doc["scopes"] == [], f"{writer}: expected no scopes"
    assert doc["history"] == [], f"{writer}: expected no history"


@pytest.mark.parametrize("writer", WRITERS)
def test_empty_ts_reads(writer, helpers: Helpers, tmp_path):
    """TypeScript reader handles empty database."""
    cdb = tmp_path / "empty.cdb"
    helpers.write(writer, "empty", cdb)
    doc = helpers.read_ts(cdb)
    assert doc["scopes"] == []
    assert doc["history"] == []


@pytest.mark.parametrize("writer", SCENARIOS_C)
def test_empty_c_reads(writer, helpers: Helpers, tmp_path):
    """C reader handles empty database."""
    cdb = tmp_path / "empty.cdb"
    helpers.write("c", "empty", cdb)
    doc = helpers.read_c(cdb)
    assert doc["scopes"] == []
    assert doc["history"] == []


# ---------------------------------------------------------------------------
# Zero-count bins
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("writer", WRITERS)
def test_zero_count_bins(writer, helpers: Helpers, tmp_path):
    """at_least scenario has count=0 for 'lo' bin — preserved exactly."""
    cdb = tmp_path / "al.cdb"
    helpers.write(writer, "at_least", cdb)
    doc = helpers.read_python(cdb)

    cp = _find_scope(doc, "cp0")
    lo = next((i for i in cp["items"] if i["name"] == "lo"), None)
    assert lo is not None
    assert lo["count"] == 0, f"{writer}: expected count=0 for 'lo', got {lo['count']}"


# ---------------------------------------------------------------------------
# Large count values
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("writer", WRITERS)
def test_large_count_in_basic(writer, helpers: Helpers, tmp_path):
    """Large-ish count value (29) survives write → read in basic scenario."""
    cdb = tmp_path / "basic.cdb"
    helpers.write(writer, "basic", cdb)
    doc = helpers.read_python(cdb)

    # basic scenario: max count is g=1, p=2, b=4 → 1*15 + 2*5 + 4 = 29
    all_counts = set()

    def collect(scope):
        for item in scope.get("items", []):
            all_counts.add(item["count"])
        for child in scope.get("children", []):
            collect(child)

    for s in doc["scopes"]:
        collect(s)

    assert 29 in all_counts, f"{writer}: count=29 not found (large-ish value test)"


# ---------------------------------------------------------------------------
# Multiple history nodes
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("writer", WRITERS)
def test_multiple_history_nodes(writer, helpers: Helpers, tmp_path):
    """history scenario has exactly 2 TEST history nodes."""
    cdb = tmp_path / "hist.cdb"
    helpers.write(writer, "history", cdb)
    doc = helpers.read_python(cdb)

    assert len(doc["history"]) == 2, (
        f"{writer}: expected 2 history nodes, got {len(doc['history'])}")
    for node in doc["history"]:
        assert node["kind"] == "TEST"


# ---------------------------------------------------------------------------
# Multiple covergroups / coverpoints
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("writer", WRITERS)
def test_many_coverpoints(writer, helpers: Helpers, tmp_path):
    """basic scenario has 3 coverpoints under each covergroup (6 total)."""
    cdb = tmp_path / "basic.cdb"
    helpers.write(writer, "basic", cdb)
    doc = helpers.read_python(cdb)

    inst = next(s for s in doc["scopes"] if s["type"] == 16)
    total_cps = sum(
        len([c for c in cg["children"] if c["type"] == 16384])
        for cg in inst["children"]
        if cg["type"] == 4096
    )
    assert total_cps == 6, f"{writer}: expected 6 coverpoints, got {total_cps}"


# ---------------------------------------------------------------------------
# Deep nesting (supplementary to test_fidelity)
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("writer", WRITERS)
def test_deep_nesting_structure(writer, helpers: Helpers, tmp_path):
    """deep scenario: 5 scopes nested, correct types at each level."""
    cdb = tmp_path / "deep.cdb"
    helpers.write(writer, "deep", cdb)
    doc = helpers.read_python(cdb)

    level = _find_scope(doc, "level_0")
    assert level is not None
    assert level["type"] == 4096, f"{writer}: level_0 should be COVERGROUP"

    for d in range(1, 5):
        child = next(
            (c for c in level["children"] if c["name"] == f"level_{d}"), None)
        assert child is not None, f"{writer}: level_{d} missing"
        assert child["type"] == 16384, f"{writer}: level_{d} should be COVERPOINT"
        level = child

    assert len(level["items"]) == 1, f"{writer}: level_4 should have exactly 1 item"
    assert level["items"][0]["name"] == "leaf_bin"


# ---------------------------------------------------------------------------
# Cross scope structure
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("writer", WRITERS)
def test_cross_covergroup_children(writer, helpers: Helpers, tmp_path):
    """cross scenario: covergroup has exactly 3 children (cp_a, cp_b, x_ab)."""
    cdb = tmp_path / "cross.cdb"
    helpers.write(writer, "cross", cdb)
    doc = helpers.read_python(cdb)

    cg = _find_scope(doc, "cg_cross")
    assert cg is not None, f"{writer}: cg_cross not found"
    child_names = {c["name"] for c in cg["children"]}
    assert child_names == {"cp_a", "cp_b", "x_ab"}, (
        f"{writer}: expected children {{cp_a, cp_b, x_ab}}, got {child_names}")


@pytest.mark.parametrize("writer", WRITERS)
def test_cross_bin_cover_type(writer, helpers: Helpers, tmp_path):
    """cross bin 'a0_x_b0' has coverType=2097152 (DEFAULTBIN)."""
    cdb = tmp_path / "cross.cdb"
    helpers.write(writer, "cross", cdb)
    doc = helpers.read_python(cdb)

    x = _find_scope(doc, "x_ab")
    item = next((i for i in x["items"] if i["name"] == "a0_x_b0"), None)
    assert item is not None
    assert item["coverType"] == 2097152, (
        f"{writer}: expected DEFAULTBIN=2097152, got {item['coverType']}")


# ---------------------------------------------------------------------------
# All-readers agreement for "full" scenario
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("writer", WRITERS)
def test_full_all_readers_agree(writer, helpers: Helpers, tmp_path):
    """full scenario: all three readers produce identical JSON."""
    if writer == "c":
        # C full scenario omits source_info — still verify readers agree
        pass
    cdb = tmp_path / "full.cdb"
    helpers.write(writer, "full", cdb)

    ref = helpers.read_python(cdb)
    ts_got = helpers.read_ts(cdb)
    c_got = helpers.read_c(cdb)

    assert ts_got == ref, (
        f"{writer}: TypeScript ≠ Python for full scenario\n"
        f"{json.dumps(ref, indent=2)[:500]}")
    assert c_got == ref, (
        f"{writer}: C ≠ Python for full scenario\n"
        f"{json.dumps(ref, indent=2)[:500]}")


# ---------------------------------------------------------------------------
# Unicode scope/item names (item 6)
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("writer", WRITERS)
def test_unicode_scope_names(writer, helpers: Helpers, tmp_path):
    """Unicode (multi-byte UTF-8) scope and item names survive write → read."""
    cdb = tmp_path / "uni.cdb"
    helpers.write(writer, "unicode_names", cdb)
    doc = helpers.read_python(cdb)

    cg = next((s for s in doc["scopes"] if s["name"] == "cg_αβγ"), None)
    assert cg is not None, f"{writer}: cg_αβγ not found in scopes"

    cp = next((c for c in cg["children"] if c["name"] == "cp_café"), None)
    assert cp is not None, f"{writer}: cp_café not found under cg_αβγ"

    item = next((i for i in cp["items"] if i["name"] == "bin_日本語"), None)
    assert item is not None, f"{writer}: bin_日本語 not found"
    assert item["count"] == 3, f"{writer}: expected count=3, got {item['count']}"


# ---------------------------------------------------------------------------
# Large count values > 2^32 (item 7)
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("writer", WRITERS)
def test_count_above_2_32(writer, helpers: Helpers, tmp_path):
    """Count value 2^32 (4294967296) survives write → read in all impls."""
    cdb = tmp_path / "large.cdb"
    helpers.write(writer, "large_count", cdb)
    doc = helpers.read_python(cdb)

    cg = next((s for s in doc["scopes"] if s["name"] == "cg_large"), None)
    assert cg is not None, f"{writer}: cg_large not found"
    cp = next((c for c in cg["children"] if c["name"] == "cp0"), None)
    assert cp is not None, f"{writer}: cp0 not found under cg_large"
    item = next((i for i in cp["items"] if i["name"] == "big_bin"), None)
    assert item is not None, f"{writer}: big_bin not found"
    assert item["count"] == 2**32, (
        f"{writer}: expected count={2**32}, got {item['count']}")
