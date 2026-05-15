"""Tests for ncdb/issues_history.py — IssueHistoryWriter and IssueHistoryReader."""
import pytest
from covsight.core.ncdb.issues import STATE_OPEN, STATE_IN_PROGRESS, STATE_RESOLVED, STATE_CLOSED
from covsight.core.ncdb.issues_history import (
    IssueHistoryWriter,
    IssueHistoryReader,
    IssueStateTransition,
)


BASE_TS = 1_700_000_000


def _roundtrip(writer: IssueHistoryWriter, use_lzma: bool = False) -> IssueHistoryReader:
    data = writer.seal(use_lzma=use_lzma)
    return IssueHistoryReader(data)


def _roundtrip_fast(writer: IssueHistoryWriter) -> IssueHistoryReader:
    return IssueHistoryReader(writer.seal_fast())


def test_empty_history():
    w = IssueHistoryWriter()
    r = _roundtrip(w)
    assert list(r.all_transitions()) == []


def test_single_issue_single_transition():
    w = IssueHistoryWriter()
    w.add("I-001", BASE_TS, STATE_OPEN)
    r = _roundtrip(w)
    transitions = list(r.history_for_issue("I-001"))
    assert len(transitions) == 1
    t = transitions[0]
    assert t.issue_id == "I-001"
    assert t.ts == BASE_TS
    assert t.new_state == STATE_OPEN
    assert t.comment == ""


def test_chronological_order():
    w = IssueHistoryWriter()
    # Add out of order deliberately
    w.add("I-001", BASE_TS + 200, STATE_RESOLVED)
    w.add("I-001", BASE_TS,       STATE_OPEN)
    w.add("I-001", BASE_TS + 100, STATE_IN_PROGRESS)
    r = _roundtrip(w)
    ts_seq = [t.ts for t in r.history_for_issue("I-001")]
    assert ts_seq == sorted(ts_seq)


def test_delta_reset_across_issues():
    """Timestamps reset at the start of each issue group."""
    w = IssueHistoryWriter()
    w.add("A", BASE_TS,       STATE_OPEN)
    w.add("A", BASE_TS + 500, STATE_RESOLVED)
    # B starts at ts_base, which is earlier than A's records
    w.add("B", BASE_TS - 10,  STATE_OPEN)
    r = _roundtrip(w)

    a_ts = [t.ts for t in r.history_for_issue("A")]
    b_ts = [t.ts for t in r.history_for_issue("B")]
    assert a_ts == [BASE_TS, BASE_TS + 500]
    assert b_ts == [BASE_TS - 10]


def test_state_at_exact():
    w = IssueHistoryWriter()
    w.add("I-001", BASE_TS,       STATE_OPEN)
    w.add("I-001", BASE_TS + 100, STATE_IN_PROGRESS)
    w.add("I-001", BASE_TS + 200, STATE_RESOLVED)
    r = _roundtrip(w)
    assert r.state_at("I-001", BASE_TS + 100) == STATE_IN_PROGRESS


def test_state_at_between():
    w = IssueHistoryWriter()
    w.add("I-001", BASE_TS,       STATE_OPEN)
    w.add("I-001", BASE_TS + 100, STATE_RESOLVED)
    r = _roundtrip(w)
    assert r.state_at("I-001", BASE_TS + 50) == STATE_OPEN


def test_state_at_before_first():
    w = IssueHistoryWriter()
    w.add("I-001", BASE_TS + 100, STATE_OPEN)
    r = _roundtrip(w)
    assert r.state_at("I-001", BASE_TS) is None


def test_all_transitions_order():
    """all_transitions() must be sorted by (issue_id, ts)."""
    w = IssueHistoryWriter()
    w.add("B", BASE_TS + 10, STATE_OPEN)
    w.add("A", BASE_TS,      STATE_OPEN)
    w.add("A", BASE_TS + 5,  STATE_RESOLVED)
    r = _roundtrip(w)
    transitions = list(r.all_transitions())
    # Issues sorted alphabetically, each in timestamp order
    ids = [t.issue_id for t in transitions]
    assert ids == ["A", "A", "B"]
    ts_seq = [t.ts for t in transitions]
    assert ts_seq[0] <= ts_seq[1]


def test_lzma_round_trip():
    w = IssueHistoryWriter()
    w.add("I-001", BASE_TS, STATE_OPEN, comment="created")
    r = _roundtrip(w, use_lzma=True)
    t = next(r.history_for_issue("I-001"))
    assert t.ts == BASE_TS
    assert t.comment == "created"


def test_deflate_round_trip():
    w = IssueHistoryWriter()
    w.add("I-001", BASE_TS, STATE_OPEN)
    r = _roundtrip_fast(w)
    t = next(r.history_for_issue("I-001"))
    assert t.new_state == STATE_OPEN


def test_comment_dedup():
    """The same comment string should appear only once in the comment table."""
    w = IssueHistoryWriter()
    repeated_comment = "Duplicate test"
    w.add("I-001", BASE_TS,       STATE_OPEN,     comment=repeated_comment)
    w.add("I-001", BASE_TS + 100, STATE_RESOLVED, comment=repeated_comment)
    r = _roundtrip(w)
    # comment_table should only have one entry for repeated_comment
    assert r._comment_table.count(repeated_comment) == 1
    # But both transitions should have that comment
    transitions = list(r.history_for_issue("I-001"))
    assert all(t.comment == repeated_comment for t in transitions)


def test_large_history_memory():
    """100K transitions: basic correctness check (memory check is informational)."""
    import os
    w = IssueHistoryWriter()
    num_issues = 1000
    per_issue  = 100
    for i in range(num_issues):
        for j in range(per_issue):
            w.add(f"I-{i:04d}", BASE_TS + i * 10000 + j * 10,
                  j % 5, comment="" if j % 5 else "note")
    data = w.seal(use_lzma=False)
    r = IssueHistoryReader(data)
    total = sum(1 for _ in r.all_transitions())
    assert total == num_issues * per_issue
