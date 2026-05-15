"""Tests for ncdb/issues_meta.py — IssuesMeta."""
import pytest
from covsight.core.ncdb.issues import IssueSet, IssueSpec, IssueHandle
from covsight.core.ncdb.issues_meta import IssuesMeta


def _handle(idx: int) -> IssueHandle:
    """Create a bare handle with the given idx (no real IssueSet needed)."""
    s = IssueSet()
    for i in range(idx + 1):
        s.add_issue(IssueSpec(id=f"I-{i:03d}"))
    return s.get(f"I-{idx:03d}")


def test_get_set_round_trip():
    s = IssueSet()
    h = s.add_issue(IssueSpec(id="I-001"))
    meta = IssuesMeta()
    meta.set_title(h, "UART parity error")
    meta.set_url(h, "https://jira.example.com/browse/PROJ-1")

    assert meta.get_title(h) == "UART parity error"
    assert meta.get_url(h) == "https://jira.example.com/browse/PROJ-1"


def test_null_entry():
    s = IssueSet()
    h = s.add_issue(IssueSpec(id="I-001"))
    meta = IssuesMeta()
    assert meta.get_title(h) is None
    assert meta.get_url(h) is None


def test_serialize_round_trip():
    s = IssueSet()
    h0 = s.add_issue(IssueSpec(id="I-000"))
    h1 = s.add_issue(IssueSpec(id="I-001"))
    h2 = s.add_issue(IssueSpec(id="I-002"))

    meta = IssuesMeta()
    meta.set_title(h0, "First issue")
    meta.set_url(h0, "https://example.com/1")
    # h1 intentionally left as None
    meta.set_title(h2, "Third issue")

    data = meta.serialize()
    meta2 = IssuesMeta.from_bytes(data)

    assert meta2.get_title(h0) == "First issue"
    assert meta2.get_url(h0) == "https://example.com/1"
    assert meta2.get_title(h1) is None
    assert meta2.get_url(h1) is None
    assert meta2.get_title(h2) == "Third issue"
    assert meta2.get_url(h2) is None


def test_sparse_entries():
    """Only issues 0 and 2 have meta; issue 1 is null."""
    s = IssueSet()
    h0 = s.add_issue(IssueSpec(id="I-000"))
    h1 = s.add_issue(IssueSpec(id="I-001"))
    h2 = s.add_issue(IssueSpec(id="I-002"))

    meta = IssuesMeta()
    meta.set_url(h0, "https://a.com/0")
    # skip h1
    meta.set_url(h2, "https://a.com/2")

    data = meta.serialize()
    import json
    parsed = json.loads(data)
    assert parsed["v"] == 1
    assert len(parsed["m"]) == 3
    assert parsed["m"][1] is None

    meta2 = IssuesMeta.from_bytes(data)
    assert meta2.get_url(h0) == "https://a.com/0"
    assert meta2.get_title(h1) is None
    assert meta2.get_url(h2) == "https://a.com/2"
