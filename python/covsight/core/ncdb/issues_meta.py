"""
ncdb/issues_meta.py — Lazy-loaded display metadata for issues.

:class:`IssuesMeta` holds titles and URLs for issues, indexed by position
(the same index used by :class:`~covsight.core.ncdb.issues.IssueHandle`).
It is stored as the optional ``issues_meta.json`` ZIP member and loaded only
when display data is needed (reports, TUI, CLI listing).

JSON layout::

    {"v": 1, "m": [{"ti": "...", "ur": "..."}, null, {"ti": "..."}, ...]}

A ``null`` entry means no metadata is stored for that issue.
"""
from __future__ import annotations

import json
from typing import List, Optional, Tuple

from covsight.core.ncdb.issues import IssueHandle


class IssuesMeta:
    """Positional metadata store for issue display fields (title and URL).

    Keyed by :attr:`IssueHandle._idx` — entry *i* corresponds to issue
    record *i* in the associated :class:`~covsight.core.ncdb.issues.IssueSet`.
    A ``None`` entry means no display data is stored for that issue.
    """

    def __init__(self) -> None:
        self._entries: List[Optional[Tuple[str, str]]] = []

    # ── Accessors ──────────────────────────────────────────────────────────

    def get_title(self, handle: IssueHandle) -> Optional[str]:
        """Return the title for *handle*, or ``None`` if none is stored."""
        idx = handle._idx
        if idx >= len(self._entries) or self._entries[idx] is None:
            return None
        return self._entries[idx][0] or None

    def get_url(self, handle: IssueHandle) -> Optional[str]:
        """Return the URL for *handle*, or ``None`` if none is stored."""
        idx = handle._idx
        if idx >= len(self._entries) or self._entries[idx] is None:
            return None
        return self._entries[idx][1] or None

    def set_title(self, handle: IssueHandle, title: str) -> None:
        """Set the title for *handle*."""
        self._grow_to(handle._idx)
        entry = self._entries[handle._idx]
        if entry is None:
            self._entries[handle._idx] = (title, "")
        else:
            self._entries[handle._idx] = (title, entry[1])

    def set_url(self, handle: IssueHandle, url: str) -> None:
        """Set the URL for *handle*."""
        self._grow_to(handle._idx)
        entry = self._entries[handle._idx]
        if entry is None:
            self._entries[handle._idx] = ("", url)
        else:
            self._entries[handle._idx] = (entry[0], url)

    # ── Serialization ──────────────────────────────────────────────────────

    def serialize(self) -> bytes:
        """Produce compact JSON bytes for the ``issues_meta.json`` ZIP member."""
        m = []
        for entry in self._entries:
            if entry is None:
                m.append(None)
            else:
                title, url = entry
                obj: dict = {}
                if title:
                    obj["ti"] = title
                if url:
                    obj["ur"] = url
                m.append(obj if obj else None)
        return json.dumps({"v": 1, "m": m}, separators=(',', ':')).encode()

    @classmethod
    def from_bytes(cls, data: bytes) -> 'IssuesMeta':
        """Deserialize ``issues_meta.json`` bytes into an :class:`IssuesMeta`."""
        obj = json.loads(data.decode())
        meta = cls()
        for entry in obj.get("m", []):
            if entry is None:
                meta._entries.append(None)
            else:
                title = entry.get("ti", "") or ""
                url   = entry.get("ur", "") or ""
                meta._entries.append((title, url))
        return meta

    # ── Internal helpers ───────────────────────────────────────────────────

    def _grow_to(self, idx: int) -> None:
        """Extend `_entries` with None entries up to *idx* (inclusive)."""
        while len(self._entries) <= idx:
            self._entries.append(None)
