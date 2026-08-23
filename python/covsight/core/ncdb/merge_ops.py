"""merge_ops.bin — per-coveritem merge operations for the fast path.

Not every bin is additive. A ``PEAKACTIVEBIN`` is a high-water mark: summing it
across 64 runs yields a plausible-looking number that is simply false. But the
same-schema merge deliberately never decodes the scope tree -- that is what
makes it fast -- so it has no way to ask a bin what type it is.

This member closes that gap the way NCDB closes others: a tiny, shape-aware
side table written at build time. It lists only the bins whose merge op is
*not* ``SUM``, so for the overwhelmingly common case (no assertion peak bins)
it is absent entirely and costs nothing.

Format::

    version:     varint (1)
    num_entries: varint
    per entry:
        delta_idx: varint   coveritem DFS index delta from previous
        op:        varint   OP_* below

An absent member means "every bin is additive", which is exactly how archives
written before this member behave -- so old files keep working unchanged.
"""

from covsight.core.api import CoverTypeT

from .dfs_util import dfs_scope_list
from .varint import encode_varint, decode_varint

MEMBER_MERGE_OPS = "merge_ops.bin"

_VERSION = 1
_COVER_ALL = 0xFFFFFFFF

#: Merge operations.  SUM is the default and is never serialized.
OP_SUM = 0
OP_MAX = 1

#: Cover types whose counts are not additive across runs.
_NON_ADDITIVE = {
    int(CoverTypeT.PEAKACTIVEBIN): OP_MAX,
}


def op_for_cover_type(cover_type) -> int:
    """The merge op for *cover_type* (``OP_SUM`` unless listed otherwise)."""
    try:
        return _NON_ADDITIVE.get(int(cover_type), OP_SUM)
    except (TypeError, ValueError):
        return OP_SUM


class MergeOpsWriter:
    """Serialize the non-additive bins of a database."""

    def serialize(self, db) -> bytes:
        entries = []
        index = 0
        for scope in dfs_scope_list(db):
            for item in scope.coverItems(_COVER_ALL):
                try:
                    cover_type = item.getCoverData().type
                except Exception:
                    cover_type = None
                op = op_for_cover_type(cover_type)
                if op != OP_SUM:
                    entries.append((index, op))
                index += 1

        if not entries:
            return b""

        buf = bytearray()
        buf.extend(encode_varint(_VERSION))
        buf.extend(encode_varint(len(entries)))
        previous = 0
        for bin_index, op in entries:
            buf.extend(encode_varint(bin_index - previous))
            buf.extend(encode_varint(op))
            previous = bin_index
        return bytes(buf)


class MergeOpsReader:
    """Deserialize ``merge_ops.bin`` into ``{bin_index: op}``."""

    def deserialize(self, data: bytes) -> dict:
        if not data:
            return {}
        offset = 0
        version, offset = decode_varint(data, offset)
        if version != _VERSION:
            return {}
        count, offset = decode_varint(data, offset)
        out = {}
        previous = 0
        for _ in range(count):
            delta, offset = decode_varint(data, offset)
            op, offset = decode_varint(data, offset)
            previous += delta
            out[previous] = op
        return out


def merge_counts(all_counts, ops=None):
    """Merge count arrays element-wise, honouring per-bin operations.

    Args:
        all_counts: One count array per source; all the same length.
        ops: ``{bin_index: OP_*}`` for the bins that are not additive.  An
            empty or absent mapping means every bin sums, which is the fast
            path and the common case.

    Returns:
        The merged count array.
    """
    if not all_counts:
        return []
    if not ops:
        return list(map(sum, zip(*all_counts)))

    merged = list(map(sum, zip(*all_counts)))
    for bin_index, op in ops.items():
        if op == OP_MAX and 0 <= bin_index < len(merged):
            merged[bin_index] = max(counts[bin_index] for counts in all_counts)
    return merged
