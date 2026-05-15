"""
test_issues_compat.py — Cross-implementation compat tests for issue tracking.

Tests that Python, TypeScript, and C readers all agree on issue data written
by Python and TypeScript writers.  C cannot write issues (read-only C API).

Scenarios:
  issues_minimal  — 2 issues, 1 waiver link, no metadata
  issues_full     — 4 issues, all link types, IssuesMeta, coexists with coverage
"""

import pytest

from conftest import (
    SCENARIOS_ISSUES,
    SCENARIOS_ISSUES_WRITERS,
    Helpers,
)


# ---------------------------------------------------------------------------
# Helper
# ---------------------------------------------------------------------------

def _diff_msg(writer, reader, scenario, ref, got):
    import json
    return (
        f"\nScenario '{scenario}' written by {writer}, read by {reader}.\n"
        f"Expected:\n{json.dumps(ref, indent=2)}\n"
        f"Got:\n{json.dumps(got, indent=2)}"
    )


# ---------------------------------------------------------------------------
# Python writer → TypeScript reader
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("scenario", SCENARIOS_ISSUES)
def test_python_writes_ts_reads_issues(scenario, helpers: Helpers, tmp_path):
    """TypeScript reader must reproduce what Python wrote (issues data)."""
    cdb = tmp_path / f"py_{scenario}.cdb"
    helpers.write_python(scenario, cdb)
    ref = helpers.read_python(cdb)
    got = helpers.read_ts(cdb)
    assert got == ref, _diff_msg("python", "typescript", scenario, ref, got)


# ---------------------------------------------------------------------------
# Python writer → C reader
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("scenario", SCENARIOS_ISSUES)
def test_python_writes_c_reads_issues(scenario, helpers: Helpers, tmp_path):
    """C reader must reproduce what Python wrote (issues data)."""
    cdb = tmp_path / f"py_{scenario}.cdb"
    helpers.write_python(scenario, cdb)
    ref = helpers.read_python(cdb)
    got = helpers.read_c(cdb)
    assert got == ref, _diff_msg("python", "c", scenario, ref, got)


# ---------------------------------------------------------------------------
# TypeScript writer → Python reader
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("scenario", SCENARIOS_ISSUES)
def test_ts_writes_python_reads_issues(scenario, helpers: Helpers, tmp_path):
    """Python reader must reproduce what TypeScript wrote (issues data)."""
    cdb = tmp_path / f"ts_{scenario}.cdb"
    helpers.write_ts(scenario, cdb)
    ref = helpers.read_ts(cdb)
    got = helpers.read_python(cdb)
    assert got == ref, _diff_msg("typescript", "python", scenario, ref, got)


# ---------------------------------------------------------------------------
# TypeScript writer → C reader
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("scenario", SCENARIOS_ISSUES)
def test_ts_writes_c_reads_issues(scenario, helpers: Helpers, tmp_path):
    """C reader must reproduce what TypeScript wrote (issues data)."""
    cdb = tmp_path / f"ts_{scenario}.cdb"
    helpers.write_ts(scenario, cdb)
    ref = helpers.read_ts(cdb)
    got = helpers.read_c(cdb)
    assert got == ref, _diff_msg("typescript", "c", scenario, ref, got)


# ---------------------------------------------------------------------------
# Issues coexist with coverage data
# ---------------------------------------------------------------------------

def test_issues_coexist_with_coverage(helpers: Helpers, tmp_path):
    """issues_full has both coverage scopes and issues; all readers agree."""
    scenario = "issues_full"
    cdb = tmp_path / f"py_{scenario}.cdb"
    helpers.write_python(scenario, cdb)
    ref = helpers.read_python(cdb)

    # Verify coverage data is present alongside issues
    assert len(ref["scopes"]) > 0, "Expected coverage scopes in issues_full"
    assert len(ref["issues"]) > 0, "Expected issues in issues_full"

    ts_doc = helpers.read_ts(cdb)
    c_doc  = helpers.read_c(cdb)
    assert ts_doc == ref, _diff_msg("python", "typescript", scenario, ref, ts_doc)
    assert c_doc  == ref, _diff_msg("python", "c",          scenario, ref, c_doc)
