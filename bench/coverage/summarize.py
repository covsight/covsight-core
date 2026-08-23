#!/usr/bin/env python3
"""
summarize.py — render a coverage→NCDB benchmark JSONL into a comparison table.

Reads the JSONL produced by run_suite.sh / coverage_to_ncdb.py --json-line and
prints a markdown table (and writes a CSV alongside), sorted by point count so
the largest stress cases are visible at a glance.

Usage:  python summarize.py results.jsonl [--csv results.csv]
"""

import sys, json, argparse


def _mb(n): return n / (1024 * 1024)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('jsonl')
    ap.add_argument('--csv')
    args = ap.parse_args()

    recs = []
    with open(args.jsonl) as f:
        for line in f:
            line = line.strip()
            if line:
                recs.append(json.loads(line))
    if not recs:
        print("no records")
        return
    # De-dup by label, keeping the last (latest run wins).
    by_label = {r['label']: r for r in recs}
    recs = sorted(by_label.values(), key=lambda r: r['points'], reverse=True)

    hdr = (f"| {'Case':<34} | {'Points':>11} | {'.dat MB':>8} | "
           f"{'.dat.gz':>8} | {'XML MB':>8} | {'XML.gz':>8} | "
           f"{'NCDB MB':>8} | {'NCDB vs XML.gz':>14} |")
    sep = "|" + "|".join(['-' * (w + 2) for w in (34, 11, 8, 8, 8, 8, 8, 14)]) + "|"
    print(hdr); print(sep)
    rows = []
    for r in recs:
        b = r['bytes']
        vs = r.get('ncdb_vs_xml_gz')
        vs_s = f"{1/vs:.2f}× smaller" if vs else "?"
        print(f"| {r['label']:<34} | {r['points']:>11,} | "
              f"{_mb(b['dat']):>8.1f} | {_mb(b['dat_gz']):>8.2f} | "
              f"{_mb(b['xml']):>8.1f} | {_mb(b['xml_gz']):>8.2f} | "
              f"{_mb(b['ncdb_deflated']):>8.2f} | {vs_s:>14} |")
        rows.append([
            r['label'], r['points'], b['dat'], b['dat_gz'], b['xml'],
            b['xml_gz'], b['ncdb_stored'], b['ncdb_deflated'],
            round(1 / vs, 3) if vs else '',
            r.get('convert_s', ''),
        ])

    if args.csv:
        import csv
        with open(args.csv, 'w', newline='') as f:
            w = csv.writer(f)
            w.writerow(['label', 'points', 'dat', 'dat_gz', 'xml', 'xml_gz',
                        'ncdb_stored', 'ncdb_deflated', 'ncdb_smaller_than_xmlgz_x',
                        'convert_s'])
            w.writerows(rows)
        print(f"\nCSV written to {args.csv}")


if __name__ == '__main__':
    main()
