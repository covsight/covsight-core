#!/usr/bin/env python3
"""
bench_size_realistic.py — NCDB vs UCIS XML size benchmark on *realistic* data.

The companion `bench_size.py` builds a worst-case-compressible database:
every bin shares the same handful of names and — because the count is passed
as `flags` and `.data` is never set — every hit count is 0. An all-zero
counts.bin plus near-identical strings deflate to almost nothing, so its
ZIP_DEFLATED numbers (e.g. 3 KB for 100k bins) are a compression artifact,
not a real-world figure.

This script models how coverage data actually looks:

  • Structural names REPEAT. A covergroup is a *type* instantiated across many
    module instances; its coverpoint and bin names are identical in every
    instance. Only the instance path is unique. NCDB's string table dedups
    these once — a real and fair win — so the entropy lives in the counts.

  • Hit counts are the real data. We draw from a zero-inflated heavy-tailed
    distribution: ~30% of bins uncovered (0), ~45% functional bins hit a
    handful of times, ~25% hot/code-like bins hit thousands→millions of times.
    This is what defeats trivial compression and is the honest test of the
    binary varint encoding vs. XML text integers.

Two name-variety regimes bracket the realistic range:

  • reuse  (default) — covergroup types reused across instances; only the
    instance path is unique. Functional-coverage-like; strings dedup heavily.
  • unique — every coverpoint/bin name is globally unique (no cross-instance
    dedup). Models a large SoC with many distinct module types, or
    toggle/auto-bin coverage where names don't repeat. strings.bin then grows
    linearly with bin count — the pessimal end for the string table.

Usage:
  python bench_size_realistic.py [N_INSTANCES [N_TESTS]] [--variety reuse|unique]
  python bench_size_realistic.py --sweep [--variety ...]   # ~1k/10k/100k bins
  python bench_size_realistic.py --sweep --both            # both regimes
"""

import sys, os, random, tempfile, zipfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'python'))
sys.path.insert(0, os.path.dirname(__file__))

from covsight.core.mem.mem_ucis import MemUCIS
from covsight.core.api import ScopeTypeT, CoverTypeT, SourceT
from covsight.core.api.cover_data import CoverData
from covsight.core.api.enums.history_node_kind import HistoryNodeKind

# Reuse the format writers + the UCIS-XML serializer from the original bench so
# the two benchmarks stay byte-comparable and can't drift.
from bench_size import write_ncdb, build_ucis_xml, write_xml, compress_xml


# ── Realistic covergroup-type catalog ────────────────────────────────────────
#
# Fixed at module load (seeded) so names are identical across every instance —
# exactly how a covergroup type reused across the design behaves. ~320 bins per
# instance, so instance count scales the total bin volume linearly.

def _build_catalog(seed: int = 1):
    rng = random.Random(seed)
    vocab = ['idle', 'busy', 'wait', 'rd', 'wr', 'cfg', 'err', 'ok', 'lo', 'hi',
             'mode', 'burst', 'single', 'incr', 'wrap', 'parity', 'even', 'odd',
             'addr', 'data', 'len', 'state', 'arb', 'grant', 'req', 'ack']
    catalog = []                       # [(cg_name, [(cp_name, [bin_name,...])])]
    n_cgs = 8
    for g in range(n_cgs):
        cg_name = f"{rng.choice(vocab)}_{rng.choice(vocab)}_cg"
        cps = []
        for p in range(rng.randint(4, 6)):
            cp_name = f"{rng.choice(vocab)}_{rng.choice(vocab)}_cp"
            n_bins = rng.choice([4, 8, 8, 16, 16, 32])     # realistic bin counts
            bins = []
            for b in range(n_bins):
                # Mix of value-range labels and auto-style names — fixed per type.
                if rng.random() < 0.5:
                    lo = b * rng.choice([1, 2, 4, 8])
                    bins.append(f"[{lo}:{lo + rng.choice([0, 1, 3, 7])}]")
                else:
                    bins.append(f"{rng.choice(vocab)}_{b}")
            cps.append((cp_name, bins))
        catalog.append((cg_name, cps))
    return catalog


_CATALOG = _build_catalog()
_BINS_PER_INSTANCE = sum(len(bins) for _, cps in _CATALOG for _, bins in cps)


def _instance_catalog(variety: str, inst_idx: int):
    """Return the covergroup catalog for one instance.

    'reuse'  → the shared catalog (identical names every instance → dedup).
    'unique' → same structural shape, but every name tagged with the instance
               index so it is globally unique (no string-table dedup).
    """
    if variety == 'reuse':
        return _CATALOG
    out = []
    for gi, (cg, cps) in enumerate(_CATALOG):
        ncps = []
        for pi, (cp, bins) in enumerate(cps):
            nbins = [f"{b}_i{inst_idx}_{pi}" for b in bins]
            ncps.append((f"{cp}_i{inst_idx}_{gi}", nbins))
        out.append((f"{cg}_i{inst_idx}", ncps))
    return out


# ── Realistic hit-count distribution ─────────────────────────────────────────

def _sample_count(rng: random.Random) -> int:
    r = rng.random()
    if r < 0.30:
        return 0                              # uncovered bin
    elif r < 0.75:
        return rng.randint(1, 64)             # functional bin, hit a few times
    else:
        # hot / code-like path: log-normal, median ~e^8.5 ≈ 4.9k, heavy tail
        return int(rng.lognormvariate(8.5, 2.0))


# ── Database builder ─────────────────────────────────────────────────────────

def build_db(n_instances: int, n_tests: int, variety: str = 'reuse',
             seed: int = 7) -> MemUCIS:
    rng = random.Random(seed)
    db = MemUCIS()
    db.createFileHandle('design.sv', '.')
    du = db.createScope('design_du', None, 1, SourceT.SV, ScopeTypeT.DU_MODULE, 0)

    for i in range(n_instances):
        inst = db.createInstance(f'top.u_block_{i}', None, 1, SourceT.SV,
                                 ScopeTypeT.INSTANCE, du, 0)
        for cg_name, cps in _instance_catalog(variety, i):
            cg = inst.createCovergroup(cg_name, None, 1, SourceT.SV)
            for cp_name, bins in cps:
                cp = cg.createCoverpoint(cp_name, None, 1, SourceT.SV)
                for bin_name in bins:
                    cd = CoverData(CoverTypeT.CVGBIN, 0)
                    cd.data = _sample_count(rng)        # the real entropy
                    cp.createNextCover(bin_name, cd, None)

    for t in range(n_tests):
        db.createHistoryNode(None, f'test_{t}', None, HistoryNodeKind.TEST)
    return db


# ── Single benchmark run ──────────────────────────────────────────────────────

def _member_breakdown(path: str, top: int = 5):
    """Return [(member, compressed_bytes)] for the largest members in a ZIP."""
    with zipfile.ZipFile(path) as zf:
        infos = sorted(zf.infolist(), key=lambda i: i.compress_size, reverse=True)
    return [(i.filename, i.compress_size) for i in infos[:top]]


def run_one(n_instances: int, n_tests: int, variety: str = 'reuse') -> None:
    total_bins = n_instances * _BINS_PER_INSTANCE
    print(f"\n{'─'*66}")
    print(f"  {n_instances} instances × {_BINS_PER_INSTANCE} bins = "
          f"{total_bins:,} bins  |  {n_tests} tests  |  names: {variety}")
    print(f"{'─'*66}")

    with tempfile.TemporaryDirectory() as tmp:
        p_ncdb_z = os.path.join(tmp, 'bench_deflated.cdb')
        p_ncdb_s = os.path.join(tmp, 'bench_stored.cdb')
        p_xml    = os.path.join(tmp, 'bench.xml')
        p_gz     = os.path.join(tmp, 'bench.xml.gz')

        db = build_db(n_instances, n_tests, variety=variety)

        t_ncdb, t_ncdb_s, sz_ncdb, sz_ncdb_s = write_ncdb(db, p_ncdb_z, p_ncdb_s)

        xml_bytes = build_ucis_xml(db)
        sz_xml = len(xml_bytes)
        write_xml(xml_bytes, p_xml)
        _, sz_gz = compress_xml(xml_bytes, p_gz)

        breakdown = _member_breakdown(p_ncdb_z)

    rows = [
        ("NCDB v2 (ZIP_DEFLATED)", sz_ncdb),
        ("NCDB v2 (ZIP_STORED)",   sz_ncdb_s),
        ("UCIS XML (compact)",     sz_xml),
        ("UCIS XML + gzip-9",      sz_gz),
    ]
    print(f"  {'Format':<26}  {'Bytes':>11}  {'KB':>7}  {'vs XML':>8}  {'vs gzip':>8}")
    print(f"  {'-'*26}  {'-'*11}  {'-'*7}  {'-'*8}  {'-'*8}")
    for label, size in rows:
        print(f"  {label:<26}  {size:>11,}  {size/1024:>7.1f}  "
              f"{size/sz_xml:>7.3f}×  {size/sz_gz:>7.3f}×")

    print(f"  largest NCDB members (deflated): " +
          ", ".join(f"{name} {sz/1024:.0f}K" for name, sz in breakdown))


def _arg(flag: str, default: str) -> str:
    return sys.argv[sys.argv.index(flag) + 1] if flag in sys.argv else default


def main():
    varieties = ['reuse', 'unique'] if '--both' in sys.argv \
        else [_arg('--variety', 'reuse')]

    if '--sweep' in sys.argv:
        # instance counts chosen to land near 1k / 10k / 100k bins
        inst_counts = (max(1, 1000 // _BINS_PER_INSTANCE),
                       max(1, 10_000 // _BINS_PER_INSTANCE),
                       max(1, 100_000 // _BINS_PER_INSTANCE))
        for variety in varieties:
            for n_inst in inst_counts:
                run_one(n_inst, n_tests=20, variety=variety)
    else:
        pos = [a for a in sys.argv[1:] if not a.startswith('--')
               and sys.argv[sys.argv.index(a) - 1] != '--variety']
        n_instances = int(pos[0]) if len(pos) > 0 else 30
        n_tests     = int(pos[1]) if len(pos) > 1 else 20
        for variety in varieties:
            run_one(n_instances, n_tests, variety=variety)


if __name__ == '__main__':
    main()
