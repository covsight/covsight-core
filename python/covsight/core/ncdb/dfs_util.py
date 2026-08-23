"""
DFS traversal utility — shared between attrs/tags/properties serializers.

Produces a flat list of (scope, dfs_index) pairs in the same DFS order
that scope_tree.py uses for encoding, so that index-based serializers
(attrs.json, tags.json, properties.bin) can map directly to scope_tree
offsets without re-reading the binary.
"""

from covsight.core.api import ScopeTypeT
from covsight.core.api import CoverTypeT

from .constants import TOGGLE_BIN_0_TO_1, TOGGLE_BIN_1_TO_0


def _is_toggle_pair(scope) -> bool:
    """Match the toggle-pair detection logic in scope_tree.py."""
    if scope.getScopeType() != ScopeTypeT.BRANCH:
        return False
    cover_items = list(scope.coverItems(CoverTypeT.ALL))
    if len(cover_items) != 2:
        return False
    if list(scope.scopes(ScopeTypeT.ALL)):
        return False
    names = {ci.getName() for ci in cover_items}
    return names == {TOGGLE_BIN_0_TO_1, TOGGLE_BIN_1_TO_0}


def dfs_scope_list(db) -> list:
    """Return all scopes in DFS order (matches scope_tree.bin encoding).

    Toggle-pair BRANCH scopes are included (they appear in scope_tree.bin
    as TOGGLE_PAIR records but still occupy one DFS slot each).
    """
    cached = getattr(db, '_dfs_scope_cache', None)
    if cached is not None:
        return cached

    result = []

    def _visit(scope):
        result.append(scope)
        if not _is_toggle_pair(scope):
            for child in scope.scopes(ScopeTypeT.ALL):
                _visit(child)

    for top_scope in db.scopes(ScopeTypeT.ALL):
        _visit(top_scope)

    db._dfs_scope_cache = result
    return result


# ── Flat bin addressing ───────────────────────────────────────────────────
#
# ``counts.bin`` is a flat array indexed by "flat bin index", and per-test
# associations, formal results and the parquet coveritem ids are all keyed by
# that same index.  The ordering is defined by ``ScopeTreeWriter``: a scope
# contributes its own cover bins, *then* its children contribute theirs
# (pre-order), and a folded toggle-pair BRANCH contributes exactly two bins in
# the canonical order 0->1, 1->0 -- which is *not* necessarily the order
# ``coverItems()`` yields them in.
#
# Everything that needs to turn a (scope, coveritem) pair into a flat index
# must go through here.  Two independent implementations of this mapping would
# drift silently, and the failure mode is not a crash but coverage attributed
# to the wrong bin.


def scope_bin_names(scope) -> list:
    """Cover-bin names for *scope*, in flat-bin-index order.

    For a folded toggle pair this is the canonical ``0 -> 1``, ``1 -> 0``
    ordering that ``scope_tree.bin`` encodes implicitly, regardless of the
    order the two bins were created in.
    """
    if _is_toggle_pair(scope):
        return [TOGGLE_BIN_0_TO_1, TOGGLE_BIN_1_TO_0]
    return [ci.getName() for ci in scope.coverItems(CoverTypeT.ALL)]


def scope_bin_count(scope) -> int:
    """Number of flat bins *scope* itself contributes (excluding children)."""
    if _is_toggle_pair(scope):
        return 2
    return len(list(scope.coverItems(CoverTypeT.ALL)))


def scope_bin_bases(db) -> dict:
    """Map ``id(scope)`` → the flat bin index of that scope's first bin.

    A scope with no bins maps to the index its first bin *would* have, so
    ``base + local_index`` is well defined for callers that already hold a
    valid local index.
    """
    cached = getattr(db, '_scope_bin_base_cache', None)
    if cached is not None:
        return cached

    bases, next_index = {}, 0
    for scope in dfs_scope_list(db):
        bases[id(scope)] = next_index
        next_index += scope_bin_count(scope)

    db._scope_bin_base_cache = bases
    return bases


def scope_bin_base(db, scope) -> int:
    """Flat bin index of *scope*'s first cover bin.

    Raises ``KeyError`` if *scope* is not reachable in *db*'s DFS -- silently
    returning 0 would point associations at the first bin in the database.
    """
    bases = scope_bin_bases(db)
    try:
        return bases[id(scope)]
    except KeyError:
        raise KeyError(
            "scope %r is not part of this database's DFS traversal; it cannot "
            "be assigned a flat bin index" % (scope.getScopeName(),)) from None


def flat_bin_index(db, scope, local_index: int) -> int:
    """Flat bin index of the *local_index*'th cover bin of *scope*."""
    n = scope_bin_count(scope)
    if not 0 <= local_index < n:
        raise IndexError(
            "local bin index %d out of range for scope %r (%d bins)"
            % (local_index, scope.getScopeName(), n))
    return scope_bin_base(db, scope) + local_index


def iter_flat_bins(db):
    """Yield ``(flat_index, scope, local_index, name)`` for every bin in *db*.

    Flat order, i.e. the order of ``counts.bin``.  ``name`` comes from
    :func:`scope_bin_names`, so toggle pairs are reported canonically.
    """
    flat = 0
    for scope in dfs_scope_list(db):
        for local_index, name in enumerate(scope_bin_names(scope)):
            yield flat, scope, local_index, name
            flat += 1


def total_bin_count(db) -> int:
    """Total number of flat bins in *db* -- the length of ``counts.bin``."""
    return sum(scope_bin_count(s) for s in dfs_scope_list(db))
