"""Flat bin addressing — ``dfs_util`` against its ground truth.

Per-test associations, formal results and parquet coveritem ids are all keyed
by the flat bin index that ``counts.bin`` uses.  The authority for that index is
``ScopeTreeWriter``, which builds ``counts_list`` as a side effect of
serializing the scope tree.  These tests assert the resolver agrees with it —
including for folded toggle pairs, where the two bins are emitted in canonical
order rather than creation order.

The failure mode being guarded against is not a crash: a resolver that is off by
one, or that orders toggle bins by creation, silently attributes coverage to the
wrong bin.
"""

import pytest

from covsight.core.mem.mem_ucis import MemUCIS
from covsight.core.api import (SourceT, ScopeTypeT, CoverData, CoverTypeT,
                               HistoryNodeKind)
from covsight.core.ncdb.constants import TOGGLE_BIN_0_TO_1, TOGGLE_BIN_1_TO_0
from covsight.core.ncdb.string_table import StringTable
from covsight.core.ncdb.scope_tree import ScopeTreeWriter
from covsight.core.ncdb import dfs_util


def _oracle_counts(db):
    """The flat count array exactly as the writer builds it."""
    w = ScopeTreeWriter(StringTable())
    w.write(db)
    return w.counts_list


def _add_bins(scope, names_and_counts, cover_type=CoverTypeT.STMTBIN):
    for name, count in names_and_counts:
        cd = CoverData(cover_type, 0)
        cd.data = count
        scope.createNextCover(name, cd, None)


def _toggle_pair(parent, name, c01, c10, reversed_order=False):
    """A BRANCH scope that folds into a TOGGLE_PAIR record."""
    br = parent.createScope(name, None, 1, SourceT.SV, ScopeTypeT.BRANCH, 0)
    pairs = [(TOGGLE_BIN_0_TO_1, c01), (TOGGLE_BIN_1_TO_0, c10)]
    if reversed_order:
        pairs.reverse()
    _add_bins(br, pairs, CoverTypeT.TOGGLEBIN)
    return br


def _nested_db(toggle_reversed=False):
    """A tree with nesting, sibling bins, and a folded toggle pair."""
    db = MemUCIS()
    db.createHistoryNode(None, "t", None, HistoryNodeKind.TEST)

    top = db.createScope("top", None, 1, SourceT.SV, ScopeTypeT.BLOCK, 0)
    _add_bins(top, [("t0", 1), ("t1", 2)])

    mid = top.createScope("mid", None, 1, SourceT.SV, ScopeTypeT.BLOCK, 0)
    _add_bins(mid, [("m0", 3)])

    _toggle_pair(mid, "tog", 4, 5, reversed_order=toggle_reversed)

    leaf = top.createScope("leaf", None, 1, SourceT.SV, ScopeTypeT.BLOCK, 0)
    _add_bins(leaf, [("l0", 6), ("l1", 7), ("l2", 8)])

    second = db.createScope("second", None, 1, SourceT.SV, ScopeTypeT.BLOCK, 0)
    _add_bins(second, [("s0", 9)])
    return db


# -- agreement with the writer --------------------------------------------

def test_total_bin_count_matches_writer():
    db = _nested_db()
    assert dfs_util.total_bin_count(db) == len(_oracle_counts(db))


def test_flat_order_matches_counts_array():
    """The decisive test: walking bins in flat order reproduces counts.bin.

    Bin counts are distinct here, so any reordering or off-by-one shows up as a
    value mismatch rather than passing by coincidence.
    """
    db = _nested_db()
    oracle = _oracle_counts(db)

    got = []
    for flat, scope, local, name in dfs_util.iter_flat_bins(db):
        assert flat == len(got)
        by_name = {ci.getName(): ci for ci in scope.coverItems(CoverTypeT.ALL)}
        got.append(by_name[name].getCoverData().data)

    assert got == oracle


def test_bases_are_cumulative_and_ordered():
    db = _nested_db()
    scopes = dfs_util.dfs_scope_list(db)
    expected, running = {}, 0
    for scope in scopes:
        expected[id(scope)] = running
        running += dfs_util.scope_bin_count(scope)
    assert dfs_util.scope_bin_bases(db) == expected
    assert running == len(_oracle_counts(db))


# -- the toggle-pair fold, which is where a naive DFS goes wrong -----------

def test_toggle_pair_contributes_exactly_two_bins():
    db = _nested_db()
    tog = next(s for s in dfs_util.dfs_scope_list(db)
               if s.getScopeName() == "tog")
    assert dfs_util.scope_bin_count(tog) == 2


def test_toggle_pair_children_are_not_traversed():
    """A folded pair occupies one DFS slot; nothing below it is addressable."""
    db = _nested_db()
    names = [s.getScopeName() for s in dfs_util.dfs_scope_list(db)]
    assert names.count("tog") == 1


def test_toggle_bins_are_canonically_ordered_regardless_of_creation_order():
    """Creation order must not change the flat layout.

    ``scope_tree.bin`` writes 0->1 then 1->0 by name lookup, so a resolver that
    trusts ``coverItems()`` order would disagree with counts.bin for any pair
    created the other way round.
    """
    forward = _nested_db(toggle_reversed=False)
    reverse = _nested_db(toggle_reversed=True)

    def bin_names(db):
        return [name for _f, _s, _l, name in dfs_util.iter_flat_bins(db)]

    assert bin_names(forward) == bin_names(reverse)

    tog = next(s for s in dfs_util.dfs_scope_list(reverse)
               if s.getScopeName() == "tog")
    assert dfs_util.scope_bin_names(tog) == [TOGGLE_BIN_0_TO_1,
                                             TOGGLE_BIN_1_TO_0]
    # And the counts follow the canonical order, not the creation order.
    assert _oracle_counts(reverse) == _oracle_counts(forward)


# -- addressing helpers ----------------------------------------------------

def test_flat_bin_index_round_trips():
    db = _nested_db()
    for flat, scope, local, _name in dfs_util.iter_flat_bins(db):
        assert dfs_util.flat_bin_index(db, scope, local) == flat


def test_flat_bin_index_rejects_out_of_range():
    db = _nested_db()
    top = dfs_util.dfs_scope_list(db)[0]
    with pytest.raises(IndexError):
        dfs_util.flat_bin_index(db, top, dfs_util.scope_bin_count(top))
    with pytest.raises(IndexError):
        dfs_util.flat_bin_index(db, top, -1)


def test_unknown_scope_raises_rather_than_returning_zero():
    """Returning 0 would silently point associations at the first bin."""
    db = _nested_db()
    other = MemUCIS().createScope("stranger", None, 1, SourceT.SV,
                                  ScopeTypeT.BLOCK, 0)
    with pytest.raises(KeyError):
        dfs_util.scope_bin_base(db, other)


def test_empty_scope_gets_a_well_defined_base():
    db = MemUCIS()
    db.createHistoryNode(None, "t", None, HistoryNodeKind.TEST)
    top = db.createScope("top", None, 1, SourceT.SV, ScopeTypeT.BLOCK, 0)
    _add_bins(top, [("a", 1)])
    empty = top.createScope("empty", None, 1, SourceT.SV, ScopeTypeT.BLOCK, 0)
    after = top.createScope("after", None, 1, SourceT.SV, ScopeTypeT.BLOCK, 0)
    _add_bins(after, [("b", 2)])

    assert dfs_util.scope_bin_count(empty) == 0
    assert dfs_util.scope_bin_base(db, empty) == 1
    assert dfs_util.scope_bin_base(db, after) == 1


def test_database_with_no_bins():
    db = MemUCIS()
    db.createHistoryNode(None, "t", None, HistoryNodeKind.TEST)
    db.createScope("top", None, 1, SourceT.SV, ScopeTypeT.BLOCK, 0)
    assert dfs_util.total_bin_count(db) == 0
    assert list(dfs_util.iter_flat_bins(db)) == []
    assert _oracle_counts(db) == []
