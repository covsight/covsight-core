"""Run-set signatures — the dictionary behind per-test association (UCIS §8.17).

A *signature* is the set of runs in which some cover bin was hit, held as a set
of **bit positions** into a run space; position *i* maps to a global run id via
the ``run_ids`` table.  Bins are then stored bin-major as one signature id each,
so the |tests| × |bins| relation costs one dictionary of distinct run-sets plus
one integer per bin, rather than a row per (test, bin) pair.

Two members implement this, and this module is the codec for both:

``assoc/signatures.bin`` (``ASIG``)
    The dictionary: the run space and the *K* distinct signatures.

``assoc/bin_sig.bin`` (``ABSG``)
    The index: one signature id per flat bin, stamped with the bin-space
    fingerprint it was built against.

Ids 0 and 1 are reserved for the empty and full sets, so the two overwhelmingly
common cases cost one varint in the index and nothing in the dictionary.

See ``docs/ucis-test-assoc-design.md`` §6 for the format and the reasoning.
"""

import io

from .manifest import Manifest
from .varint import encode_varint, decode_varint


# ── Format constants ──────────────────────────────────────────────────────

MAGIC_SIGNATURES = 0x41534947   # 'ASIG'
MAGIC_BIN_SIG    = 0x41425347   # 'ABSG'

SIGNATURES_VERSION = 1
BIN_SIG_VERSION    = 1

MEMBER_SIGNATURES = "assoc/signatures.bin"
MEMBER_BIN_SIG    = "assoc/bin_sig.bin"

#: ``UCIS_ASSOC_TESTHIT``.  The only association type the spec defines.
ASSOC_TESTHIT = 1

# Signature encodings.
ENC_EMPTY  = 0
ENC_FULL   = 1
ENC_ARRAY  = 2
ENC_BITMAP = 3
ENC_DELTA  = 4

# Bin-index encodings.
IDX_VARINT = 0
IDX_RLE    = 1

#: Reserved dictionary ids.
SIG_EMPTY = 0
SIG_FULL  = 1

#: A delta signature is decoded by walking back to a non-delta base.  The cap
#: bounds that walk, so a read costs a known number of steps and a malformed or
#: hostile file cannot make the reader chase a chain indefinitely.  Writers must
#: not exceed it; readers reject files that do.
MAX_DELTA_CHAIN_DEPTH = 8


class SignatureFormatError(ValueError):
    """A signature member is malformed or uses an unsupported encoding."""


# ── Single-signature codec ────────────────────────────────────────────────

def _encode_positions(sorted_positions) -> bytes:
    """Varint gaps between ascending positions, first relative to zero."""
    out = bytearray()
    previous = 0
    for position in sorted_positions:
        out += encode_varint(position - previous)
        previous = position
    return bytes(out)


def _decode_positions(data: bytes, offset: int, count: int):
    positions, previous = [], 0
    for _ in range(count):
        gap, offset = decode_varint(data, offset)
        previous += gap
        positions.append(previous)
    return positions, offset


def bitmap_bytes(run_space: int) -> int:
    """Bytes a bitmap-encoded signature occupies for *run_space* positions."""
    return (run_space + 7) // 8


def encode_signature(bits, run_space: int, *,
                     base=None, base_id: int = None) -> bytes:
    """Encode one signature record, choosing the smallest applicable encoding.

    Args:
        bits: Iterable of set bit positions.
        run_space: Number of bit positions in the run space.
        base: Bit positions of a candidate base signature, or None.  Supplying
            one enables the delta encoding, which is only chosen if it is
            actually smaller.
        base_id: Dictionary id of *base*.  Required when *base* is given, and
            must be less than the id being written so the chain cannot cycle.
    """
    bits = frozenset(bits)
    if not bits:
        return bytes([ENC_EMPTY])
    if len(bits) == run_space:
        return bytes([ENC_FULL])

    ordered = sorted(bits)
    array_payload = encode_varint(len(ordered)) + _encode_positions(ordered)
    candidates = [(len(array_payload) + 1, ENC_ARRAY, array_payload)]

    bitmap = bytearray(bitmap_bytes(run_space))
    for position in ordered:
        bitmap[position >> 3] |= 1 << (position & 7)
    candidates.append((len(bitmap) + 1, ENC_BITMAP, bytes(bitmap)))

    if base is not None and base_id is not None:
        flipped = sorted(bits ^ frozenset(base))
        delta_payload = (encode_varint(base_id) + encode_varint(len(flipped))
                         + _encode_positions(flipped))
        candidates.append((len(delta_payload) + 1, ENC_DELTA, delta_payload))

    _size, encoding, payload = min(candidates, key=lambda c: (c[0], c[1]))
    return bytes([encoding]) + payload


def decode_signature(data: bytes, offset: int, run_space: int, resolved):
    """Decode one signature record.

    Args:
        resolved: Sequence of already-decoded signatures, indexed by id, used
            to resolve a delta base.  Because records are written in id order
            and a base id must be smaller than its dependant, every base is
            present by the time it is needed.

    Returns:
        ``(frozenset_of_positions, new_offset)``.
    """
    if offset >= len(data):
        raise SignatureFormatError("truncated signature record")
    encoding = data[offset]
    offset += 1

    if encoding == ENC_EMPTY:
        return frozenset(), offset
    if encoding == ENC_FULL:
        return frozenset(range(run_space)), offset

    if encoding == ENC_ARRAY:
        cardinality, offset = decode_varint(data, offset)
        positions, offset = _decode_positions(data, offset, cardinality)
        return frozenset(positions), offset

    if encoding == ENC_BITMAP:
        width = bitmap_bytes(run_space)
        chunk = data[offset:offset + width]
        if len(chunk) != width:
            raise SignatureFormatError("truncated bitmap signature")
        offset += width
        positions = [index for index in range(run_space)
                     if chunk[index >> 3] & (1 << (index & 7))]
        return frozenset(positions), offset

    if encoding == ENC_DELTA:
        base_id, offset = decode_varint(data, offset)
        cardinality, offset = decode_varint(data, offset)
        flipped, offset = _decode_positions(data, offset, cardinality)
        if not 0 <= base_id < len(resolved):
            raise SignatureFormatError(
                "delta signature references unresolved base id %d; bases must "
                "precede their dependants" % base_id)
        return resolved[base_id] ^ frozenset(flipped), offset

    raise SignatureFormatError("unsupported signature encoding %d" % encoding)


# ── Set operations ────────────────────────────────────────────────────────
#
# Signatures decode to plain frozensets, so these are thin.  They exist so that
# callers express intent against this module rather than reaching for set
# internals, and so an encoding-aware fast path can be slid underneath later
# without touching them.

def popcount(bits) -> int:
    """Number of runs in a signature."""
    return len(bits)


def contains(bits, position: int) -> bool:
    """Whether a signature includes *position*."""
    return position in bits


def union(*signatures) -> frozenset:
    """Union of any number of signatures; empty when given none."""
    result = frozenset()
    for bits in signatures:
        result |= frozenset(bits)
    return result


def intersection(*signatures) -> frozenset:
    """Intersection of one or more signatures."""
    if not signatures:
        return frozenset()
    result = frozenset(signatures[0])
    for bits in signatures[1:]:
        result &= frozenset(bits)
    return result


# ── The dictionary member ─────────────────────────────────────────────────

class SignatureTable:
    """Intern run-sets to dictionary ids, and serialize the ``ASIG`` member.

    Ids 0 (empty) and 1 (full) always exist, whether or not any bin uses them,
    so a reader can assume the reserved meanings without consulting the table.
    """

    def __init__(self, run_ids, assoc_type: int = ASSOC_TESTHIT):
        """Args:
            run_ids: Global run ids, indexed by bit position.  Order defines
                the run space and must be stable for the life of the table.
        """
        self.run_ids = list(run_ids)
        self.assoc_type = int(assoc_type)
        self._signatures = [frozenset(), frozenset(range(self.run_space))]
        # ``setdefault`` in id order, so that in the degenerate empty run space
        # -- where the empty and full sets are the same set -- the reserved
        # meaning of id 0 wins and ``intern(frozenset())`` stays 0.
        self._by_key = {}
        for sig_id, bits in enumerate(self._signatures):
            self._by_key.setdefault(bits, sig_id)
        # Chain depth per id, so the writer can respect MAX_DELTA_CHAIN_DEPTH.
        self._depth = [0, 0]
        self._position_of = {run_id: position
                             for position, run_id in enumerate(self.run_ids)}

    @property
    def run_space(self) -> int:
        return len(self.run_ids)

    def __len__(self) -> int:
        return len(self._signatures)

    def __getitem__(self, sig_id: int) -> frozenset:
        return self._signatures[sig_id]

    def intern(self, bits) -> int:
        """Return the id for *bits*, adding it to the dictionary if new."""
        key = frozenset(bits)
        existing = self._by_key.get(key)
        if existing is not None:
            return existing
        sig_id = len(self._signatures)
        self._signatures.append(key)
        self._by_key[key] = sig_id
        self._depth.append(0)
        return sig_id

    def cardinality(self, sig_id: int) -> int:
        return len(self._signatures[sig_id])

    def contains_run(self, sig_id: int, run_id) -> bool:
        """Whether *run_id* is a member of the signature."""
        position = self._position_of.get(run_id)
        if position is None:
            return False
        return position in self._signatures[sig_id]

    def runs_of(self, sig_id: int) -> list:
        """Global run ids of a signature, ascending by bit position."""
        return [self.run_ids[position]
                for position in sorted(self._signatures[sig_id])]

    # -- serialization -----------------------------------------------------

    def _choose_base(self, sig_id: int):
        """Pick a delta base for *sig_id*, or ``(None, None)``.

        Prefers the immediately preceding signature: the append structure makes
        consecutive signatures differ by about one run, so that is where the
        delta encoding pays.  A base at the depth cap is not eligible, which
        naturally restarts the chain.
        """
        if sig_id <= SIG_FULL:
            return None, None
        candidate = sig_id - 1
        if self._depth[candidate] >= MAX_DELTA_CHAIN_DEPTH - 1:
            return None, None
        return self._signatures[candidate], candidate

    def serialize(self) -> bytes:
        # Recomputed from scratch: chain depths are a property of this
        # serialization, not accumulated state from an earlier one.
        self._depth = [0] * len(self._signatures)
        out = io.BytesIO()
        out.write(MAGIC_SIGNATURES.to_bytes(4, "little"))
        out.write(bytes([SIGNATURES_VERSION]))
        out.write(self.assoc_type.to_bytes(2, "little"))
        out.write(self.run_space.to_bytes(4, "little"))
        out.write(encode_varint(len(self._signatures)))
        for run_id in self.run_ids:
            out.write(encode_varint(int(run_id)))

        for sig_id, bits in enumerate(self._signatures):
            base, base_id = self._choose_base(sig_id)
            record = encode_signature(bits, self.run_space,
                                      base=base, base_id=base_id)
            if record[0] == ENC_DELTA:
                self._depth[sig_id] = self._depth[base_id] + 1
            else:
                self._depth[sig_id] = 0
            out.write(record)
        return out.getvalue()

    @classmethod
    def deserialize(cls, data: bytes) -> "SignatureTable":
        if len(data) < 11:
            raise SignatureFormatError("signatures member is too short")
        if int.from_bytes(data[0:4], "little") != MAGIC_SIGNATURES:
            raise SignatureFormatError("bad magic in signatures member")
        version = data[4]
        if version != SIGNATURES_VERSION:
            raise SignatureFormatError(
                "unsupported signatures version %d" % version)
        assoc_type = int.from_bytes(data[5:7], "little")
        run_space = int.from_bytes(data[7:11], "little")
        offset = 11
        num_sigs, offset = decode_varint(data, offset)

        run_ids = []
        for _ in range(run_space):
            run_id, offset = decode_varint(data, offset)
            run_ids.append(run_id)

        table = cls(run_ids, assoc_type)
        signatures, by_key, depth = [], {}, []
        for sig_id in range(num_sigs):
            record_start = offset
            bits, offset = decode_signature(data, offset, run_space, signatures)
            if data[record_start] == ENC_DELTA:
                base_id, _ = decode_varint(data, record_start + 1)
                if base_id >= sig_id:
                    raise SignatureFormatError(
                        "delta base id %d does not precede signature %d"
                        % (base_id, sig_id))
                chain = depth[base_id] + 1
                if chain >= MAX_DELTA_CHAIN_DEPTH:
                    raise SignatureFormatError(
                        "delta chain depth %d exceeds the cap of %d"
                        % (chain, MAX_DELTA_CHAIN_DEPTH))
                depth.append(chain)
            else:
                depth.append(0)
            signatures.append(bits)
            by_key.setdefault(bits, sig_id)

        table._signatures = signatures
        table._by_key = by_key
        table._depth = depth
        return table


# ── The bin index member ──────────────────────────────────────────────────

def _rle_pairs(sig_ids):
    run_value, run_length = None, 0
    for sig_id in sig_ids:
        if sig_id == run_value:
            run_length += 1
            continue
        if run_value is not None:
            yield run_value, run_length
        run_value, run_length = sig_id, 1
    if run_value is not None:
        yield run_value, run_length


def encode_bin_signatures(sig_ids, schema_fingerprint: int) -> bytes:
    """Serialize the ``ABSG`` member, picking the smaller of varint and RLE.

    *schema_fingerprint* identifies the bin space these indices address; a
    reader that finds a different one must refuse the member rather than
    resolve indices against bins that mean something else.
    """
    sig_ids = list(sig_ids)

    flat = bytearray()
    for sig_id in sig_ids:
        flat += encode_varint(sig_id)

    rle = bytearray()
    for value, length in _rle_pairs(sig_ids):
        rle += encode_varint(value)
        rle += encode_varint(length)

    if len(rle) < len(flat):
        encoding, payload = IDX_RLE, bytes(rle)
    else:
        encoding, payload = IDX_VARINT, bytes(flat)

    out = io.BytesIO()
    out.write(MAGIC_BIN_SIG.to_bytes(4, "little"))
    out.write(bytes([BIN_SIG_VERSION]))
    out.write(int(schema_fingerprint).to_bytes(8, "little"))
    out.write(encode_varint(len(sig_ids)))
    out.write(bytes([encoding]))
    out.write(payload)
    return out.getvalue()


def decode_bin_signatures(data: bytes, expected_fingerprint: int = None):
    """Deserialize the ``ABSG`` member.

    Args:
        expected_fingerprint: When given, the member's fingerprint must match.
            A fingerprint of 0 on either side means *unknown* and is never
            treated as a match -- see :meth:`Manifest.schema_fingerprint`.

    Returns:
        ``(sig_ids, schema_fingerprint)``.
    """
    if len(data) < 14:
        raise SignatureFormatError("bin_sig member is too short")
    if int.from_bytes(data[0:4], "little") != MAGIC_BIN_SIG:
        raise SignatureFormatError("bad magic in bin_sig member")
    version = data[4]
    if version != BIN_SIG_VERSION:
        raise SignatureFormatError("unsupported bin_sig version %d" % version)
    fingerprint = int.from_bytes(data[5:13], "little")

    if expected_fingerprint is not None:
        if not fingerprint or not expected_fingerprint:
            raise SignatureFormatError(
                "bin_sig fingerprint is unknown (0); associations cannot be "
                "safely resolved against this database's bins")
        if fingerprint != expected_fingerprint:
            raise SignatureFormatError(
                "bin_sig was built against a different bin space "
                "(0x%016x, expected 0x%016x)"
                % (fingerprint, expected_fingerprint))

    offset = 13
    num_bins, offset = decode_varint(data, offset)
    encoding = data[offset]
    offset += 1

    sig_ids = []
    if encoding == IDX_VARINT:
        for _ in range(num_bins):
            sig_id, offset = decode_varint(data, offset)
            sig_ids.append(sig_id)
    elif encoding == IDX_RLE:
        while len(sig_ids) < num_bins:
            value, offset = decode_varint(data, offset)
            length, offset = decode_varint(data, offset)
            sig_ids.extend([value] * length)
    else:
        raise SignatureFormatError(
            "unsupported bin_sig encoding %d" % encoding)

    if len(sig_ids) != num_bins:
        raise SignatureFormatError(
            "bin_sig declares %d bins but decodes %d" % (num_bins, len(sig_ids)))
    return sig_ids, fingerprint


def schema_fingerprint_of(manifest) -> int:
    """Fingerprint to stamp into ``ABSG`` for a database with *manifest*."""
    return Manifest.schema_fingerprint(manifest.schema_hash)
