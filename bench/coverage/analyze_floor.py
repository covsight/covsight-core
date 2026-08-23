#!/usr/bin/env python3
"""
analyze_floor.py — estimate the achievable NCDB size floor for a coverage.dat
under aggressive per-component codecs, to chart the path to the 10x target
(NCDB <= 10% of gzipped UCIS-XML).

Components measured (each deflated independently; zip members are independent):
  strings        : front-coded (insertion order)
  toggle signals : signal table (hier delta, name idx, width)
  toggle counts  : LOSSLESS varint vector  vs  LOSSY hit-bitmap (1 bit/bin)
  non-toggle     : COLUMNAR (hier RLE, file_id, line delta-per-file, col,
                   comment dict-idx, count varint) — vs the row-oriented tree

Prints lossless and lossy floors vs current NCDB and the 10x target.
Usage:  python analyze_floor.py <coverage.dat> <xml_gz_bytes> <current_ncdb_bytes>
"""
import sys, os, re, zlib, io
_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
import coverage_to_ncdb as c2n


def vint(n):
    n &= (1 << 64) - 1
    b = bytearray()
    while True:
        x = n & 0x7f; n >>= 7
        b.append(x | 0x80 if n else x)
        if not n:
            break
    return bytes(b)


def zz(n):                      # zigzag for signed deltas
    return (n << 1) ^ (n >> 63) if n < 0 else (n << 1)


def dz(b):
    return len(zlib.compress(b, 9))


def frontcode(strings):
    buf = io.BytesIO(); buf.write(vint(len(strings))); prev = ""
    for s in strings:
        cp = 0
        while cp < len(prev) and cp < len(s) and prev[cp] == s[cp] and cp < 255:
            cp += 1
        sb = s[cp:].encode(); buf.write(bytes([cp]) + vint(len(sb)) + sb); prev = s
    return buf.getvalue()


def main():
    dat = sys.argv[1]; xml_gz = int(sys.argv[2]); cur = int(sys.argv[3])
    items = c2n.parse_coverage_dat(dat)

    # ── string pools ────────────────────────────────────────────────────────
    files, hiers, sigs, comments = {}, {}, {}, {}
    def intern(d, s):
        if s not in d: d[s] = len(d)
        return d[s]

    tog = {}          # (hier, sig) -> {(bit,dir): count}
    nontog = []       # (hier_id, file_id, line, col, comment_id, count)
    for it in items:
        hid = intern(hiers, it['hier'])
        if it['type'] == 'toggle':
            base, bit, direction = c2n._toggle_parts(it['comment'])
            sid = intern(sigs, base); intern(files, it['file'])
            d = 0 if direction.startswith('0') else 1
            tog.setdefault((hid, sid), {})[(bit, d)] = it['count']
        else:
            fid = intern(files, it['file'])
            cid = intern(comments, it['comment'])
            try: line = int(it['line'])
            except Exception: line = 0
            try: col = int(it['col'])
            except Exception: col = 0
            nontog.append((hid, fid, line, col, cid, it['count']))

    # ── strings (front-coded) ───────────────────────────────────────────────
    allstr = list(files) + list(hiers) + list(sigs) + list(comments)
    strings_fc = dz(frontcode(allstr))

    # ── toggle signal table ─────────────────────────────────────────────────
    sig_keys = sorted(tog)
    tbl = bytearray(); prev_h = 0
    for (h, s) in sig_keys:
        tbl += vint(zz(h - prev_h)) + vint(s); prev_h = h
        w = max((b for b, _ in tog[(h, s)]), default=0) + 1
        tbl += vint(w)
    sigtable = dz(bytes(tbl))

    # toggle counts: lossless varint vector vs lossy bitmap
    cv = bytearray(); bm = bytearray()
    for k in sig_keys:
        bins = tog[k]; w = max((b for b, _ in bins), default=0) + 1
        ba = bytearray((2 * w + 7) // 8)
        for b in range(w):
            for d in (0, 1):
                c = bins.get((b, d), 0)
                cv += vint(c)
                if c > 0:
                    idx = 2 * b + d; ba[idx >> 3] |= 1 << (idx & 7)
        bm += ba
    tog_counts_lossless = dz(bytes(cv))
    tog_counts_bitmap = dz(bytes(bm))

    # ── non-toggle: columnar, sorted by (hier,file,line,col) ────────────────
    nontog.sort()
    col_h = bytearray(); col_f = bytearray(); col_l = bytearray()
    col_c = bytearray(); col_cm = bytearray(); col_cnt = bytearray()
    ph = pf = pl = 0
    for (h, f, line, col, cid, cnt) in nontog:
        col_h += vint(zz(h - ph)); ph = h
        col_f += vint(f)
        if f != pf: pl = 0
        col_l += vint(zz(line - pl)); pl = line; pf = f
        col_c += vint(col)
        col_cm += vint(cid)
        col_cnt += vint(cnt)
    nt_cols = sum(dz(bytes(c)) for c in (col_h, col_f, col_l, col_c, col_cm, col_cnt))

    base = strings_fc + sigtable + nt_cols
    floor_lossless = base + tog_counts_lossless
    floor_bitmap = base + tog_counts_bitmap
    tgt = xml_gz / 10

    # ── compressor lever: deflate (today) vs LZMA/zstd, big window ──────────
    import lzma
    raw_base = bytes(bytearray()  # rebuild concatenated RAW component streams
                     )
    # recompute raw component blobs to compress as one stream
    raw_strings = frontcode(allstr)
    raw_sig = bytes(tbl)
    raw_nt = bytes(col_h + col_f + col_l + col_c + col_cm + col_cnt)
    raw_lossless = raw_strings + raw_sig + raw_nt + bytes(cv)
    raw_bitmap = raw_strings + raw_sig + raw_nt + bytes(bm)
    def L(b): return len(lzma.compress(b, preset=9 | lzma.PRESET_EXTREME))
    try:
        import zstandard as _zstd
        zc = _zstd.ZstdCompressor(level=19)
        def Z(b): return len(zc.compress(b))
        have_z = True
    except Exception:
        have_z = False
    lz_lossless = L(raw_lossless); lz_bitmap = L(raw_bitmap)

    def line(name, v):
        print(f"  {name:<40}{v:>12,}  ({v/1024:.0f} KB)")
    print(f"\n{dat}")
    print(f"  toggle signals={len(sig_keys):,}  non-toggle points={len(nontog):,}")
    line("strings (front-coded)", strings_fc)
    line("toggle signal table", sigtable)
    line("toggle counts LOSSLESS (varint)", tog_counts_lossless)
    line("toggle counts LOSSY (hit-bitmap)", tog_counts_bitmap)
    line("non-toggle columnar (6 cols)", nt_cols)
    print("  " + "-" * 54)
    line("FLOOR lossless", floor_lossless)
    line("FLOOR + toggle bitmap (lossy)", floor_bitmap)
    print(f"\n  current NCDB : {cur:,} ({cur/1024:.0f} KB)")
    print(f"  10x target   : {tgt:,.0f} ({tgt/1024:.0f} KB)  [10% of xml.gz {xml_gz:,}]")
    df_concat_lossless = dz(raw_lossless); df_concat_bitmap = dz(raw_bitmap)
    print(f"  -- deflate (per-member, today's ZIP) --")
    print(f"  floor lossless : {floor_lossless/tgt:5.2f}x  ({'MEETS' if floor_lossless<=tgt else 'over '})  {floor_lossless/1024:.0f} KB")
    print(f"  floor bitmap   : {floor_bitmap/tgt:5.2f}x  ({'MEETS' if floor_bitmap<=tgt else 'over '})  {floor_bitmap/1024:.0f} KB")
    print(f"  -- deflate (ONE stream, concat) --")
    print(f"  floor lossless : {df_concat_lossless/tgt:5.2f}x  {df_concat_lossless/1024:.0f} KB")
    print(f"  floor bitmap   : {df_concat_bitmap/tgt:5.2f}x  {df_concat_bitmap/1024:.0f} KB")
    print(f"  -- LZMA (ONE stream, 64MB window) --")
    print(f"  floor lossless : {lz_lossless/tgt:5.2f}x  ({'MEETS' if lz_lossless<=tgt else 'over '})  {lz_lossless/1024:.0f} KB")
    print(f"  floor bitmap   : {lz_bitmap/tgt:5.2f}x  ({'MEETS' if lz_bitmap<=tgt else 'over '})  {lz_bitmap/1024:.0f} KB")
    if have_z:
        print(f"  -- zstd-19 -- lossless {Z(raw_lossless)/1024:.0f} KB  bitmap {Z(raw_bitmap)/1024:.0f} KB")


if __name__ == '__main__':
    main()
