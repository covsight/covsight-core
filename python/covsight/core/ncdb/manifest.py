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
    # v4 schema-version fields (Phase 4.8 / M8). Default values match a v3
    # fixture: numeric schema 3.0, no v4 features, derived counts at zero.
    schema_version_major: int = 3
    schema_version_minor: int = 0
    feature_flags:        int = 0
    n_history_nodes:      int = 0
    n_associations:       int = 0

    _MAGIC = b"NMAN"
    _BIN_VERSION = 2   # v2 = M8 (Phase 4.8)
    _BIN_VERSION_V1 = 1  # legacy; still accepted on deserialize
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
        # v2 extension
        out += int(self.schema_version_major).to_bytes(4, "little")
        out += int(self.schema_version_minor).to_bytes(4, "little")
        out += int(self.feature_flags).to_bytes(8, "little")
        out += _enc_varint(int(self.n_history_nodes))
        out += _enc_varint(int(self.n_associations))
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
        if version not in (cls._BIN_VERSION, cls._BIN_VERSION_V1):
            raise ValueError(f"unsupported manifest binary version {version}")
        m = cls()
        for attr in cls._BIN_STRINGS:
            n, o = _dec_varint(data, o)
            setattr(m, attr, data[o:o + n].decode("utf-8")); o += n
        for attr in cls._BIN_NUMBERS:
            v, o = _dec_varint(data, o)
            setattr(m, attr, v)
        if version >= cls._BIN_VERSION:
            m.schema_version_major = int.from_bytes(data[o:o + 4], "little"); o += 4
            m.schema_version_minor = int.from_bytes(data[o:o + 4], "little"); o += 4
            m.feature_flags        = int.from_bytes(data[o:o + 8], "little"); o += 8
            m.n_history_nodes, o   = _dec_varint(data, o)
            m.n_associations, o    = _dec_varint(data, o)
        else:
            # v1 binary: synthesize uniform defaults so consumers can compare numerically.
            m.schema_version_major = 3
            m.schema_version_minor = 0
            m.feature_flags        = 0
            m.n_history_nodes      = m.test_count
            m.n_associations       = 0
        return m

    #: Domain-separation tag for the v2 schema-hash construction.  Bumping this
    #: string is how the construction is versioned; a v1 and a v2 hash can never
    #: compare equal by accident, because the prefixes differ too.
    _SCHEMA_HASH_V2_TAG = b"ncdb-schema-v2\x00"

    @staticmethod
    def compute_schema_hash(scope_tree_bytes: bytes,
                            strings_bytes: bytes = None) -> str:
        """Fingerprint of the *bin space* — its shape **and** its names.

        ``scope_tree.bin`` stores string-table *references*, not strings, so it
        alone does not identify a design: two unrelated databases with the same
        shape (same scope types, same bin counts, same nesting) serialize to
        byte-identical ``scope_tree.bin`` even when every scope and bin is named
        differently.  Hashing it alone therefore made the merger's same-schema
        fast path treat unrelated designs as interchangeable, adding one
        design's counts into the other's bins and reporting the result under the
        first source's names.

        ``strings.bin`` is folded in to close that hole.  This is sound only
        because the string table is serialized immediately after the scope-tree
        walk and before any per-run member (history, attrs, tags), so at that
        point it holds exactly the schema strings — scope and coveritem names —
        and nothing that varies between runs of the same design.  Fast merge is
        therefore preserved for genuinely identical designs.

        Passing *strings_bytes* selects the v2 construction, tagged and
        length-prefixed so the two inputs cannot be confused by concatenation.
        Omitting it reproduces the v1 hash, for reading databases written
        before this change.
        """
        if strings_bytes is None:
            digest = hashlib.sha256(scope_tree_bytes).hexdigest()
            return f"sha256:{digest}"
        h = hashlib.sha256()
        h.update(Manifest._SCHEMA_HASH_V2_TAG)
        h.update(_enc_varint(len(scope_tree_bytes)))
        h.update(scope_tree_bytes)
        h.update(_enc_varint(len(strings_bytes)))
        h.update(strings_bytes)
        return f"sha256v2:{h.hexdigest()}"

    @staticmethod
    def schema_fingerprint(schema_hash: str) -> int:
        """Fold a ``schema_hash`` string into the u64 stored in ``assoc/*``.

        The association members identify the bin space they were built against
        by a 64-bit fingerprint rather than the full digest, because the value
        is repeated in every member header.  The derivation is deliberately
        trivial so that all three NCDB implementations agree without sharing
        code: **the first 8 bytes of the SHA-256 digest, big-endian**, i.e. the
        first 16 hex characters read as a hexadecimal number.

        Truncation is safe here: the fingerprint guards against *accidentally*
        pairing associations with the wrong database, not against an adversary
        constructing a collision.

        A ``schema_hash`` that is empty or carries an unrecognized prefix
        yields 0, which readers must treat as "unknown" — never as a match.

        Note that only the ``sha256v2:`` construction covers bin *names*; a
        fingerprint derived from a v1 hash identifies the bin space's shape
        alone.  Association members should be written against v2 hashes.
        """
        if not schema_hash:
            return 0
        for prefix in ("sha256v2:", "sha256:"):
            if schema_hash.startswith(prefix):
                hex_digest = schema_hash[len(prefix):]
                break
        else:
            return 0
        if len(hex_digest) < 16:
            return 0
        try:
            return int(hex_digest[:16], 16)
        except ValueError:
            return 0

    @classmethod
    def build(cls, db, scope_tree_bytes: bytes,
              counts: list, history_nodes: list,
              strings_bytes: bytes = None) -> "Manifest":
        """Build a Manifest from a UCIS database and serialized members.

        *strings_bytes* should be the serialized string table; supplying it
        selects the v2 schema hash, which is the one that actually identifies
        the design.  It is optional only so that callers constructing a
        Manifest for comparison against a legacy database can reproduce a v1
        hash.
        """
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
            schema_hash=cls.compute_schema_hash(scope_tree_bytes, strings_bytes),
        )
