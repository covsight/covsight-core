"""
sources.json — source file table serialization.

JSON array mapping integer IDs to file paths.  The order of entries
matches the file IDs used in scope_tree.bin source references.
"""

import json
from covsight.core.mem.mem_file_handle import MemFileHandle


class SourcesWriter:
    """Serialize source file handles to sources.json bytes."""

    def serialize(self, file_handles) -> bytes:
        records = []
        for fh in file_handles:
            records.append(fh.getFileName())
        return json.dumps(records, indent=2).encode("utf-8")


_NSRC_MAGIC = b"NSRC"
_NSRC_VERSION = 1


def _dec_varint(data: bytes, off: int):
    r = 0; shift = 0
    while True:
        b = data[off]; off += 1
        r |= (b & 0x7F) << shift
        if (b & 0x80) == 0: return r, off
        shift += 7


class SourcesReader:
    """Deserialize source file handles (binary NSRC or legacy JSON)."""

    def deserialize(self, data: bytes) -> list:
        if data[:4] == _NSRC_MAGIC:
            return self._deserialize_binary(data)
        records = json.loads(data.decode("utf-8"))
        return [MemFileHandle(fn) for fn in records]

    def _deserialize_binary(self, data: bytes) -> list:
        o = 4
        version = data[o]; o += 1
        if version != _NSRC_VERSION:
            raise ValueError(f"unsupported sources binary version {version}")
        n, o = _dec_varint(data, o)
        handles = []
        for _ in range(n):
            ln, o = _dec_varint(data, o)
            handles.append(MemFileHandle(data[o:o + ln].decode("utf-8") if ln else ""))
            o += ln
        return handles
