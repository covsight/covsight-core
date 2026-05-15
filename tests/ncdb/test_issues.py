"""Tests for ncdb/issues.py — IssueSet and IssueHandle."""
import pytest
from covsight.core.ncdb.issues import (
    IssueSet, IssueSpec, IssueHandle,
    WaiverIssueLinkInfo, TestpointIssueLinkInfo, CoverageIssueLinkInfo,
    SEV_HIGH, SEV_LOW, SEV_MEDIUM, SEV_CRITICAL,
    KIND_DESIGN_BUG, KIND_TEST_BUG,
    STATE_OPEN, STATE_IN_PROGRESS, STATE_RESOLVED, STATE_CLOSED,
    RES_NONE, RES_FIXED,
    LINK_BLOCKED_BY, LINK_RELATED,
    STATE_NAMES,
)


def _make_set_with_issues():
    """Helper: returns an IssueSet with 3 issues."""
    s = IssueSet()
    h1 = s.add_issue(IssueSpec(id="I-001", severity=SEV_HIGH, state=STATE_OPEN))
    h2 = s.add_issue(IssueSpec(id="I-002", severity=SEV_LOW,
                               state=STATE_CLOSED, resolution=RES_FIXED))
    h3 = s.add_issue(IssueSpec(id="I-003", severity=SEV_MEDIUM,
                               state=STATE_IN_PROGRESS))
    return s, h1, h2, h3


def test_add_and_get():
    s = IssueSet()
    h = s.add_issue(IssueSpec(id="I-001", severity=SEV_HIGH,
                              kind=KIND_TEST_BUG, state=STATE_OPEN,
                              created_at=1000, updated_at=2000, synced_at=3000))
    h2 = s.get("I-001")
    assert h2 is not None
    assert h2.id == "I-001"
    assert h2.severity == SEV_HIGH
    assert h2.kind == KIND_TEST_BUG
    assert h2.state == STATE_OPEN
    assert h2.created_at == 1000
    assert h2.updated_at == 2000
    assert h2.synced_at == 3000


def test_duplicate_id_raises():
    s = IssueSet()
    s.add_issue(IssueSpec(id="I-001"))
    with pytest.raises(ValueError, match="I-001"):
        s.add_issue(IssueSpec(id="I-001"))


def test_issues_iterator():
    s, h1, h2, h3 = _make_set_with_issues()
    ids = [h.id for h in s.issues()]
    assert ids == ["I-001", "I-002", "I-003"]


def test_open_issues():
    s, h1, h2, h3 = _make_set_with_issues()
    open_ids = {h.id for h in s.open_issues()}
    assert "I-001" in open_ids   # STATE_OPEN
    assert "I-003" in open_ids   # STATE_IN_PROGRESS
    assert "I-002" not in open_ids   # STATE_CLOSED


def test_issues_by_severity():
    s, h1, h2, h3 = _make_set_with_issues()
    high = list(s.issues_by_severity(SEV_HIGH))
    assert len(high) == 1
    assert high[0].id == "I-001"
    assert list(s.issues_by_severity(SEV_CRITICAL)) == []


def test_waiver_link_round_trip():
    s, h1, h2, h3 = _make_set_with_issues()
    s.add_waiver_link("W-001", "I-001")
    s.add_waiver_link("W-001", "I-002")

    issues = list(s.issues_for_waiver("W-001"))
    assert {h.id for h in issues} == {"I-001", "I-002"}

    links = list(s.waivers_for_issue(h1))
    assert len(links) == 1
    assert links[0].waiver_id == "W-001"
    assert links[0].issue_id == "I-001"


def test_testpoint_link_round_trip():
    s, h1, h2, h3 = _make_set_with_issues()
    s.add_testpoint_link("tp_uart_tx", "I-001", LINK_BLOCKED_BY)
    s.add_testpoint_link("tp_uart_tx", "I-003", LINK_RELATED)

    issues = list(s.issues_for_testpoint("tp_uart_tx"))
    assert {h.id for h in issues} == {"I-001", "I-003"}

    links = list(s.testpoints_for_issue(h1))
    assert len(links) == 1
    assert links[0].testpoint_name == "tp_uart_tx"
    assert links[0].link_type == LINK_BLOCKED_BY


def test_coverage_links():
    s, h1, h2, h3 = _make_set_with_issues()
    s.add_coverage_link("top.uart", "*", "I-001", LINK_BLOCKED_BY)
    s.add_coverage_link("top.apb",  "*", "I-002", LINK_RELATED)

    cl = list(s.coverage_links())
    assert len(cl) == 2
    assert cl[0].scope_path == "top.uart"
    assert cl[0].bin_name == "*"
    assert cl[0].issue_id == "I-001"
    assert cl[1].issue_id == "I-002"


def test_serialize_empty():
    s = IssueSet()
    data = s.serialize()
    s2 = IssueSet.from_bytes(data)
    assert len(s2) == 0
    assert list(s2.issues()) == []


def test_serialize_roundtrip_small():
    s = IssueSet()
    h1 = s.add_issue(IssueSpec(id="A-1", severity=SEV_HIGH, state=STATE_OPEN,
                               created_at=100, updated_at=200, synced_at=300,
                               ext="PROJ-100"))
    h2 = s.add_issue(IssueSpec(id="A-2", severity=SEV_LOW,
                               state=STATE_RESOLVED, resolution=RES_FIXED,
                               kind=KIND_TEST_BUG))
    s.add_waiver_link("W-1", "A-1")
    s.add_testpoint_link("tp_foo", "A-1", LINK_BLOCKED_BY)
    s.add_coverage_link("top.mod", "*", "A-2", LINK_RELATED)

    data = s.serialize()
    s2 = IssueSet.from_bytes(data)

    assert len(s2) == 2
    h1r = s2.get("A-1")
    assert h1r.id == "A-1"
    assert h1r.ext == "PROJ-100"
    assert h1r.severity == SEV_HIGH
    assert h1r.state == STATE_OPEN
    assert h1r.created_at == 100
    assert h1r.updated_at == 200
    assert h1r.synced_at == 300

    h2r = s2.get("A-2")
    assert h2r.severity == SEV_LOW
    assert h2r.state == STATE_RESOLVED
    assert h2r.resolution == RES_FIXED
    assert h2r.kind == KIND_TEST_BUG

    # Waiver links
    wl = list(s2.issues_for_waiver("W-1"))
    assert len(wl) == 1 and wl[0].id == "A-1"

    # Testpoint links
    tl = list(s2.issues_for_testpoint("tp_foo"))
    assert len(tl) == 1 and tl[0].id == "A-1"

    # Coverage links
    cl = list(s2.coverage_links())
    assert len(cl) == 1
    assert cl[0].scope_path == "top.mod"
    assert cl[0].issue_id == "A-2"


def test_serialize_roundtrip_1k():
    """1000 issues + 3500 links: verify data integrity and reasonable speed."""
    import time
    s = IssueSet()
    handles = []
    for i in range(1000):
        h = s.add_issue(IssueSpec(
            id=f"I-{i:04d}",
            severity=i % 5,
            kind=i % 4,
            state=i % 5,
            resolution=(i % 5) if (i % 5) >= 2 else 0,
            created_at=1700000000 + i,
            updated_at=1700001000 + i,
        ))
        handles.append(h)

    for i in range(1000):
        s.add_waiver_link(f"W-{i % 100}", f"I-{i:04d}")
    for i in range(1500):
        s.add_testpoint_link(f"tp_{i % 50}", f"I-{i % 1000:04d}", LINK_BLOCKED_BY)
    for i in range(1000):
        s.add_coverage_link(f"top.mod_{i % 20}", "*", f"I-{i:04d}", LINK_BLOCKED_BY)

    t0 = time.perf_counter()
    data = s.serialize()
    s2 = IssueSet.from_bytes(data)
    elapsed = time.perf_counter() - t0

    assert len(s2) == 1000
    # Spot-check a few issues
    h = s2.get("I-0000")
    assert h.id == "I-0000"
    h = s2.get("I-0999")
    assert h.id == "I-0999"
    assert elapsed < 0.5, f"Round-trip took {elapsed:.3f}s (expected < 0.5s)"


def test_string_dedup():
    """Two issues sharing a scope path should use the same string table index."""
    s = IssueSet()
    s.add_issue(IssueSpec(id="I-001"))
    s.add_issue(IssueSpec(id="I-002"))
    s.add_coverage_link("top.shared_path", "*", "I-001", LINK_BLOCKED_BY)
    s.add_coverage_link("top.shared_path", "*", "I-002", LINK_BLOCKED_BY)

    data = s.serialize()
    s2 = IssueSet.from_bytes(data)

    # Both coverage links point to the same path string
    cl = list(s2.coverage_links())
    assert cl[0].scope_path == cl[1].scope_path == "top.shared_path"
    # Verify the string table only has the path once
    path_count = sum(
        1 for s_str in s2._strings
        if s_str == "top.shared_path"
    )
    assert path_count == 1


def test_handle_is_opaque():
    """IssueHandle cannot be constructed directly from outside the module."""
    with pytest.raises(TypeError):
        IssueHandle("not-a-token", None, 0)


def test_update_issue():
    s = IssueSet()
    h = s.add_issue(IssueSpec(id="I-001", severity=SEV_HIGH, state=STATE_OPEN))
    assert h.state == STATE_OPEN

    spec2 = IssueSpec(id="I-001", severity=SEV_HIGH,
                      state=STATE_RESOLVED, resolution=RES_FIXED)
    s.update_issue(h, spec2)

    assert h.state == STATE_RESOLVED
    assert h.resolution == RES_FIXED

    # Verify via get() as well
    h2 = s.get("I-001")
    assert h2.state == STATE_RESOLVED
