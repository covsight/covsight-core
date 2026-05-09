"""
conftest.py — pytest fixtures for NCDB cross-implementation compat tests.

Fixtures:
  ts_dump   session-scoped path to ts_dump.mjs (skips if ts/dist not built)
  c_dump    session-scoped path to compiled c_dump binary (builds if needed)
  helpers   per-test Helpers instance wrapping write/read for all three impls

Scenarios are the canonical set of test databases (Python-implemented here;
ts_dump.mjs and c_dump.c carry parallel implementations).
"""

import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

# ---------------------------------------------------------------------------
# Repository layout
# ---------------------------------------------------------------------------

REPO = Path(__file__).parent.parent.parent
PYTHON_SRC = REPO / "python"
TS_DUMP = Path(__file__).parent / "helpers" / "ts_dump.mjs"
C_DUMP_SRC = Path(__file__).parent / "helpers" / "c_dump.c"
C_DUMP_BIN = Path(__file__).parent / "helpers" / "c_dump"
GOLDEN_DIR = Path(__file__).parent / "golden"

sys.path.insert(0, str(PYTHON_SRC))

from covsight.core.api import ScopeTypeT, CoverTypeT, HistoryNodeKind  # noqa: E402
from covsight.core.mem.mem_ucis import MemUCIS  # noqa: E402
from covsight.core.ncdb.ncdb_writer import NcdbWriter  # noqa: E402
from covsight.core.ncdb.ncdb_reader import NcdbReader  # noqa: E402
from covsight.core.api.source_info import SourceInfo  # noqa: E402

# ---------------------------------------------------------------------------
# Scenario builders — Python reference implementations
# ---------------------------------------------------------------------------

def scenario_empty(db: MemUCIS) -> None:
    pass


def scenario_minimal(db: MemUCIS) -> None:
    cg = db.createScope("cg_minimal", None, 1, None, ScopeTypeT.COVERGROUP, 0)
    cp = cg.createCoverpoint("cp0", None, 1, None)
    cp.createBin("bin0", None, 1, 7, "", CoverTypeT.CVGBIN)


def scenario_basic(db: MemUCIS) -> None:
    fh = db.createFileHandle("rtl/top.sv", None)
    du = db.createScope("top", None, 1, None, ScopeTypeT.DU_MODULE, 0)
    inst = db.createInstance("top", None, 1, None, ScopeTypeT.INSTANCE, du, 0)
    for g in range(2):
        cg = inst.createScope(f"cg_{g}", SourceInfo(fh, 1, 0), 1, None, ScopeTypeT.COVERGROUP, 0)
        for p in range(3):
            cp = cg.createCoverpoint(f"cp_{p}", None, 1, None)
            for b in range(5):
                cp.createBin(f"bin_{b}", None, 1, g * 15 + p * 5 + b, "", CoverTypeT.CVGBIN)


def scenario_at_least(db: MemUCIS) -> None:
    cg = db.createScope("cg_al", None, 1, None, ScopeTypeT.COVERGROUP, 0)
    cp = cg.createCoverpoint("cp0", None, 1, None)
    cp.createBin("lo", None, 2, 0, "", CoverTypeT.CVGBIN)
    cp.createBin("hi", None, 2, 5, "", CoverTypeT.CVGBIN)


def scenario_toggle(db: MemUCIS) -> None:
    from covsight.core.api.cover_data import CoverData
    fh = db.createFileHandle("rtl/top.sv", None)
    du = db.createScope("top", None, 1, None, ScopeTypeT.DU_MODULE, 0)
    inst = db.createInstance("top", None, 1, None, ScopeTypeT.INSTANCE, du, 0)
    br = inst.createScope("sig_valid", None, 1, None, ScopeTypeT.BRANCH, 0)
    cd01 = CoverData(CoverTypeT.TOGGLEBIN, 0); cd01.data = 3; cd01.at_least = 0
    cd10 = CoverData(CoverTypeT.TOGGLEBIN, 0); cd10.data = 2; cd10.at_least = 0
    br.createNextCover("0 -> 1", cd01, None)
    br.createNextCover("1 -> 0", cd10, None)


def scenario_source_info(db: MemUCIS) -> None:
    fh = db.createFileHandle("rtl/foo.sv", None)
    du = db.createScope("foo", None, 1, None, ScopeTypeT.DU_MODULE, 0)
    inst = db.createInstance("foo", None, 1, None, ScopeTypeT.INSTANCE, du, 0)
    cg = inst.createScope("cg_src", SourceInfo(fh, 10, 0), 1, None, ScopeTypeT.COVERGROUP, 0)
    cp = cg.createCoverpoint("cp0", SourceInfo(fh, 11, 5), 1, None)
    cp.createBin("b0", None, 1, 1, "", CoverTypeT.CVGBIN)


def scenario_history(db: MemUCIS) -> None:
    cg = db.createScope("cg_h", None, 1, None, ScopeTypeT.COVERGROUP, 0)
    cp = cg.createCoverpoint("cp0", None, 1, None)
    cp.createBin("b0", None, 1, 1, "", CoverTypeT.CVGBIN)
    h1 = db.createHistoryNode(None, "smoke", None, HistoryNodeKind.TEST)
    h1.setUserName("alice"); h1.setSeed("42")
    h1.setToolCategory("sim"); h1.setComment("smoke run")
    h2 = db.createHistoryNode(None, "regression", None, HistoryNodeKind.TEST)
    h2.setUserName("bob"); h2.setSeed("99")
    h2.setToolCategory("sim"); h2.setComment("full regression")


def scenario_cross(db: MemUCIS) -> None:
    from covsight.core.api.cover_data import CoverData
    cg  = db.createScope("cg_cross", None, 1, None, ScopeTypeT.COVERGROUP, 0)
    cpA = cg.createCoverpoint("cp_a", None, 1, None)
    cpA.createBin("a0", None, 1, 1, "", CoverTypeT.CVGBIN)
    cpA.createBin("a1", None, 1, 2, "", CoverTypeT.CVGBIN)
    cpB = cg.createCoverpoint("cp_b", None, 1, None)
    cpB.createBin("b0", None, 1, 3, "", CoverTypeT.CVGBIN)
    cpB.createBin("b1", None, 1, 4, "", CoverTypeT.CVGBIN)
    x = cg.createCross("x_ab", None, 1, None, [cpA, cpB])
    cd = CoverData(CoverTypeT.DEFAULTBIN, 0); cd.data = 5
    x.createNextCover("a0_x_b0", cd, None)


def scenario_deep(db: MemUCIS) -> None:
    cur = db.createScope("level_0", None, 1, None, ScopeTypeT.COVERGROUP, 0)
    for d in range(1, 5):
        cur = cur.createScope(f"level_{d}", None, 1, None, ScopeTypeT.COVERPOINT, 0)
    cur.createBin("leaf_bin", None, 1, 42, "", CoverTypeT.CVGBIN)


def scenario_full(db: MemUCIS) -> None:
    scenario_basic(db)
    scenario_at_least(db)
    scenario_toggle(db)
    scenario_history(db)
    scenario_cross(db)
    scenario_deep(db)


def scenario_unicode_names(db: MemUCIS) -> None:
    cg = db.createScope("cg_αβγ", None, 1, None, ScopeTypeT.COVERGROUP, 0)
    cp = cg.createCoverpoint("cp_café", None, 1, None)
    cp.createBin("bin_日本語", None, 1, 3, "", CoverTypeT.CVGBIN)


def scenario_large_count(db: MemUCIS) -> None:
    cg = db.createScope("cg_large", None, 1, None, ScopeTypeT.COVERGROUP, 0)
    cp = cg.createCoverpoint("cp0", None, 1, None)
    cp.createBin("big_bin", None, 1, 2**32, "", CoverTypeT.CVGBIN)


def scenario_weight_goal(db: MemUCIS) -> None:
    cg = db.createScope("cg_wg", None, 2, None, ScopeTypeT.COVERGROUP, 0)
    cg.setGoal(80)
    cp = cg.createCoverpoint("cp0", None, 1, None)
    cp.createBin("b0", None, 1, 1, "", CoverTypeT.CVGBIN)


SCENARIO_FNS = {
    "empty":         scenario_empty,
    "minimal":       scenario_minimal,
    "basic":         scenario_basic,
    "at_least":      scenario_at_least,
    "toggle":        scenario_toggle,
    "source_info":   scenario_source_info,
    "history":       scenario_history,
    "cross":         scenario_cross,
    "deep":          scenario_deep,
    "full":          scenario_full,
    "unicode_names": scenario_unicode_names,
    "large_count":   scenario_large_count,
    "weight_goal":   scenario_weight_goal,
}

#: Scenarios supported by all three implementations
SCENARIOS_ALL = list(SCENARIO_FNS.keys())
#: Scenarios where C writer can fully participate (now includes source_info)
SCENARIOS_C = SCENARIOS_ALL

# ---------------------------------------------------------------------------
# Helpers: canonical JSON dump
# ---------------------------------------------------------------------------

def _scope_to_dict(scope) -> dict:
    """Recursively convert a Python MemScope to the canonical dict."""
    children = [_scope_to_dict(cs) for cs in scope.scopes(ScopeTypeT.ALL)]
    items = []
    for ci in scope.coverItems(CoverTypeT.ALL):
        d = ci.getCoverData()
        items.append({
            "atLeast":   int(d.at_least),
            "count":     int(d.data),
            "coverType": int(d.type),
            "name":      ci.getName(),
        })
    weight = scope.getWeight() if hasattr(scope, 'getWeight') else 1
    goal   = scope.getGoal()   if hasattr(scope, 'getGoal')   else -1
    src_file  = None
    src_line  = 0
    src_token = 0
    if hasattr(scope, 'getSourceInfo'):
        si = scope.getSourceInfo()
        if si is not None and si.file is not None:
            src_file  = si.file.getFileName()
            src_line  = int(si.line)  if si.line  is not None else 0
            src_token = int(si.token) if si.token is not None else 0
    crossed_points = None
    if int(scope.getScopeType()) == int(ScopeTypeT.CROSS) and hasattr(scope, 'getNumCrossedCoverpoints'):
        n = scope.getNumCrossedCoverpoints()
        if n > 0:
            crossed_points = [scope.getIthCrossedCoverpoint(i).getScopeName() for i in range(n)]
    return {
        "children": children,
        "goal":        int(goal)   if goal   is not None else -1,
        "items":       items,
        "name":        scope.getScopeName(),
        "source_file":  src_file,
        "source_line":  src_line,
        "source_token": src_token,
        "type":        int(scope.getScopeType()),
        "weight":      int(weight) if weight is not None else 1,
        "crossed_points": crossed_points,
    }


_KIND_MAP = {
    int(HistoryNodeKind.TEST):  "TEST",
    int(HistoryNodeKind.MERGE): "MERGE",
}


def db_to_dict(db: MemUCIS) -> dict:
    """Serialize a loaded MemUCIS to the canonical JSON dict."""
    scopes = [_scope_to_dict(s) for s in db.scopes(ScopeTypeT.ALL)]
    history = []
    for node in db.historyNodes(HistoryNodeKind.ALL):
        history.append({
            "kind":          _KIND_MAP.get(int(node.getKind()), "TEST"),
            "name":          node.getLogicalName(),
            "user_name":     node.getUserName(),
            "seed":          node.getSeed(),
            "tool_category": node.getToolCategory(),
            "comment":       node.getComment(),
        })
    return {"format": "ncdb-dump-v1", "history": history, "scopes": scopes}

# ---------------------------------------------------------------------------
# Session fixtures
# ---------------------------------------------------------------------------

@pytest.fixture(scope="session")
def ts_dump_path():
    """Absolute path to ts_dump.mjs; skips if ts/dist not built."""
    dist = REPO / "ts" / "dist"
    if not dist.exists():
        pytest.skip("TypeScript not built — run: cd ts && npm run build")
    return str(TS_DUMP)


@pytest.fixture(scope="session")
def c_dump_path():
    """Path to compiled c_dump binary; auto-builds if libncdb.so is present."""
    libncdb = REPO / "c" / "build" / "libncdb.so"
    if not libncdb.exists():
        pytest.skip("C library not built — run: cmake -S c -B c/build && cmake --build c/build")
    if not C_DUMP_BIN.exists() or C_DUMP_BIN.stat().st_mtime < C_DUMP_SRC.stat().st_mtime:
        result = subprocess.run(
            ["make", "-C", str(C_DUMP_BIN.parent)],
            capture_output=True, text=True,
        )
        if result.returncode != 0:
            pytest.fail(f"c_dump build failed:\n{result.stderr}")
    return str(C_DUMP_BIN)

# ---------------------------------------------------------------------------
# Helpers class
# ---------------------------------------------------------------------------

class Helpers:
    def __init__(self, ts_dump, c_dump, tmp_path):
        self._ts = ts_dump
        self._c  = c_dump
        self._tmp = tmp_path

    # --- Write ---

    def write_python(self, scenario: str, dest: Path) -> Path:
        fn = SCENARIO_FNS[scenario]
        db = MemUCIS()
        fn(db)
        NcdbWriter().write(db, str(dest))
        return dest

    def write_ts(self, scenario: str, dest: Path) -> Path:
        result = subprocess.run(
            ["node", self._ts, "write", scenario, str(dest)],
            capture_output=True, text=True,
        )
        if result.returncode != 0:
            raise RuntimeError(f"ts_dump write failed:\n{result.stderr}")
        return dest

    def write_c(self, scenario: str, dest: Path) -> Path:
        result = subprocess.run(
            [self._c, "write", scenario, str(dest)],
            capture_output=True, text=True,
        )
        if result.returncode != 0:
            raise RuntimeError(f"c_dump write failed:\n{result.stderr}")
        return dest

    # --- Read → canonical dict ---

    def read_python(self, cdb: Path) -> dict:
        db = NcdbReader().read(str(cdb))
        return db_to_dict(db)

    def read_ts(self, cdb: Path) -> dict:
        result = subprocess.run(
            ["node", self._ts, "read", str(cdb)],
            capture_output=True, text=True,
        )
        if result.returncode != 0:
            raise RuntimeError(f"ts_dump read failed:\n{result.stderr}")
        return json.loads(result.stdout)

    def read_c(self, cdb: Path) -> dict:
        result = subprocess.run(
            [self._c, "read", str(cdb)],
            capture_output=True, text=True,
        )
        if result.returncode != 0:
            raise RuntimeError(f"c_dump read failed:\n{result.stderr}")
        return json.loads(result.stdout)

    # --- Convenience: write with impl name ---

    def write(self, impl: str, scenario: str, dest: Path) -> Path:
        if impl == "python":
            return self.write_python(scenario, dest)
        elif impl == "typescript":
            return self.write_ts(scenario, dest)
        elif impl == "c":
            return self.write_c(scenario, dest)
        raise ValueError(f"Unknown impl: {impl}")

    def read(self, impl: str, cdb: Path) -> dict:
        if impl == "python":
            return self.read_python(cdb)
        elif impl == "typescript":
            return self.read_ts(cdb)
        elif impl == "c":
            return self.read_c(cdb)
        raise ValueError(f"Unknown impl: {impl}")


@pytest.fixture
def helpers(ts_dump_path, c_dump_path, tmp_path):
    return Helpers(ts_dump_path, c_dump_path, tmp_path)
