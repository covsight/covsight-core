"""Flat-bin-index → coveritem-id mapping in the Parquet writer.

Per-test associations and formal results are keyed by the flat bin index of
``counts.bin``.  Parquet, however, numbers a scope's bins in ``coverItems()``
order.  Those two orders disagree for a folded toggle pair whose bins were
created ``1 -> 0`` first, because ``scope_tree.bin`` always emits the canonical
``0 -> 1``, ``1 -> 0`` ordering.

Resolving flat indices positionally therefore attributed a toggle pair's
associations to the opposite edge.  These tests pin the name-based resolution
that fixes it.
"""

import pytest

pytest.importorskip("pyarrow", reason="Parquet backend needs pyarrow")

from covsight.core.api import (CoverData, CoverTypeT, HistoryNodeKind,
                               ScopeTypeT, SourceT)
from covsight.core.mem import MemFactory
from covsight.core.ncdb.constants import TOGGLE_BIN_0_TO_1, TOGGLE_BIN_1_TO_0
from covsight.core.ncdb.scope_tree import ScopeTreeWriter
from covsight.core.ncdb.string_table import StringTable
from covsight.core.parquet.writer import _Walk


COUNT_0_TO_1 = 11
COUNT_1_TO_0 = 99


def _db_with_toggle_pair(reverse_creation_order):
    db = MemFactory.create()
    db.createHistoryNode(None, "t", "./run.sh", HistoryNodeKind.TEST)
    top = db.createScope("top", None, 1, SourceT.SV, ScopeTypeT.BLOCK, 0)
    pair = top.createScope("sig", None, 1, SourceT.SV, ScopeTypeT.BRANCH, 0)

    bins = [(TOGGLE_BIN_0_TO_1, COUNT_0_TO_1), (TOGGLE_BIN_1_TO_0, COUNT_1_TO_0)]
    if reverse_creation_order:
        bins.reverse()
    for name, count in bins:
        data = CoverData(CoverTypeT.TOGGLEBIN, 0)
        data.data = count
        pair.createNextCover(name, data, None)
    return db


def _counts_bin(db):
    writer = ScopeTreeWriter(StringTable())
    writer.write(db)
    return writer.counts_list


def _flat_names(db):
    """Bin names in the order ``_flat_coveritem_ids`` resolves them."""
    walk = _Walk(db, run_id="r0")
    walk.run()
    name_by_id = dict(zip(walk.c_id, walk.c_name))
    return [name_by_id[cid] for cid in walk._flat_coveritem_ids()]


@pytest.mark.parametrize("reverse", [False, True],
                         ids=["created-forward", "created-reversed"])
def test_counts_bin_is_always_canonical(reverse):
    """Precondition: the count array does not follow creation order."""
    assert _counts_bin(_db_with_toggle_pair(reverse)) == [COUNT_0_TO_1,
                                                          COUNT_1_TO_0]


@pytest.mark.parametrize("reverse", [False, True],
                         ids=["created-forward", "created-reversed"])
def test_flat_ids_follow_canonical_order(reverse):
    """The regression: flat index 0 must be ``0 -> 1`` either way.

    Created in reverse, the old positional resolution returned the id of
    ``1 -> 0`` for flat bin 0, silently recording every association and formal
    result against the opposite toggle edge.
    """
    db = _db_with_toggle_pair(reverse)
    assert _flat_names(db) == [TOGGLE_BIN_0_TO_1, TOGGLE_BIN_1_TO_0]


def test_creation_order_does_not_change_the_mapping():
    forward = _flat_names(_db_with_toggle_pair(False))
    reverse = _flat_names(_db_with_toggle_pair(True))
    assert forward == reverse


def test_non_toggle_scopes_keep_positional_order():
    """Ordinary scopes are unaffected -- names there may even repeat."""
    db = MemFactory.create()
    db.createHistoryNode(None, "t", "./run.sh", HistoryNodeKind.TEST)
    top = db.createScope("top", None, 1, SourceT.SV, ScopeTypeT.BLOCK, 0)
    for name in ("b0", "b1", "b2"):
        data = CoverData(CoverTypeT.STMTBIN, 0)
        data.data = 1
        top.createNextCover(name, data, None)

    assert _flat_names(db) == ["b0", "b1", "b2"]
