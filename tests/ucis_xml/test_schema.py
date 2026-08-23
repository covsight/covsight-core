"""T-1 and T-2: every fixture validates, and the corpus covers every XSD type.

T-1 is the definition of done for the writer: an emitter that produces
something the schema rejects has not done its job, whatever else it gets right.

T-2 is the completeness contract from design section 6. The check here is
mechanical rather than a hand-maintained list: read the complex types out of
ucis.xsd, walk the example documents, and require that each type was actually
instantiated somewhere. A type nobody exercises is a type nobody has tested.
"""

from __future__ import annotations

import re
from pathlib import Path
from xml.etree import ElementTree as ET

import pytest

REPO = Path(__file__).resolve().parents[2]
SCHEMA = REPO / "python" / "covsight" / "core" / "schema" / "ucis.xsd"

# Element name -> the XSD complex type it instantiates. Several types appear
# under more than one element name (BLOCK is both `block` and
# `hierarchicalBlock`), which is exactly the kind of thing that goes wrong, so
# the mapping is explicit rather than inferred.
ELEMENT_TYPES = {
    "UCIS": "UCIS",
    "sourceFiles": "SOURCE_FILE",
    "historyNodes": "HISTORY_NODE",
    "instanceCoverages": "INSTANCE_COVERAGE",
    "designParameter": "NAME_VALUE",
    "cgParms": "NAME_VALUE",
    "id": "STATEMENT_ID",
    "blockId": "STATEMENT_ID",
    "statementId": "STATEMENT_ID",
    "cginstSourceId": "STATEMENT_ID",
    "cgSourceId": "STATEMENT_ID",
    "toggleCoverage": "TOGGLE_COVERAGE",
    "toggleObject": "TOGGLE_OBJECT",
    "toggleBit": "TOGGLE_BIT",
    "toggle": "TOGGLE",
    "dimension": "DIMENSION",
    "blockCoverage": "BLOCK_COVERAGE",
    "statement": None,  # STATEMENT or BRANCH_STATEMENT, resolved by parent
    "block": "BLOCK",
    "hierarchicalBlock": "BLOCK",
    "process": "PROCESS_BLOCK",
    "conditionCoverage": "CONDITION_COVERAGE",
    "expr": "EXPR",
    "hierarchicalExpr": "EXPR",
    "branchCoverage": "BRANCH_COVERAGE",
    "branch": "BRANCH",
    "nestedBranch": "BRANCH_STATEMENT",
    "fsmCoverage": "FSM_COVERAGE",
    "fsm": "FSM",
    "state": None,  # FSM_STATE under <fsm>, plain string under <stateTransition>
    "stateTransition": "FSM_TRANSITION",
    "assertionCoverage": "ASSERTION_COVERAGE",
    "assertion": "ASSERTION",
    "covergroupCoverage": "COVERGROUP_COVERAGE",
    "cgInstance": "CGINSTANCE",
    "cgId": "CG_ID",
    "coverpoint": "COVERPOINT",
    "coverpointBin": "COVERPOINT_BIN",
    "range": "RANGE_VALUE",
    "sequence": "SEQUENCE",
    "cross": "CROSS",
    "crossBin": "CROSS_BIN",
    "options": None,  # one of the three *_OPTIONS, resolved by parent
    "bin": "BIN",
    "blockBin": "BIN",
    "branchBin": "BIN",
    "stateBin": "BIN",
    "transitionBin": "BIN",
    "coverBin": "BIN",
    "passBin": "BIN",
    "failBin": "BIN",
    "vacuousBin": "BIN",
    "disabledBin": "BIN",
    "attemptBin": "BIN",
    "activeBin": "BIN",
    "peakActiveBin": "BIN",
    "contents": "BIN_CONTENTS",
    "userAttr": "USER_ATTR",
    "metricMode": "METRIC_MODE",
}

# Types the schema declares that this writer does not produce, each with the
# reason. Anything not listed here and not exercised fails the test.
NOT_PRODUCED = {
    # LINE_ID is declared in ucis.xsd and then never referenced by any element
    # (grep the schema: the only two hits are the comment and the declaration).
    # It is unreachable in a valid document, so no fixture can exercise it.
    "LINE_ID",
    # METRIC_MODE is an *element* carrying userAttr children, distinct from the
    # metricMode *attribute* of metricAttributes. The vocabulary API exposes
    # the attribute (Scope::metricMode), which is what tools actually set; the
    # element exists to hang user attributes off a metric, and no producer we
    # know of emits it.
    "METRIC_MODE",
}


def complex_types() -> set[str]:
    text = SCHEMA.read_text()
    return set(re.findall(r'<xsd:complexType\s+name="([A-Z_]+)"', text)) | {"UCIS"}


def types_in(path: Path) -> set[str]:
    """Every XSD complex type instantiated by the document at `path`."""
    found: set[str] = set()
    root = ET.parse(path).getroot()

    def walk(elem, parent_tag):
        tag = elem.tag
        t = ELEMENT_TYPES.get(tag, "")
        if t is None:
            # The ambiguous names, resolved by their parent.
            if tag == "statement":
                t = "BRANCH_STATEMENT" if parent_tag == "branchCoverage" else "STATEMENT"
            elif tag == "state":
                t = "FSM_STATE" if parent_tag == "fsm" else None
            elif tag == "options":
                t = {"coverpoint": "COVERPOINT_OPTIONS",
                     "cross": "CROSS_OPTIONS",
                     "cgInstance": "CGINST_OPTIONS"}.get(parent_tag)
        if t:
            found.add(t)
        for child in elem:
            walk(child, tag)

    walk(root, None)
    return found


def test_every_example_validates(ucis_schema, example_docs):
    assert example_docs, "no example documents were produced"
    for doc in example_docs:
        ucis_schema.validate(str(doc))


def test_corpus_covers_every_complex_type(example_docs):
    covered: set[str] = set()
    for doc in example_docs:
        covered |= types_in(doc)

    declared = complex_types()
    missing = declared - covered - NOT_PRODUCED
    assert not missing, (
        "no fixture exercises these XSD complex types: "
        + ", ".join(sorted(missing))
        + " -- design section 6 is the completeness contract, so either add an "
          "example or record the type in NOT_PRODUCED with a reason"
    )

    # And the exclusion list must not rot: anything listed as not produced but
    # now appearing means the list is stale.
    stale = NOT_PRODUCED & covered
    assert not stale, f"NOT_PRODUCED is stale, these are produced now: {sorted(stale)}"


@pytest.mark.parametrize("name", ["10_full_coverage"])
def test_full_coverage_fixture_is_the_broad_one(example_out, name):
    """The completeness fixture should be the one carrying most of the corpus.

    If it stops being so, either it lost coverage or another example silently
    became the de facto completeness test -- both worth knowing about.
    """
    doc = example_out / f"{name}.xml"
    assert doc.exists()
    assert len(types_in(doc)) >= 30
