"""T-11: scale, and the memory claim design section 5 rests on.

Opt-in: it needs bench/coverage/rtlmeter-out/coverage_0.dat, a 241 MB OpenTitan
capture that is not in the repository. Skipped when absent.

The claim under test is not "it is fast" but "peak staging is bounded by the
largest single scope, not by the design". So the assertion is on the writer's
reported staging peak, and on it being far below the size of the document
produced -- a writer that accumulated would show the two converging.
"""

from __future__ import annotations

import gzip
import shutil
import subprocess
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
WRITER_DIR = REPO / "cpp" / "ucis-xml"
DAT = REPO / "bench" / "coverage" / "rtlmeter-out" / "coverage_0.dat"

# From bench/coverage/scope_distribution.py on this capture; the same numbers
# design section 5.1 is written from.
EXPECTED_POINTS = 1_241_667
EXPECTED_SCOPES = 1_520
LARGEST_SCOPE_POINTS = 37_404

pytestmark = pytest.mark.skipif(not DAT.exists(),
                                reason=f"{DAT} not present (241 MB, not in-tree)")


@pytest.fixture(scope="module")
def replay(tmp_path_factory):
    if shutil.which("cmake") is None:
        pytest.skip("cmake not available")
    build = tmp_path_factory.mktemp("ucis_xml_scale")
    subprocess.run(["cmake", "-S", str(WRITER_DIR), "-B", str(build),
                    "-DCMAKE_BUILD_TYPE=Release"], check=True, capture_output=True)
    subprocess.run(["cmake", "--build", str(build), "-j", "--target", "scale_replay"],
                   check=True, capture_output=True)
    exe = build / "tests" / "scale_replay"
    assert exe.exists(), f"scale_replay not built at {exe}"
    return exe


def run(replay: Path, out: Path, *args) -> dict[str, str]:
    proc = subprocess.run([str(replay), str(DAT), str(out), *args],
                          check=True, capture_output=True, text=True)
    stats = {}
    for line in proc.stdout.splitlines():
        key, _, value = line.partition(" ")
        stats[key] = value.strip()
    return stats


@pytest.mark.parametrize("mode", ["upfront", "deferred"])
def test_replay(replay, tmp_path, mode):
    out = tmp_path / f"{mode}.xml"
    stats = run(replay, out, *(["--deferred"] if mode == "deferred" else []))

    assert stats["ok"] == "1", stats.get("error", "")
    assert int(stats["points"]) == EXPECTED_POINTS
    assert int(stats["skipped"]) == 0
    assert int(stats["warnings"]) == 0
    assert int(stats["distinctScopes"]) == EXPECTED_SCOPES

    raw = out.stat().st_size

    # The claim. Staging holds one scope, so the peak is a few MB against a
    # 120 MB document -- and it is a function of the largest scope, not of the
    # 1.24 M points that went through it.
    staged = int(stats["stagingPeak"])
    assert staged < 16 * 1024 * 1024, f"staging peak {staged} is not scope-bounded"
    assert staged < raw / 10

    # Sanity on the shape of the arithmetic: roughly 160 bytes of staging per
    # point in the largest scope. A big miss either way means the largest scope
    # is not what is being staged.
    assert staged / LARGEST_SCOPE_POINTS < 1024

    if mode == "upfront":
        assert int(stats["spoolBytes"]) == 0, "UpFront must not spool"
    else:
        assert int(stats["spoolBytes"]) > 0, "Deferred must spool the body"


def test_output_is_the_expected_size(replay, tmp_path):
    """Design section 8.4's numbers, re-measured against this emitter.

    The reference is 122.89 MiB raw / 3.11 MiB gzipped from
    bench/coverage/ucis_xml_writer.py on the same capture. This writer should
    land in the same neighbourhood -- it emits the same information with the
    same group-by-bit layout -- and a large divergence means one of them
    changed what it stores.
    """
    out = tmp_path / "size.xml"
    run(replay, out)
    raw_mib = out.stat().st_size / (1024 * 1024)
    gz_mib = len(gzip.compress(out.read_bytes(), 9)) / (1024 * 1024)

    assert 100 < raw_mib < 145, f"raw {raw_mib:.2f} MiB is off the measured 122.89"
    assert 2.0 < gz_mib < 4.5, f"gzipped {gz_mib:.2f} MiB is off the measured 3.11"


def test_schema_valid_at_scale(replay, ucis_schema, tmp_path):
    """Validation of a 120 MB document, streamed.

    xmlschema caps a non-lazy resource at a million elements, and this document
    has several million, so it has to be validated lazily. Slow -- minutes --
    which is the other reason this file is opt-in.
    """
    xmlschema = pytest.importorskip("xmlschema")
    out = tmp_path / "valid.xml"
    run(replay, out)
    ucis_schema.validate(xmlschema.XMLResource(str(out), lazy=True))
