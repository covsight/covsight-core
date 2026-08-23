#!/usr/bin/env python3
"""Benchmark: Python UCIS write + read roundtrip."""
import sys, os, time, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'python'))

from covsight.core.mem.mem_ucis import MemUCIS
from covsight.core.api.enums.scope_type import ScopeTypeT
from covsight.core.api.enums.cover_type import CoverTypeT
from covsight.core.api.enums.history_node_kind import HistoryNodeKind
from covsight.core.api import ScopeTypeT, CoverTypeT, SourceT
from covsight.core.api.cover_data import CoverData
from covsight.core.ncdb.ncdb_writer import NcdbWriter
from covsight.core.ncdb.ncdb_reader import NcdbReader

N_GROUPS   = int(sys.argv[1]) if len(sys.argv) > 1 else 50
N_POINTS   = int(sys.argv[2]) if len(sys.argv) > 2 else 10
N_BINS     = int(sys.argv[3]) if len(sys.argv) > 3 else 20
N_TESTS    = int(sys.argv[4]) if len(sys.argv) > 4 else 5

def build_db():
    db = MemUCIS()
    fh = db.createFileHandle('bench.sv', '.')
    du = db.createScope('bench_du', None, 1, SourceT.SV, ScopeTypeT.DU_MODULE, 0)
    top = db.createInstance('top', None, 1, SourceT.SV, ScopeTypeT.INSTANCE, du, 0)
    for g in range(N_GROUPS):
        cg = top.createCovergroup(f'cg_{g}', None, 1, SourceT.SV)
        for p in range(N_POINTS):
            cp = cg.createCoverpoint(f'cp_{p}', None, 1, SourceT.SV)
            for b in range(N_BINS):
                cd = CoverData(CoverTypeT.CVGBIN, b % 10)
                cp.createNextCover(f'bin_{b}', cd, None)
    for t in range(N_TESTS):
        db.createHistoryNode(None, f'test_{t}', None, HistoryNodeKind.TEST)
    return db

def main():
    with tempfile.NamedTemporaryFile(suffix='.cdb', delete=False) as f:
        path = f.name

    # Build
    t0 = time.perf_counter()
    db = build_db()
    t_build = time.perf_counter() - t0

    # Write
    t0 = time.perf_counter()
    writer = NcdbWriter()
    writer.write(db, path)
    t_write = time.perf_counter() - t0

    fsize = os.path.getsize(path)

    # Read
    t0 = time.perf_counter()
    reader = NcdbReader()
    db2 = reader.read(path)
    t_read = time.perf_counter() - t0

    os.unlink(path)

    total_bins = N_GROUPS * N_POINTS * N_BINS
    print(f"Python  build={t_build*1000:.1f}ms  write={t_write*1000:.1f}ms  read={t_read*1000:.1f}ms  "
          f"total={(t_build+t_write+t_read)*1000:.1f}ms  "
          f"file={fsize//1024}KB  bins={total_bins}")

if __name__ == '__main__':
    main()
