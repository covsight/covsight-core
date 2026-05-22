"""
manifest.json — NCDB archive manifest.

Stores format identity, version, statistics, and the schema hash that
enables the same-schema fast-merge path.
"""

import hashlib
import json
from dataclasses import dataclass, field, asdict
from datetime import datetime, timezone
from typing import Optional

from .constants import NCDB_FORMAT, NCDB_VERSION, NCDB_GENERATOR, HISTORY_FORMAT_V1


def _enc_varint(v: int) -> bytes:
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        if v:
            out.append(b | 0x80)
        else:
            out.append(b); return bytes(out)


def _dec_varint(data: bytes, off: int):
    r = 0; shift = 0
    while True:
        b = data[off]; off += 1
        r |= (b & 0x7F) << shift
        if (b & 0x80) == 0:
            return r, off
        shift += 7


@dataclass
class Manifest:
    format:         str = NCDB_FORMAT
    version:        str = NCDB_VERSION
    ucis_version:   str = "1.0"
    created:        str = ""
    path_separator: str = "/"
    scope_count:    int = 0
    coveritem_count:int = 0
    test_count:     int = 0
    total_hits:     int = 0
    covered_bins:   int = 0
    schema_hash:    str = ""
    generator:      str = NCDB_GENERATOR
    history_format: str = HISTORY_FORMAT_V1   # "v1" (JSON) or "v2" (binary + JSON)
    vendor_id:           str = ""
    vendor_tool:         str = ""
    vendor_tool_version: str = ""
    ucis_standard:       str = ""

    _MAGIC = b"NMAN"
    _BIN_VERSION = 1
    _BIN_STRINGS = (
        "format", "version", "ucis_version", "created", "path_separator",
        "schema_hash", "generator", "history_format",
        "vendor_id", "vendor_tool", "vendor_tool_version", "ucis_standard",
    )
    _BIN_NUMBERS = (
        "scope_count", "coveritem_count", "test_count",
        "total_hits", "covered_bins",
    )

    def serialize(self) -> bytes:
        out = bytearray()
        out += self._MAGIC
        out.append(self._BIN_VERSION)
        for attr in self._BIN_STRINGS:
            s = (getattr(self, attr) or "").encode("utf-8")
            out += _enc_varint(len(s)); out += s
        for attr in self._BIN_NUMBERS:
            out += _enc_varint(int(getattr(self, attr) or 0))
        return bytes(out)

    @classmethod
    def from_bytes(cls, data: bytes) -> "Manifest":
        if data[:4] == cls._MAGIC:
            return cls._from_binary(data)
        d = json.loads(data.decode("utf-8"))
        m = cls()
        for k, v in d.items():
            if hasattr(m, k):
                setattr(m, k, v)
        return m

    @classmethod
    def _from_binary(cls, data: bytes) -> "Manifest":
        o = 4
        version = data[o]; o += 1
        if version != cls._BIN_VERSION:
            raise ValueError(f"unsupported manifest binary version {version}")
        m = cls()
        for attr in cls._BIN_STRINGS:
            n, o = _dec_varint(data, o)
            setattr(m, attr, data[o:o + n].decode("utf-8")); o += n
        for attr in cls._BIN_NUMBERS:
            v, o = _dec_varint(data, o)
            setattr(m, attr, v)
        return m

    @staticmethod
    def compute_schema_hash(scope_tree_bytes: bytes) -> str:
        """SHA-256 of the *uncompressed* scope_tree.bin content."""
        digest = hashlib.sha256(scope_tree_bytes).hexdigest()
        return f"sha256:{digest}"

    @classmethod
    def build(cls, db, scope_tree_bytes: bytes,
              counts: list, history_nodes: list) -> "Manifest":
        """Build a Manifest from a UCIS database and serialized members."""
        from covsight.core.api import ScopeTypeT
        from covsight.core.api import CoverTypeT

        total_hits   = sum(counts)
        covered_bins = sum(1 for c in counts if c > 0)

        # Count history TEST nodes
        from covsight.core.api import HistoryNodeKind
        test_count = sum(
            1 for n in history_nodes
            if n.getKind() == HistoryNodeKind.TEST
        )

        return cls(
            created=datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            path_separator=db.getPathSeparator()
                if hasattr(db, 'getPathSeparator') else "/",
            coveritem_count=len(counts),
            test_count=test_count,
            total_hits=total_hits,
            covered_bins=covered_bins,
            schema_hash=cls.compute_schema_hash(scope_tree_bytes),
        )
