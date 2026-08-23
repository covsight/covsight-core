#!/usr/bin/env python3
"""
proto_vector_toggle.py — measure-only prototype of the vector-toggle storage
form (improvements #1) on a real coverage.dat.

Compares the current per-bit-cover representation against a packed per-signal
form, where each toggle signal stores a count ARRAY (bit i, direction d at
index 2*i+d) instead of 2*width separate named cover items. The signal's
TOGGLE scope still exists (name once); only the per-bit bins move out of
scope_tree/counts into a packed member ("NTGV").

This does NOT change the on-disk format — it estimates the achievable size by:
  base NCDB (non-toggle covers + toggle signal scopes, NO per-bit covers)
  + packed NTGV member (per-signal count array), deflated.

Two NTGV encodings:
  - counts   : varint per (bit,dir) — lossless.
  - bitmap   : 1 bit per (bit,dir), hit/not — lossy (improvements #2).

Usage:  python proto_vector_toggle.py <coverage.dat>
"""

import sys, os, io, re, zlib, zipfile

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_HERE, '..', '..', 'python'))
sys.path.insert(0, _HERE)

import coverage_to_ncdb as c2n
from covsight.core.ncdb.varint import encode_varint
from covsight.core.ncdb.string_table import StringTable
from bench_size import write_ncdb


def _dz(b):
    return len(zlib.compress(b, 9))


def _frontcode(strings):
    buf = io.BytesIO()
    buf.write(encode_varint(len(strings)))
    prev = ""
    for s in strings:
        cp = 0
        while cp < len(prev) and cp < len(s) and prev[cp] == s[cp] and cp < 255:
            cp += 1
        sb = s[cp:].encode("utf-8")
        buf.write(bytes([cp])); buf.write(encode_varint(len(sb))); buf.write(sb)
        prev = s
    return buf.getvalue()


def _group_toggle(items):
    """(hier, base) -> {(*) width, counts dict[(bit,dir)] = count}."""
    sig = {}
    for it in items:
        if it['type'] != 'toggle':
            continue
        base, bit, direction = c2n._toggle_parts(it['comment'])
        d = 0 if direction.startswith('0') else 1     # 0->1 vs 1->0
        rec = sig.setdefault((it['hier'], base), {})
        rec[(bit, d)] = it['count']
    return sig


def _ntgv(sig, bitmap=False):
    """Packed per-signal member. counts mode (lossless) or bitmap (lossy)."""
    buf = io.BytesIO()
    buf.write(encode_varint(len(sig)))
    for (hier, base), bins in sig.items():
        width = max((b for b, _ in bins), default=0) + 1
        buf.write(encode_varint(width))
        if bitmap:
            nbits = 2 * width
            ba = bytearray((nbits + 7) // 8)
            for (b, d), c in bins.items():
                if c > 0:
                    idx = 2 * b + d
                    ba[idx >> 3] |= 1 << (idx & 7)
            buf.write(bytes(ba))
        else:
            for b in range(width):
                for d in (0, 1):
                    buf.write(encode_varint(bins.get((b, d), 0)))
    return buf.getvalue()


def main():
    dat = sys.argv[1]
    items = c2n.parse_coverage_dat(dat)
    n_tog = sum(1 for it in items if it['type'] == 'toggle')
    print(f"parsed {len(items):,} points ({n_tog:,} toggle) from {dat}")

    import tempfile
    tmp = tempfile.mkdtemp()

    # --- A. current: per-bit toggle covers (source-info converter) ---
    dbA = c2n.build_db(items, toggle_covers=True)
    a = os.path.join(tmp, 'cur.cdb'); _, _, szA, _ = write_ncdb(dbA, a, os.path.join(tmp, 'cur_s.cdb'))

    # --- B. vector: toggle signal scopes only (no per-bit covers) + NTGV ---
    dbB = c2n.build_db(items, toggle_covers=False)
    b = os.path.join(tmp, 'base.cdb'); _, _, szB_base, _ = write_ncdb(dbB, b, os.path.join(tmp, 'base_s.cdb'))

    sig = _group_toggle(items)
    ntgv_counts = _ntgv(sig, bitmap=False)
    ntgv_bitmap = _ntgv(sig, bitmap=True)

    # Per-member deflate (zip deflates members independently → additive).
    def base_members(path):
        with zipfile.ZipFile(path) as z:
            return {i.filename: z.read(i.filename) for i in z.infolist()}
    mem = base_members(b)
    base_def = sum(_dz(v) for v in mem.values())
    # front-coded strings variant
    st = StringTable.from_bytes(mem['strings.bin'])
    base_def_fc = base_def - _dz(mem['strings.bin']) + _dz(_frontcode(list(st)))

    vec_counts = base_def + _dz(ntgv_counts)
    vec_bitmap = base_def + _dz(ntgv_bitmap)
    vec_counts_fc = base_def_fc + _dz(ntgv_counts)
    vec_bitmap_fc = base_def_fc + _dz(ntgv_bitmap)

    print(f"\n  {'representation':<46}{'deflated bytes':>16}")
    print(f"  {'-'*46}{'-'*16}")
    print(f"  {'A. current (per-bit covers)':<46}{szA:>16,}")
    print(f"  {'B. base (signal scopes, no bit covers)':<46}{base_def:>16,}")
    print(f"  {'   NTGV counts member (lossless)':<46}{_dz(ntgv_counts):>16,}")
    print(f"  {'   NTGV bitmap member (hit/not, lossy)':<46}{_dz(ntgv_bitmap):>16,}")
    print(f"  {'B. vector + counts (lossless)':<46}{vec_counts:>16,}")
    print(f"  {'B. vector + counts + front-coding':<46}{vec_counts_fc:>16,}")
    print(f"  {'B. vector + bitmap (lossy)':<46}{vec_bitmap:>16,}")
    print(f"  {'B. vector + bitmap + front-coding':<46}{vec_bitmap_fc:>16,}")
    print(f"\n  best lossless vs current: {szA/vec_counts_fc:.2f}x smaller "
          f"({szA:,} -> {vec_counts_fc:,})")


if __name__ == '__main__':
    main()
