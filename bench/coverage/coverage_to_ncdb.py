#!/usr/bin/env python3
"""
coverage_to_ncdb.py — import a Verilator coverage.dat into NCDB and compare
on-disk sizes against the native .dat and a UCIS-XML rendering.

Pipeline:
    coverage.dat  →  parse (SystemC::Coverage-3)  →  MemUCIS  →  NCDB
    + size comparison vs. the native .dat (gzipped) and UCIS-XML (gzipped).

This is the *code-coverage* analogue of bench_size_realistic.py: instead of a
synthetic count/name model, it measures whatever Verilator actually emitted for
a real design (line / branch / toggle points, real hierarchy, real counts).

The .dat parser mirrors pyucis `ucis/vltcov` (kept self-contained here so the
benchmark has no cross-repo path dependency; the format is a stable Verilator
output format, not covsight logic).

Usage:
    python coverage_to_ncdb.py <coverage.dat> [--out db.cdb] [--keep-xml out.xml]
"""

import sys, os, re, gzip, json, time, argparse, tempfile, zipfile
import xml.etree.ElementTree as ET

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_HERE, '..', '..', 'python'))   # covsight core
sys.path.insert(0, os.path.join(_HERE, '..'))                    # bench helpers

from covsight.core.mem.mem_ucis import MemUCIS
from covsight.core.api import ScopeTypeT, CoverTypeT, SourceT
from covsight.core.api.cover_data import CoverData
from covsight.core.api.source_info import SourceInfo
from covsight.core.ncdb.ncdb_writer import NcdbWriter

# Reuse the STORED/DEFLATED writer + gzip helper from the size bench.
from bench_size import write_ncdb, compress_xml


# ── Parse Verilator SystemC::Coverage-3 .dat ──────────────────────────────────

def _decode_compact(compact: str) -> dict:
    """Decode \\001key\\002value\\001... into a dict (Verilator compact form)."""
    out, pos, n = {}, 0, len(compact)
    while pos < n:
        if compact[pos] == '\001':
            pos += 1
            ks = pos
            while pos < n and compact[pos] != '\002':
                pos += 1
            if pos >= n:
                break
            key = compact[ks:pos]
            pos += 1
            vs = pos
            while pos < n and compact[pos] != '\001':
                pos += 1
            out[key] = compact[vs:pos]
        else:
            pos += 1
    return out


def parse_coverage_dat(path: str) -> list:
    """Return a list of dicts: {file, line, type, page, hier, comment, count}."""
    import re
    pat = re.compile(r"C\s+'([^']*)'\s+(\d+)")
    items = []
    with open(path, 'r', errors='replace') as f:
        for line in f:
            line = line.rstrip('\n')
            if not line or line.startswith('#'):
                continue
            m = pat.match(line)
            if not m:
                continue
            a = _decode_compact(m.group(1))
            page = a.get('page', '')
            if 'v_toggle' in page or a.get('t') == 'toggle':
                ctype = 'toggle'
            elif 'v_branch' in page or a.get('t') == 'branch':
                ctype = 'branch'
            elif 'v_line' in page or a.get('t') == 'line':
                ctype = 'line'
            elif 'v_funccov' in page or a.get('t') == 'funccov':
                ctype = 'funccov'
            else:
                ctype = a.get('t') or 'line'
            items.append(dict(
                file=a.get('f', ''), line=a.get('l', ''), col=a.get('n', ''),
                type=ctype, page=page, hier=a.get('h', '') or 'top',
                comment=a.get('o', '') or a.get('bin', ''),
                count=int(m.group(2))))
    return items


# ── Map parsed items → covsight-core MemUCIS (signal-grouped) ────────────────
#
# Toggle coverage is GROUPED by signal: one UCIS_TOGGLE scope per
# (instance, signal), the signal name stored once. The per-bit / per-direction
# points become covers named compactly "<bit>:<dir>" — a small, highly-repeated
# string set instead of 1.2M unique "sig[bit]:dir" names. Line/branch/expr stay
# one cover per point (they're a tiny fraction of the data).

_NONTOG = {
    'branch': (ScopeTypeT.BRANCH, CoverTypeT.BRANCHBIN, 'branches'),
    'line':   (ScopeTypeT.BLOCK,  CoverTypeT.STMTBIN,   'lines'),
}

_BIT_RE = re.compile(r'\[\d+\]')
_LASTBIT_RE = re.compile(r'\[(\d+)\]\s*$')


def _toggle_parts(o: str):
    """'adc_chnsel_i[0]:0->1' -> (base, bit, dir)."""
    net, _, direction = o.partition(':')
    m = _LASTBIT_RE.search(net)
    bit = int(m.group(1)) if m else 0
    base = _BIT_RE.sub('', net) or net
    return base, bit, direction


def build_db(items: list, toggle_covers: bool = True) -> MemUCIS:
    # toggle_covers=False creates the per-signal TOGGLE scope but omits the
    # per-bit/direction cover items — used by the vector-toggle prototype to
    # measure the tree/counts saving when bit bins live in a packed member.
    db = MemUCIS()
    files = {}
    for it in items:
        if it['file'] and it['file'] not in files:
            files[it['file']] = db.createFileHandle(it['file'], '.')
    du = db.createScope('design_du', None, 1, SourceT.SV, ScopeTypeT.DU_MODULE, 0)

    instances = {}      # hier -> instance scope
    tog = {}            # (hier, signal) -> TOGGLE scope
    loc = {}            # (hier, file, line, col, type) -> per-location BLOCK/BRANCH scope

    def get_inst(hier):
        inst = instances.get(hier)
        if inst is None:
            inst = db.createInstance(hier, None, 1, SourceT.SV,
                                     ScopeTypeT.INSTANCE, du, 0)
            instances[hier] = inst
        return inst

    def as_int(s):
        try:
            return int(s)
        except (TypeError, ValueError):
            return 0

    for it in items:
        hier = it['hier']
        if it['type'] == 'toggle':
            base, bit, direction = _toggle_parts(it['comment'])
            key = (hier, base)
            sig = tog.get(key)
            if sig is None:
                sig = get_inst(hier).createScope(base, None, 1, SourceT.SV,
                                                 ScopeTypeT.TOGGLE, 0)
                tog[key] = sig
            if toggle_covers:
                cd = CoverData(CoverTypeT.TOGGLEBIN, 0)
                cd.data = it['count']
                sig.createNextCover(f"{bit}:{direction}", cd, None)  # few-distinct
        else:
            # Per-location scope carries the location via SourceInfo (file_id +
            # line + col), NOT a monolithic "file:line:comment" name string.
            # The cover is named by just the short comment (small vocabulary).
            scope_t, cover_t, _ = _NONTOG.get(it['type'], _NONTOG['line'])
            key = (hier, it['file'], it['line'], it['col'], scope_t)
            child = loc.get(key)
            if child is None:
                fh = files.get(it['file'])
                src = SourceInfo(fh, as_int(it['line']), as_int(it['col']))
                child = get_inst(hier).createScope("", src, 1, SourceT.SV,
                                                   scope_t, 0)
                loc[key] = child
            cd = CoverData(cover_t, 0)
            cd.data = it['count']
            child.createNextCover(it['comment'], cd, None)        # comment only
    return db


# ── Grouped UCIS-XML (apples-to-apples: signal name once, per-bit bins) ───────

def build_ucis_xml(items: list) -> bytes:
    root = ET.Element('UCIS', ucisVersion='1.0', writtenBy='covsight-bench')
    files = {}
    for it in items:
        if it['file'] and it['file'] not in files:
            fid = str(len(files) + 1)
            files[it['file']] = fid
            ET.SubElement(root, 'sourceFiles', fileName=it['file'], id=fid)

    # Bucket by instance, splitting toggle (grouped by signal) from the rest.
    by_hier = {}
    for it in items:
        h = by_hier.setdefault(it['hier'], {'tog': {}, 'other': []})
        if it['type'] == 'toggle':
            base, bit, direction = _toggle_parts(it['comment'])
            h['tog'].setdefault(base, []).append((bit, direction, it['count']))
        else:
            h['other'].append(it)

    key = 0
    for hier, buckets in by_hier.items():
        key += 1
        ic = ET.SubElement(root, 'instanceCoverages', name=hier, key=str(key))
        if buckets['tog']:
            tc = ET.SubElement(ic, 'toggleCoverage')
            for base, bins in buckets['tog'].items():
                tg = ET.SubElement(tc, 'toggle', name=base)   # signal name ONCE
                for bit, direction, count in bins:
                    b = ET.SubElement(tg, 'toggleBin', name=f"{bit}:{direction}")
                    ET.SubElement(b, 'contents', coverageCount=str(count))
        if buckets['other']:
            sc = ET.SubElement(ic, 'statementCoverage')
            for it in buckets['other']:
                # Location via file-id + line attrs (sourceFiles table), name =
                # comment only — apples-to-apples with NCDB's source-info.
                pt = ET.SubElement(sc, 'point', name=it['comment'],
                                   file=files.get(it['file'], '0'),
                                   line=it['line'], col=it['col'])
                ET.SubElement(pt, 'contents', coverageCount=str(it['count']))
    return ET.tostring(root, encoding='utf-8', xml_declaration=True)


# ── Report ────────────────────────────────────────────────────────────────────

def _members(path: str, top: int = 6):
    with zipfile.ZipFile(path) as zf:
        infos = sorted(zf.infolist(), key=lambda i: i.compress_size, reverse=True)
    return [(i.filename, i.compress_size) for i in infos[:top]]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dat', help='Verilator coverage.dat file')
    ap.add_argument('--out', help='write the NCDB here (kept)')
    ap.add_argument('--keep-xml', help='write the UCIS-XML rendering here (kept)')
    ap.add_argument('--label', default='', help='tag for the metrics record (e.g. Design:config:test)')
    ap.add_argument('--json-line', help='append one JSON metrics record to this file (for suite aggregation)')
    args = ap.parse_args()

    t0 = time.perf_counter()
    items = parse_coverage_dat(args.dat)
    by_type = {}
    for it in items:
        by_type[it['type']] = by_type.get(it['type'], 0) + 1
    n_hier = len({it['hier'] for it in items})
    sz_dat = os.path.getsize(args.dat)

    print(f"\nparsed {len(items):,} coverage points from {args.dat}")
    print(f"  by type: " + ", ".join(f"{k}={v:,}" for k, v in sorted(by_type.items())))
    print(f"  instances (hierarchies): {n_hier:,}")

    db = build_db(items)
    xml_bytes = build_ucis_xml(items)

    with tempfile.TemporaryDirectory() as tmp:
        p_def = args.out or os.path.join(tmp, 'cov.cdb')
        p_sto = os.path.join(tmp, 'cov_stored.cdb')
        _, _, sz_def, sz_sto = write_ncdb(db, p_def, p_sto)
        members = _members(p_def)

        with open(args.dat, 'rb') as f:
            sz_dat_gz = len(gzip.compress(f.read(), 9, mtime=0))

        sz_xml = len(xml_bytes)
        p_xmlgz = os.path.join(tmp, 'cov.xml.gz')
        _, sz_xml_gz = compress_xml(xml_bytes, p_xmlgz)

        if args.keep_xml:
            with open(args.keep_xml, 'wb') as f:
                f.write(xml_bytes)
    elapsed = time.perf_counter() - t0

    rows = [
        ("Verilator coverage.dat (raw)", sz_dat),
        ("coverage.dat + gzip-9",        sz_dat_gz),
        ("UCIS-XML (compact)",           sz_xml),
        ("UCIS-XML + gzip-9",            sz_xml_gz),
        ("NCDB (ZIP_STORED)",            sz_sto),
        ("NCDB (ZIP_DEFLATED)",          sz_def),
    ]
    print(f"\n  {'Format':<30}  {'Bytes':>11}  {'KB':>8}  {'vs .dat':>8}  {'vs .dat.gz':>10}")
    print(f"  {'-'*30}  {'-'*11}  {'-'*8}  {'-'*8}  {'-'*10}")
    for label, size in rows:
        print(f"  {label:<30}  {size:>11,}  {size/1024:>8.1f}  "
              f"{size/sz_dat:>7.3f}×  {size/sz_dat_gz:>9.3f}×")
    print("  largest NCDB members (deflated): " +
          ", ".join(f"{n} {s/1024:.0f}K" for n, s in members))
    if args.out:
        print(f"\n  NCDB written to {args.out}")

    if args.json_line:
        rec = {
            "label": args.label, "dat": os.path.abspath(args.dat),
            "points": len(items), "by_type": by_type, "instances": n_hier,
            "bytes": {
                "dat": sz_dat, "dat_gz": sz_dat_gz, "xml": sz_xml,
                "xml_gz": sz_xml_gz, "ncdb_stored": sz_sto, "ncdb_deflated": sz_def,
            },
            "ncdb_vs_xml_gz": round(sz_def / sz_xml_gz, 4) if sz_xml_gz else None,
            "members": {n: s for n, s in members},
            "convert_s": round(elapsed, 1),
        }
        with open(args.json_line, 'a') as f:
            f.write(json.dumps(rec) + "\n")
        print(f"  metrics appended to {args.json_line}")


if __name__ == '__main__':
    main()
