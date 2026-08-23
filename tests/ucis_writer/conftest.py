"""Fixtures for the ucis_writer schema and golden tests.

The examples are the fixture corpus (plan 7.2): rather than hand-writing XML
here, we build and run c/ucis-writer/examples and validate what they produce.
An example that stops compiling, stops running, or starts emitting something
the schema rejects fails here.

c/ucis-writer configures standalone and has no dependencies, so this builds in
a session tmpdir without needing the rest of the C tree.
"""

from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
WRITER_DIR = REPO / "c" / "ucis-writer"
SCHEMA = REPO / "python" / "covsight" / "core" / "schema" / "ucis.xsd"


@pytest.fixture(scope="session")
def ucis_schema():
    xmlschema = pytest.importorskip("xmlschema")
    return xmlschema.XMLSchema(str(SCHEMA))


@pytest.fixture(scope="session")
def example_outputs(tmp_path_factory) -> list[Path]:
    """Build and run every example; return the documents they wrote.

    Honours UCIS_WRITER_EXAMPLE_OUT so CI can reuse the build the C job
    already did instead of configuring a second one.
    """
    preexisting = os.environ.get("UCIS_WRITER_EXAMPLE_OUT")
    if preexisting:
        out = Path(preexisting)
        docs = sorted(out.glob("*.xml"))
        if not docs:
            pytest.fail(f"UCIS_WRITER_EXAMPLE_OUT={out} contains no documents")
        return docs

    if shutil.which("cmake") is None:
        pytest.skip("cmake not available")

    build = tmp_path_factory.mktemp("ucis_writer_build")
    try:
        subprocess.run(["cmake", "-S", str(WRITER_DIR), "-B", str(build)],
                       check=True, capture_output=True, text=True)
        subprocess.run(["cmake", "--build", str(build), "-j"],
                       check=True, capture_output=True, text=True)
        subprocess.run(["ctest", "--test-dir", str(build), "--output-on-failure"],
                       check=True, capture_output=True, text=True)
    except FileNotFoundError:
        pytest.skip("no C toolchain available")
    except subprocess.CalledProcessError as e:
        pytest.fail(f"building c/ucis-writer failed:\n{e.stdout}\n{e.stderr}")

    docs = sorted((build / "examples" / "out").glob("*.xml"))
    assert docs, "examples produced no output"
    return docs
