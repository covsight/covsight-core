"""Cross-subsystem integration tests."""
import tempfile
import os
from covsight.core.mem import MemFactory
from covsight.core.api import (
    UCIS, ScopeTypeT, SourceT, CoverData, CoverTypeT,
    CoverFlagsT, FlagsT, HistoryNodeKind, TestStatusT, SourceInfo,
)
from covsight.core.api.test_data import TestData
from covsight.core.merge import DbMerger
from covsight.core.visitors import traverse, UCISVisitor


def _make_db_with_instance(cg_name="cg", bin_count=5):
    """Create a DB with proper DU+instance hierarchy and a covergroup."""
    db = MemFactory.create()
    fh = db.createFileHandle("tb.sv", "/tb")
    si = SourceInfo(fh, 1, 0)
    du = db.createScope(
        "work.top", si, 1, SourceT.SV, ScopeTypeT.DU_MODULE,
        FlagsT.INST_ONCE | FlagsT.SCOPE_UNDER_DU,
    )
    inst = db.createInstance(
        "top", si, 1, SourceT.SV, ScopeTypeT.INSTANCE, du,
        FlagsT.INST_ONCE,
    )
    cg = inst.createCovergroup(cg_name, si, 1, SourceT.SV)
    cp = cg.createCoverpoint("cp", si, 1, SourceT.SV)
    for i in range(bin_count):
        cd = CoverData(type=CoverTypeT.CVGBIN, flags=CoverFlagsT.IS_32BIT)
        cd.data = i
        cp.createNextCover(f"bin_{i}", cd, None)
    return db


def _make_simple_db(cg_name="cg", bin_count=5):
    """Create a flat DB with a covergroup directly at root (for NCDB round-trip)."""
    db = MemFactory.create()
    cg = db.createScope(cg_name, None, 1, SourceT.SV, ScopeTypeT.COVERGROUP, 0)
    cp = cg.createScope("cp", None, 1, SourceT.SV, ScopeTypeT.COVERPOINT, 0)
    for i in range(bin_count):
        cd = CoverData(type=CoverTypeT.CVGBIN, flags=CoverFlagsT.IS_32BIT)
        cd.data = i
        cp.createNextCover(f"bin_{i}", cd, None)
    return db


def test_merge_two_databases():
    db1 = _make_db_with_instance("cg_a", 3)
    db2 = _make_db_with_instance("cg_a", 3)
    dst = MemFactory.create()
    merger = DbMerger()
    merger.merge(dst, [db1, db2])
    # After merge, check for a merged instance scope
    instances = list(dst.scopes(ScopeTypeT.INSTANCE))
    assert len(instances) >= 1


def test_visitor_traversal():
    db = _make_db_with_instance()
    visited = []

    class V(UCISVisitor):
        def visit_covergroup(self, cg):
            visited.append(cg.getScopeName())

    traverse(db, V())
    assert len(visited) > 0


def test_ncdb_roundtrip():
    from covsight.core.ncdb.ncdb_writer import NcdbWriter
    from covsight.core.ncdb.ncdb_reader import NcdbReader

    db = _make_simple_db("cg_rt", 4)
    with tempfile.TemporaryDirectory() as td:
        path = os.path.join(td, "test.cdb")
        NcdbWriter().write(db, path)
        db2 = NcdbReader().read(path)

        cgs = list(db2.scopes(ScopeTypeT.COVERGROUP))
        assert len(cgs) == 1
        assert cgs[0].getScopeName() == "cg_rt"
