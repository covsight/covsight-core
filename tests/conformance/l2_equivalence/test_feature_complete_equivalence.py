"""L2: the Parquet view and the MemUCIS oracle answer the same questions.

L1 asks whether a round trip loses anything. L2 asks a stronger question: does
the Parquet backend *behave* like a UCIS database, or only store one? A backend
can round-trip every byte and still iterate in the wrong order, honour a type
mask incorrectly, or resolve an identity lookup to the wrong object -- none of
which a snapshot comparison catches, because the snapshot is built by the very
traversal under test.

`tests/parquet/test_cross_backend.py` does this for the small fixture; this does
it for the feature-complete corpus, where the constructs that differ between
backends (covergroups, crosses, FSM, assertions) actually exist.
"""

from __future__ import annotations

import pytest

pytest.importorskip("pyarrow", reason="Parquet backend needs pyarrow")

from covsight.core.api import CoverTypeT, ScopeTypeT, StrProperty  # noqa: E402
from covsight.core.conformance import ucis_feature                # noqa: E402
from covsight.core.parquet import ParquetUCIS, ParquetWriter      # noqa: E402

from ..fixtures.feature_complete import build_feature_complete    # noqa: E402


@pytest.fixture(scope="module")
def pair(tmp_path_factory):
    """(oracle, subject) -- the same database seen two ways."""
    oracle = build_feature_complete()
    path = tmp_path_factory.mktemp("fc_l2") / "dataset"
    ParquetWriter(path).write(oracle, run_id="r0")
    return oracle, ParquetUCIS(path)


def _paths(db):
    """Full scope paths in traversal order."""
    out = []

    def visit(scope, prefix):
        here = prefix + "/" + str(scope.getScopeName())
        out.append(here)
        for child in scope.scopes(-1):
            visit(child, here)

    for root in db.scopes(-1):
        visit(root, "")
    return out


def _bin_names(db):
    out = []

    def visit(scope, prefix):
        here = prefix + "/" + str(scope.getScopeName())
        for ci in scope.coverItems(CoverTypeT.ALL):
            out.append(f"{here}:{ci.getName()}")
        for child in scope.scopes(-1):
            visit(child, here)

    for root in db.scopes(-1):
        visit(root, "")
    return out


@ucis_feature("IT.1", "IT.2", level="L2", surface="read")
def test_scope_iteration_order_agrees(pair):
    """`scopes.dfs_ordinal` exists to reproduce this without re-deriving it.

    Order, not membership: comparing sets would pass for a backend that
    returned every scope in an arbitrary sequence.
    """
    oracle, subject = pair
    assert _paths(subject) == _paths(oracle)


@ucis_feature("IT.4", "IT.5", level="L2", surface="read")
def test_cover_iteration_order_agrees(pair):
    """`(scope_id, local_index)` reproduces CoverIterate order."""
    oracle, subject = pair
    assert _bin_names(subject) == _bin_names(oracle)


@ucis_feature("IT.4", "C6.3", level="L2", surface="read")
def test_cover_type_mask_filtering_agrees(pair):
    """A mask is a predicate on `cover_type`, and both sides must apply it the
    same way -- including for the assertion bins, where one scope holds seven
    different types."""
    oracle, subject = pair
    for mask in (CoverTypeT.CVGBIN, CoverTypeT.TOGGLEBIN, CoverTypeT.FSMBIN,
                 CoverTypeT.STMTBIN, CoverTypeT.PASSBIN, CoverTypeT.USERBIN):
        want = _masked(oracle, mask)
        got = _masked(subject, mask)
        assert got == want, f"mask {mask!r}: {want} != {got}"
        assert want, f"mask {mask!r} matched nothing; the corpus lost coverage"


def _masked(db, mask):
    out = []

    def visit(scope):
        for ci in scope.coverItems(mask):
            out.append(ci.getName())
        for child in scope.scopes(-1):
            visit(child)

    for root in db.scopes(-1):
        visit(root)
    return out


@ucis_feature("IT.1", "S4.17", level="L2", surface="read")
def test_scope_type_mask_filtering_agrees(pair):
    oracle, subject = pair
    for mask in (ScopeTypeT.COVERGROUP, ScopeTypeT.COVERPOINT,
                 ScopeTypeT.CROSS, ScopeTypeT.FSM, ScopeTypeT.TOGGLE,
                 ScopeTypeT.ASSERT, ScopeTypeT.BLOCK):
        want = _scope_masked(oracle, mask)
        got = _scope_masked(subject, mask)
        assert got == want, f"mask {mask!r}: {want} != {got}"
        assert want, f"mask {mask!r} matched nothing; the corpus lost coverage"


def _scope_masked(db, mask):
    out = []

    def visit(scope):
        for child in scope.scopes(mask):
            out.append(child.getScopeName())
        for child in scope.scopes(-1):
            visit(child)

    for root in db.scopes(-1):
        visit(root)
    return out


@ucis_feature("S4.11", "S4.15", level="L2", surface="read")
def test_identity_lookup_resolves_to_the_same_object(pair):
    """`UCIS_STR_UNIQUE_ID` is the primary key, so a lookup by it is the
    operation the whole identity scheme exists to serve."""
    _oracle, subject = pair
    checked = 0

    def visit(scope):
        nonlocal checked
        uid = scope.getStringProperty(-1, StrProperty.UNIQUE_ID)
        if uid:
            assert subject.matchScopeByUniqueId(uid) is scope, (
                f"{uid} resolved to a different object"
            )
            checked += 1
        for child in scope.scopes(-1):
            visit(child)

    for root in subject.scopes(-1):
        visit(root)
    assert checked >= 15, f"only {checked} scopes carried a unique id"


@ucis_feature("H11.1", "H11.5", level="L2", surface="read")
def test_history_agrees(pair):
    from covsight.core.api import HistoryNodeKind

    oracle, subject = pair
    want = [(n.getLogicalName(), int(n.getKind()))
            for n in oracle.historyNodes(HistoryNodeKind.ALL)]
    got = [(n.getLogicalName(), int(n.getKind()))
           for n in subject.historyNodes(HistoryNodeKind.ALL)]
    assert got == want
    assert len(want) == 2


@ucis_feature("S4.19", "CG.11", level="L2", surface="read")
def test_a_cross_resolves_its_operands_to_the_same_scopes(pair):
    """`ucis_GetIthCrossedCvp` returns a *scope handle*, so equivalence is not
    "the same name" but "the same object this backend would hand out anywhere
    else" -- resolved here through the identity the dataset stores."""
    oracle, subject = pair

    def crosses(db):
        out = {}

        def visit(scope, prefix):
            here = prefix + "/" + str(scope.getScopeName())
            if scope.getScopeType() & ScopeTypeT.CROSS:
                out[here] = scope
            for child in scope.scopes(-1):
                visit(child, here)

        for root in db.scopes(-1):
            visit(root, "")
        return out

    want, got = crosses(oracle), crosses(subject)
    assert set(got) == set(want) and want

    for path, cross in got.items():
        n = cross.getNumCrossedCoverpoints()
        assert n == want[path].getNumCrossedCoverpoints()
        for i in range(n):
            point = cross.getIthCrossedCoverpoint(i)
            uid = point.getStringProperty(-1, StrProperty.UNIQUE_ID)
            assert subject.matchScopeByUniqueId(uid) is point, (
                f"{path} operand {i} is not the scope the dataset identifies"
            )
            assert point.getScopeName() == \
                want[path].getIthCrossedCoverpoint(i).getScopeName()
