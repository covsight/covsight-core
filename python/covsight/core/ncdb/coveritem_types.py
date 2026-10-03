"""
coveritem_types.bin — per-coveritem cover type, at_least and goal.

scope_tree.bin records one cover type and one at_least per scope (taken from
the scope's first item) and no goal; a reader gives every item of the scope
those values and goal 0.  That loses data whenever the items differ: a
coverpoint holds ordinary, ignore, illegal and default bins side by side, and
importers set a goal on every item.  This member records each item whose
``(type, at_least, goal)`` differs from what the scope tree alone rebuilds.

Consecutive items with the same values are run-length encoded, so a database
whose scopes are uniform costs about one run per scope.  Items of folded
toggle-pair scopes occupy two slots but are never recorded: the scope tree
rebuilds them canonically.  Readers that do not know this member ignore it
and see the scope tree's values, as before.

Format:
    version:   varint (1)
    num_runs:  varint
    per run:
        delta_idx: varint  (first coveritem DFS index, delta from the end of
                            the previous run)
        length:    varint  (number of consecutive items)
        type:      varint  (CoverTypeT)
        at_least:  varint
        goal:      varint
"""

from covsight.core.api import CoverTypeT

from .varint import encode_varint, decode_varint
from .constants import COVER_TYPE_DEFAULTS
from .dfs_util import dfs_scope_list, _is_toggle_pair

_VERSION = 1


def _scopes(db):
    """Yield ``(first_dfs_index, items)``; ``items`` is None for toggle pairs."""
    idx = 0
    for scope in dfs_scope_list(db):
        if _is_toggle_pair(scope):
            yield idx, None
            idx += 2
            continue
        items = list(scope.coverItems(CoverTypeT.ALL))
        yield idx, items
        idx += len(items)


def _values(cd):
    return (int(cd.type), max(0, int(cd.at_least or 0)), max(0, int(cd.goal or 0)))


class CoveritemTypesWriter:
    """Serialize the items whose type, at_least or goal the scope tree loses."""

    def serialize(self, db) -> bytes:
        runs = []                       # [start, length, values]
        for first, items in _scopes(db):
            if not items:
                continue
            cd0 = items[0].getCoverData()
            rebuilt = (int(cd0.type), max(0, int(cd0.at_least or 0)), 0)
            for i, ci in enumerate(items):
                vals = _values(ci.getCoverData())
                if vals == rebuilt:
                    continue
                idx = first + i
                last = runs[-1] if runs else None
                if last and last[0] + last[1] == idx and last[2] == vals:
                    last[1] += 1
                else:
                    runs.append([idx, 1, vals])
        if not runs:
            return b""

        buf = bytearray()
        buf.extend(encode_varint(_VERSION))
        buf.extend(encode_varint(len(runs)))
        end = 0
        for start, length, (ct, at_least, goal) in runs:
            buf.extend(encode_varint(start - end))
            buf.extend(encode_varint(length))
            buf.extend(encode_varint(ct))
            buf.extend(encode_varint(at_least))
            buf.extend(encode_varint(goal))
            end = start + length
        return bytes(buf)


class CoveritemTypesReader:
    """Apply coveritem_types.bin to a scope tree read from the same archive.

    Must run before coveritem_flags.bin is applied: flag defaults depend on
    the item's cover type.
    """

    def apply(self, db, data: bytes) -> None:
        if not data:
            return
        off = 0
        version, off = decode_varint(data, off)
        if version != _VERSION:
            return
        n, off = decode_varint(data, off)
        runs = []
        end = 0
        for _ in range(n):
            delta, off = decode_varint(data, off)
            length, off = decode_varint(data, off)
            ct, off = decode_varint(data, off)
            at_least, off = decode_varint(data, off)
            goal, off = decode_varint(data, off)
            start = end + delta
            runs.append((start, start + length, CoverTypeT(ct), at_least, goal))
            end = start + length
        if not runs:
            return

        r = 0
        for first, items in _scopes(db):
            if items is None:
                continue
            for i, ci in enumerate(items):
                idx = first + i
                while r < len(runs) and runs[r][1] <= idx:
                    r += 1
                if r == len(runs):
                    return
                start, stop, ct, at_least, goal = runs[r]
                if start <= idx < stop:
                    cd = ci.getCoverData()
                    if cd.type != ct:
                        cd.flags = COVER_TYPE_DEFAULTS.get(ct, (0, 0, 1))[0]
                    cd.type = ct
                    cd.at_least = at_least
                    cd.goal = goal
