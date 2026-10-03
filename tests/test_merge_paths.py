"""Every way a database reaches disk or a merge keeps the same data.

A coverage database can be written to NCDB and read back, merged in memory
(``DbMerger``) or merged as NCDB files (``NcdbMerger``, whose cross-schema
fallback is ``DbMerger`` again).  Each path once lost something the others
kept: ``DbMerger`` dropped expression scopes, default bins, item attributes
and bin source locations; NCDB lost per-bin cover types (ignore, illegal and
default bins read back as ordinary bins), item goals and instance-to-DU links.

The check is a full snapshot -- every scope and cover item with its type,
counts, thresholds, source location, attributes and DU -- compared across
paths, not per-kind totals, which hid most of those losses.
"""
import collections
import io
import warnings
import zipfile

import pytest

from covsight.core.api import (
    CoverData, CoverTypeT, FlagsT, ScopeTypeT, SourceInfo, SourceT,
)
from covsight.core.mem import MemFactory
from covsight.core.merge import DbMerger
from covsight.core.ncdb.constants import MEMBER_COVERITEM_TYPES
from covsight.core.ncdb.ncdb_merger import NcdbMerger
from covsight.core.ncdb.ncdb_reader import NcdbReader
from covsight.core.ncdb.ncdb_writer import NcdbWriter
from covsight.core.vltcov import read_vlt_coverage

_ALL = 0xFFFFFFFFFFFFFFFF


# -- snapshot -----------------------------------------------------------------

def _src(obj):
    si = obj.getSourceInfo()
    if si is None or si.file is None or si.line is None or si.line < 0:
        return None
    return (si.file.getFileName(), si.line, si.token)


def _attrs(obj):
    return tuple(sorted(obj.getAttributes().items())) if hasattr(obj, "getAttributes") else ()


def snapshot(db):
    """``{path: scope record}`` and ``{(path, item name, n): item record}``."""
    scopes, items = {}, {}

    def walk(parent, path):
        seen = collections.Counter()
        for s in parent.scopes(ScopeTypeT.ALL):
            key = (s.getScopeType().name, s.getScopeName())
            seen[key] += 1
            p = "%s/%s:%s#%d" % (path, key[0], key[1], seen[key])
            du = s.getInstanceDu() if hasattr(s, "getInstanceDu") else None
            scopes[p] = (_src(s), _attrs(s),
                         du.getScopeName() if du is not None else None)
            n = collections.Counter()
            for ci in s.coverItems(_ALL):
                cd = ci.getCoverData()
                n[ci.getName()] += 1
                items[(p, ci.getName(), n[ci.getName()])] = (
                    CoverTypeT(cd.type).name, cd.data, cd.at_least, cd.goal,
                    _src(ci), _attrs(ci))
            walk(s, p)

    walk(db, "")
    return scopes, items


def assert_same(a, b):
    (sa, ia), (sb, ib) = a, b
    assert sorted(sa) == sorted(sb), "scopes differ: -%s +%s" % (
        sorted(set(sa) - set(sb))[:5], sorted(set(sb) - set(sa))[:5])
    bad = [(p, sa[p], sb[p]) for p in sa if sa[p] != sb[p]]
    assert not bad, bad[:5]
    assert sorted(ia) == sorted(ib), "items differ: -%s +%s" % (
        sorted(set(ia) - set(ib))[:5], sorted(set(ib) - set(ia))[:5])
    bad = [(k, ia[k], ib[k]) for k in ia if ia[k] != ib[k]]
    assert not bad, bad[:5]


# -- inputs -------------------------------------------------------------------

def mixed_db(scale=1):
    """What the Verilator fixtures lack: ignore and illegal bins beside
    ordinary ones, non-default at_least and goal, condition coverage, and an
    instance whose DU name differs from its own."""
    db = MemFactory.create()
    fh = db.createFileHandle("fifo.sv", "/rtl")
    du_top = db.createScope("top", SourceInfo(fh, 1, 0), 1, SourceT.SV,
                            ScopeTypeT.DU_MODULE, FlagsT.ENABLED_STMT)
    du_fifo = db.createScope("fifo", SourceInfo(fh, 20, 0), 1, SourceT.SV,
                             ScopeTypeT.DU_MODULE, FlagsT.ENABLED_STMT)
    top = db.createInstance("top", None, 1, SourceT.SV, ScopeTypeT.INSTANCE,
                            du_top, FlagsT.INST_ONCE)
    u = top.createInstance("u_fifo", None, 1, SourceT.SV, ScopeTypeT.INSTANCE,
                           du_fifo, FlagsT.INST_ONCE)

    cond = u.createScope("fifo.sv:25:9", SourceInfo(fh, 25, 9), 1, SourceT.SV,
                         ScopeTypeT.COND, FlagsT.ENABLED_COND)
    for name, count in (("a=0", 3), ("a=1", 0)):
        cd = CoverData(CoverTypeT.CONDBIN, 0)
        cd.data, cd.goal, cd.at_least = count * scale, 1, 1
        ci = cond.createNextCover(name, cd, SourceInfo(fh, 25, 9))
        ci.setAttribute("origin", "synthetic")

    cg = u.createCovergroup("cg", SourceInfo(fh, 30, 0), 1, SourceT.SV)
    cp = cg.createCoverpoint("cp_lvl", SourceInfo(fh, 31, 0), 1, SourceT.SV)
    for name, count, at_least, kind in (
            ("empty", 4, 1, CoverTypeT.CVGBIN),
            ("full", 0, 2, CoverTypeT.CVGBIN),
            ("skip", 7, 1, CoverTypeT.IGNOREBIN),
            ("overflow", 1, 1, CoverTypeT.ILLEGALBIN),
            ("other", 2, 1, CoverTypeT.DEFAULTBIN)):
        cp.createBin(name, SourceInfo(fh, 32, 0), at_least, count * scale, name, kind)
    return db


def vlt_db(testdata_dir, name):
    return read_vlt_coverage(str(testdata_dir / "vltcov" / name))


INPUTS = ["cov_top.dat", "hier_top.dat", "hier_top_pi.dat", "mixed"]


@pytest.fixture(params=INPUTS)
def make_db(request, testdata_dir):
    if request.param == "mixed":
        return mixed_db
    return lambda: vlt_db(testdata_dir, request.param)


def write(db, path):
    NcdbWriter().write(db, str(path))
    return str(path)


def merged_in_memory(dbs):
    out = MemFactory.create()
    DbMerger().merge(out, dbs)
    return out


def merged_ncdb(paths, out):
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        NcdbMerger().merge(paths, str(out))
    return NcdbReader().read(str(out))


# -- tests --------------------------------------------------------------------

def test_single_input_every_path_identical(make_db, tmp_path):
    ref = snapshot(make_db())
    p = write(make_db(), tmp_path / "a.cdb")
    assert_same(ref, snapshot(NcdbReader().read(p)))
    assert_same(ref, snapshot(merged_in_memory([make_db()])))
    assert_same(ref, snapshot(merged_ncdb([p], tmp_path / "m.cdb")))


def _summed(*snaps):
    want = collections.Counter()
    for _, items in snaps:
        for k, rec in items.items():
            want[k] += rec[1]
    return want


@pytest.mark.parametrize("pair", [
    ("mixed", "mixed"),                         # same schema: fast NCDB merge
    ("cov_top.dat", "hier_top.dat"),            # different designs: fallback
    ("hier_top.dat", "mixed"),
])
def test_two_inputs_paths_agree_and_counts_sum(pair, testdata_dir, tmp_path):
    def mk(name, scale):
        return mixed_db(scale) if name == "mixed" else vlt_db(testdata_dir, name)

    a, b = mk(pair[0], 1), mk(pair[1], 3)
    mem = snapshot(merged_in_memory([mk(pair[0], 1), mk(pair[1], 3)]))
    pa = write(mk(pair[0], 1), tmp_path / "a.cdb")
    pb = write(mk(pair[1], 3), tmp_path / "b.cdb")
    assert_same(mem, snapshot(merged_ncdb([pa, pb], tmp_path / "m.cdb")))

    # Matching items (same path) are summed; others carried over.
    got = collections.Counter({k: rec[1] for k, rec in mem[1].items()})
    assert got == _summed(snapshot(a), snapshot(b))


def test_ncdb_keeps_bin_types(tmp_path):
    db = NcdbReader().read(write(mixed_db(), tmp_path / "a.cdb"))
    (cp,) = [s for s in _walk(db) if s.getScopeType() == ScopeTypeT.COVERPOINT]
    got = {ci.getName(): (CoverTypeT(ci.getCoverData().type), ci.getCoverData().at_least)
           for ci in cp.coverItems(_ALL)}
    assert got == {"empty": (CoverTypeT.CVGBIN, 1), "full": (CoverTypeT.CVGBIN, 2),
                   "skip": (CoverTypeT.IGNOREBIN, 1),
                   "overflow": (CoverTypeT.ILLEGALBIN, 1),
                   "other": (CoverTypeT.DEFAULTBIN, 1)}
    assert [ci.getName() for ci in cp.coverItems(CoverTypeT.CVGBIN)] == ["empty", "full"]


def test_ncdb_keeps_instance_du(tmp_path):
    db = NcdbReader().read(write(mixed_db(), tmp_path / "a.cdb"))
    du = {s.getScopeName(): s.getInstanceDu() for s in _walk(db)
          if s.getScopeType() == ScopeTypeT.INSTANCE}
    assert du["u_fifo"].getScopeName() == "fifo"
    assert du["u_fifo"] in list(db.scopes(ScopeTypeT.ALL))    # the real DU


def test_types_member_is_run_length_encoded(testdata_dir, tmp_path):
    """Uniform scopes cost about one run each, not one entry per item."""
    from covsight.core.ncdb.varint import decode_varint
    db = vlt_db(testdata_dir, "hier_top.dat")
    p = write(db, tmp_path / "a.cdb")
    with zipfile.ZipFile(p) as zf:
        data = zf.read(MEMBER_COVERITEM_TYPES)
    _, off = decode_varint(data, 0)
    runs, _ = decode_varint(data, off)
    n_scopes = sum(1 for s in _walk(db) if list(s.coverItems(_ALL)))
    n_items = sum(len(list(s.coverItems(_ALL))) for s in _walk(db))
    assert runs <= n_scopes < n_items


def test_file_without_types_member_reads_as_before(tmp_path):
    """Old files (and old writers) have no coveritem_types.bin: every item
    takes its scope's first item's type, as readers always did."""
    p = write(mixed_db(), tmp_path / "a.cdb")
    old = tmp_path / "old.cdb"
    with zipfile.ZipFile(p) as src, zipfile.ZipFile(old, "w") as dst:
        for info in src.infolist():
            if info.filename != MEMBER_COVERITEM_TYPES:
                dst.writestr(info, src.read(info.filename))
    db = NcdbReader().read(str(old))
    (cp,) = [s for s in _walk(db) if s.getScopeType() == ScopeTypeT.COVERPOINT]
    assert {CoverTypeT(ci.getCoverData().type) for ci in cp.coverItems(_ALL)} == {
        CoverTypeT.CVGBIN}


def _walk(parent):
    for s in parent.scopes(ScopeTypeT.ALL):
        yield s
        yield from _walk(s)
