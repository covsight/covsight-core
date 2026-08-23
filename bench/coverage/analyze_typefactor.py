#!/usr/bin/env python3
"""
analyze_typefactor.py — measure module-type (Verilator 'page') replication and
the size win from factoring structure by type instead of by instance.

Idea (deflate-window-friendly, no compressor change): instances of the same
Verilator module variant share an identical structure (signal set, source
locations). Store that structure ONCE per type ('page'), and per instance store
only (type ref + counts). This pulls the redundant structure together so it
lands inside deflate's 32 KB window — and removes the per-instance repetition.

Usage:  python analyze_typefactor.py <coverage.dat>
"""
import sys, os, re, zlib, io
_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
import coverage_to_ncdb as c2n


def vint(n):
    n &= (1 << 64) - 1; b = bytearray()
    while True:
        x = n & 0x7f; n >>= 7; b.append(x | 0x80 if n else x)
        if not n: break
    return bytes(b)


def dz(b): return len(zlib.compress(b, 9))


def modtype(page):
    # page like 'v_toggle/uart__pi5' -> 'uart__pi5' (the Verilator variant)
    return page.split('/', 1)[1] if '/' in page else page


def main():
    dat = sys.argv[1]
    items = c2n.parse_coverage_dat(dat)

    hiers = set()
    pages = set()
    hier_page = set()           # (hier, page)
    hs = set()                  # (hier, signal)      -- per-instance signal set
    ps = set()                  # (page, signal)      -- per-type signal set
    hl = set()                  # (hier, file,line,col,comment) per-instance loc
    pl = set()                  # (page, file,line,col,comment) per-type loc
    for it in items:
        h = it['hier']; pg = modtype(it['page'])
        hiers.add(h); pages.add(pg); hier_page.add((h, pg))
        if it['type'] == 'toggle':
            base, _, _ = c2n._toggle_parts(it['comment'])
            hs.add((h, base)); ps.add((pg, base))
        else:
            loc = (it['file'], it['line'], it['col'], it['comment'])
            hl.add((h, *loc)); pl.add((pg, *loc))

    print(f"\n{dat}")
    print(f"  instances (hier)        : {len(hiers):,}")
    print(f"  module types (page)     : {len(pages):,}")
    print(f"  (hier,page) pairs       : {len(hier_page):,}  "
          f"-> avg {len(hier_page)/max(len(pages),1):.1f} instances/type")
    print(f"  toggle (hier,signal)    : {len(hs):,}   [stored per-instance today]")
    print(f"  toggle (page,signal)    : {len(ps):,}   [stored per-type if factored]")
    print(f"     signal-structure replication factor : "
          f"{len(hs)/max(len(ps),1):.1f}x")
    print(f"  loc  (hier,file/line..) : {len(hl):,}")
    print(f"  loc  (page,file/line..) : {len(pl):,}")
    print(f"     location replication factor          : "
          f"{len(hl)/max(len(pl),1):.1f}x")

    # crude size proxy: the per-instance vs per-type "structure mapping" bytes,
    # deflated (one varint id per association).
    def assoc_bytes(pairs, key_card):
        # store sorted associations as delta-coded id pairs
        buf = bytearray()
        for a, b in sorted(pairs):
            buf += vint(abs(hash(a)) % key_card) + vint(abs(hash(b)) % key_card)
        return dz(bytes(buf))
    print(f"  --- structure-mapping size proxy (deflated) ---")
    print(f"  per-instance signal assoc : {dz(bytes().join(vint(i) for i in range(len(hs)))):>9,} (count only)")
    print(f"  factored: per-type sigs {len(ps):,} + per-inst type-ref {len(hier_page):,}")


if __name__ == '__main__':
    main()
