#!/usr/bin/env python3
"""Read-only benchmark: repeated reads of a pre-built .cdb file."""
import sys, os, time
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'python'))

from covsight.core.ncdb.ncdb_reader import NcdbReader
from covsight.core.api import ScopeTypeT, CoverTypeT

path = sys.argv[1] if len(sys.argv) > 1 else '/tmp/bench_large.cdb'
reps = int(sys.argv[2]) if len(sys.argv) > 2 else 5

def walk(db):
    """Walk all scopes and cover items to fully exercise the read."""
    def recurse(scope):
        for ci in scope.coverItems(CoverTypeT.ALL):
            _ = ci.getCoverData().data
        for child in scope.scopes(ScopeTypeT.ALL):
            recurse(child)
    for top in db.scopes(ScopeTypeT.ALL):
        recurse(top)

# warm-up
NcdbReader().read(path)

total = 0.0
for _ in range(reps):
    t0 = time.perf_counter()
    db = NcdbReader().read(path)
    walk(db)
    total += time.perf_counter() - t0

print(f"Python    read  avg={total/reps*1000:.2f}ms  (×{reps})  file={path}")
