"""
test_golden.py — Golden file regression tests.

Every committed golden .cdb file must be readable by all three implementations
and produce output that exactly matches the expected JSON sidecar.

A golden test failure means either:
  - An unintended format change was introduced in one of the implementations
  - Intentional format evolution → regenerate golden: python generate_golden.py --update
"""

import json
from pathlib import Path

import pytest

from conftest import (
    GOLDEN_DIR,
    db_to_dict,
)

from covsight.core.ncdb.ncdb_reader import NcdbReader


# ---------------------------------------------------------------------------
# Discover golden files
# ---------------------------------------------------------------------------

def _golden_files():
    """Return list of .cdb stems that have a matching .json sidecar."""
    if not GOLDEN_DIR.exists():
        return []
    return [
        p.stem
        for p in sorted(GOLDEN_DIR.glob("*.cdb"))
        if (GOLDEN_DIR / f"{p.stem}.json").exists()
    ]


GOLDEN_STEMS = _golden_files()


def _skip_if_missing(stem, required_prefix):
    """Return a pytest mark that skips if the golden file doesn't exist."""
    return pytest.mark.skipif(
        not (GOLDEN_DIR / f"{stem}.cdb").exists(),
        reason=f"Golden file {stem}.cdb not present — run generate_golden.py --update",
    )


# ---------------------------------------------------------------------------
# Python reader
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("stem", GOLDEN_STEMS)
def test_python_reads_golden(stem):
    """Python reader must reproduce the expected JSON for every golden file."""
    cdb = GOLDEN_DIR / f"{stem}.cdb"
    expected_json = json.loads((GOLDEN_DIR / f"{stem}.json").read_text())
    db = NcdbReader().read(str(cdb))
    got = db_to_dict(db)
    assert got == expected_json, (
        f"Python reader mismatch for {stem}.cdb\n"
        f"Expected: {json.dumps(expected_json, indent=2)}\n"
        f"Got:      {json.dumps(got, indent=2)}"
    )


# ---------------------------------------------------------------------------
# TypeScript reader
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("stem", GOLDEN_STEMS)
def test_ts_reads_golden(stem, ts_dump_path):
    """TypeScript reader must reproduce the expected JSON for every golden file."""
    import subprocess
    cdb = GOLDEN_DIR / f"{stem}.cdb"
    expected_json = json.loads((GOLDEN_DIR / f"{stem}.json").read_text())

    result = subprocess.run(
        ["node", ts_dump_path, "read", str(cdb)],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        pytest.fail(f"ts_dump read failed for {stem}.cdb:\n{result.stderr}")

    got = json.loads(result.stdout)
    assert got == expected_json, (
        f"TypeScript reader mismatch for {stem}.cdb\n"
        f"Expected: {json.dumps(expected_json, indent=2)}\n"
        f"Got:      {json.dumps(got, indent=2)}"
    )


# ---------------------------------------------------------------------------
# C reader
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("stem", GOLDEN_STEMS)
def test_c_reads_golden(stem, c_dump_path):
    """C reader must reproduce the expected JSON for every golden file."""
    import subprocess
    cdb = GOLDEN_DIR / f"{stem}.cdb"
    expected_json = json.loads((GOLDEN_DIR / f"{stem}.json").read_text())

    result = subprocess.run(
        [c_dump_path, "read", str(cdb)],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        pytest.fail(f"c_dump read failed for {stem}.cdb:\n{result.stderr}")

    got = json.loads(result.stdout)
    assert got == expected_json, (
        f"C reader mismatch for {stem}.cdb\n"
        f"Expected: {json.dumps(expected_json, indent=2)}\n"
        f"Got:      {json.dumps(got, indent=2)}"
    )
