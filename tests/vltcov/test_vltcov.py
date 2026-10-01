"""Verilator coverage.dat import (format 'vltcov').

The fixtures in testdata/vltcov were produced by Verilator 5.052 (see
regen.sh).  The per-kind covered/total counts printed by
``verilator_coverage --report summary`` serve as an independent oracle for
the import: every point must land somewhere, and once in the database the
same counts must be recoverable.
"""
import re

import pytest

from covsight.core.api.enums import CoverTypeT, HistoryNodeKind, ScopeTypeT, TestStatusT
from covsight.core.conversion import apply_test_info
from covsight.core.ext.registry import FormatRegistry
from covsight.core.ncdb.ncdb_reader import NcdbReader
from covsight.core.ncdb.ncdb_writer import NcdbWriter
from covsight.core.vltcov import (
    VltParser, VltToUcis, decode_keys, is_vlt_coverage, read_vlt_coverage,
)

# Summary kind -> cover types that hold it
_KIND_COVER = {
    "line": (CoverTypeT.STMTBIN,),
    "branch": (CoverTypeT.BRANCHBIN,),
    "expr": (CoverTypeT.EXPRBIN,),
    "toggle": (CoverTypeT.TOGGLEBIN,),
    "user": (CoverTypeT.COVERBIN,),
    "covergroup": (CoverTypeT.CVGBIN, CoverTypeT.DEFAULTBIN,
                   CoverTypeT.IGNOREBIN, CoverTypeT.ILLEGALBIN),
}


@pytest.fixture
def vltdir(testdata_dir):
    return testdata_dir / "vltcov"


def read_summary(path):
    ret = {}
    for line in open(path):
        m = re.match(r"\s*(\w+)\s*:\s*[\d.]+%\s*\(\s*(\d+)/\s*(\d+)\)", line)
        if m:
            ret[m.group(1)] = (int(m.group(2)), int(m.group(3)))
    return {k: v for k, v in ret.items() if v[1]}


def tally(db):
    """(covered, total) per summary kind, from the database alone."""
    ret = {}

    def add(kind, count):
        cov, tot = ret.get(kind, (0, 0))
        ret[kind] = (cov + (1 if count > 0 else 0), tot + 1)

    def walk(scope):
        st = scope.getScopeType()
        for ci in scope.coverItems(CoverTypeT.ALL):
            ct = ci.getCoverData().type
            if st == ScopeTypeT.FSM_STATES:
                add("fsm_state", ci.getCoverData().data)
            elif st == ScopeTypeT.FSM_TRANS:
                add("fsm_arc", ci.getCoverData().data)
            else:
                for kind, cts in _KIND_COVER.items():
                    if ct in cts:
                        add(kind, ci.getCoverData().data)
        for sub in scope.scopes(ScopeTypeT.ALL):
            walk(sub)

    for s in db.scopes(ScopeTypeT.ALL):
        walk(s)
    return ret


def find(db, path):
    """Scope by '/'-separated name path from the root.  Names may repeat
    across scope types (a signal's TOGGLE and FSM scopes), so ``x:FSM``
    selects by type."""
    scopes = list(db.scopes(ScopeTypeT.ALL))
    scope = None
    for name in path.split("/"):
        name, _, stype = name.partition(":") if name.endswith((":FSM", ":TOGGLE")) else (name, "", "")
        scope = next((s for s in scopes if s.getScopeName() == name
                      and not ScopeTypeT.DU_ANY(s.getScopeType())
                      and (not stype or s.getScopeType() == ScopeTypeT[stype])), None)
        assert scope is not None, "no scope %r in %s" % (name, path)
        scopes = list(scope.scopes(ScopeTypeT.ALL))
    return scope


def bins(scope):
    return {ci.getName(): ci.getCoverData().data
            for ci in scope.coverItems(CoverTypeT.ALL)}


# --------------------------------------------------------------------------
# Parser
# --------------------------------------------------------------------------
def test_decode_keys():
    assert decode_keys("\001f\002a.sv\001l\00212\001o\002x:0->1") == {
        "f": "a.sv", "l": "12", "o": "x:0->1"}


def test_parse_line_kinds():
    p = VltParser()
    it = p.parse_line("C '\001t\002covergroup\001page\002v_covergroup/cg"
                      "\001f\002a.sv\001l\0023\001bin\002b0\001h\002cg.cp.b0' 7")
    assert (it.kind, it.page_scope, it.count) == ("covergroup", "cg", 7)
    it = p.parse_line("C '\001page\002v_funccov/cg\001h\002cg.cp.b' 1")
    assert it.kind == "covergroup"          # legacy page name
    assert p.parse_line("garbage") is None


def test_parse_errors_recorded():
    p = VltParser()
    items = p.parse(["# SystemC::Coverage-3", "C '\001t\002line' 3", "bogus"])
    assert len(items) == 1 and p.errors == [(3, "bogus")]


def test_detect(vltdir, tmp_path):
    assert is_vlt_coverage(str(vltdir / "cov_top.dat"))
    other = tmp_path / "x.dat"
    other.write_text("not coverage")
    assert not is_vlt_coverage(str(other))


# --------------------------------------------------------------------------
# Oracle: verilator_coverage --report summary
# --------------------------------------------------------------------------
@pytest.mark.parametrize("name", ["cov_top", "hier_top"])
def test_summary_matches_verilator(vltdir, name):
    db = read_vlt_coverage(str(vltdir / ("%s.dat" % name)), include_std=True)
    assert tally(db) == read_summary(vltdir / ("%s.summary.txt" % name))


def test_per_instance_fsm_duplicates_folded(vltdir):
    # With --coverage-per-instance Verilator writes every FSM point once per
    # instance of the module (only the owner counts).  The import folds the
    # copies, so FSM totals are half the summary's; everything else matches.
    db = read_vlt_coverage(str(vltdir / "hier_top_pi.dat"), include_std=True)
    got, exp = tally(db), read_summary(vltdir / "hier_top_pi.summary.txt")
    for kind in ("fsm_state", "fsm_arc"):
        assert got[kind][1] * 2 == exp[kind][1]
        assert got.pop(kind)[0] == exp.pop(kind)[0]
    assert got == exp


@pytest.mark.parametrize("name", ["cov_top", "hier_top", "hier_top_pi"])
def test_ncdb_roundtrip(vltdir, tmp_path, name):
    db = read_vlt_coverage(str(vltdir / ("%s.dat" % name)), include_std=True)
    out = tmp_path / ("%s.cdb" % name)
    NcdbWriter().write(db, str(out))
    assert tally(NcdbReader().read(str(out))) == tally(db)


# --------------------------------------------------------------------------
# Structure
# --------------------------------------------------------------------------
def test_std_package_dropped_by_default(vltdir):
    db = read_vlt_coverage(str(vltdir / "cov_top.dat"))
    tops = [s.getScopeName() for s in db.scopes(ScopeTypeT.INSTANCE)]
    assert tops == ["cov_top"]
    items = VltParser().parse_file(str(vltdir / "cov_top.dat"))
    user_lines = [i for i in items if i.kind == "line" and not i.hier.startswith("std")]
    assert tally(db)["line"] == (sum(1 for i in user_lines if i.count), len(user_lines))


def test_cov_top_structure(vltdir):
    db = read_vlt_coverage(str(vltdir / "cov_top.dat"))
    cg = find(db, "cov_top/cg")
    assert cg.getScopeType() == ScopeTypeT.COVERGROUP
    assert bins(find(db, "cov_top/cg/cp_flag")) == {"hit_low": 8, "hit_high": 2}
    assert bins(find(db, "cov_top/cg/cp_count")) == {"lo": 8, "never": 0}
    assert bins(find(db, "cov_top/count:TOGGLE")) == {
        "0:0->1": 5, "0:1->0": 5, "1:0->1": 3, "1:1->0": 2,
        "2:0->1": 1, "2:1->0": 1, "3:0->1": 1, "3:1->0": 0}
    assert bins(find(db, "cov_top/cov_top.sv:17")) == {"if": 2, "else": 8}
    assert bins(find(db, "cov_top/state:FSM/UCIS:STATE")) == {"IDLE": 1, "BUSY": 1}
    assert bins(find(db, "cov_top/cover@cov_top.sv:42")) == {"coverBin": 1}
    inst = find(db, "cov_top")
    assert inst.getInstanceDu().getScopeName() == "cov_top"


def test_hier_aggregated(vltdir):
    db = read_vlt_coverage(str(vltdir / "hier_top.dat"))
    sub = find(db, "hier_top/u_*")
    assert sub.getInstanceDu().getScopeName() == "sub"
    # The covergroup belongs with the aggregated instance of its module.
    cg = find(db, "hier_top/u_*/sub_cg")
    cross = find(db, "hier_top/u_*/sub_cg/x_dq")
    assert cross.getScopeType() == ScopeTypeT.CROSS
    assert [cross.getIthCrossedCoverpoint(i).getScopeName() for i in range(cross.getNumCrossedCoverpoints())] == ["cp_d", "cp_q"]
    assert bins(find(db, "hier_top/u_*/sub_cg/cp_q")) == {"zero": 5, "other": 7}
    # FSMs come from Fv, so they keep real instance names even here.
    assert bins(find(db, "hier_top/u_b/st:FSM/UCIS:TRANSITION")) == {
        "S0->S1": 3, "S1->S0": 3, "S1->S2": 0, "S2->S0": 0}
    del cg


def test_hier_per_instance(vltdir):
    db = read_vlt_coverage(str(vltdir / "hier_top_pi.dat"))
    a = bins(find(db, "hier_top/u_a/q"))
    b = bins(find(db, "hier_top/u_b/q"))
    assert a and b and a != b
    # Type-level covergroup with two module instances: own top-level scope.
    assert find(db, "sub/sub_cg").getScopeType() == ScopeTypeT.COVERGROUP


def test_unknown_kind_warns():
    items = VltParser().parse([
        "C '\001t\002line\001page\002v_line/m\001f\002m.sv\001l\0021\001h\002m' 1",
        "C '\001t\002mystery\001page\002v_mystery/m\001h\002m' 1",
    ])
    m = VltToUcis()
    m.map(items)
    assert any("mystery" in w for w in m.warnings)


# --------------------------------------------------------------------------
# Registry and test info
# --------------------------------------------------------------------------
def test_registered_format(vltdir):
    desc = FormatRegistry().get_db_format("vltcov")
    db = desc.fmt_if.read(str(vltdir / "cov_top.dat"))
    assert "covergroup" in tally(db)


def test_apply_test_info(vltdir):
    db = read_vlt_coverage(str(vltdir / "cov_top.dat"))
    apply_test_info(db, name="smoke_1", seed=42, status="fail")
    (node,) = list(db.historyNodes(HistoryNodeKind.TEST))
    assert node.getLogicalName() == "smoke_1"
    assert node.getSeed() == "42"
    assert node.getTestStatus() == TestStatusT.ERROR


def test_line_points_keep_locations(vltdir, tmp_path):
    """Statement items carry their own file/line (and, for multi-line basic
    blocks, the full line set) through NCDB -- line-level reports need it."""
    path = str(tmp_path / "cov_top.cdb")
    NcdbWriter().write(read_vlt_coverage(str(vltdir / "cov_top.dat")), path)
    db = NcdbReader().read(path)
    (block,) = [s for s in find(db, "cov_top").scopes(ScopeTypeT.BLOCK)]
    items = {ci.getName(): ci for ci in block.coverItems(CoverTypeT.ALL)}
    si = items["cov_top.sv:44:3"].getSourceInfo()
    assert (si.file.getFileName(), si.line, si.token) == ("cov_top.sv", 44, 3)
    assert items["cov_top.sv:44:3"].getAttribute("lines") == "44,49-51"
    assert items["cov_top.sv:13:3"].getAttribute("lines") is None


def test_branch_arms_keep_body_lines(vltdir):
    db = read_vlt_coverage(str(vltdir / "cov_top.dat"))
    arms = {ci.getName(): ci for ci in find(db, "cov_top/cov_top.sv:17").coverItems(CoverTypeT.ALL)}
    assert arms["if"].getAttribute("lines") == "17-18"
    assert arms["else"].getAttribute("lines") == "20"
