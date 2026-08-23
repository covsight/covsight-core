"""T-P.1 — every example's output validates against the UCIS 1.0 XSD.

This is the test that decides whether ucis_writer produces UCIS-XML or merely
something UCIS-shaped. It is deliberately blunt: no per-element assertions,
just the schema's own verdict on every document the example suite emits.
"""

from __future__ import annotations

import pytest


def test_examples_exist(example_outputs):
    assert example_outputs, "no example documents were produced"


def test_every_example_validates(ucis_schema, example_outputs):
    failures = []
    for doc in example_outputs:
        try:
            ucis_schema.validate(str(doc))
        except Exception as exc:  # xmlschema raises several types
            failures.append(f"{doc.name}: {exc}")
    if failures:
        pytest.fail("schema validation failed:\n" + "\n\n".join(failures))


def test_documents_are_utf8(example_outputs):
    """The writer sanitises to valid UTF-8; nothing downstream should have to
    guess an encoding."""
    for doc in example_outputs:
        doc.read_bytes().decode("utf-8")
