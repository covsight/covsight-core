"""The C writer's output is read back by covsight's own UCIS-XML reader.

This is the drift control for having two writers of one format. Schema
validation (test_schema_valid.py) proves a document is *well formed*; it says
nothing about whether a coverage count landed in the attribute a reader looks
in. Only reading it back does.

It earned its keep immediately: the first run found that the C writer emits UTC
as `...Z` -- valid xsd:dateTime, and the conventional form -- while the reader
ported from pyucis accepted no timezone designator at all, so no document
ucis_writer produced could be read by covsight.
"""

from __future__ import annotations

import pytest

from covsight.core.api.enums import CoverTypeT, HistoryNodeKind, ScopeTypeT
from covsight.core.xml import XmlReader


def _by_name(outputs, stem):
    for p in outputs:
        if p.stem == stem:
            return p
    pytest.skip(f"example {stem} not in the corpus")


def _scopes(scope):
    return list(scope.scopes(ScopeTypeT.ALL))


def _bins(scope):
    return {ci.getName(): ci.getCoverData().data
            for ci in scope.coverItems(CoverTypeT.ALL)}


def test_every_example_reads_back(example_outputs):
    """Blunt pass/fail over the whole corpus, mirroring the schema test."""
    failures = []
    for doc in example_outputs:
        try:
            XmlReader().read(str(doc))
        except Exception as exc:
            failures.append(f"{doc.name}: {type(exc).__name__}: {exc}")
    if failures:
        pytest.fail("documents the C writer produced but covsight cannot read:\n"
                    + "\n".join(failures))


def test_covergroup_content_survives(example_outputs):
    """Names, counts and structure, not just parseability.

    10_cross.c writes two coverpoints and their cross with counts chosen so
    that a mix-up between them would be visible rather than plausible.
    """
    db = XmlReader().read(str(_by_name(example_outputs, "10_cross")))

    inst = next(s for s in _scopes(db) if s.getScopeName() == "tb.u_mon")
    cg = next(s for s in _scopes(inst) if s.getScopeName() == "cg_xfer")
    cgi = next(iter(cg.scopes(ScopeTypeT.COVERINSTANCE)))

    got = {s.getScopeName(): _bins(s) for s in _scopes(cgi)}

    assert got["cp_mode"] == {"0": 500, "1": 501}
    assert got["cp_len"] == {"0": 300, "1": 301, "2": 302}
    assert got["x_mode_len"] == {
        "0,0": 100, "0,1": 101, "0,2": 102,
        "1,0": 110, "1,1": 111, "1,2": 112,
    }


def test_covergroup_type_identity_survives(example_outputs):
    """D14: the covergroup type scope emits no element of its own, so the type's
    name reaches a reader only through cgId/@cgName. If that mapping breaks,
    the type silently becomes anonymous -- which validates fine."""
    db = XmlReader().read(str(_by_name(example_outputs, "09_covergroup")))

    inst = next(s for s in _scopes(db) if s.getScopeName() == "tb.u_mon")
    cg = next(s for s in _scopes(inst) if s.getScopeName() == "cg_bus")
    cgi = next(iter(cg.scopes(ScopeTypeT.COVERINSTANCE)))

    assert cgi.getScopeName() == "cg_bus_inst"
    assert {s.getScopeName() for s in _scopes(cgi)} == {"cp_len", "cp_addr"}


def test_history_node_timezone(example_outputs):
    """The regression this file was written by: a UTC 'Z' designator must read."""
    for doc in example_outputs:
        db = XmlReader().read(str(doc))
        nodes = list(db.historyNodes(HistoryNodeKind.ALL))
        assert nodes, f"{doc.name} lost its history node"
        # A date that failed to parse would have raised in read().
        assert nodes[0].getDate() is not None
