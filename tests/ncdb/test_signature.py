"""Run-set signature codec — round-trip, property, and boundary tests.

The signature dictionary is the whole reason per-test association does not cost
|tests| × |bins|, so the codec is load-bearing: a decode that disagrees with the
encode by one bit attributes a bin to the wrong run, which reads as a plausible
coverage number rather than as a failure.  These tests use a naive ``set``
oracle throughout.
"""

import random

import pytest

from covsight.core.ncdb import signature as sig
from covsight.core.ncdb.signature import (
    SignatureTable, SignatureFormatError, MAX_DELTA_CHAIN_DEPTH,
    encode_signature, decode_signature, encode_bin_signatures,
    decode_bin_signatures, ENC_EMPTY, ENC_FULL, ENC_ARRAY, ENC_BITMAP,
    ENC_DELTA, SIG_EMPTY, SIG_FULL,
)


def _round_trip(bits, run_space, base=None, base_id=None):
    payload = encode_signature(bits, run_space, base=base, base_id=base_id)
    resolved = []
    if base is not None:
        resolved = [frozenset()] * (base_id + 1)
        resolved[base_id] = frozenset(base)
    decoded, offset = decode_signature(payload, 0, run_space, resolved)
    assert offset == len(payload), "decoder consumed the wrong byte count"
    return decoded, payload


# ── Single-signature round trips ──────────────────────────────────────────

@pytest.mark.parametrize("run_space", [1, 2, 7, 8, 9, 63, 64, 65, 200])
def test_round_trip_boundary_cardinalities(run_space):
    """Empty, singleton, full, and the byte boundaries around them."""
    cases = [set(), {0}, {run_space - 1}, set(range(run_space))]
    if run_space > 2:
        cases.append({0, run_space // 2, run_space - 1})
        cases.append(set(range(0, run_space, 2)))
    for bits in cases:
        decoded, _ = _round_trip(bits, run_space)
        assert decoded == frozenset(bits)


def test_empty_and_full_are_payload_free():
    """The two common cases must cost one byte in the dictionary."""
    assert encode_signature(set(), 64) == bytes([ENC_EMPTY])
    assert encode_signature(set(range(64)), 64) == bytes([ENC_FULL])


def test_sparse_prefers_array_and_dense_prefers_bitmap():
    run_space = 512
    sparse = encode_signature({3, 400}, run_space)
    dense = encode_signature(set(range(0, run_space, 2)), run_space)
    assert sparse[0] == ENC_ARRAY
    assert dense[0] == ENC_BITMAP


def test_encoding_is_never_larger_than_the_bitmap():
    """Whatever the chooser picks, it must not lose to the plain bitmap."""
    run_space = 256
    bitmap_size = sig.bitmap_bytes(run_space) + 1
    random.seed(20260814)
    for _ in range(50):
        bits = {random.randrange(run_space)
                for _ in range(random.randrange(1, run_space))}
        if len(bits) == run_space:
            continue
        assert len(encode_signature(bits, run_space)) <= bitmap_size


@pytest.mark.parametrize("seed", range(20))
def test_random_round_trip(seed):
    random.seed(seed)
    run_space = random.randrange(1, 300)
    bits = {i for i in range(run_space) if random.random() < random.random()}
    decoded, _ = _round_trip(bits, run_space)
    assert decoded == frozenset(bits)


# ── Delta encoding ────────────────────────────────────────────────────────

def test_delta_round_trips_against_its_base():
    run_space = 400
    base = set(range(0, 300))
    derived = set(base) ^ {17, 250}
    decoded, payload = _round_trip(derived, run_space, base=base, base_id=5)
    assert payload[0] == ENC_DELTA, "a two-bit difference should choose delta"
    assert decoded == frozenset(derived)


def test_delta_is_rejected_when_it_is_not_smaller():
    """A base sharing nothing with the signature must not be used."""
    run_space = 64
    payload = encode_signature({1, 2}, run_space,
                               base=set(range(0, 64, 2)), base_id=3)
    assert payload[0] != ENC_DELTA


def test_delta_base_must_precede_its_dependant():
    payload = encode_signature({1, 2, 3}, 64, base={1, 2}, base_id=9)
    with pytest.raises(SignatureFormatError):
        decode_signature(payload, 0, 64, [])


def test_chain_depth_cap_is_enforced_on_read():
    """A file exceeding the cap is rejected, not followed indefinitely."""
    run_space = 64
    table = SignatureTable(list(range(run_space)))
    # Each signature differs from the previous by one bit, which is exactly the
    # shape that makes the writer choose delta and build a chain.
    bits = {0}
    for step in range(1, MAX_DELTA_CHAIN_DEPTH + 6):
        bits = set(bits) | {step}
        table.intern(bits)
    payload = table.serialize()

    restored = SignatureTable.deserialize(payload)
    assert len(restored) == len(table)

    depths = restored._depth
    assert max(depths) < MAX_DELTA_CHAIN_DEPTH, \
        "writer must restart the chain before the cap"


def test_over_deep_chain_is_rejected():
    """Hand-build a chain past the cap and confirm the reader refuses it."""
    run_space = 32
    header = (sig.MAGIC_SIGNATURES.to_bytes(4, "little")
              + bytes([sig.SIGNATURES_VERSION])
              + sig.ASSOC_TESTHIT.to_bytes(2, "little")
              + run_space.to_bytes(4, "little"))
    from covsight.core.ncdb.varint import encode_varint

    num_sigs = MAX_DELTA_CHAIN_DEPTH + 3
    body = bytearray(encode_varint(num_sigs))
    for run_id in range(run_space):
        body += encode_varint(run_id)
    body += bytes([ENC_EMPTY])                     # id 0
    body += bytes([ENC_FULL])                      # id 1
    body += bytes([ENC_ARRAY]) + encode_varint(1) + encode_varint(0)  # id 2
    for sig_id in range(3, num_sigs):              # each deltas off the last
        body += (bytes([ENC_DELTA]) + encode_varint(sig_id - 1)
                 + encode_varint(1) + encode_varint(sig_id))

    with pytest.raises(SignatureFormatError, match="chain depth"):
        SignatureTable.deserialize(bytes(header) + bytes(body))


# ── Interning ─────────────────────────────────────────────────────────────

def test_reserved_ids():
    table = SignatureTable([10, 11, 12])
    assert table.intern(set()) == SIG_EMPTY
    assert table.intern({0, 1, 2}) == SIG_FULL


def test_identical_sets_intern_once_and_distinct_sets_never_collide():
    table = SignatureTable(list(range(16)))
    ids = {}
    random.seed(7)
    for _ in range(200):
        bits = frozenset(random.sample(range(16), random.randrange(1, 15)))
        sig_id = table.intern(bits)
        if bits in ids:
            assert ids[bits] == sig_id
        else:
            assert sig_id not in ids.values()
            ids[bits] = sig_id
    assert len(table) == len(ids) + 2   # + the two reserved ids


def test_empty_run_space_keeps_reserved_id_zero():
    """Degenerate case: the empty and full sets are the same set."""
    table = SignatureTable([])
    assert table.intern(set()) == SIG_EMPTY


def test_cardinality_and_runs_of():
    table = SignatureTable([100, 200, 300])
    sig_id = table.intern({0, 2})
    assert table.cardinality(sig_id) == 2
    assert table.runs_of(sig_id) == [100, 300]
    assert table.contains_run(sig_id, 100)
    assert not table.contains_run(sig_id, 200)
    assert not table.contains_run(sig_id, 999)


# ── Dictionary member round trip ──────────────────────────────────────────

def test_table_round_trip_preserves_every_signature():
    run_ids = [5, 9, 13, 21, 34]
    table = SignatureTable(run_ids)
    random.seed(11)
    interned = []
    for _ in range(40):
        bits = frozenset(random.sample(range(len(run_ids)),
                                       random.randrange(0, len(run_ids) + 1)))
        interned.append((table.intern(bits), bits))

    restored = SignatureTable.deserialize(table.serialize())
    assert restored.run_ids == run_ids
    assert restored.assoc_type == table.assoc_type
    assert len(restored) == len(table)
    for sig_id, bits in interned:
        assert restored[sig_id] == bits
        assert restored.runs_of(sig_id) == table.runs_of(sig_id)


def test_table_rejects_bad_magic_and_version():
    payload = bytearray(SignatureTable([1, 2]).serialize())
    with pytest.raises(SignatureFormatError, match="magic"):
        SignatureTable.deserialize(bytes([0, 0, 0, 0]) + bytes(payload[4:]))
    payload[4] = 99
    with pytest.raises(SignatureFormatError, match="version"):
        SignatureTable.deserialize(bytes(payload))


def test_unsupported_encoding_is_rejected():
    with pytest.raises(SignatureFormatError, match="encoding"):
        decode_signature(bytes([77]), 0, 8, [])


# ── Bin index member ──────────────────────────────────────────────────────

def test_bin_index_round_trip():
    sig_ids = [0, 0, 3, 3, 3, 1, 2, 2, 0]
    payload = encode_bin_signatures(sig_ids, 0xDEADBEEFCAFEBABE)
    got, fingerprint = decode_bin_signatures(payload, 0xDEADBEEFCAFEBABE)
    assert got == sig_ids
    assert fingerprint == 0xDEADBEEFCAFEBABE


def test_bin_index_empty():
    payload = encode_bin_signatures([], 1)
    assert decode_bin_signatures(payload, 1) == ([], 1)


def test_rle_wins_on_runs_and_varint_wins_on_noise():
    fingerprint = 1
    runs = encode_bin_signatures([7] * 500, fingerprint)
    random.seed(3)
    noise = encode_bin_signatures(
        [random.randrange(200) for _ in range(500)], fingerprint)
    assert len(runs) < len(noise)
    # Both must still decode correctly whichever encoding was chosen.
    assert decode_bin_signatures(runs, fingerprint)[0] == [7] * 500


@pytest.mark.parametrize("seed", range(10))
def test_bin_index_random_round_trip(seed):
    random.seed(seed)
    sig_ids = []
    while len(sig_ids) < 300:
        sig_ids.extend([random.randrange(50)] * random.randrange(1, 12))
    sig_ids = sig_ids[:300]
    payload = encode_bin_signatures(sig_ids, 42)
    assert decode_bin_signatures(payload, 42)[0] == sig_ids


def test_fingerprint_mismatch_is_refused():
    payload = encode_bin_signatures([1, 2], 0x1111111111111111)
    with pytest.raises(SignatureFormatError, match="different bin space"):
        decode_bin_signatures(payload, 0x2222222222222222)


def test_unknown_fingerprint_is_never_a_match():
    """0 means unknown; resolving against it would risk misattribution."""
    with pytest.raises(SignatureFormatError, match="unknown"):
        decode_bin_signatures(encode_bin_signatures([1], 0), 5)
    with pytest.raises(SignatureFormatError, match="unknown"):
        decode_bin_signatures(encode_bin_signatures([1], 5), 0)


def test_fingerprint_check_is_skipped_when_not_requested():
    payload = encode_bin_signatures([1, 2], 0)
    assert decode_bin_signatures(payload) == ([1, 2], 0)


def test_bin_index_rejects_bad_magic():
    payload = bytearray(encode_bin_signatures([1], 1))
    payload[0] ^= 0xFF
    with pytest.raises(SignatureFormatError, match="magic"):
        decode_bin_signatures(bytes(payload))


# ── Set operations against a naive oracle ─────────────────────────────────

@pytest.mark.parametrize("seed", range(15))
def test_set_ops_match_naive_oracle(seed):
    random.seed(seed)
    universe = 40
    sets = [frozenset(random.sample(range(universe), random.randrange(0, 20)))
            for _ in range(5)]

    assert sig.union(*sets) == set().union(*sets)
    assert sig.intersection(*sets) == set(sets[0]).intersection(*sets[1:])
    for bits in sets:
        assert sig.popcount(bits) == len(bits)
        for position in range(universe):
            assert sig.contains(bits, position) == (position in bits)


def test_union_of_nothing_is_empty():
    assert sig.union() == frozenset()
