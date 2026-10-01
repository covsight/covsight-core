"""coveritem_sources.bin: per-coveritem source locations survive NCDB.

Line-level code coverage (per-file reports, LCOV) needs the file and line of
every statement, and those live on the cover items -- a BLOCK scope holds all
of an instance's statements.  scope_tree.bin records scope locations only.
"""
import zipfile

from covsight.core.api import CoverData, SourceInfo
from covsight.core.api.enums import CoverTypeT, FlagsT, ScopeTypeT, SourceT
from covsight.core.mem.mem_ucis import MemUCIS
from covsight.core.ncdb.constants import (
    MEMBER_COVERITEM_SOURCES, TOGGLE_BIN_0_TO_1, TOGGLE_BIN_1_TO_0,
)
from covsight.core.ncdb.ncdb_merger import NcdbMerger
from covsight.core.ncdb.ncdb_reader import NcdbReader
from covsight.core.ncdb.ncdb_ucis import NcdbUCIS
from covsight.core.ncdb.ncdb_writer import NcdbWriter


def _cover(scope, name, ct, count, src):
    cd = CoverData(ct, 0)
    cd.data = count
    scope.createNextCover(name, cd, src)


def build(counts=(3, 0, 5)):
    db = MemUCIS()
    a = db.createFileHandle("rtl/a.sv", ".")
    b = db.createFileHandle("rtl/b.sv", ".")
    du = db.createScope("m", None, 1, SourceT.SV, ScopeTypeT.DU_MODULE, 0)
    inst = db.createInstance("top", SourceInfo(a, 1, 0), 1, SourceT.SV,
                             ScopeTypeT.INSTANCE, du, FlagsT.INST_ONCE)
    blk = inst.createScope("block", None, 1, SourceT.SV, ScopeTypeT.BLOCK, 0)
    _cover(blk, "s0", CoverTypeT.STMTBIN, counts[0], SourceInfo(a, 10, 2))
    _cover(blk, "s1", CoverTypeT.STMTBIN, counts[1], None)
    # A folded toggle pair sits between items with locations: it occupies
    # two DFS slots that must not shift the entries after it.
    tp = inst.createScope("t", None, 1, SourceT.SV, ScopeTypeT.BRANCH, 0)
    _cover(tp, TOGGLE_BIN_1_TO_0, CoverTypeT.TOGGLEBIN, 1, None)
    _cover(tp, TOGGLE_BIN_0_TO_1, CoverTypeT.TOGGLEBIN, 2, None)
    br = inst.createScope("b.sv:7", SourceInfo(b, 7, 0), 1, SourceT.SV,
                          ScopeTypeT.BRANCH, 0)
    _cover(br, "if", CoverTypeT.BRANCHBIN, counts[2], SourceInfo(b, 7, 4))
    _cover(br, "else", CoverTypeT.BRANCHBIN, 1, SourceInfo(b, 9, 4))
    return db


def locations(db):
    out = {}

    def walk(scope, path):
        path = path + "/" + scope.getScopeName()
        for ci in scope.coverItems(CoverTypeT.ALL):
            si = ci.getSourceInfo()
            if si is not None and si.file is not None:
                out[path + "/" + ci.getName()] = (
                    si.file.getFileName(), si.line, si.token)
        for child in scope.scopes(ScopeTypeT.ALL):
            walk(child, path)

    for s in db.scopes(ScopeTypeT.ALL):
        walk(s, "")
    return out


EXPECTED = {
    "/top/block/s0": ("rtl/a.sv", 10, 2),
    "/top/b.sv:7/if": ("rtl/b.sv", 7, 4),
    "/top/b.sv:7/else": ("rtl/b.sv", 9, 4),
}


def test_roundtrip(tmp_path):
    path = str(tmp_path / "a.cdb")
    NcdbWriter().write(build(), path)
    assert locations(NcdbReader().read(path)) == EXPECTED
    assert locations(NcdbUCIS(path)) == EXPECTED


def test_member_omitted_without_locations(tmp_path):
    db = MemUCIS()
    du = db.createScope("m", None, 1, SourceT.SV, ScopeTypeT.DU_MODULE, 0)
    inst = db.createInstance("top", None, 1, SourceT.SV, ScopeTypeT.INSTANCE,
                             du, FlagsT.INST_ONCE)
    blk = inst.createScope("block", None, 1, SourceT.SV, ScopeTypeT.BLOCK, 0)
    _cover(blk, "s0", CoverTypeT.STMTBIN, 1, None)
    path = str(tmp_path / "a.cdb")
    NcdbWriter().write(db, path)
    assert MEMBER_COVERITEM_SOURCES not in zipfile.ZipFile(path).namelist()


def test_survives_merge(tmp_path):
    a, b, out = (str(tmp_path / n) for n in ("a.cdb", "b.cdb", "m.cdb"))
    NcdbWriter().write(build((1, 0, 0)), a)
    NcdbWriter().write(build((0, 1, 1)), b)
    NcdbMerger().merge([a, b], out)
    db = NcdbReader().read(out)
    assert locations(db) == EXPECTED
