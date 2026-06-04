"""
Manifest round-trip and validation tests.
"""

import hashlib
import tempfile
import os

import pytest

from covsight.core.mem.mem_ucis import MemUCIS
from covsight.core.api import SourceT
from covsight.core.api import ScopeTypeT
from covsight.core.api import CoverData
from covsight.core.api import CoverTypeT
from covsight.core.api import HistoryNodeKind

from covsight.core.ncdb.manifest import Manifest
from covsight.core.ncdb.string_table import StringTable
from covsight.core.ncdb.scope_tree import ScopeTreeWriter
from covsight.core.ncdb.constants import NCDB_FORMAT


def _make_db():
    db = MemUCIS()
    db.createHistoryNode(None, "t1", None, HistoryNodeKind.TEST)
    blk = db.createScope("top", None, 1, SourceT.SV, ScopeTypeT.BLOCK, 0)
    cd = CoverData(CoverTypeT.STMTBIN, 0)
    cd.data = 5
    blk.createNextCover("s0", cd, None)
    return db


def _scope_tree_bytes(db):
    st = StringTable()
    writer = ScopeTreeWriter(st)
    return writer.write(db)


def test_manifest_format_field():
    db = _make_db()
    tree = _scope_tree_bytes(db)
    m = Manifest.build(db, tree, [], [])
    data = Manifest.from_bytes(m.serialize())
    assert data.format == NCDB_FORMAT


def test_manifest_schema_hash_sha256():
    db = _make_db()
    tree = _scope_tree_bytes(db)
    m = Manifest.build(db, tree, [], [])
    data = Manifest.from_bytes(m.serialize())
    expected = "sha256:" + hashlib.sha256(tree).hexdigest()
    assert data.schema_hash == expected


def test_manifest_schema_hash_deterministic():
    """Same DB → same schema hash."""
    db1 = _make_db()
    db2 = _make_db()
    tree1 = _scope_tree_bytes(db1)
    tree2 = _scope_tree_bytes(db2)
    m1 = Manifest.build(db1, tree1, [], [])
    m2 = Manifest.build(db2, tree2, [], [])
    assert m1.schema_hash == m2.schema_hash


def test_manifest_different_schema_different_hash():
    """DBs with different scope trees get different hashes."""
    db1 = _make_db()
    db2 = MemUCIS()
    db2.createScope("other", None, 1, SourceT.SV, ScopeTypeT.BLOCK, 0)
    tree1 = _scope_tree_bytes(db1)
    tree2 = _scope_tree_bytes(db2)
    m1 = Manifest.build(db1, tree1, [], [])
    m2 = Manifest.build(db2, tree2, [], [])
    assert m1.schema_hash != m2.schema_hash


def test_manifest_round_trip():
    """Manifest serializes and deserializes cleanly."""
    db = _make_db()
    tree = _scope_tree_bytes(db)
    m = Manifest.build(db, tree, [], [])
    data = m.serialize()
    m2 = Manifest.from_bytes(data)
    assert m2.format == m.format
    assert m2.schema_hash == m.schema_hash
    assert m2.path_separator == m.path_separator


def test_manifest_version_present():
    db = _make_db()
    tree = _scope_tree_bytes(db)
    m = Manifest.build(db, tree, [], [])
    data = Manifest.from_bytes(m.serialize())
    assert data.version


def test_manifest_path_separator_stored():
    db = MemUCIS()
    db.setPathSeparator(".")
    tree = _scope_tree_bytes(db)
    m = Manifest.build(db, tree, [], [])
    data = Manifest.from_bytes(m.serialize())
    assert data.path_separator == "."
