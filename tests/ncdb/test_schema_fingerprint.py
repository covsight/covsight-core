"""Schema hash / fingerprint — bin-space identity.

The association members (UCIS §8.17) stamp a u64 fingerprint of the bin space
they were built against, so that a signature table can never be paired with a
database whose bins mean something else.  These tests pin the two properties
that fingerprint depends on:

  1. The hash distinguishes designs that differ **in name only**.  This is the
     non-obvious one: ``scope_tree.bin`` stores string-table *references*, so
     shape-identical designs with entirely different names serialize to
     identical bytes.
  2. The hash does *not* change between runs of the same design, or the
     merger's same-schema fast path would never be taken.
"""

import tempfile
import os
import pytest

from covsight.core.mem.mem_ucis import MemUCIS
from covsight.core.api import (SourceT, ScopeTypeT, CoverData, CoverTypeT,
                               HistoryNodeKind)
from covsight.core.ncdb.manifest import Manifest
from covsight.core.ncdb.string_table import StringTable
from covsight.core.ncdb.scope_tree import ScopeTreeWriter
from covsight.core.ncdb.ncdb_writer import NcdbWriter
from covsight.core.ncdb.ncdb_reader import NcdbReader
from covsight.core.ncdb.ncdb_merger import NcdbMerger


def _make_db(scope_name="top", bin_names=("b0", "b1"), value=1):
    db = MemUCIS()
    db.createHistoryNode(None, "t1", None, HistoryNodeKind.TEST)
    blk = db.createScope(scope_name, None, 1, SourceT.SV, ScopeTypeT.BLOCK, 0)
    for name in bin_names:
        cd = CoverData(CoverTypeT.STMTBIN, 0)
        cd.data = value
        blk.createNextCover(name, cd, None)
    return db


def _members(db):
    """Return (scope_tree_bytes, strings_bytes) exactly as the writer builds them."""
    st = StringTable()
    tree = ScopeTreeWriter(st).write(db)
    return tree, st.serialize()


def _hash(db):
    tree, strings = _members(db)
    return Manifest.compute_schema_hash(tree, strings)


# -- the property the whole fingerprint rests on ---------------------------

def test_scope_tree_alone_cannot_distinguish_names():
    """Documents *why* strings.bin is folded in.

    If this ever starts failing, ``scope_tree.bin`` grew name content and the
    v2 construction could be simplified.  Until then it is the reason v1 was
    unsound.
    """
    tree_a, _ = _members(_make_db("alpha", ("bin_a", "bin_b")))
    tree_b, _ = _members(_make_db("beta", ("bin_x", "bin_y")))
    assert tree_a == tree_b


def test_schema_hash_distinguishes_names():
    """Same shape, different names ⇒ different schema hash."""
    assert _hash(_make_db("alpha", ("bin_a", "bin_b"))) != \
           _hash(_make_db("beta", ("bin_x", "bin_y")))


def test_schema_hash_distinguishes_bin_names_only():
    """Even a single renamed bin is a different bin space."""
    assert _hash(_make_db("top", ("b0", "b1"))) != \
           _hash(_make_db("top", ("b0", "RENAMED")))


def test_schema_hash_stable_across_runs():
    """Counts are per-run data, not schema — the fast path must survive them."""
    assert _hash(_make_db(value=1)) == _hash(_make_db(value=999))


def test_schema_hash_distinguishes_shape():
    assert _hash(_make_db(bin_names=("b0", "b1"))) != \
           _hash(_make_db(bin_names=("b0", "b1", "b2")))


def test_v1_and_v2_hashes_never_compare_equal():
    tree, strings = _members(_make_db())
    v1 = Manifest.compute_schema_hash(tree)
    v2 = Manifest.compute_schema_hash(tree, strings)
    assert v1 != v2
    assert v1.startswith("sha256:")
    assert v2.startswith("sha256v2:")


def test_length_prefix_prevents_concatenation_ambiguity():
    """(tree, strings) split must matter, not just the concatenation."""
    a = Manifest.compute_schema_hash(b"AB", b"CD")
    b = Manifest.compute_schema_hash(b"ABC", b"D")
    assert a != b


# -- the u64 fold ----------------------------------------------------------

def test_fingerprint_is_first_8_bytes_big_endian():
    digest = "0011223344556677" + "8899aabbccddeeff" * 3
    assert Manifest.schema_fingerprint("sha256v2:" + digest) == 0x0011223344556677
    assert Manifest.schema_fingerprint("sha256:" + digest) == 0x0011223344556677


def test_fingerprint_fits_u64():
    fp = Manifest.schema_fingerprint(_hash(_make_db()))
    assert 0 <= fp < 2 ** 64


@pytest.mark.parametrize("bad", ["", "md5:abcd", "sha256v2:short", "nonsense"])
def test_fingerprint_unknown_is_zero(bad):
    """0 means 'unknown' and readers must never treat it as a match."""
    assert Manifest.schema_fingerprint(bad) == 0


def test_fingerprint_tracks_the_hash():
    a = Manifest.schema_fingerprint(_hash(_make_db("alpha", ("bin_a", "bin_b"))))
    b = Manifest.schema_fingerprint(_hash(_make_db("beta", ("bin_x", "bin_y"))))
    assert a != b


# -- end-to-end: the merge defect this closes ------------------------------

def test_unrelated_designs_do_not_fast_merge(tmp_path):
    """Regression: shape-identical, differently-named designs merged silently.

    Before the v2 hash, merging these two took the same-schema fast path: it
    added ``beta``'s counts into ``alpha``'s bins and reported the total under
    ``alpha``'s names, with no warning.  The merged database claimed coverage
    for bins that were never exercised.
    """
    a = str(tmp_path / "a.cdb")
    b = str(tmp_path / "b.cdb")
    NcdbWriter().write(_make_db("alpha", ("bin_a", "bin_b"), 3), a)
    NcdbWriter().write(_make_db("beta", ("bin_x", "bin_y"), 5), b)

    with pytest.raises(Exception):
        NcdbMerger().merge([a, b], str(tmp_path / "out.cdb"),
                           allow_cross_schema=False)


def _read_schema_hash(path):
    import zipfile
    from covsight.core.ncdb.constants import MEMBER_MANIFEST
    with zipfile.ZipFile(path) as zf:
        return Manifest.from_bytes(zf.read(MEMBER_MANIFEST)).schema_hash


def _downgrade_to_v1_hash(path):
    """Rewrite *path*'s manifest with a v1 (scope_tree-only) schema hash.

    Stands in for a database written before the v2 hash, or by an
    implementation that still emits v1 -- both of which the merger must not
    take at face value.
    """
    import zipfile
    from covsight.core.ncdb.constants import MEMBER_MANIFEST

    with zipfile.ZipFile(path, "r") as zf:
        members = {n: zf.read(n) for n in zf.namelist()}
    m = Manifest.from_bytes(members[MEMBER_MANIFEST])
    tree = members[[n for n in members if "scope_tree" in n][0]]
    m.schema_hash = Manifest.compute_schema_hash(tree)
    members[MEMBER_MANIFEST] = m.serialize()
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for name, payload in members.items():
            zf.writestr(name, payload)


def test_legacy_v1_hash_does_not_authorize_fast_merge(tmp_path):
    """The v1 blind spot must not survive in databases already on disk.

    Two unrelated designs both carrying v1 hashes hash identically.  The merger
    has to verify their names directly rather than trust the match.
    """
    a = str(tmp_path / "a.cdb")
    b = str(tmp_path / "b.cdb")
    NcdbWriter().write(_make_db("alpha", ("bin_a", "bin_b"), 3), a)
    NcdbWriter().write(_make_db("beta", ("bin_x", "bin_y"), 5), b)
    _downgrade_to_v1_hash(a)
    _downgrade_to_v1_hash(b)

    # Precondition: the v1 hashes really do collide, so it is the guard that
    # rejects this merge -- not a hash difference.
    assert _read_schema_hash(a) == _read_schema_hash(b)

    with pytest.raises(Exception):
        NcdbMerger().merge([a, b], str(tmp_path / "out.cdb"),
                           allow_cross_schema=False)


def test_legacy_v1_hash_still_fast_merges_same_design(tmp_path):
    """Verification must not cost legacy files their fast path."""
    a = str(tmp_path / "a.cdb")
    b = str(tmp_path / "b.cdb")
    out = str(tmp_path / "out.cdb")
    NcdbWriter().write(_make_db("top", ("b0", "b1"), 3), a)
    NcdbWriter().write(_make_db("top", ("b0", "b1"), 7), b)
    _downgrade_to_v1_hash(a)
    _downgrade_to_v1_hash(b)

    NcdbMerger().merge([a, b], out, allow_cross_schema=False)

    db = NcdbReader().read(out)
    got = {(s.getScopeName(), ci.getName()): ci.getCoverData().data
           for s in db.scopes(ScopeTypeT.ALL)
           for ci in s.coverItems(CoverTypeT.ALL)}
    assert got == {("top", "b0"): 10, ("top", "b1"): 10}


def test_same_design_still_fast_merges(tmp_path):
    """The fix must not push genuine same-schema merges onto the slow path."""
    a = str(tmp_path / "a.cdb")
    b = str(tmp_path / "b.cdb")
    out = str(tmp_path / "out.cdb")
    NcdbWriter().write(_make_db("top", ("b0", "b1"), 3), a)
    NcdbWriter().write(_make_db("top", ("b0", "b1"), 7), b)

    # allow_cross_schema=False proves the *fast* path was taken.
    NcdbMerger().merge([a, b], out, allow_cross_schema=False)

    db = NcdbReader().read(out)
    got = {(s.getScopeName(), ci.getName()): ci.getCoverData().data
           for s in db.scopes(ScopeTypeT.ALL)
           for ci in s.coverItems(CoverTypeT.ALL)}
    assert got == {("top", "b0"): 10, ("top", "b1"): 10}
