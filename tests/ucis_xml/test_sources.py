"""T-7 and task I-0.2: the two facts design section 3.1 rests on.

The source-mode argument in the design is built on two claims about tooling
outside the header. Both were verified by hand while the design was written,
which is not the same as being true tomorrow, so they are pinned here.

Fact 1: the source-file table may list files no coverage item references.
    ucis.xsd declares sourceFiles with fileName and id and has no xsd:key or
    xsd:keyref anywhere, so unreferenced entries validate. This is what turns
    "register your files first" from "pre-scan your coverage data" into "hand
    over the compile file list you already have" -- and it is what makes
    Sources::UpFront a reasonable default (decision D-5).

Fact 2: concatenated gzip members decompress transparently (RFC 1952 section
    2.2), and every ordinary reader handles them. That is what lets deferred
    mode cost only the *compressed* size of the body rather than the raw size:
    3.11 MiB against 122.89 MiB on OpenTitan.

The caveat the design states is also checked here: a consumer that calls raw
inflate() once and stops sees only the first member. That is precisely why the
multi-member path is kept off the default (D-5), and the test asserts the
caveat is real rather than leaving it as a worry.
"""

from __future__ import annotations

import gzip
import shutil
import subprocess
import zlib
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]

HEADER = (
    '<?xml version="1.0" encoding="utf-8"?>'
    '<UCIS ucisVersion="1.0" writtenBy="t" writtenTime="2026-01-01T00:00:00">'
)
FILES = (
    '<sourceFiles fileName="referenced.sv" id="1"/>'
    '<sourceFiles fileName="never-referenced.sv" id="2"/>'
    '<sourceFiles fileName="also-unreferenced.sv" id="3"/>'
)
HISTORY = (
    '<historyNodes historyNodeId="0" logicalName="run" testStatus="true"'
    ' date="2026-01-01T00:00:00" toolCategory="UCIS:simulator" ucisVersion="1.0"'
    ' vendorId="T" vendorTool="t" vendorToolVersion="1"/>'
)
BODY = (
    '<instanceCoverages name="top" key="top">'
    '<id file="1" line="1" inlineCount="1"/>'
    '<blockCoverage><statement><id file="1" line="7" inlineCount="1"/>'
    '<bin><contents coverageCount="3"/></bin></statement></blockCoverage>'
    '</instanceCoverages>'
)
FOOTER = "</UCIS>"

DOCUMENT = (HEADER + FILES + HISTORY + BODY + FOOTER).encode()


def test_unreferenced_source_files_validate(ucis_schema, tmp_path):
    """Fact 1. Two of the three sourceFiles entries are referenced by nothing."""
    doc = tmp_path / "superset.xml"
    doc.write_bytes(DOCUMENT)
    ucis_schema.validate(str(doc))


def test_writer_accepts_a_superset_file_list(ucis_schema, example_out):
    """The same fact, through the writer: 02_statement declares rtl/unused.sv."""
    doc = example_out / "02_statement.xml"
    text = doc.read_text()
    assert 'fileName="rtl/unused.sv"' in text, "the example no longer tests a superset"
    assert 'file="4"' not in text, "unused.sv should not be referenced by any item"
    ucis_schema.validate(str(doc))


def multi_member(tmp_path: Path) -> Path:
    """A gzip file built the way deferred mode builds one: two members, written
    separately and concatenated byte for byte, with no recompression."""
    head = zlib.compressobj(9, zlib.DEFLATED, 16 + zlib.MAX_WBITS)
    member1 = head.compress(HEADER.encode() + FILES.encode() + HISTORY.encode())
    member1 += head.flush()

    tail = zlib.compressobj(9, zlib.DEFLATED, 16 + zlib.MAX_WBITS)
    member2 = tail.compress(BODY.encode() + FOOTER.encode())
    member2 += tail.flush()

    path = tmp_path / "multi.xml.gz"
    path.write_bytes(member1 + member2)
    return path


def test_concatenated_gzip_members_read_back_with_python(tmp_path):
    """Fact 2, reader 1 of 3: Python's gzip module."""
    path = multi_member(tmp_path)
    assert gzip.decompress(path.read_bytes()) == DOCUMENT
    with gzip.open(path, "rb") as f:
        assert f.read() == DOCUMENT


@pytest.mark.skipif(shutil.which("zcat") is None, reason="zcat not available")
def test_concatenated_gzip_members_read_back_with_zcat(tmp_path):
    """Fact 2, reader 2 of 3."""
    path = multi_member(tmp_path)
    out = subprocess.run(["zcat", str(path)], capture_output=True, check=True)
    assert out.stdout == DOCUMENT


@pytest.mark.skipif(shutil.which("gzip") is None, reason="gzip not available")
def test_concatenated_gzip_members_pass_gzip_t(tmp_path):
    """Fact 2, reader 3 of 3: gzip's own integrity check accepts the result."""
    path = multi_member(tmp_path)
    subprocess.run(["gzip", "-t", str(path)], check=True, capture_output=True)


def test_single_inflate_sees_only_the_first_member(tmp_path):
    """The caveat, asserted rather than assumed.

    A consumer that calls raw inflate() once gets the first member and stops.
    Making this a test means the day it stops being true -- or the day someone
    proposes making multi-member the default -- there is a concrete statement of
    what the risk actually is.
    """
    path = multi_member(tmp_path)
    d = zlib.decompressobj(16 + zlib.MAX_WBITS)
    first = d.decompress(path.read_bytes())
    assert first != DOCUMENT
    assert first == (HEADER + FILES + HISTORY).encode()
    # The rest is still there, as an unconsumed tail: nothing was lost, it just
    # needs a second pass.
    assert d.unused_data
