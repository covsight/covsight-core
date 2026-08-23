"""L1: the UCIS typed-property vocabulary survives a Parquet round trip.

The registry gained the § 5 and § 8 key spaces in T21 -- every
`ucisIntPropertyEnumT` / `ucisStringPropertyEnumT` / `ucisRealPropertyEnumT` /
`ucisHandleEnumT` constant and the get/set surface that reaches them. This file
is the evidence for the ones the corpus can exercise.

What makes these tests worth more than the general round-trip assertion is that
they pin the *key*: `properties.prop_id` is the enum constant name with its
`UCIS_INT_` / `UCIS_STR_` prefix removed, and a writer that spelled it any other
way would still round-trip through our own reader while being unreadable to
anyone else. That is exactly the class of defect the mapping document's
"The UCIS property vocabulary" section exists to prevent, so it is asserted
against the stored bytes rather than through the reader.
"""

from __future__ import annotations

import pytest

pytest.importorskip("pyarrow", reason="Parquet backend needs pyarrow")

import pyarrow.parquet as pq                                   # noqa: E402

from covsight.core.api import (                                # noqa: E402
    HistoryNodeKind, IntProperty, RealProperty, StrProperty,
)
from covsight.core.api.unimpl_error import UnimplError         # noqa: E402
from covsight.core.conformance import ucis_feature             # noqa: E402
from covsight.core.parquet import ParquetUCIS, ParquetWriter    # noqa: E402

from ..fixtures.feature_complete import build_feature_complete  # noqa: E402

UCIS_NS = "ucis"


@pytest.fixture(scope="module")
def corpus():
    return build_feature_complete()


@pytest.fixture(scope="module")
def dataset(corpus, tmp_path_factory):
    path = tmp_path_factory.mktemp("propvocab") / "dataset"
    ParquetWriter(path).write(corpus, run_id="r0")
    return path


@pytest.fixture(scope="module")
def round_tripped(dataset):
    return ParquetUCIS(dataset)


def _walk(db):
    """(hierarchical path, scope) for every scope, in iteration order."""
    def visit(scope, prefix):
        path = prefix + "/" + str(scope.getScopeName())
        yield path, scope
        for child in scope.scopes(-1):
            yield from visit(child, path)

    for root in db.scopes(-1):
        yield from visit(root, "")


def _typed_properties(scope):
    """Every UCIS typed property *this* scope can answer, by enum name."""
    out = {}
    for enum, getter in ((IntProperty, "getIntProperty"),
                         (StrProperty, "getStringProperty"),
                         (RealProperty, "getRealProperty")):
        for prop in enum:
            try:
                value = getattr(scope, getter)(-1, prop)
            except Exception:
                continue
            if value is not None:
                out[prop.name] = value
    return out


def _scopes_by_path(db):
    return {path: scope for path, scope in _walk(db)}


# -- the vocabulary as a whole ------------------------------------------


@ucis_feature(
    "PI.4", "PI.5", "PI.6", "PI.8", "PI.11", "PI.12", "PI.13", "PI.22",
    "PI.25", "PI.26", "PI.27", "PI.29", "PI.30", "PI.32", "PI.33",
    "PS.2", "PS.3", "PS.5", "PS.11", "PS.12", "PS.15", "PS.24", "PS.25",
    "PP.1", "PP.5",
    level="L1",
)
def test_every_scope_property_the_source_answers_survives(corpus, round_tripped):
    """No typed property the source could state is lost.

    Per-property rather than per-snapshot: the feature-complete round trip
    compares whole scopes, so a property that vanished would be reported as
    "this scope differs" and the *name* of the lost fact -- which is what
    identifies the defect -- would not appear.
    """
    target = _scopes_by_path(round_tripped)
    lost = []
    for path, scope in _walk(corpus):
        assert path in target, f"scope {path} did not survive"
        before, after = _typed_properties(scope), _typed_properties(target[path])
        for name, value in before.items():
            if name not in after:
                lost.append(f"{path}: {name} (was {value!r})")
            elif after[name] != value:
                lost.append(f"{path}: {name} {value!r} -> {after[name]!r}")

    assert not lost, "typed properties lost or altered:\n  " + "\n  ".join(lost)


@ucis_feature("PP.1", "PP.5", "DB.2", level="L1", surface="write")
def test_prop_id_is_the_enum_name_without_its_prefix(dataset):
    """The documented key spelling, asserted against the stored bytes.

    `UCIS_INT_CVG_ATLEAST` is stored as `CVG_ATLEAST`. Reading this through our
    own backend would prove nothing -- it would agree with whatever the writer
    chose. A third-party reader has only the document, which says the key is the
    enum constant minus its type prefix.
    """
    known = {p.name for enum in (IntProperty, StrProperty, RealProperty)
             for p in enum}
    table = pq.read_table(dataset / "properties")
    ucis_keys = {prop_id for ns, prop_id in
                 zip(table["namespace"].to_pylist(), table["prop_id"].to_pylist())
                 if ns == UCIS_NS}

    assert ucis_keys, "the corpus is meant to exercise the standard namespace"
    assert not [k for k in ucis_keys if k.startswith("UCIS_")], (
        "prop_id carries the bare enum name; the UCIS_INT_/UCIS_STR_ prefix is "
        "the type, and the type is already in prop_type"
    )
    unknown = sorted(ucis_keys - known)
    assert not unknown, (
        f"{unknown} are in the `ucis` namespace but are not UCIS property "
        f"constants. A key outside the standard vocabulary belongs in an "
        f"extension namespace, where a reader knows it may not understand it."
    )


@ucis_feature("PI.4", "PI.5", "PI.6", "PS.2", "PS.5", "DB.6", "X.6",
              level="L1", surface="write")
def test_a_promoted_property_is_a_column_instead_of_a_row(dataset, round_tripped):
    """Never both -- two places for one fact is two places to disagree.

    `SCOPE_SOURCE_TYPE` is the case that motivated this: `scopes.source`
    existed, nothing read it, and the property was *also* written as an EAV row
    on every scope.
    """
    table = pq.read_table(dataset / "properties")
    ucis_keys = {prop_id for ns, prop_id in
                 zip(table["namespace"].to_pylist(), table["prop_id"].to_pylist())
                 if ns == UCIS_NS}

    promoted = {IntProperty.SCOPE_WEIGHT.name, IntProperty.SCOPE_GOAL.name,
                IntProperty.SCOPE_SOURCE_TYPE.name, StrProperty.SCOPE_NAME.name,
                StrProperty.UNIQUE_ID.name}
    assert not (ucis_keys & promoted), (
        f"{sorted(ucis_keys & promoted)} are promoted to columns and must not "
        f"also appear as rows"
    )

    # ... and they are still answerable, which is the half a "no duplicate rows"
    # rule can quietly break.
    du = _scopes_by_path(round_tripped)["/work.dut"]
    assert du.getIntProperty(-1, IntProperty.SCOPE_SOURCE_TYPE) is not None
    assert du.getIntProperty(-1, IntProperty.SCOPE_WEIGHT) == 1
    assert du.getStringProperty(-1, StrProperty.SCOPE_NAME) == "work.dut"


@ucis_feature("SL.3", "SL.11", "PI.6", level="L1")
def test_the_source_language_of_a_scope_round_trips(corpus, round_tripped):
    """`ucisSourceT` is numbered consecutively from `UCIS_VHDL` = 0, so a
    mapping that dropped or defaulted the value would read back as VHDL rather
    than fail -- the failure mode that made this worth a test of its own."""
    before = {path: _typed_properties(s).get(IntProperty.SCOPE_SOURCE_TYPE.name)
              for path, s in _walk(corpus)}
    after = {path: _typed_properties(s).get(IntProperty.SCOPE_SOURCE_TYPE.name)
             for path, s in _walk(round_tripped)}

    assert before == after
    distinct = {v for v in before.values() if v is not None}
    assert len(distinct) >= 2, (
        f"the corpus exercises only {distinct}; a single value cannot "
        f"distinguish a carried source type from a defaulted one"
    )


@ucis_feature("PI.17", "PI.18", "PI.19", "PP.9", level="L1")
def test_per_bin_properties_are_addressed_by_cover_index(round_tripped):
    """`coverindex >= 0` selects the bin; `-1` selects the scope. One call,
    two objects -- and they resolve to different tables in the mapping."""
    cp = _scopes_by_path(round_tripped)["/top/addr_cg/addr"]
    bins = list(cp.coverItems(-1))
    assert bins, "the coverpoint is meant to have bins"

    for index in range(len(bins)):
        goal = cp.getIntProperty(index, IntProperty.COVER_GOAL)
        weight = cp.getIntProperty(index, IntProperty.COVER_WEIGHT)
        limit = cp.getIntProperty(index, IntProperty.COVER_LIMIT)
        assert (goal, weight, limit) == (
            bins[index].getCoverData().goal,
            bins[index].getCoverData().weight,
            bins[index].getCoverData().limit,
        )

    with pytest.raises(UnimplError):
        cp.getIntProperty(len(bins), IntProperty.COVER_GOAL)


@ucis_feature("PI.20", "PS.13", "PS.14", "PS.16", "PS.17", "PS.19", "PS.21",
              "PS.29", "PS.30", "PS.31", "PR.2", level="L1")
def test_history_node_properties_survive(corpus, round_tripped):
    """The test-run half of the vocabulary, which lands in `history_nodes`
    columns rather than in the EAV table."""
    fields = ("getLogicalName", "getPhysicalName", "getTestStatus", "getSeed",
              "getCmd", "getUserName", "getDate", "getToolCategory",
              "getTimeUnit", "getSimTime")

    def facts(db):
        out = []
        for node in db.historyNodes(HistoryNodeKind.ALL):
            row = {}
            for name in fields:
                try:
                    row[name] = getattr(node, name)()
                except Exception:
                    row[name] = None
            out.append(row)
        return out

    before, after = facts(corpus), facts(round_tripped)
    assert len(before) == len(after)
    for src, dst in zip(before, after):
        for name, value in src.items():
            if value is not None:
                assert dst[name] == value, f"history node lost {name}"
