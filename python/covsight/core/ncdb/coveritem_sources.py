"""
coveritem_sources.bin — per-coveritem source locations.

scope_tree.bin records a source location for scopes only.  Code coverage
needs one per item -- a BLOCK scope holds every statement of an instance --
so line-level views (per-file reports, LCOV) depend on this member.

Stores items that have a source file as sparse delta-encoded entries keyed by
coveritem DFS index (the same indexing as coveritem_flags.bin).  Items of
folded toggle-pair scopes occupy two slots but are never recorded: their bin
order is canonical rather than creation order.  ``file_id`` indexes the
sources member.

Format:
    version:     varint (1)
    num_entries: varint
    per entry:
        delta_idx: varint  (coveritem DFS index delta from previous)
        file_id:   varint
        line:      varint
        token:     varint
"""

from covsight.core.api import CoverTypeT, SourceInfo

from .varint import encode_varint, decode_varint
from .dfs_util import dfs_scope_list, _is_toggle_pair

_VERSION = 1


def _items(db):
    """Yield ``(dfs_index, coveritem or None)``; None for toggle-pair slots."""
    idx = 0
    for scope in dfs_scope_list(db):
        if _is_toggle_pair(scope):
            yield idx, None
            yield idx + 1, None
            idx += 2
            continue
        for ci in scope.coverItems(CoverTypeT.ALL):
            yield idx, ci
            idx += 1


class CoveritemSourcesWriter:
    """Serialize coveritem source locations.

    ``file_id`` maps a file handle to its index in the sources member; pass
    the scope-tree writer's mapping so both members share one file table.
    """

    def __init__(self, file_id):
        self._file_id = file_id

    def serialize(self, db) -> bytes:
        entries = []
        for idx, ci in _items(db):
            if ci is None:
                continue
            si = ci.getSourceInfo()
            if si is None or si.file is None:
                continue
            entries.append((idx, self._file_id(si.file),
                            max(0, si.line or 0), max(0, si.token or 0)))
        if not entries:
            return b""

        buf = bytearray()
        buf.extend(encode_varint(_VERSION))
        buf.extend(encode_varint(len(entries)))
        prev = 0
        for idx, fid, line, token in entries:
            buf.extend(encode_varint(idx - prev))
            buf.extend(encode_varint(fid))
            buf.extend(encode_varint(line))
            buf.extend(encode_varint(token))
            prev = idx
        return bytes(buf)


class CoveritemSourcesReader:
    """Apply coveritem_sources.bin to a scope tree read from the same archive."""

    def apply(self, db, data: bytes, file_handles: list) -> None:
        if not data:
            return
        off = 0
        version, off = decode_varint(data, off)
        if version != _VERSION:
            return
        n, off = decode_varint(data, off)
        want = {}
        idx = 0
        for _ in range(n):
            delta, off = decode_varint(data, off)
            fid, off = decode_varint(data, off)
            line, off = decode_varint(data, off)
            token, off = decode_varint(data, off)
            idx += delta
            want[idx] = (fid, line, token)
        if not want:
            return

        for idx, ci in _items(db):
            ent = want.get(idx)
            if ent is None or ci is None or not hasattr(ci, "setSourceInfo"):
                continue
            fid, line, token = ent
            fh = file_handles[fid] if fid < len(file_handles) else None
            if fh is not None:
                ci.setSourceInfo(SourceInfo(fh, line, token))
