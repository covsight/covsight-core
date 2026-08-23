"""Test 1 -- schema and type fidelity.

The neutrality rule is only real if something enforces it, so this asserts the
declared types against an allowlist: an engine-specific type added for
performance fails the build rather than quietly making the format
non-portable.
"""

import pyarrow as pa
import pytest

from covsight.core.conformance import ucis_feature
from covsight.core.parquet import schema as sch


def test_every_table_has_a_schema():
    assert set(sch.TABLES) == set(sch.DEFINITION_TABLES) | \
        set(sch.MEASUREMENT_TABLES)
    for name in sch.TABLES:
        assert isinstance(sch.table_schema(name), pa.Schema)


@pytest.mark.parametrize("name", sch.TABLES)
def test_only_portable_types(name):
    """No ClickHouse/Snowflake/BigQuery-only types may appear."""
    for field in sch.table_schema(name):
        assert field.type in sch.PORTABLE_TYPES, (
            "%s.%s uses non-portable type %s" % (name, field.name, field.type))


@pytest.mark.parametrize("name", sch.TABLES)
@ucis_feature("V3.2", "V3.3", level="L0")
def test_schema_version_in_metadata(name):
    schema = sch.with_metadata(sch.table_schema(name), name)
    assert schema.metadata[b"covsight.schema_version"] == \
        sch.SCHEMA_VERSION.encode()
    assert schema.metadata[b"covsight.table"] == name.encode()


@pytest.mark.parametrize("name", sch.MEASUREMENT_TABLES)
def test_measurement_tables_take_run_id(name):
    schema = sch.table_schema(name, with_run_id=True)
    assert sch.RUN_PARTITION_COL in schema.names
    assert not schema.field(sch.RUN_PARTITION_COL).nullable


@pytest.mark.parametrize("name", sch.DEFINITION_TABLES)
def test_definition_tables_reject_run_id(name):
    """A definition table carrying a run id would not be run-independent."""
    with pytest.raises(ValueError):
        sch.table_schema(name, with_run_id=True)


@ucis_feature("S4.17", level="L0")
def test_scope_type_is_64_bit():
    """ScopeTypeT exceeds 32 bits, so a 32-bit column would truncate it."""
    from covsight.core.api import ScopeTypeT
    assert max(int(m) for m in ScopeTypeT) > 2 ** 31
    assert sch.table_schema("scopes").field("scope_type").type == pa.int64()


def test_empty_table_matches_schema():
    for name in sch.TABLES:
        table = sch.empty_table(name)
        assert table.num_rows == 0
        assert table.schema.names == sch.table_schema(name).names


@ucis_feature("BT.19", "X.5", level="L0")
def test_merge_is_not_uniformly_sum():
    """A peak-active count must not be summed across runs."""
    from covsight.core.api import CoverTypeT
    assert sch.merge_op(CoverTypeT.PEAKACTIVEBIN) == sch.MERGE_MAX
    for additive in (CoverTypeT.CVGBIN, CoverTypeT.STMTBIN,
                     CoverTypeT.TOGGLEBIN, CoverTypeT.BRANCHBIN,
                     CoverTypeT.FSMBIN):
        assert sch.merge_op(additive) == sch.MERGE_SUM


def test_formal_status_ranks_by_informativeness():
    from covsight.core.api.enums import FormalStatusT
    ranks = {s: sch.formal_status_rank(s) for s in FormalStatusT}
    assert ranks[FormalStatusT.FAILURE] > ranks[FormalStatusT.PROOF]
    assert ranks[FormalStatusT.PROOF] > ranks[FormalStatusT.INCONCLUSIVE]
    assert ranks[FormalStatusT.NONE] == min(ranks.values())


def test_formal_status_merge_matches_the_shared_implementation():
    """Both backends must answer the same question the same way."""
    from covsight.core.api.enums import (
        FormalStatusT, merge_formal_statuses as shared,
    )
    cases = [
        [FormalStatusT.PROOF, FormalStatusT.PROOF],
        [FormalStatusT.PROOF, FormalStatusT.FAILURE],
        [FormalStatusT.NONE, FormalStatusT.INCONCLUSIVE],
        [FormalStatusT.INCONCLUSIVE, FormalStatusT.PROOF],
        [],
    ]
    for case in cases:
        assert sch.merge_formal_statuses(case) == shared(case)
    # Contradictory definitive claims are a conflict, not a winner.
    assert sch.merge_formal_statuses(
        [FormalStatusT.PROOF, FormalStatusT.FAILURE]) == \
        int(FormalStatusT.CONFLICT)


@ucis_feature("DB.6", "X.6", level="L0")
def test_promoted_property_order_is_stable():
    """Bit positions are part of the format: append only, never reorder."""
    from covsight.core.api import IntProperty
    promoted = sch.promoted_properties(sch.ObjectKind.SCOPE)
    assert [p.name for p, _t, _c in promoted][:3] == [
        IntProperty.SCOPE_WEIGHT.name, IntProperty.SCOPE_GOAL.name,
        IntProperty.COVER_GOAL.name]
    assert sch.promoted_properties(sch.ObjectKind.COVERITEM) == ()
