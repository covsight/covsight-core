"""One consistent size comparison: coverage.dat / UCIS-XML (lean + schema-valid) / NCDB.

Every artifact is produced from a SINGLE parse of the same coverage.dat, in one
run, so the six numbers are directly comparable.  Sizes are reported in bytes
(authoritative) and MiB.  The schema-valid XML is validated against ucis.xsd on
a subset produced by the same code path used for the full file.
"""
import argparse, gzip, io, json, os, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../python'))

from coverage_to_ncdb import parse_coverage_dat, build_db, build_ucis_xml
from ucis_xml_writer import write_valid_xml
from bench_size import write_ncdb

XSD = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   '../../python/covsight/core/schema/ucis.xsd')
MIB = 1024 * 1024


def gz_size(path_in, path_out):
    t0 = time.perf_counter()
    with open(path_in, 'rb') as fi, gzip.open(path_out, 'wb', compresslevel=9) as fo:
        while chunk := fi.read(1 << 20):
            fo.write(chunk)
    return os.path.getsize(path_out), time.perf_counter() - t0


def validate_subset(items, n=20000):
    try:
        import xmlschema
    except ImportError:
        return None
    b = io.BytesIO()
    write_valid_xml(items[:n], b)
    with tempfile.NamedTemporaryFile(suffix='.xml', delete=False) as f:
        f.write(b.getvalue()); p = f.name
    try:
        errs = [e.reason for e in xmlschema.XMLSchema(XSD).iter_errors(p)]
    finally:
        os.unlink(p)
    return errs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dat')
    ap.add_argument('--label', default='')
    ap.add_argument('--json-line')
    args = ap.parse_args()

    t0 = time.perf_counter()
    items = parse_coverage_dat(args.dat)
    by_type = {}
    for it in items:
        by_type[it['type']] = by_type.get(it['type'], 0) + 1
    tog_share = by_type.get('toggle', 0) / max(1, len(items))
    print(f"parsed {len(items):,} points in {time.perf_counter()-t0:.1f}s "
          f"({tog_share:.1%} toggle)")
    print(f"  by type: " + ", ".join(f"{k}={v:,}" for k, v in sorted(by_type.items())))

    errs = validate_subset(items)
    print(f"  schema validation (20k-point subset, same code path): "
          + ("SKIPPED (no xmlschema)" if errs is None else f"{len(errs)} errors"))
    if errs:
        for e in errs[:5]:
            print("    -", e)

    sizes, times = {}, {}
    with tempfile.TemporaryDirectory() as tmp:
        sizes['dat'] = os.path.getsize(args.dat)
        sizes['dat_gz'], times['dat_gz'] = gz_size(args.dat, f'{tmp}/c.dat.gz')

        t = time.perf_counter()
        lean = build_ucis_xml(items)
        times['xml_lean'] = time.perf_counter() - t
        p = f'{tmp}/lean.xml'
        with open(p, 'wb') as f:
            f.write(lean)
        sizes['xml_lean'] = len(lean)
        del lean
        sizes['xml_lean_gz'], times['xml_lean_gz'] = gz_size(p, f'{tmp}/lean.xml.gz')
        os.unlink(p)

        t = time.perf_counter()
        p = f'{tmp}/valid.xml'
        with open(p, 'wb') as f:
            write_valid_xml(items, f)
        times['xml_valid'] = time.perf_counter() - t
        sizes['xml_valid'] = os.path.getsize(p)
        sizes['xml_valid_gz'], times['xml_valid_gz'] = gz_size(p, f'{tmp}/valid.xml.gz')
        os.unlink(p)

        t = time.perf_counter()
        db = build_db(items)
        times['ncdb_build'] = time.perf_counter() - t
        t_def, t_sto, sz_def, sz_sto = write_ncdb(db, f'{tmp}/c.cdb', f'{tmp}/c_s.cdb')
        sizes['ncdb_deflated'], sizes['ncdb_stored'] = sz_def, sz_sto
        times['ncdb_deflated'], times['ncdb_stored'] = t_def, t_sto

    order = ['dat', 'dat_gz', 'xml_lean', 'xml_lean_gz', 'xml_valid',
             'xml_valid_gz', 'ncdb_stored', 'ncdb_deflated']
    base = sizes['xml_valid_gz']
    print(f"\n{'artifact':<20}{'bytes':>15}{'MiB':>10}{'B/point':>10}{'vs valid.gz':>13}")
    for k in order:
        print(f"{k:<20}{sizes[k]:>15,}{sizes[k]/MIB:>10.2f}"
              f"{sizes[k]/len(items):>10.2f}{sizes[k]/base:>12.2f}x")

    print(f"\nkey ratios")
    print(f"  valid XML / lean XML      raw {sizes['xml_valid']/sizes['xml_lean']:.2f}x"
          f"   gz {sizes['xml_valid_gz']/sizes['xml_lean_gz']:.2f}x")
    print(f"  .dat / valid XML          raw {sizes['dat']/sizes['xml_valid']:.2f}x"
          f"   gz {sizes['dat_gz']/sizes['xml_valid_gz']:.2f}x")
    print(f"  valid XML.gz / NCDB.defl  {base/sizes['ncdb_deflated']:.2f}x")
    print(f"  .dat.gz / NCDB.defl       {sizes['dat_gz']/sizes['ncdb_deflated']:.2f}x")

    if args.json_line:
        rec = {'label': args.label or args.dat, 'dat': args.dat,
               'points': len(items), 'by_type': by_type,
               'toggle_share': round(tog_share, 4),
               'schema_errors': (None if errs is None else len(errs)),
               'bytes': sizes, 'times_s': {k: round(v, 2) for k, v in times.items()}}
        with open(args.json_line, 'a') as f:
            f.write(json.dumps(rec) + '\n')


if __name__ == '__main__':
    main()
