"""L1: a cross keeps its operand list across a Parquet round trip (T23).

Before `cross_operands`, a cross kept its bins and lost the coverpoints it
crossed: `UCIS_STR_ITH_CROSSED_CVP_NAME` is a single-valued string property, so
a two-way cross round-tripped as one name and no order. The mapping document
listed that under "Structure this mapping does not carry"; these tests are what
lets that entry go away.

Order is the fact worth asserting hardest. A cross of (addr, data) and a cross
of (data, addr) have the same operand *set*, so a mapping that recovered the set
without the order would pass every "is my coverpoint there" check and still
mis-describe the cross.
"""

from __future__ import annotations

import pytest

pytest.importorskip("pyarrow", reason="Parquet backend needs pyarrow")

from covsight.core.api import (                                   # noqa: E402
    IntProperty, ScopeTypeT, SourceInfo, SourceT, StrProperty,
)
from covsight.core.conformance import ucis_feature                # noqa: E402
from covsight.core.mem import MemFactory                          # noqa: E402
from covsight.core.parquet import ParquetUCIS, ParquetWriter      # noqa: E402

from ..fixtures.feature_complete import build_feature_complete    # noqa: E402


@pytest.fixture(scope="module")
def pair(tmp_path_factory):
    """(oracle, round-tripped) -- the same corpus seen two ways."""
    oracle = build_feature_complete()
    path = tmp_path_factory.mktemp("crossops") / "dataset"
    ParquetWriter(path).write(oracle, run_id="r0")
    return oracle, ParquetUCIS(path)


def _crosses(db):
    """Every cross scope, keyed by hierarchical path."""
    out = {}

    def visit(scope, prefix):
        here = prefix + "/" + str(scope.getScopeName())
        if scope.getScopeType() & ScopeTypeT.CROSS:
            out[here] = scope
        for child in scope.scopes(-1):
            visit(child, here)

    for root in db.scopes(-1):
        visit(root, "")
    return out


def _operands(cross):
    """(name via the property, name via the handle) per operand, in order."""
    out = []
    for i in range(cross.getNumCrossedCoverpoints()):
        point = cross.getIthCrossedCoverpoint(i)
        out.append((cross.getStringProperty(i, StrProperty.ITH_CROSSED_CVP_NAME),
                    None if point is None else point.getScopeName()))
    return out


@ucis_feature("S4.19", "CG.11", "PI.7", "PS.12", level="L1")
def test_the_crossed_coverpoint_list_survives(pair):
    """Same operands, same order, resolved to the same coverpoints."""
    oracle, subject = pair
    before, after = _crosses(oracle), _crosses(subject)
    assert set(before) == set(after)
    assert before, "the corpus is meant to contain a cross"

    for path in before:
        assert _operands(after[path]) == _operands(before[path]), (
            "cross %s lost or reordered its operands" % path)


@ucis_feature("CG.11", "PI.7", level="L1")
def test_the_operand_count_is_the_list_and_not_a_stored_number(pair):
    """`UCIS_INT_NUM_CROSSED_CVPS` is read-only in UCIS, so it must agree with
    the operands by construction rather than by luck."""
    _oracle, subject = pair
    for path, cross in _crosses(subject).items():
        count = cross.getIntProperty(-1, IntProperty.NUM_CROSSED_CVPS)
        assert count == cross.getNumCrossedCoverpoints(), path
        assert count == 2, "the corpus crosses addr with data"

    # Out of range is an error, not a silent None: `ucis_GetIthCrossedCvp`
    # returns non-zero for an index the cross does not have.
    cross = next(iter(_crosses(subject).values()))
    with pytest.raises(IndexError):
        cross.getIthCrossedCoverpoint(2)


@ucis_feature("PS.12", level="L1", surface="write")
def test_a_cross_with_only_a_name_still_round_trips(tmp_path):
    """The pre-`cross_operands` shape: a source that can state one crossed
    coverpoint by name and hold no handles at all.

    Kept working because it is what a UCIS XML import produces -- dropping it
    would trade one gap for another.
    """
    db = MemFactory.create()
    fh = db.createFileHandle("cross.sv", None)
    du = db.createScope("work.du", SourceInfo(fh, 1, 0), 1, SourceT.SV,
                        ScopeTypeT.DU_MODULE, 0)
    inst = db.createInstance("top", None, 1, SourceT.SV, ScopeTypeT.INSTANCE,
                             du, 0)
    cg = inst.createCovergroup("cg", SourceInfo(fh, 2, 0), 1, SourceT.SV)
    # createScope, not createCross: no operand list exists to record.
    cross = cg.createScope("x", SourceInfo(fh, 3, 0), 1, SourceT.SV,
                           ScopeTypeT.CROSS, 0)
    cross.setStringProperty(-1, StrProperty.ITH_CROSSED_CVP_NAME, "addr")

    path = tmp_path / "dataset"
    ParquetWriter(path).write(db, run_id="r0")
    subject = next(iter(_crosses(ParquetUCIS(path)).values()))

    assert subject.getStringProperty(
        -1, StrProperty.ITH_CROSSED_CVP_NAME) == "addr"
    # One operand, named: the count follows the operand list either way round,
    # so the property and the operand API cannot disagree.
    assert subject.getIntProperty(-1, IntProperty.NUM_CROSSED_CVPS) == 1
    # The name is carried; the handle is not, because there was none to carry.
    # Reporting a coverpoint here would be an invention.
    assert subject.getIthCrossedCoverpoint(0) is None
