"""
test_cross_write_read.py — Cross-implementation write-then-read tests.

Each implementation (Python, TypeScript, C) writes each scenario; then all
three readers read the written file and must produce identical JSON.

Parametrization: 3 writers × 10 scenarios = 30 writer tests, each asserting
all readers agree.

Python is always used as the canonical reference reader because:
  - It was the first implementation
  - It has the most comprehensive existing unit tests
  - Its db_to_dict() provides the reference JSON format

Known limitations (see docs/cross-impl-todo.md):
  - C writer does not support source_info: test is skipped for C + source_info
  - Toggle atLeast: C/TS write 0, Python reads back 0 after the scope_tree fix
"""

import json
import pytest

from conftest import (
    SCENARIO_FNS,
    SCENARIOS_C,
    db_to_dict,
    Helpers,
)


# ---------------------------------------------------------------------------
# Python writer → all readers
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("scenario", list(SCENARIO_FNS.keys()))
def test_python_writes_ts_reads(scenario, helpers: Helpers, tmp_path):
    """TypeScript reader must reproduce what Python writer wrote."""
    cdb = tmp_path / f"py_{scenario}.cdb"
    helpers.write_python(scenario, cdb)
    ref = helpers.read_python(cdb)
    got = helpers.read_ts(cdb)
    assert got == ref, _diff_msg("python", "typescript", scenario, ref, got)


@pytest.mark.parametrize("scenario", list(SCENARIO_FNS.keys()))
def test_python_writes_c_reads(scenario, helpers: Helpers, tmp_path):
    """C reader must reproduce what Python writer wrote."""
    cdb = tmp_path / f"py_{scenario}.cdb"
    helpers.write_python(scenario, cdb)
    ref = helpers.read_python(cdb)
    got = helpers.read_c(cdb)
    assert got == ref, _diff_msg("python", "c", scenario, ref, got)


# ---------------------------------------------------------------------------
# TypeScript writer → all readers
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("scenario", list(SCENARIO_FNS.keys()))
def test_ts_writes_python_reads(scenario, helpers: Helpers, tmp_path):
    """Python reader must reproduce what TypeScript writer wrote."""
    cdb = tmp_path / f"ts_{scenario}.cdb"
    helpers.write_ts(scenario, cdb)
    ref = helpers.read_python(cdb)
    got = helpers.read_ts(cdb)
    assert got == ref, _diff_msg("typescript", "python", scenario, got, ref)


@pytest.mark.parametrize("scenario", list(SCENARIO_FNS.keys()))
def test_ts_writes_c_reads(scenario, helpers: Helpers, tmp_path):
    """C reader must reproduce what TypeScript writer wrote.

    Python is used as reference (reads the TS-written file).
    """
    cdb = tmp_path / f"ts_{scenario}.cdb"
    helpers.write_ts(scenario, cdb)
    ref = helpers.read_python(cdb)
    got = helpers.read_c(cdb)
    assert got == ref, _diff_msg("typescript", "c", scenario, ref, got)


# ---------------------------------------------------------------------------
# C writer → all readers (source_info skipped — C writer doesn't support it)
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("scenario", SCENARIOS_C)
def test_c_writes_python_reads(scenario, helpers: Helpers, tmp_path):
    """Python reader must reproduce what C writer wrote."""
    cdb = tmp_path / f"c_{scenario}.cdb"
    helpers.write_c(scenario, cdb)
    ref = helpers.read_ts(cdb)    # use TS as reference for C-written files
    got = helpers.read_python(cdb)
    assert got == ref, _diff_msg("c", "python", scenario, ref, got)


@pytest.mark.parametrize("scenario", SCENARIOS_C)
def test_c_writes_ts_reads(scenario, helpers: Helpers, tmp_path):
    """TypeScript reader must reproduce what C writer wrote."""
    cdb = tmp_path / f"c_{scenario}.cdb"
    helpers.write_c(scenario, cdb)
    ref = helpers.read_c(cdb)     # C reads its own file as reference
    got = helpers.read_ts(cdb)
    assert got == ref, _diff_msg("c", "typescript", scenario, ref, got)


# ---------------------------------------------------------------------------
# Utilities
# ---------------------------------------------------------------------------

def _diff_msg(writer, reader, scenario, expected, got):
    return (
        f"Cross-compat failure: {writer} → {reader}, scenario={scenario!r}\n"
        f"Expected: {json.dumps(expected, indent=2)}\n"
        f"Got:      {json.dumps(got, indent=2)}"
    )
