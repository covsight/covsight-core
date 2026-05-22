"""
attrs.bin — user-defined attribute serialization.

Format v1 (legacy): JSON object
  {"version": 1, "entries": [{"idx": <int>, "attrs": {<key>: <val>}}, ...]}

Format v2 (current): JSON object with sections for scopes, coveritems,
history nodes, and global attrs.
  {"version": 2,
   "scopes": [{"idx": <int>, "attrs": {<key>: <val>}}, ...],
   "coveritems": [{"scope_idx": <int>, "ci_idx": <int>, "attrs": {...}}, ...],
   "history": [{"idx": <int>, "attrs": {...}}, ...],
   "global": {<key>: <val>}}
"""

import json

from .dfs_util import dfs_scope_list
from covsight.core.api import HistoryNodeKind

_VERSION = 2
_COVER_ALL = 0xFFFFFFFF


class AttrsWriter:
    """Serialize user-defined attributes to attrs.bin bytes."""

    def serialize(self, db) -> bytes:
        scopes = dfs_scope_list(db)
        scope_entries = []
        for idx, scope in enumerate(scopes):
            if not hasattr(scope, 'getAttributes'):
                continue
            attrs = scope.getAttributes()
            if attrs:
                scope_entries.append({"idx": idx, "attrs": attrs})

        ci_entries = []
        for idx, scope in enumerate(scopes):
            try:
                items = list(scope.coverItems(_COVER_ALL))
            except Exception:
                continue
            for ci_idx, ci in enumerate(items):
                if not hasattr(ci, 'getAttributes'):
                    continue
                attrs = ci.getAttributes()
                if attrs:
                    ci_entries.append({
                        "scope_idx": idx, "ci_idx": ci_idx, "attrs": attrs
                    })

        hist_entries = []
        for kind in (HistoryNodeKind.TEST, HistoryNodeKind.MERGE):
            try:
                nodes = list(db.historyNodes(kind))
            except Exception:
                continue
            for hi, node in enumerate(nodes):
                if not hasattr(node, 'getAttributes'):
                    continue
                attrs = node.getAttributes()
                if attrs:
                    hist_entries.append({
                        "idx": hi, "kind": kind.name, "attrs": attrs
                    })

        global_attrs = {}
        if hasattr(db, 'getAttributes'):
            global_attrs = db.getAttributes()

        payload = {
            "version": _VERSION,
            "scopes": scope_entries,
            "coveritems": ci_entries,
            "history": hist_entries,
            "global": global_attrs,
        }
        return json.dumps(payload, separators=(',', ':')).encode()


class AttrsReader:
    """Deserialize attrs.bin bytes and apply attributes."""

    def deserialize(self, data: bytes, db) -> None:
        if not data:
            return
        if data[:4] == b"NATR":
            self._deserialize_binary_v3(data, db)
            return
        payload = json.loads(data.decode())
        version = payload.get("version", 1)

        if version == 1:
            self._deserialize_v1(payload, db)
        elif version == 2:
            self._deserialize_v2(payload, db)

    def _deserialize_v1(self, payload, db):
        """Legacy v1: scope attrs only."""
        entries = payload.get("entries", [])
        if not entries:
            return
        scopes = dfs_scope_list(db)
        for entry in entries:
            idx = entry["idx"]
            if idx < len(scopes):
                scope = scopes[idx]
                for key, val in entry.get("attrs", {}).items():
                    if hasattr(scope, 'setAttribute'):
                        scope.setAttribute(key, val)

    def _deserialize_v2(self, payload, db):
        """V2: scopes + coveritems + history + global."""
        scopes = dfs_scope_list(db)

        for entry in payload.get("scopes", []):
            idx = entry["idx"]
            if idx < len(scopes):
                scope = scopes[idx]
                for key, val in entry.get("attrs", {}).items():
                    if hasattr(scope, 'setAttribute'):
                        scope.setAttribute(key, val)

        for entry in payload.get("coveritems", []):
            scope_idx = entry["scope_idx"]
            ci_idx = entry["ci_idx"]
            if scope_idx < len(scopes):
                scope = scopes[scope_idx]
                try:
                    items = list(scope.coverItems(_COVER_ALL))
                    if ci_idx < len(items):
                        ci = items[ci_idx]
                        for key, val in entry.get("attrs", {}).items():
                            if hasattr(ci, 'setAttribute'):
                                ci.setAttribute(key, val)
                except Exception:
                    pass

        hist_nodes = {}
        for kind in (HistoryNodeKind.TEST, HistoryNodeKind.MERGE):
            try:
                hist_nodes[kind.name] = list(db.historyNodes(kind))
            except Exception:
                pass
        for entry in payload.get("history", []):
            kind_name = entry.get("kind", "TEST")
            idx = entry["idx"]
            nodes = hist_nodes.get(kind_name, [])
            if idx < len(nodes):
                node = nodes[idx]
                for key, val in entry.get("attrs", {}).items():
                    if hasattr(node, 'setAttribute'):
                        node.setAttribute(key, val)

        for key, val in payload.get("global", {}).items():
            if hasattr(db, 'setAttribute'):
                db.setAttribute(key, val)

    def apply(self, db, data: bytes) -> None:
        """Alias for deserialize (matches other readers' API)."""
        self.deserialize(data, db)

    # ── Binary v3 (NATR) reader ─────────────────────────────────────────
    # Format matches c/src/ncdb_attrs.c. Currently parses-and-ignores everything
    # except scope/cover/history/db lookups, which would require accessor APIs
    # not yet present on every Python DB type. Parsing validates the payload
    # and is forward-compatible; setAttribute calls are best-effort.
    def _deserialize_binary_v3(self, data: bytes, db) -> None:
        from struct import unpack

        ATTR_INT32, ATTR_INT64, ATTR_FLOAT, ATTR_DOUBLE, ATTR_STRING, ATTR_BYTES = 1, 2, 3, 4, 5, 6

        if data[:4] != b"NATR":
            return
        off = 4
        if off >= len(data):
            return
        version = data[off]; off += 1
        if version != 1:
            return

        def rd_varint(o):
            shift = 0; r = 0
            while True:
                b = data[o]; o += 1
                r |= (b & 0x7F) << shift
                if (b & 0x80) == 0:
                    return r, o
                shift += 7

        def zz_dec(u):
            return (u >> 1) ^ (-(u & 1))

        def rd_entry(o):
            klen, o = rd_varint(o)
            key = data[o:o + klen].decode(); o += klen
            t = data[o]; o += 1
            if t == ATTR_INT32 or t == ATTR_INT64:
                u, o = rd_varint(o); return key, zz_dec(u), o
            if t == ATTR_FLOAT:
                val, = unpack("<f", data[o:o + 4]); o += 4; return key, val, o
            if t == ATTR_DOUBLE:
                val, = unpack("<d", data[o:o + 8]); o += 8; return key, val, o
            if t == ATTR_STRING:
                slen, o = rd_varint(o); val = data[o:o + slen].decode(); o += slen; return key, val, o
            if t == ATTR_BYTES:
                blen, o = rd_varint(o); val = bytes(data[o:o + blen]); o += blen; return key, val, o
            raise ValueError(f"unknown attr type {t}")

        def rd_table(o):
            n, o = rd_varint(o)
            items = []
            for _ in range(n):
                k, v, o = rd_entry(o)
                items.append((k, v))
            return items, o

        scopes = dfs_scope_list(db)

        # DB-level
        global_attrs, off = rd_table(off)
        if hasattr(db, 'setAttribute'):
            for k, v in global_attrs:
                db.setAttribute(k, v)

        # Scope attrs
        n_scope, off = rd_varint(off)
        for _ in range(n_scope):
            idx, off = rd_varint(off)
            items, off = rd_table(off)
            if idx < len(scopes) and hasattr(scopes[idx], 'setAttribute'):
                for k, v in items:
                    scopes[idx].setAttribute(k, v)

        # Cover attrs
        n_cov, off = rd_varint(off)
        for _ in range(n_cov):
            s_idx, off = rd_varint(off)
            c_idx, off = rd_varint(off)
            items, off = rd_table(off)
            if s_idx < len(scopes):
                try:
                    covers = list(scopes[s_idx].coverItems(_COVER_ALL))
                    if c_idx < len(covers) and hasattr(covers[c_idx], 'setAttribute'):
                        for k, v in items:
                            covers[c_idx].setAttribute(k, v)
                except Exception:
                    pass

        # History attrs
        n_hist, off = rd_varint(off)
        hist_nodes = []
        for kind in (HistoryNodeKind.TEST, HistoryNodeKind.MERGE):
            try:
                hist_nodes.extend(db.historyNodes(kind))
            except Exception:
                pass
        for _ in range(n_hist):
            idx, off = rd_varint(off)
            items, off = rd_table(off)
            if idx < len(hist_nodes) and hasattr(hist_nodes[idx], 'setAttribute'):
                for k, v in items:
                    hist_nodes[idx].setAttribute(k, v)
