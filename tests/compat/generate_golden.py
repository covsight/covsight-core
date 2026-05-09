#!/usr/bin/env python3
"""
generate_golden.py — Regenerate golden fixtures for the NCDB compat test suite.

Usage:
    python generate_golden.py [--update]

Without --update: verifies existing golden files match expected JSON.
With    --update: regenerates all golden/*.cdb and golden/*.json files.

Golden files:
    py_{scenario}.cdb + .json — written by Python, JSON from Python reader
    ts_full.cdb     + .json  — written by TypeScript
    ts_minimal.cdb  + .json  — written by TypeScript
    c_full.cdb      + .json  — written by C (if libncdb.so is available)
    c_minimal.cdb   + .json  — written by C
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).parent.parent.parent
PYTHON_SRC = REPO / "python"
sys.path.insert(0, str(PYTHON_SRC))

GOLDEN_DIR = Path(__file__).parent / "golden"
TS_DUMP = Path(__file__).parent / "helpers" / "ts_dump.mjs"
C_DUMP = Path(__file__).parent / "helpers" / "c_dump"
C_DUMP_SRC = Path(__file__).parent / "helpers" / "c_dump.c"

from covsight.core.mem.mem_ucis import MemUCIS                    # noqa: E402
from covsight.core.ncdb.ncdb_writer import NcdbWriter              # noqa: E402
from covsight.core.ncdb.ncdb_reader import NcdbReader              # noqa: E402

# Import scenario builders and db_to_dict from conftest
import importlib.util
_spec = importlib.util.spec_from_file_location(
    "conftest", Path(__file__).parent / "conftest.py")
_conftest = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_conftest)

SCENARIO_FNS = _conftest.SCENARIO_FNS
db_to_dict = _conftest.db_to_dict


# ---------------------------------------------------------------------------
# Helper: check TS availability
# ---------------------------------------------------------------------------

def ts_available() -> bool:
    dist = REPO / "ts" / "dist"
    return dist.exists()


def c_available() -> bool:
    libncdb = REPO / "c" / "build" / "libncdb.so"
    if not libncdb.exists():
        return False
    # Try to build c_dump if needed
    if not C_DUMP.exists() or C_DUMP.stat().st_mtime < C_DUMP_SRC.stat().st_mtime:
        result = subprocess.run(
            ["make", "-C", str(C_DUMP.parent)],
            capture_output=True, text=True,
        )
        if result.returncode != 0:
            print(f"  [WARN] c_dump build failed: {result.stderr}", file=sys.stderr)
            return False
    return C_DUMP.exists()


# ---------------------------------------------------------------------------
# Write helpers
# ---------------------------------------------------------------------------

def write_python(scenario: str, cdb_path: Path) -> dict:
    """Write scenario with Python and return its canonical JSON."""
    db = MemUCIS()
    SCENARIO_FNS[scenario](db)
    NcdbWriter().write(db, str(cdb_path))
    db2 = NcdbReader().read(str(cdb_path))
    return db_to_dict(db2)


def write_ts(scenario: str, cdb_path: Path) -> dict:
    """Write scenario with TS helper and return canonical JSON (read by Python)."""
    result = subprocess.run(
        ["node", str(TS_DUMP), "write", scenario, str(cdb_path)],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(f"ts_dump write failed:\n{result.stderr}")
    db = NcdbReader().read(str(cdb_path))
    return db_to_dict(db)


def write_c(scenario: str, cdb_path: Path) -> dict:
    """Write scenario with C helper and return canonical JSON (read by Python)."""
    result = subprocess.run(
        [str(C_DUMP), "write", scenario, str(cdb_path)],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(f"c_dump write failed:\n{result.stderr}")
    db = NcdbReader().read(str(cdb_path))
    return db_to_dict(db)


# ---------------------------------------------------------------------------
# Golden registry
# ---------------------------------------------------------------------------

def build_golden_registry(with_ts: bool, with_c: bool):
    """Return list of (stem, writer_fn, scenario) tuples."""
    entries = []

    # Python writes all scenarios
    for name in SCENARIO_FNS:
        entries.append((f"py_{name}", write_python, name))

    # TS writes minimal + full
    if with_ts:
        for name in ["minimal", "full"]:
            entries.append((f"ts_{name}", write_ts, name))

    # C writes minimal + full (skip source_info which C doesn't support)
    if with_c:
        for name in ["minimal", "full"]:
            entries.append((f"c_{name}", write_c, name))

    return entries


# ---------------------------------------------------------------------------
# Actions
# ---------------------------------------------------------------------------

def update_golden(entries):
    """Regenerate all golden files."""
    GOLDEN_DIR.mkdir(exist_ok=True)
    errors = 0
    for stem, writer_fn, scenario in entries:
        cdb_path = GOLDEN_DIR / f"{stem}.cdb"
        json_path = GOLDEN_DIR / f"{stem}.json"
        try:
            data = writer_fn(scenario, cdb_path)
            json_path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
            print(f"  OK  {stem}.cdb + .json")
        except Exception as e:
            print(f"  ERR {stem}: {e}", file=sys.stderr)
            errors += 1
    return errors == 0


def verify_golden(entries):
    """Verify existing golden files match their JSON sidecars."""
    errors = 0
    for stem, writer_fn, scenario in entries:
        cdb_path = GOLDEN_DIR / f"{stem}.cdb"
        json_path = GOLDEN_DIR / f"{stem}.json"
        if not cdb_path.exists() or not json_path.exists():
            print(f"  MISSING {stem} — run with --update")
            errors += 1
            continue
        expected = json.loads(json_path.read_text())
        try:
            db = NcdbReader().read(str(cdb_path))
            got = db_to_dict(db)
            if got == expected:
                print(f"  OK  {stem}")
            else:
                print(f"  DIFFER {stem}")
                errors += 1
        except Exception as e:
            print(f"  ERR {stem}: {e}", file=sys.stderr)
            errors += 1
    return errors == 0


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--update", action="store_true",
        help="Regenerate golden files (default: verify only)",
    )
    args = parser.parse_args()

    with_ts = ts_available()
    with_c = c_available()

    if not with_ts:
        print("[INFO] TypeScript not built — skipping ts_* goldens", file=sys.stderr)
    if not with_c:
        print("[INFO] C library not built — skipping c_* goldens", file=sys.stderr)

    entries = build_golden_registry(with_ts, with_c)

    if args.update:
        print("Updating golden fixtures...")
        ok = update_golden(entries)
    else:
        print("Verifying golden fixtures...")
        ok = verify_golden(entries)

    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
