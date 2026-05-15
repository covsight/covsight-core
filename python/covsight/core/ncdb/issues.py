"""
ncdb/issues.py — Issue tracking data model for the NCDB format.

Provides :class:`IssueSet` (binary-serializable backing store) plus the
read-only :class:`IssueHandle` view, the write-path :class:`IssueSpec`
container, and link-info dataclasses.

Binary member name: ``issues.bin``.  Wire format documented in
``ISSUES_STORAGE_DESIGN.md`` §3.3.
"""
from __future__ import annotations

import struct
from array import array
from dataclasses import dataclass
from typing import Dict, Iterator, List, Optional, Tuple

from .string_table import StringTable

# ── Severity constants ─────────────────────────────────────────────────────

SEV_INFO, SEV_LOW, SEV_MEDIUM, SEV_HIGH, SEV_CRITICAL = 0, 1, 2, 3, 4
SEV_NAMES = {
    SEV_INFO:     "info",
    SEV_LOW:      "low",
    SEV_MEDIUM:   "medium",
    SEV_HIGH:     "high",
    SEV_CRITICAL: "critical",
}

# ── Kind constants ─────────────────────────────────────────────────────────

KIND_DESIGN_BUG, KIND_TEST_BUG, KIND_INFRA, KIND_SPEC_GAP = 0, 1, 2, 3
KIND_NAMES = {
    KIND_DESIGN_BUG: "design-bug",
    KIND_TEST_BUG:   "test-bug",
    KIND_INFRA:      "infra",
    KIND_SPEC_GAP:   "spec-gap",
}

# ── State constants ────────────────────────────────────────────────────────

STATE_OPEN, STATE_IN_PROGRESS, STATE_RESOLVED, STATE_CLOSED, STATE_WONTFIX \
    = 0, 1, 2, 3, 4
STATE_NAMES = {
    STATE_OPEN:        "open",
    STATE_IN_PROGRESS: "in-progress",
    STATE_RESOLVED:    "resolved",
    STATE_CLOSED:      "closed",
    STATE_WONTFIX:     "wontfix",
}

# ── Resolution constants ───────────────────────────────────────────────────

RES_NONE, RES_FIXED, RES_WONT_FIX, RES_DUPLICATE, RES_NOT_A_BUG = 0, 1, 2, 3, 4
RES_NAMES = {
    RES_NONE:       "none",
    RES_FIXED:      "fixed",
    RES_WONT_FIX:   "wont-fix",
    RES_DUPLICATE:  "duplicate",
    RES_NOT_A_BUG:  "not-a-bug",
}

# ── Link type constants ────────────────────────────────────────────────────

LINK_BLOCKED_BY, LINK_CAUSED_BY, LINK_RELATED = 0, 1, 2
LINK_NAMES = {
    LINK_BLOCKED_BY: "blocked-by",
    LINK_CAUSED_BY:  "caused-by",
    LINK_RELATED:    "related",
}

# ── Binary format constants ────────────────────────────────────────────────

_MAGIC   = 0x49535342   # 'ISSB'
_VERSION = 1

# Header: magic(4) version(1) synced_at(4) num_issues(4) num_wl(4) num_tl(4) num_cl(4) = 25 bytes
_HDR = struct.Struct("<IBIIIiI")  # note: using signed i for num_tl placeholder; fix below
_HDR = struct.Struct("<IBIIIII")  # magic version synced_at num_issues num_wl num_tl num_cl

# Issue record: id_idx(2) ext_idx(2) enums(2) created_at(4) updated_at(4) synced_at(4) = 18 bytes
_ISSUE_REC = struct.Struct("<HHHIiI")  # fix: all unsigned
_ISSUE_REC = struct.Struct("<HHHIII")

# WaiverIssueLink record: waiver_id_idx(2) issue_id_idx(2) = 4 bytes
_WL_REC = struct.Struct("<HH")

# TestpointIssueLink record: tp_name_idx(2) issue_id_idx(2) link_type(1) = 5 bytes
_TL_REC = struct.Struct("<HHB")

# CoverageIssueLink record: scope_idx(2) bin_idx(2) issue_id_idx(2) link_type(1) = 7 bytes
_CL_REC = struct.Struct("<HHHB")


# ── Enum packing helpers ───────────────────────────────────────────────────

def _pack_enums(severity: int, kind: int, state: int, resolution: int) -> int:
    """Pack severity/kind/state/resolution into a u16 bitmask.

    Bits 15:13 severity, 12:11 kind, 10:8 state, 7:5 resolution, 4:0 reserved.
    """
    return ((severity & 0x7) << 13) | ((kind & 0x3) << 11) | \
           ((state & 0x7) << 8) | ((resolution & 0x7) << 5)


def _unpack_enums(v: int) -> Tuple[int, int, int, int]:
    """Unpack a u16 enums field into (severity, kind, state, resolution)."""
    severity   = (v >> 13) & 0x7
    kind       = (v >> 11) & 0x3
    state      = (v >> 8)  & 0x7
    resolution = (v >> 5)  & 0x7
    return severity, kind, state, resolution


# ── Link info dataclasses ──────────────────────────────────────────────────

@dataclass(frozen=True)
class WaiverIssueLinkInfo:
    """A link from a waiver to an issue."""
    waiver_id: str
    issue_id:  str


@dataclass(frozen=True)
class TestpointIssueLinkInfo:
    """A link from a testpoint to an issue."""
    testpoint_name: str
    issue_id:       str
    link_type:      int


@dataclass(frozen=True)
class CoverageIssueLinkInfo:
    """A link from a coverage bin to an issue."""
    scope_path: str
    bin_name:   str
    issue_id:   str
    link_type:  int


# ── IssueSpec (write path) ─────────────────────────────────────────────────

@dataclass
class IssueSpec:
    """Mutable specification for creating or updating an issue.

    Never returned by queries; used only on the write path.
    """
    id:         str
    ext:        str = ""
    severity:   int = SEV_MEDIUM
    kind:       int = KIND_DESIGN_BUG
    state:      int = STATE_OPEN
    resolution: int = RES_NONE
    created_at: int = 0
    updated_at: int = 0
    synced_at:  int = 0


# ── IssueHandle (read path) ────────────────────────────────────────────────

class IssueHandle:
    """Opaque read-only view of a single issue record inside an :class:`IssueSet`.

    Instances are created by :meth:`IssueSet.add_issue` and
    :meth:`IssueSet.get`; they must not be constructed directly.
    Construction from outside this module raises :class:`TypeError`.
    """

    __slots__ = ('_owner', '_idx')
    _INTERNAL_TOKEN = object()   # package-private sentinel

    def __init__(self, _token, owner: 'IssueSet', idx: int):
        if _token is not IssueHandle._INTERNAL_TOKEN:
            raise TypeError(
                "IssueHandle cannot be constructed directly; "
                "use IssueSet.add_issue() or IssueSet.get()"
            )
        self._owner = owner
        self._idx   = idx

    # ── field accessors ───────────────────────────────────────────────────

    @property
    def id(self) -> str:
        """Unique issue identifier."""
        return self._owner._strings.get(self._owner._id_idxs[self._idx])

    @property
    def ext(self) -> str:
        """External reference (empty string if none)."""
        return self._owner._strings.get(self._owner._ext_idxs[self._idx])

    @property
    def severity(self) -> int:
        """Severity constant (SEV_*)."""
        sev, _, _, _ = _unpack_enums(self._owner._enums[self._idx])
        return sev

    @property
    def kind(self) -> int:
        """Kind constant (KIND_*)."""
        _, knd, _, _ = _unpack_enums(self._owner._enums[self._idx])
        return knd

    @property
    def state(self) -> int:
        """State constant (STATE_*)."""
        _, _, st, _ = _unpack_enums(self._owner._enums[self._idx])
        return st

    @property
    def resolution(self) -> int:
        """Resolution constant (RES_*)."""
        _, _, _, res = _unpack_enums(self._owner._enums[self._idx])
        return res

    @property
    def created_at(self) -> int:
        """Unix timestamp when the issue was created."""
        return self._owner._created[self._idx]

    @property
    def updated_at(self) -> int:
        """Unix timestamp of the most recent update."""
        return self._owner._updated[self._idx]

    @property
    def synced_at(self) -> int:
        """Unix timestamp of the most recent sync from the external tracker."""
        return self._owner._synced[self._idx]

    # ── convenience helpers ───────────────────────────────────────────────

    def is_open(self) -> bool:
        """Return True if the issue is open or in-progress."""
        return self.state in (STATE_OPEN, STATE_IN_PROGRESS)

    def is_closed(self) -> bool:
        """Return True if the issue is resolved, closed, or wontfix."""
        return self.state in (STATE_RESOLVED, STATE_CLOSED, STATE_WONTFIX)

    def __repr__(self) -> str:
        return f"IssueHandle(id={self.id!r}, state={STATE_NAMES[self.state]})"


# ── IssueSet ───────────────────────────────────────────────────────────────

class IssueSet:
    """Binary-serializable collection of issues with link tables.

    All data is stored in compact parallel arrays (``array`` module) plus a
    shared :class:`StringTable` for string deduplication.  Lazy indices are
    built on the first query and invalidated by any mutation.

    Serializes to / deserializes from the ``issues.bin`` ZIP member as
    documented in ``ISSUES_STORAGE_DESIGN.md`` §3.3.
    """

    def __init__(self) -> None:
        # Parallel arrays — one entry per issue record
        self._id_idxs:  array = array('H')   # u16 string-table indices for IDs
        self._ext_idxs: array = array('H')   # u16 string-table indices for ext refs
        self._enums:    array = array('H')   # packed u16 severity/kind/state/res
        self._created:  array = array('I')   # u32 unix timestamps
        self._updated:  array = array('I')
        self._synced:   array = array('I')

        self._strings: StringTable = StringTable()
        self._strings.add("")  # ensure index 0 is always empty string

        # Link tables
        self._wl: List[Tuple[int, int]]             = []   # (waiver_id_idx, issue_id_idx)
        self._tl: List[Tuple[int, int, int]]         = []   # (tp_name_idx, issue_id_idx, lt)
        self._cl: List[Tuple[int, int, int, int]]    = []   # (scope_idx, bin_idx, issue_id_idx, lt)

        # Lazy indices (built on first use, None = dirty/unbuilt)
        self._by_id:                  Optional[Dict[str, int]]                       = None
        self._by_severity:            Optional[Dict[int, List[int]]]                 = None
        self._open_idxs:              Optional[List[int]]                            = None
        self._waiver_to_idxs:         Optional[Dict[str, List[int]]]                 = None
        self._issue_to_waiver_links:  Optional[Dict[str, List[WaiverIssueLinkInfo]]] = None
        self._tp_to_idxs:             Optional[Dict[str, List[int]]]                 = None
        self._issue_to_tp_links:      Optional[Dict[str, List[TestpointIssueLinkInfo]]] = None
        self._coverage_links_cache:   Optional[List[CoverageIssueLinkInfo]]          = None

    # ── Internal helpers ───────────────────────────────────────────────────

    def _invalidate_indices(self) -> None:
        self._by_id               = None
        self._by_severity         = None
        self._open_idxs           = None
        self._waiver_to_idxs      = None
        self._issue_to_waiver_links  = None
        self._tp_to_idxs          = None
        self._issue_to_tp_links   = None
        self._coverage_links_cache = None

    def _ensure_by_id(self) -> None:
        if self._by_id is None:
            self._by_id = {
                self._strings.get(self._id_idxs[i]): i
                for i in range(len(self._id_idxs))
            }

    def _ensure_severity_index(self) -> None:
        if self._by_severity is None:
            self._by_severity = {}
            for i in range(len(self._enums)):
                sev, _, _, _ = _unpack_enums(self._enums[i])
                self._by_severity.setdefault(sev, []).append(i)

    def _ensure_open_index(self) -> None:
        if self._open_idxs is None:
            self._open_idxs = [
                i for i in range(len(self._enums))
                if _unpack_enums(self._enums[i])[2] in (STATE_OPEN, STATE_IN_PROGRESS)
            ]

    def _ensure_waiver_indices(self) -> None:
        if self._waiver_to_idxs is not None:
            return
        self._ensure_by_id()
        w2i: Dict[str, List[int]] = {}
        i2w: Dict[str, List[WaiverIssueLinkInfo]] = {}
        for wid_idx, iid_idx in self._wl:
            wid = self._strings.get(wid_idx)
            iid = self._strings.get(iid_idx)
            # waiver → issue indices
            issue_idx = self._by_id.get(iid)
            if issue_idx is not None:
                w2i.setdefault(wid, []).append(issue_idx)
            # issue → waiver links
            i2w.setdefault(iid, []).append(WaiverIssueLinkInfo(wid, iid))
        self._waiver_to_idxs = w2i
        self._issue_to_waiver_links = i2w

    def _ensure_tp_indices(self) -> None:
        if self._tp_to_idxs is not None:
            return
        self._ensure_by_id()
        t2i: Dict[str, List[int]] = {}
        i2t: Dict[str, List[TestpointIssueLinkInfo]] = {}
        for tp_idx, iid_idx, lt in self._tl:
            tp   = self._strings.get(tp_idx)
            iid  = self._strings.get(iid_idx)
            issue_idx = self._by_id.get(iid)
            if issue_idx is not None:
                t2i.setdefault(tp, []).append(issue_idx)
            i2t.setdefault(iid, []).append(
                TestpointIssueLinkInfo(tp, iid, lt))
        self._tp_to_idxs = t2i
        self._issue_to_tp_links = i2t

    def _ensure_coverage_links(self) -> None:
        if self._coverage_links_cache is None:
            self._coverage_links_cache = [
                CoverageIssueLinkInfo(
                    self._strings.get(s_idx),
                    self._strings.get(b_idx),
                    self._strings.get(i_idx),
                    lt,
                )
                for s_idx, b_idx, i_idx, lt in self._cl
            ]

    def _make_handle(self, idx: int) -> IssueHandle:
        return IssueHandle(IssueHandle._INTERNAL_TOKEN, self, idx)

    # ── Write API ──────────────────────────────────────────────────────────

    def add_issue(self, spec: IssueSpec) -> IssueHandle:
        """Add a new issue from *spec* and return its handle.

        Raises:
            ValueError: if ``spec.id`` already exists in this set.
        """
        self._ensure_by_id()
        if spec.id in self._by_id:
            raise ValueError(f"Issue id {spec.id!r} already exists")
        idx = len(self._id_idxs)
        self._id_idxs.append(self._strings.add(spec.id))
        self._ext_idxs.append(self._strings.add(spec.ext or ""))
        self._enums.append(_pack_enums(spec.severity, spec.kind,
                                       spec.state, spec.resolution))
        self._created.append(spec.created_at)
        self._updated.append(spec.updated_at)
        self._synced.append(spec.synced_at)
        # Update the ID index directly (avoids full rebuild)
        self._by_id[spec.id] = idx
        # Invalidate all other indices
        self._by_severity    = None
        self._open_idxs      = None
        return self._make_handle(idx)

    def update_issue(self, handle: IssueHandle, spec: IssueSpec) -> None:
        """Replace mutable fields of *handle* with data from *spec*.

        The handle's internal index continues to point to the same record.
        """
        idx = handle._idx
        self._ext_idxs[idx] = self._strings.add(spec.ext or "")
        self._enums[idx]    = _pack_enums(spec.severity, spec.kind,
                                          spec.state, spec.resolution)
        self._created[idx]  = spec.created_at
        self._updated[idx]  = spec.updated_at
        self._synced[idx]   = spec.synced_at
        self._invalidate_indices()

    def add_waiver_link(self, waiver_id: str, issue_id: str) -> None:
        """Record that *waiver_id* is linked to *issue_id*."""
        self._wl.append((self._strings.add(waiver_id),
                         self._strings.add(issue_id)))
        self._waiver_to_idxs     = None
        self._issue_to_waiver_links = None

    def add_testpoint_link(self, tp_name: str, issue_id: str,
                           link_type: int = LINK_BLOCKED_BY) -> None:
        """Record that testpoint *tp_name* is linked to *issue_id*."""
        self._tl.append((self._strings.add(tp_name),
                         self._strings.add(issue_id),
                         link_type))
        self._tp_to_idxs       = None
        self._issue_to_tp_links = None

    def add_coverage_link(self, scope_path: str, bin_name: str,
                          issue_id: str,
                          link_type: int = LINK_BLOCKED_BY) -> None:
        """Record that coverage bin (*scope_path*, *bin_name*) is linked to *issue_id*."""
        self._cl.append((self._strings.add(scope_path),
                         self._strings.add(bin_name),
                         self._strings.add(issue_id),
                         link_type))
        self._coverage_links_cache = None

    # ── Query API ──────────────────────────────────────────────────────────

    def issues(self) -> Iterator[IssueHandle]:
        """Yield all issues in insertion order."""
        for i in range(len(self._id_idxs)):
            yield self._make_handle(i)

    def get(self, issue_id: str) -> Optional[IssueHandle]:
        """Return the handle for *issue_id*, or ``None`` if not found."""
        self._ensure_by_id()
        idx = self._by_id.get(issue_id)
        if idx is None:
            return None
        return self._make_handle(idx)

    def open_issues(self) -> Iterator[IssueHandle]:
        """Yield all open or in-progress issues."""
        self._ensure_open_index()
        for i in self._open_idxs:
            yield self._make_handle(i)

    def issues_by_severity(self, severity: int) -> Iterator[IssueHandle]:
        """Yield all issues with the given *severity* (SEV_* constant)."""
        self._ensure_severity_index()
        for i in self._by_severity.get(severity, []):
            yield self._make_handle(i)

    def issues_for_waiver(self, waiver_id: str) -> Iterator[IssueHandle]:
        """Yield all issues linked to *waiver_id*."""
        self._ensure_waiver_indices()
        for i in self._waiver_to_idxs.get(waiver_id, []):
            yield self._make_handle(i)

    def issues_for_testpoint(self, tp_name: str) -> Iterator[IssueHandle]:
        """Yield all issues linked to testpoint *tp_name*."""
        self._ensure_tp_indices()
        for i in self._tp_to_idxs.get(tp_name, []):
            yield self._make_handle(i)

    def waivers_for_issue(self, handle: IssueHandle) -> Iterator[WaiverIssueLinkInfo]:
        """Yield all waiver–issue links for the given *handle*."""
        self._ensure_waiver_indices()
        issue_id = handle.id
        yield from self._issue_to_waiver_links.get(issue_id, [])

    def testpoints_for_issue(self, handle: IssueHandle) -> Iterator[TestpointIssueLinkInfo]:
        """Yield all testpoint–issue links for the given *handle*."""
        self._ensure_tp_indices()
        issue_id = handle.id
        yield from self._issue_to_tp_links.get(issue_id, [])

    def coverage_links(self) -> Iterator[CoverageIssueLinkInfo]:
        """Yield all coverage–issue links."""
        self._ensure_coverage_links()
        yield from self._coverage_links_cache

    # ── Serialization ──────────────────────────────────────────────────────

    def serialize(self) -> bytes:
        """Produce the ``issues.bin`` binary payload.

        Layout: header (25 bytes) | string table | issue records (18 B each) |
        waiver links (4 B each) | testpoint links (5 B each) |
        coverage links (7 B each).
        """
        import io
        buf = io.BytesIO()

        n = len(self._id_idxs)
        # Header
        buf.write(_HDR.pack(
            _MAGIC, _VERSION, 0,
            n,
            len(self._wl),
            len(self._tl),
            len(self._cl),
        ))

        # String table
        buf.write(self._strings.serialize())

        # Issue records
        for i in range(n):
            buf.write(_ISSUE_REC.pack(
                self._id_idxs[i],
                self._ext_idxs[i],
                self._enums[i],
                self._created[i],
                self._updated[i],
                self._synced[i],
            ))

        # Waiver link records
        for wid_idx, iid_idx in self._wl:
            buf.write(_WL_REC.pack(wid_idx, iid_idx))

        # Testpoint link records
        for tp_idx, iid_idx, lt in self._tl:
            buf.write(_TL_REC.pack(tp_idx, iid_idx, lt))

        # Coverage link records
        for s_idx, b_idx, iid_idx, lt in self._cl:
            buf.write(_CL_REC.pack(s_idx, b_idx, iid_idx, lt))

        return buf.getvalue()

    @classmethod
    def from_bytes(cls, data: bytes) -> 'IssueSet':
        """Deserialize an ``issues.bin`` payload into an :class:`IssueSet`.

        Raises:
            ValueError: if the magic or version is not recognized.
        """
        offset = 0
        magic, version, synced_at_hdr, num_issues, num_wl, num_tl, num_cl = \
            _HDR.unpack_from(data, offset)
        offset += _HDR.size

        if magic != _MAGIC:
            raise ValueError(
                f"Invalid issues.bin magic: 0x{magic:08X} (expected 0x{_MAGIC:08X})")
        if version != _VERSION:
            raise ValueError(
                f"Unsupported issues.bin version: {version}")

        # String table
        strings = StringTable.from_bytes(data[offset:])
        # Skip past the string table bytes
        from .varint import decode_varint
        count, off2 = decode_varint(data, offset)
        for _ in range(count):
            slen, off2 = decode_varint(data, off2)
            off2 += slen
        offset = off2

        obj = cls.__new__(cls)
        obj._strings = strings
        obj._id_idxs  = array('H')
        obj._ext_idxs = array('H')
        obj._enums    = array('H')
        obj._created  = array('I')
        obj._updated  = array('I')
        obj._synced   = array('I')
        obj._wl = []
        obj._tl = []
        obj._cl = []
        obj._by_id               = None
        obj._by_severity         = None
        obj._open_idxs           = None
        obj._waiver_to_idxs      = None
        obj._issue_to_waiver_links  = None
        obj._tp_to_idxs          = None
        obj._issue_to_tp_links   = None
        obj._coverage_links_cache = None

        # Issue records
        for _ in range(num_issues):
            id_idx, ext_idx, enums, created, updated, synced = \
                _ISSUE_REC.unpack_from(data, offset)
            offset += _ISSUE_REC.size
            obj._id_idxs.append(id_idx)
            obj._ext_idxs.append(ext_idx)
            obj._enums.append(enums)
            obj._created.append(created)
            obj._updated.append(updated)
            obj._synced.append(synced)

        # Waiver link records
        for _ in range(num_wl):
            wid_idx, iid_idx = _WL_REC.unpack_from(data, offset)
            offset += _WL_REC.size
            obj._wl.append((wid_idx, iid_idx))

        # Testpoint link records
        for _ in range(num_tl):
            tp_idx, iid_idx, lt = _TL_REC.unpack_from(data, offset)
            offset += _TL_REC.size
            obj._tl.append((tp_idx, iid_idx, lt))

        # Coverage link records
        for _ in range(num_cl):
            s_idx, b_idx, iid_idx, lt = _CL_REC.unpack_from(data, offset)
            offset += _CL_REC.size
            obj._cl.append((s_idx, b_idx, iid_idx, lt))

        return obj

    def __len__(self) -> int:
        return len(self._id_idxs)
