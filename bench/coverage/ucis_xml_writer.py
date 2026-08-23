"""Schema-valid UCIS-XML writer (streaming) + the 'lean' dialect, for size benchmarking.

Emits UCIS-XML that validates cleanly against python/covsight/core/schema/ucis.xsd.
Streaming (writes text directly) so a multi-million-point design does not need an
in-memory element tree.

Information contract -- MUST stay equal to what the NCDB path stores, or the size
comparison is meaningless:
  * toggle  : grouped by signal (name stored once); per bit/direction bins
  * others  : file + line + column + comment + count
UCIS has no column field on STATEMENT_ID and no name attribute on BIN, so:
  * comment -> contents/@nameComponent
  * column  -> <userAttr key="column" type="int">
Dropping either would understate schema-valid XML against .dat and NCDB.
"""
import gzip
from xml.sax.saxutils import escape, quoteattr

WRITTEN_TIME = '2026-07-26T00:00:00'


def _toggle_parts(comment: str):
    """'sig[3]:0->1' -> ('sig', '3', '0->1'); tolerant of missing pieces."""
    base, bit, direction = comment, '0', '0->1'
    if ':' in comment:
        base, _, direction = comment.rpartition(':')
    if base.endswith(']') and '[' in base:
        base, _, rest = base.rpartition('[')
        bit = rest[:-1]
    return base, bit, direction


def _posint(v, default='1'):
    """STATEMENT_ID/@line and @file are positiveInteger; Verilator can emit 0/''."""
    try:
        n = int(v)
    except (TypeError, ValueError):
        return default
    return str(n) if n >= 1 else default


def write_valid_xml(items, out, *, written_by='covsight-bench',
                    compact_keys=False, group_by_bit=True):
    """Stream schema-valid UCIS-XML for `items` to binary file object `out`.

    compact_keys: emit short integer @key instead of duplicating @name.  Legal
        (the XSD types @key as a plain xsd:string with no stated relation to
        @name) but DEFAULTS OFF because it is measurably worse: on OpenTitan it
        costs +2.9% raw and +69% gzipped.  Repeating the signal name is nearly
        free under deflate -- it back-references the adjacent @name a few bytes
        away -- whereas a unique counter is incompressible entropy.  Kept as a
        flag only so the result stays reproducible.
    group_by_bit: emit one <toggleBit> per bit carrying both directions as
        sibling <toggle> children.  TOGGLE_BIT allows toggle maxOccurs=unbounded,
        so the per-(bit,direction) element is a writer choice, not a requirement.
    """
    w = lambda s: out.write(s.encode('utf-8'))

    files = {}
    for it in items:
        if it['file'] and it['file'] not in files:
            files[it['file']] = str(len(files) + 1)

    w("<?xml version='1.0' encoding='utf-8'?>\n")
    w(f'<UCIS ucisVersion="1.0" writtenBy={quoteattr(written_by)} '
      f'writtenTime="{WRITTEN_TIME}">')
    for path, fid in files.items():
        w(f'<sourceFiles fileName={quoteattr(path)} id="{fid}"/>')
    # historyNodes is minOccurs=1 with nine required attributes.
    w('<historyNodes historyNodeId="0" logicalName="run" testStatus="true" '
      f'date="{WRITTEN_TIME}" toolCategory="UCIS:simulator" ucisVersion="1.0" '
      'vendorId="VLTR" vendorTool="verilator" vendorToolVersion="5.0"/>')

    by_hier = {}
    for it in items:
        h = by_hier.setdefault(it['hier'], {'tog': {}, 'other': []})
        if it['type'] == 'toggle':
            base, bit, direction = _toggle_parts(it['comment'])
            h['tog'].setdefault(base, []).append((bit, direction, it['count']))
        else:
            h['other'].append(it)

    key = 0
    nkey = 0
    for hier, buckets in by_hier.items():
        key += 1
        w(f'<instanceCoverages name={quoteattr(hier)} key="{key}">')
        w('<id file="1" line="1" inlineCount="1"/>')
        if buckets['tog']:
            w('<toggleCoverage>')
            for base, bins in buckets['tog'].items():
                # toggleCoverage > toggleObject > toggleBit > toggle > bin > contents
                nkey += 1
                okey = str(nkey) if compact_keys else base
                w(f'<toggleObject name={quoteattr(base)} key={quoteattr(okey)}>')
                w('<id file="1" line="1" inlineCount="1"/>')
                if group_by_bit:
                    per_bit = {}
                    for bit, direction, count in bins:
                        per_bit.setdefault(bit, []).append((direction, count))
                else:
                    per_bit = {}
                    for i, (bit, direction, count) in enumerate(bins):
                        per_bit[(bit, i)] = [(direction, count)]
                for bkey, dirs in per_bit.items():
                    bit = bkey[0] if isinstance(bkey, tuple) else bkey
                    nkey += 1
                    bk = str(nkey) if compact_keys else bit
                    w(f'<toggleBit name={quoteattr(bit)} key={quoteattr(bk)}>')
                    for direction, count in dirs:
                        frm, _, to_ = direction.partition('->')
                        w(f'<toggle from={quoteattr(frm or "0")} to={quoteattr(to_ or "1")}>'
                          f'<bin><contents coverageCount="{count}"/></bin>'
                          f'</toggle>')
                    w('</toggleBit>')
                w('</toggleObject>')
            w('</toggleCoverage>')
        if buckets['other']:
            w('<blockCoverage>')
            for it in buckets['other']:
                w(f'<statement><id file="{files.get(it["file"], "1")}" '
                  f'line="{_posint(it["line"])}" inlineCount="1"/>'
                  f'<bin><contents coverageCount="{it["count"]}" '
                  f'nameComponent={quoteattr(it["comment"])}/>')
                if it.get('col'):
                    w(f'<userAttr key="column" type="int">{escape(str(it["col"]))}</userAttr>')
                w('</bin></statement>')
            w('</blockCoverage>')
        w('</instanceCoverages>')
    w('</UCIS>')


def write_valid_xml_gz(items, path, **kw):
    with gzip.open(path, 'wb', compresslevel=9) as f:
        write_valid_xml(items, f, **kw)
