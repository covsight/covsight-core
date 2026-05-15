"""
ncdb/issues_history.py — Columnar binary state-transition log for issues.

Wire format documented in ``ISSUES_STORAGE_DESIGN.md`` §4.2.  The file is
self-compressed (LZMA for sealed archives, DEFLATE-1 for live archives) so
that the ZIP layer stores it with ``ZIP_STORED``.

Classes:
    :class:`IssueStateTransition` — read-only record returned by query methods.
    :class:`IssueHistoryWriter`   — accumulates transitions and seals to bytes.
    :class:`IssueHistoryReader`   — decompresses, parses, and queries history.
"""
from __future__ import annotations

import io
import struct
from dataclasses import dataclass
from typing import Dict, Iterator, List, Optional, Tuple

from .varint import encode_varint, decode_varint, encode_varints, decode_varints

# ── Binary format constants ────────────────────────────────────────────────

_MAGIC   = 0x49535348   # 'ISSH'
_VERSION = 1
_NO_COMMENT = 0xFFFF    # sentinel: no comment for this transition

# Header: magic(4) version(1) num_id_strings(2) num_comments(2) num_issues(2) num_records(4) = 15 bytes
_HDR = struct.Struct("<IBHHHH")   # note num_records is u16 here — fix below
_HDR = struct.Struct("<IBHHHI")   # magic version num_id_strings num_comments num_issues num_records

# Issue index entry: id_str_idx(2) start_row(4) count(2) = 8 bytes
_IDX_ENTRY = struct.Struct("<HiH")  # fix: unsigned
_IDX_ENTRY = struct.Struct("<HIH")


# ── IssueStateTransition ──────────────────────────────────────────────────

@dataclass(frozen=True)
class IssueStateTransition:
    """A single state transition for one issue."""
    issue_id:  str
    ts:        int   # unix timestamp
    new_state: int   # STATE_* constant
    comment:   str   # "" if none


# ── IssueHistoryWriter ────────────────────────────────────────────────────

class IssueHistoryWriter:
    """Accumulates state transitions and seals them to a compressed binary payload.

    Example::

        w = IssueHistoryWriter()
        w.add("I-001", ts=1700000000, new_state=STATE_IN_PROGRESS)
        w.add("I-001", ts=1700001000, new_state=STATE_RESOLVED,
              comment="Fixed in PR #42")
        data = w.seal()   # LZMA-compressed bytes
    """

    def __init__(self) -> None:
        # issue_id → list of (ts, state, comment)
        self._transitions: Dict[str, List[Tuple[int, int, str]]] = {}

    def add(self, issue_id: str, ts: int, new_state: int,
            comment: str = "") -> None:
        """Record one state transition for *issue_id*.

        Args:
            issue_id:  Issue identifier.
            ts:        Unix timestamp of the transition.
            new_state: New state (STATE_* constant).
            comment:   Optional free-text comment (empty string if none).
        """
        self._transitions.setdefault(issue_id, []).append(
            (ts, new_state, comment))

    def seal(self, use_lzma: bool = True) -> bytes:
        """Seal all recorded transitions to a compressed binary payload.

        Uses LZMA (FORMAT_XZ) by default; falls back to DEFLATE-9 if the
        ``lzma`` module is unavailable.

        Args:
            use_lzma: If True, prefer LZMA compression.

        Returns:
            Compressed bytes suitable for storage as ``issues_history.bin``.
        """
        raw = self._build_raw()
        if use_lzma:
            try:
                import lzma
                return lzma.compress(raw, format=lzma.FORMAT_XZ)
            except ImportError:
                pass
        import zlib
        return zlib.compress(raw, 9)

    def seal_fast(self) -> bytes:
        """Seal with DEFLATE-1 (fast, low CPU) for live/mutable archives."""
        import zlib
        return zlib.compress(self._build_raw(), 1)

    # ── Internal ──────────────────────────────────────────────────────────

    def _build_raw(self) -> bytes:
        """Build the uncompressed binary payload."""
        # Sort each issue's transitions by timestamp
        for issue_id in self._transitions:
            self._transitions[issue_id].sort(key=lambda r: r[0])

        # Sort issues alphabetically for the index
        sorted_ids = sorted(self._transitions.keys())

        # Build ID string table (u8-length-prefixed)
        id_table: List[str] = sorted_ids
        # Assign each issue an index (position in sorted_ids)

        # Build comment deduplication table
        comment_index: Dict[str, int] = {}
        comment_table: List[str] = []
        for issue_id in sorted_ids:
            for _, _, comment in self._transitions[issue_id]:
                if comment and comment not in comment_index:
                    comment_index[comment] = len(comment_table)
                    comment_table.append(comment)

        # Flatten all rows (id_idx, ts, state, comment_idx)
        all_rows: List[Tuple[int, int, int, int]] = []
        issue_index: List[Tuple[int, int, int]] = []   # (id_str_idx, start_row, count)
        for issue_str_idx, issue_id in enumerate(sorted_ids):
            rows = self._transitions[issue_id]
            start = len(all_rows)
            for ts, state, comment in rows:
                cidx = comment_index[comment] if comment else _NO_COMMENT
                all_rows.append((issue_str_idx, ts, state, cidx))
            issue_index.append((issue_str_idx, start, len(rows)))

        num_records = len(all_rows)
        ts_base = min(row[1] for row in all_rows) if all_rows else 0

        # Build delta-encoded timestamps (reset to 0 at start of each issue group)
        ts_deltas: List[int] = []
        prev_ts_per_issue: Dict[int, int] = {}
        for row in all_rows:
            issue_str_idx, ts, _, _ = row
            prev = prev_ts_per_issue.get(issue_str_idx, ts_base)
            delta = ts - prev
            ts_deltas.append(delta)
            prev_ts_per_issue[issue_str_idx] = ts

        # Assemble binary
        buf = io.BytesIO()

        # Header
        buf.write(_HDR.pack(
            _MAGIC, _VERSION,
            len(id_table),
            len(comment_table),
            len(issue_index),
            num_records,
        ))

        # ID string table
        for s in id_table:
            enc = s.encode("utf-8")
            buf.write(struct.pack("B", len(enc)))
            buf.write(enc)

        # Comment string table
        for s in comment_table:
            enc = s.encode("utf-8")
            buf.write(struct.pack("B", len(enc)))
            buf.write(enc)

        # Issue index
        for id_str_idx, start_row, count in issue_index:
            buf.write(_IDX_ENTRY.pack(id_str_idx, start_row, count))

        # Column arrays
        # ts_base (u32)
        buf.write(struct.pack("<I", ts_base))
        # ts_deltas (varint per record)
        buf.write(encode_varints(ts_deltas))
        # state_bytes (u8 per record)
        buf.write(bytes(row[2] for row in all_rows))
        # comment_idxs (u16 per record)
        for row in all_rows:
            buf.write(struct.pack("<H", row[3]))

        return buf.getvalue()


# ── IssueHistoryReader ────────────────────────────────────────────────────

class IssueHistoryReader:
    """Decompresses and queries an ``issues_history.bin`` payload.

    Decompression and header/table/index parsing happen in ``__init__``.
    Column arrays are accessed via memoryview for zero-copy iteration.

    Example::

        reader = IssueHistoryReader(compressed_data)
        for t in reader.history_for_issue("I-001"):
            print(t.ts, t.new_state)
    """

    def __init__(self, compressed_data: bytes) -> None:
        """Decompress *compressed_data* and parse all tables and the index."""
        data = _decompress(compressed_data)
        offset = 0

        magic, version, num_id_strings, num_comments, num_issues, num_records = \
            _HDR.unpack_from(data, offset)
        offset += _HDR.size

        if magic != _MAGIC:
            raise ValueError(
                f"Invalid issues_history.bin magic: 0x{magic:08X}")
        if version != _VERSION:
            raise ValueError(
                f"Unsupported issues_history.bin version: {version}")

        # ID string table
        id_table: List[str] = []
        for _ in range(num_id_strings):
            slen = data[offset]
            offset += 1
            id_table.append(data[offset: offset + slen].decode("utf-8"))
            offset += slen
        self._id_table = id_table

        # Comment string table
        comment_table: List[str] = []
        for _ in range(num_comments):
            slen = data[offset]
            offset += 1
            comment_table.append(data[offset: offset + slen].decode("utf-8"))
            offset += slen
        self._comment_table = comment_table

        # Issue index
        issue_index: List[Tuple[int, int, int]] = []
        for _ in range(num_issues):
            id_str_idx, start_row, count = _IDX_ENTRY.unpack_from(data, offset)
            offset += _IDX_ENTRY.size
            issue_index.append((id_str_idx, start_row, count))
        self._issue_index = issue_index

        # Build id → index lookup
        self._id_to_issue_idx: Dict[str, int] = {
            id_table[e[0]]: i for i, e in enumerate(issue_index)
        }

        # Column arrays
        self._ts_base: int = struct.unpack_from("<I", data, offset)[0]
        offset += 4

        # ts_deltas — decode all varints at once
        ts_deltas, offset = decode_varints(data, num_records, offset)
        self._ts_deltas = ts_deltas

        # state_bytes — raw bytes view
        self._state_bytes: bytes = data[offset: offset + num_records]
        offset += num_records

        # comment_idxs — list of u16
        self._comment_idxs: List[int] = list(
            struct.unpack_from(f"<{num_records}H", data, offset)
        ) if num_records else []

        self._num_records = num_records

    # ── Query API ──────────────────────────────────────────────────────────

    def history_for_issue(self, issue_id: str) -> Iterator[IssueStateTransition]:
        """Yield state transitions for *issue_id* in chronological order.

        Complexity: O(log n) lookup + O(k) iteration.
        """
        entry_idx = self._id_to_issue_idx.get(issue_id)
        if entry_idx is None:
            return
        id_str_idx, start_row, count = self._issue_index[entry_idx]
        yield from self._iter_rows(issue_id, start_row, count)

    def state_at(self, issue_id: str, ts: int) -> Optional[int]:
        """Return the state of *issue_id* at unix timestamp *ts*.

        Returns ``None`` if the issue has no history before *ts*.

        Complexity: O(log n + k) via linear scan of the issue's row range.
        """
        last_state: Optional[int] = None
        curr_ts = self._ts_base
        entry_idx = self._id_to_issue_idx.get(issue_id)
        if entry_idx is None:
            return None
        _, start_row, count = self._issue_index[entry_idx]
        for i in range(start_row, start_row + count):
            curr_ts += self._ts_deltas[i]
            if curr_ts > ts:
                break
            last_state = self._state_bytes[i]
        return last_state

    def all_transitions(self) -> Iterator[IssueStateTransition]:
        """Yield all transitions in order sorted by (issue_id, ts).

        O(1) peak memory (generator).
        """
        for entry_idx, (id_str_idx, start_row, count) in enumerate(self._issue_index):
            issue_id = self._id_table[id_str_idx]
            yield from self._iter_rows(issue_id, start_row, count)

    # ── Internal ──────────────────────────────────────────────────────────

    def _iter_rows(self, issue_id: str, start_row: int,
                   count: int) -> Iterator[IssueStateTransition]:
        curr_ts = self._ts_base
        for i in range(start_row, start_row + count):
            curr_ts += self._ts_deltas[i]
            state   = self._state_bytes[i]
            cidx    = self._comment_idxs[i]
            comment = self._comment_table[cidx] if cidx != _NO_COMMENT else ""
            yield IssueStateTransition(issue_id, curr_ts, state, comment)


# ── Decompression helper ───────────────────────────────────────────────────

def _decompress(data: bytes) -> bytes:
    """Auto-detect LZMA or DEFLATE and decompress *data*."""
    # XZ magic: FD 37 7A 58 5A 00
    if data[:6] == b'\xfd7zXZ\x00':
        import lzma
        return lzma.decompress(data, format=lzma.FORMAT_XZ)
    # zlib: first byte 0x78 (deflate with zlib wrapper)
    import zlib
    return zlib.decompress(data)
