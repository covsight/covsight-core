"""Source locations resolve to real files, not to the "(unknown)" placeholder.

Linking coverage back to source is what reporting tools do with a UCIS-XML
document, so a document whose locations all point at file 1 validates perfectly
and is useless. The completeness test next door checks that every XSD complex
type is *instantiated*; this one checks that the ones carrying a location say
something. Presence is not fidelity, and that distinction is what let three
constructs ship with no way to carry a location at all:

  * ``instanceCoverages/id``           -- ``Scope::declaredAt()``
  * ``toggleObject/id``                -- ``Scope::signal(name, file, line)``
  * ``CG_ID/cginstSourceId``/``cgSourceId``
                                       -- ``Covergroup::instanceAt()``/``typeAt()``

File id 1 is always the synthetic ``(unknown)`` entry, which exists so that a
fact with no path has somewhere legal to point. Every assertion here is that a
construct which *should* know its location does not land there.
"""

from __future__ import annotations

from pathlib import Path
from xml.etree import ElementTree as ET

import pytest

# Elements whose @file must name a real source file in the fixtures below.
LOCATED = ("id", "blockId", "statementId", "cginstSourceId", "cgSourceId")

UNKNOWN_FILE_ID = "1"


def source_files(root) -> dict[str, str]:
    return {e.get("id"): e.get("fileName") for e in root.iter("sourceFiles")}


def located_elements(root):
    """Yield (element, parent) for everything carrying a source-file id."""
    for parent in root.iter():
        for child in parent:
            if child.tag in LOCATED:
                yield child, parent


@pytest.mark.parametrize("name", [
    "01_hello",
    "02_statement",
    "03_branch",
    "04_condition",
    "05_toggle",
    "06_pointsink",
    "07_covergroup",
    "09_fsm_assertion",
    "08_cross",
    "10_full_coverage",
])
def test_no_location_falls_back_to_unknown(example_out, name):
    root = ET.parse(example_out / f"{name}.xml").getroot()
    files = source_files(root)
    assert files.get(UNKNOWN_FILE_ID) == "(unknown)"

    unresolved = [
        f"{parent.tag}/{el.tag}"
        for el, parent in located_elements(root)
        if el.get("file") == UNKNOWN_FILE_ID
    ]
    assert not unresolved, (
        f"{name}: these point at the synthetic (unknown) file instead of a real "
        f"source: {sorted(set(unresolved))}"
    )


def test_toggle_objects_carry_their_declaration_site(example_out):
    """A toggle fact is about a signal, not a line.

    So ``toggleObject/id`` can only come from ``Scope::signal(name, file, line)``
    -- there is no per-toggle location to fall back on. Before that overload
    existed, every toggle object in every document pointed at file 1 line 1.
    """
    root = ET.parse(example_out / "05_toggle.xml").getroot()
    files = source_files(root)

    objects = list(root.iter("toggleObject"))
    assert objects, "the toggle fixture stopped emitting toggle objects"

    seen = {}
    for obj in objects:
        ident = obj.find("id")
        assert ident is not None
        assert files[ident.get("file")] == "rtl/fifo.sv"
        seen[obj.get("name")] = int(ident.get("line"))

    # Distinct signals declared on distinct lines: a single shared fallback
    # would make these all equal.
    assert len(set(seen.values())) == len(seen), (
        f"every toggle object got the same line: {seen}"
    )


def test_covergroup_ids_can_name_two_different_files(example_out):
    """CG_ID's two locations are usually in different files.

    The covergroup *type* lives in a package; the *instance* lives in the
    testbench that samples it. A writer that could only express one of them
    would lose that, so the fixture deliberately uses two files.
    """
    root = ET.parse(example_out / "07_covergroup.xml").getroot()
    files = source_files(root)

    cg_ids = list(root.iter("cgId"))
    assert cg_ids, "the covergroup fixture stopped emitting cgId"

    for cg_id in cg_ids:
        inst = cg_id.find("cginstSourceId")
        typ = cg_id.find("cgSourceId")
        assert inst is not None and typ is not None
        assert files[inst.get("file")] == "tb/fifo_cg.sv"
        assert files[typ.get("file")] == "tb/fifo_pkg.sv"
        assert inst.get("line") != typ.get("line")


def test_scope_declaration_site_is_distinct_from_its_coverage(example_out):
    """``instanceCoverages/id`` is where the *instance* is declared.

    It is not the location of any coverage item inside it, so a writer that
    reused a child's location would be wrong in a way nothing else catches.
    """
    root = ET.parse(example_out / "01_hello.xml").getroot()
    files = source_files(root)

    scope = root.find("instanceCoverages")
    assert scope is not None
    scope_id = scope.find("id")
    assert files[scope_id.get("file")] == "rtl/alu.sv"
    assert scope_id.get("line") == "12"

    stmt_id = scope.find("blockCoverage/statement/id")
    assert stmt_id is not None
    assert stmt_id.get("line") == "42"


def test_pointsink_carries_locations_through(example_out):
    """The flat path must not lose locations the records had.

    A converter's records carry a signal's declaration site, and it is the only
    source of ``toggleObject/id`` -- but declaring it per record would stage one
    signal entry per point, so PointSink declares it once per signal. Whether
    that dedup works is invisible in the output, so the assertion here is the
    consequence: the location arrives.
    """
    root = ET.parse(example_out / "06_pointsink.xml").getroot()
    files = source_files(root)

    for obj in root.iter("toggleObject"):
        ident = obj.find("id")
        assert ident is not None
        assert files[ident.get("file")] != "(unknown)", (
            "PointSink dropped the signal's declaration site"
        )

    scopes = {s.get("name"): s for s in root.iter("instanceCoverages")}
    assert scopes, "the flat fixture stopped emitting scopes"
    for name, scope in scopes.items():
        ident = scope.find("id")
        assert files[ident.get("file")] != "(unknown)", (
            f"{name}: Point::declaredAt() did not reach instanceCoverages/id"
        )


def test_every_declared_source_file_is_reachable(example_out):
    """Superset file lists are legal, but a *referenced* id must exist.

    The complement of the unreferenced-entries test in test_sources.py: entries
    may go unused, but nothing may reference an id that was never declared.
    """
    for doc in sorted(Path(example_out).glob("*.xml")):
        root = ET.parse(doc).getroot()
        declared = set(source_files(root))
        for el, parent in located_elements(root):
            assert el.get("file") in declared, (
                f"{doc.name}: {parent.tag}/{el.tag} references undeclared "
                f"source file id {el.get('file')}"
            )
