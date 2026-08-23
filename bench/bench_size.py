#!/usr/bin/env python3
"""
bench_size.py — NCDB v2 vs UCIS XML vs UCIS XML+gzip file-size benchmark.

Builds an identical coverage database at several scales and compares the
on-disk sizes of:
  • NCDB v2   — ZIP-deflated binary (production format)
  • XML       — compact UCIS 1.0 XML, no indentation (best-case XML)
  • XML+gzip  — compact XML compressed with gzip -9

Usage:
  python bench_size.py [N_GROUPS [N_POINTS [N_BINS [N_TESTS]]]]
  python bench_size.py --sweep          # run 1k / 10k / 100k bin sweep

Default single run: 50 groups × 10 points × 20 bins = 10 000 bins
"""

import sys, os, gzip, time, zipfile, tempfile
from datetime import datetime, timezone
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'python'))

from covsight.core.mem.mem_ucis import MemUCIS
from covsight.core.api import ScopeTypeT, CoverTypeT, SourceT
from covsight.core.api.cover_data import CoverData
from covsight.core.api.enums.history_node_kind import HistoryNodeKind
from covsight.core.ncdb.ncdb_writer import NcdbWriter


# ── Database builder ────────────────────────────────────────────────────────

def build_db(n_groups: int, n_points: int, n_bins: int, n_tests: int) -> MemUCIS:
    db = MemUCIS()
    db.createFileHandle('bench.sv', '.')
    du  = db.createScope('bench_du', None, 1, SourceT.SV, ScopeTypeT.DU_MODULE, 0)
    top = db.createInstance('top', None, 1, SourceT.SV, ScopeTypeT.INSTANCE, du, 0)
    for g in range(n_groups):
        cg = top.createCovergroup(f'cg_{g}', None, 1, SourceT.SV)
        for p in range(n_points):
            cp = cg.createCoverpoint(f'cp_{p}', None, 1, SourceT.SV)
            for b in range(n_bins):
                cd = CoverData(CoverTypeT.CVGBIN, b % 10)
                cp.createNextCover(f'bin_{b}', cd, None)
    for t in range(n_tests):
        db.createHistoryNode(None, f'test_{t}', None, HistoryNodeKind.TEST)
    return db


# ── NCDB v2 writer ──────────────────────────────────────────────────────────

def write_ncdb(db: MemUCIS, path_deflated: str, path_stored: str) -> tuple[float, float, int, int]:
    """Write NCDB twice: once with ZIP_DEFLATED, once with ZIP_STORED.

    Returns (t_deflated_s, t_stored_s, sz_deflated, sz_stored).
    ZIP_STORED shows the true binary encoding size without compression;
    ZIP_DEFLATED is the production artifact.
    """
    t0 = time.perf_counter()
    NcdbWriter().write(db, path_deflated)
    t_deflated = time.perf_counter() - t0

    # Re-pack the already-serialised members without compression so we get
    # a real file (not a synthetic sum) representing the raw binary payload.
    t0 = time.perf_counter()
    with zipfile.ZipFile(path_deflated) as src, \
         zipfile.ZipFile(path_stored, 'w', compression=zipfile.ZIP_STORED) as dst:
        for info in src.infolist():
            dst.writestr(info.filename, src.read(info.filename))
    t_stored = time.perf_counter() - t0

    return t_deflated, t_stored, os.path.getsize(path_deflated), os.path.getsize(path_stored)


# ── UCIS XML builder ────────────────────────────────────────────────────────

def _stmt_id(parent: ET.Element, tag: str) -> None:
    sid = ET.SubElement(parent, tag)
    ET.SubElement(sid, 'file').text = '1'
    ET.SubElement(sid, 'line').text = '1'
    ET.SubElement(sid, 'inlineCount').text = '1'


def build_ucis_xml(db: MemUCIS) -> bytes:
    """Return compact (no indentation) UCIS 1.0 XML bytes for *db*."""
    now = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
    root = ET.Element('UCIS',
                       ucisVersion='1.0',
                       writtenBy='covsight-bench',
                       writtenTime=now)

    # <sourceFiles> — one entry per file handle
    ET.SubElement(root, 'sourceFiles', fileName='bench.sv', id='1')

    # <historyNodes>
    for idx, node in enumerate(db.historyNodes(HistoryNodeKind.TEST)):
        ET.SubElement(root, 'historyNodes',
                      historyNodeId=str(idx),
                      logicalName=node.getLogicalName(),
                      kind='test',
                      testStatus='true')

    # <instanceCoverages> — one per top-level HDL instance
    key = [1]

    def next_key() -> str:
        k = key[0]; key[0] += 1; return str(k)

    for inst in db.scopes(ScopeTypeT.INSTANCE):
        ic = ET.SubElement(root, 'instanceCoverages',
                           name=inst.getScopeName(),
                           key=next_key())
        _stmt_id(ic, 'id')

        cgcov = ET.SubElement(ic, 'covergroupCoverage')
        for cg in inst.scopes(ScopeTypeT.COVERGROUP):
            cg_el = ET.SubElement(cgcov, 'cgInstance',
                                  name=cg.getScopeName(),
                                  key=next_key())
            ET.SubElement(cg_el, 'options')
            cgid = ET.SubElement(cg_el, 'cgId',
                                 cgName=cg.getScopeName(),
                                 moduleName='bench_du')
            _stmt_id(cgid, 'cginstSourceId')
            _stmt_id(cgid, 'cgSourceId')

            for cp in cg.scopes(ScopeTypeT.COVERPOINT):
                cp_el = ET.SubElement(cg_el, 'coverpoint',
                                      name=cp.getScopeName(),
                                      key=next_key(),
                                      exprString='')
                ET.SubElement(cp_el, 'options')
                for ci in cp.coverItems(CoverTypeT.ALL):
                    bin_el = ET.SubElement(cp_el, 'coverpointBin',
                                           name=ci.getName(),
                                           key=next_key(),
                                           type='bins')
                    count = ci.getCoverData().data if ci.getCoverData() else 0
                    ET.SubElement(bin_el, 'contents',
                                  coverageCount=str(int(count)))

    return ET.tostring(root, encoding='utf-8', xml_declaration=True)


# ── Writers for each format ─────────────────────────────────────────────────

def write_xml(xml_bytes: bytes, path: str) -> float:
    t0 = time.perf_counter()
    with open(path, 'wb') as f:
        f.write(xml_bytes)
    return time.perf_counter() - t0


def compress_xml(xml_bytes: bytes, path: str) -> tuple[float, int]:
    t0 = time.perf_counter()
    gz = gzip.compress(xml_bytes, compresslevel=9, mtime=0)
    with open(path, 'wb') as f:
        f.write(gz)
    elapsed = time.perf_counter() - t0
    return elapsed, len(gz)


# ── Single benchmark run ────────────────────────────────────────────────────

def run_one(n_groups: int, n_points: int, n_bins: int, n_tests: int) -> None:
    total_bins = n_groups * n_points * n_bins
    print(f"\n{'─'*62}")
    print(f"  {n_groups}g × {n_points}p × {n_bins}b = {total_bins:,} bins  |  {n_tests} tests")
    print(f"{'─'*62}")

    with tempfile.TemporaryDirectory() as tmp:
        p_ncdb_z = os.path.join(tmp, 'bench_deflated.cdb')
        p_ncdb_s = os.path.join(tmp, 'bench_stored.cdb')
        p_xml    = os.path.join(tmp, 'bench.xml')
        p_gz     = os.path.join(tmp, 'bench.xml.gz')

        # Build
        t0 = time.perf_counter()
        db = build_db(n_groups, n_points, n_bins, n_tests)
        t_build = time.perf_counter() - t0

        # NCDB v2 (deflated + stored)
        t_ncdb, t_ncdb_s, sz_ncdb, sz_ncdb_s = write_ncdb(db, p_ncdb_z, p_ncdb_s)

        # UCIS XML (compact)
        t0 = time.perf_counter()
        xml_bytes = build_ucis_xml(db)
        t_xml_gen = time.perf_counter() - t0
        sz_xml = len(xml_bytes)
        t_xml_write = write_xml(xml_bytes, p_xml)

        # UCIS XML + gzip -9
        t_gz, sz_gz = compress_xml(xml_bytes, p_gz)

    t_xml_total = t_xml_gen + t_xml_write

    # ── Results table ────────────────────────────────────────────────────────
    rows = [
        ("NCDB v2 (ZIP_DEFLATED)",   sz_ncdb,   t_ncdb),
        ("NCDB v2 (ZIP_STORED)",     sz_ncdb_s, None),
        ("UCIS XML (compact)",       sz_xml,    t_xml_total),
        ("UCIS XML + gzip-9",        sz_gz,     t_gz + t_xml_gen),
    ]

    hdr = f"  {'Format':<26}  {'Bytes':>10}  {'KB':>7}  {'vs XML':>8}  {'Write':>9}"
    print(hdr)
    print(f"  {'-'*26}  {'-'*10}  {'-'*7}  {'-'*8}  {'-'*9}")
    for label, size, t_write in rows:
        ratio = size / sz_xml
        t_str = f"{t_write*1000:6.1f} ms" if t_write is not None else "         "
        print(f"  {label:<26}  {size:>10,}  {size//1024:>7,}  {ratio:>7.3f}×  {t_str}")

    print(f"\n  build={t_build*1000:.1f} ms  |  "
          f"NCDB write={t_ncdb*1000:.1f} ms  |  "
          f"XML gen={t_xml_gen*1000:.1f} ms  |  "
          f"gzip={t_gz*1000:.1f} ms")


# ── Entry point ──────────────────────────────────────────────────────────────

def main():
    if '--sweep' in sys.argv:
        # 1k, 10k, 100k bins
        configs = [
            (10, 10, 10, 5),   #  1 000 bins
            (50, 10, 20, 5),   # 10 000 bins
            (50, 20, 100, 10), # 100 000 bins
        ]
        for cfg in configs:
            run_one(*cfg)
    else:
        n_groups = int(sys.argv[1]) if len(sys.argv) > 1 else 50
        n_points = int(sys.argv[2]) if len(sys.argv) > 2 else 10
        n_bins   = int(sys.argv[3]) if len(sys.argv) > 3 else 20
        n_tests  = int(sys.argv[4]) if len(sys.argv) > 4 else 5
        run_one(n_groups, n_points, n_bins, n_tests)


if __name__ == '__main__':
    main()
