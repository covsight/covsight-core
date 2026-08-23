"""Fixtures for the ucis_xml schema and source-mode tests.

The examples are the fixture corpus: rather than hand-writing XML here, we
build and run cpp/ucis-xml/examples and validate what they produce. An example
that stops compiling, stops running, or starts emitting something the schema
rejects fails here. That also keeps the published documentation honest, since
the doc pages include those same files.

cpp/ucis-xml configures standalone and has no dependencies, so this builds in a
session tmpdir without needing the rest of the tree.
"""

from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
WRITER_DIR = REPO / "cpp" / "ucis-xml"
SCHEMA = REPO / "python" / "covsight" / "core" / "schema" / "ucis.xsd"


@pytest.fixture(scope="session")
def ucis_schema():
    xmlschema = pytest.importorskip("xmlschema")
    return xmlschema.XMLSchema(str(SCHEMA))


@pytest.fixture(scope="session")
def example_out(tmp_path_factory) -> Path:
    """Directory holding one XML document per example.

    Honours UCIS_XML_EXAMPLE_OUT so CI can reuse the build it already did
    rather than configuring cmake a second time.
    """
    preset = os.environ.get("UCIS_XML_EXAMPLE_OUT")
    if preset:
        out = Path(preset)
        if any(out.glob("*.xml")):
            return out

    if shutil.which("cmake") is None:
        pytest.skip("cmake not available")

    build = tmp_path_factory.mktemp("ucis_xml_build")
    subprocess.run(["cmake", "-S", str(WRITER_DIR), "-B", str(build)],
                   check=True, capture_output=True)
    subprocess.run(["cmake", "--build", str(build), "-j"],
                   check=True, capture_output=True)
    subprocess.run(["ctest", "--test-dir", str(build), "--output-on-failure"],
                   check=True, capture_output=True)

    out = build / "examples" / "out"
    assert any(out.glob("*.xml")), f"no example output in {out}"
    return out


@pytest.fixture(scope="session")
def example_docs(example_out) -> list[Path]:
    return sorted(example_out.glob("*.xml"))
