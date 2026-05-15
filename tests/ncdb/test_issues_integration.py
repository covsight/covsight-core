"""Integration tests for issues round-trip through NcdbWriter + NcdbReader."""
import io
import os
import tempfile
import zipfile
import pytest

from covsight.core.ncdb.ncdb_writer import NcdbWriter
from covsight.core.ncdb.ncdb_reader import NcdbReader
from covsight.core.ncdb.ncdb_ucis import NcdbUCIS
from covsight.core.mem.mem_ucis import MemUCIS
from covsight.core.ncdb.issues import (
    IssueSet, IssueSpec,
    SEV_HIGH, SEV_LOW, SEV_MEDIUM,
    STATE_OPEN, STATE_RESOLVED,
    RES_FIXED,
    LINK_BLOCKED_BY, LINK_RELATED,
)
from covsight.core.ncdb.issues_meta import IssuesMeta
from covsight.core.ncdb.issues_history import IssueHistoryWriter

_GOLDEN_DIR = os.path.join(os.path.dirname(__file__), "..", "compat", "golden")
_PY_BASIC = os.path.join(_GOLDEN_DIR, "py_basic.cdb")


def _write_and_read(db, tmp_path: str) -> MemUCIS:
    """Write *db* to a temp file and read it back via NcdbReader."""
    path = tmp_path
    NcdbWriter().write(db, path)
    return NcdbReader().read(path)


def _make_minimal_db() -> MemUCIS:
    """Return a MemUCIS with no scopes (minimal valid NCDB)."""
    return MemUCIS()


def test_roundtrip_issues_only(tmp_path):
    """Write + read a .cdb with only issues.bin; issues present."""
    db = _make_minimal_db()
    issues = IssueSet()
    issues.add_issue(IssueSpec(id="I-001", severity=SEV_HIGH, state=STATE_OPEN))
    issues.add_issue(IssueSpec(id="I-002", severity=SEV_LOW,
                               state=STATE_RESOLVED, resolution=RES_FIXED))
    db._issues = issues
    db._issues_dirty = True

    out_path = str(tmp_path / "out.cdb")
    db2 = _write_and_read(db, out_path)

    assert hasattr(db2, '_issues') and db2._issues is not None
    ids = [h.id for h in db2._issues.issues()]
    assert "I-001" in ids
    assert "I-002" in ids


def test_roundtrip_all_three_members(tmp_path):
    """Write all three issue members; read back equals what was written."""
    db = _make_minimal_db()

    # Issues
    issues = IssueSet()
    h1 = issues.add_issue(IssueSpec(id="I-001", severity=SEV_HIGH,
                                    state=STATE_OPEN, created_at=1000))
    issues.add_waiver_link("W-1", "I-001")
    issues.add_testpoint_link("tp_foo", "I-001", LINK_BLOCKED_BY)
    issues.add_coverage_link("top.mod", "*", "I-001", LINK_RELATED)
    db._issues = issues
    db._issues_dirty = True

    # Meta
    meta = IssuesMeta()
    meta.set_title(h1, "High severity bug")
    meta.set_url(h1, "https://example.com/I-001")
    db._issues_meta = meta
    db._issues_meta_dirty = True

    # History
    w = IssueHistoryWriter()
    w.add("I-001", 1_700_000_000, STATE_OPEN)
    w.add("I-001", 1_700_001_000, STATE_RESOLVED)
    hist_data = w.seal_fast()
    db._issues_history_raw = hist_data
    db._issues_history_dirty = True

    out_path = str(tmp_path / "out.cdb")
    NcdbWriter().write(db, out_path)
    db2 = NcdbReader().read(out_path)

    # Verify issues
    assert db2._issues is not None
    h1r = db2._issues.get("I-001")
    assert h1r.severity == SEV_HIGH
    assert h1r.created_at == 1000

    # Verify links
    wl = list(db2._issues.issues_for_waiver("W-1"))
    assert len(wl) == 1
    tl = list(db2._issues.issues_for_testpoint("tp_foo"))
    assert len(tl) == 1
    cl = list(db2._issues.coverage_links())
    assert len(cl) == 1 and cl[0].scope_path == "top.mod"

    # Verify meta (lazy)
    assert hasattr(db2, '_issues_meta_raw') and db2._issues_meta_raw
    from covsight.core.ncdb.issues_meta import IssuesMeta as IM
    meta2 = IM.from_bytes(db2._issues_meta_raw)
    assert meta2.get_title(h1r) == "High severity bug"
    assert meta2.get_url(h1r) == "https://example.com/I-001"

    # Verify history
    assert db2._issues_history is not None
    ts_list = [t.ts for t in db2._issues_history.history_for_issue("I-001")]
    assert ts_list == [1_700_000_000, 1_700_001_000]


@pytest.mark.skipif(not os.path.exists(_PY_BASIC), reason="py_basic.cdb not found")
def test_cdb_without_issues_loads_cleanly():
    """Load py_basic.cdb (no issues); getIssues() returns None."""
    db = NcdbUCIS(_PY_BASIC)
    assert db.getIssues() is None


def test_issues_meta_lazy(tmp_path):
    """Read a .cdb; _issues_meta is None until getIssuesMeta() called."""
    db = _make_minimal_db()
    issues = IssueSet()
    h = issues.add_issue(IssueSpec(id="I-001"))
    db._issues = issues
    db._issues_dirty = True

    meta = IssuesMeta()
    meta.set_title(h, "Test issue")
    db._issues_meta = meta
    db._issues_meta_dirty = True

    out_path = str(tmp_path / "out.cdb")
    NcdbWriter().write(db, out_path)

    db2 = NcdbUCIS(out_path)
    # Meta must not be loaded yet
    assert db2._issues_meta is None
    # getIssues triggers loading of meta raw bytes
    db2.getIssues()
    assert db2._issues_meta is None   # still not parsed
    assert db2._issues_meta_raw != b''  # but raw bytes are present
    # getIssuesMeta triggers parse
    meta2 = db2.getIssuesMeta()
    assert meta2 is not None


def test_issues_without_history(tmp_path):
    """Omitting issues_history.bin loads fine; getIssueHistory() → None."""
    db = _make_minimal_db()
    issues = IssueSet()
    issues.add_issue(IssueSpec(id="I-001"))
    db._issues = issues
    db._issues_dirty = True
    # Do NOT set _issues_history_dirty

    out_path = str(tmp_path / "out.cdb")
    NcdbWriter().write(db, out_path)

    db2 = NcdbUCIS(out_path)
    db2.getIssues()
    assert db2.getIssueHistory() is None


def test_ncdb_writer_skips_issues_if_not_dirty(tmp_path):
    """Writing a db without dirty issues → no issues member in ZIP."""
    db = _make_minimal_db()
    issues = IssueSet()
    issues.add_issue(IssueSpec(id="I-001"))
    # Assign issues but do NOT set dirty flag
    db._issues = issues
    # db._issues_dirty remains False (default)

    out_path = str(tmp_path / "out.cdb")
    NcdbWriter().write(db, out_path)

    with zipfile.ZipFile(out_path, "r") as zf:
        assert "issues.bin" not in zf.namelist()


@pytest.mark.skipif(not os.path.exists(_PY_BASIC), reason="py_basic.cdb not found")
def test_backward_compat_round_trip(tmp_path):
    """Add issues to an existing py_basic.cdb scenario; coverage data unchanged."""
    from covsight.core.api import ScopeTypeT, CoverTypeT

    # Read the original
    db_orig = NcdbReader().read(_PY_BASIC)
    orig_counts = [
        ci.getCoverData().data
        for scope in db_orig.scopes(ScopeTypeT.ALL)
        for ci in scope.coverItems(CoverTypeT.ALL)
    ]

    # Write a new .cdb with issues added
    db_orig._issues = IssueSet()
    db_orig._issues.add_issue(IssueSpec(id="I-001"))
    db_orig._issues_dirty = True

    out_path = str(tmp_path / "with_issues.cdb")
    NcdbWriter().write(db_orig, out_path)

    # Read back and compare coverage
    db_back = NcdbReader().read(out_path)
    back_counts = [
        ci.getCoverData().data
        for scope in db_back.scopes(ScopeTypeT.ALL)
        for ci in scope.coverItems(CoverTypeT.ALL)
    ]
    assert back_counts == orig_counts

    # Verify issues survived
    assert db_back._issues is not None
    assert db_back._issues.get("I-001") is not None
