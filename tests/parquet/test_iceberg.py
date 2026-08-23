"""Test 6 -- ephemeral Iceberg integration.

Tier 2 (PyIceberg + SQLite catalog + ``tmp_path`` warehouse) runs in the
default job: real Iceberg table semantics, no service to stand up, teardown is
deleting a directory.

Tier 3 (the Docker REST-catalog fixture) is gated behind the ``iceberg_rest``
marker and skipped unless ``COVSIGHT_ICEBERG_REST=1``, so default CI needs no
Docker.
"""

import os

import pytest

pytest.importorskip("pyiceberg", reason="Iceberg target needs pyiceberg")

from covsight.core.conformance import ucis_feature
from covsight.core.parquet import ParquetUCIS, ParquetWriter    # noqa: E402
from covsight.core.parquet import iceberg, schema as sch        # noqa: E402

from .conftest import build_db, walk_facts                      # noqa: E402


@pytest.fixture
def catalog(tmp_path):
    return iceberg.ephemeral_catalog(tmp_path / "warehouse")


def test_ephemeral_catalog_needs_no_service(catalog, tmp_path):
    iceberg.ensure_namespace(catalog, "covsight")
    assert (tmp_path / "warehouse" / "catalog.db").exists()


@ucis_feature("X.13", level="L2")
def test_round_trip_through_iceberg_is_lossless(catalog, tmp_path,
                                               single_run):
    """The same golden comparison as raw Parquet, through Iceberg tables."""
    _db, source, _run_id = single_run
    back = tmp_path / "from-iceberg.parquet"

    iceberg.to_iceberg(source, catalog)
    assert iceberg.to_dataset(catalog, back) == ("r0",)

    assert walk_facts(ParquetUCIS(back)) == walk_facts(ParquetUCIS(source))


def test_all_tables_are_created(catalog, single_run):
    _db, source, _run_id = single_run
    created = iceberg.to_iceberg(source, catalog)
    assert set(created) == set(sch.TABLES)


def test_multi_run_partitions_survive_iceberg(catalog, tmp_path, multi_run):
    """Run identity is data, so it survives whatever an engine does with it."""
    source, runs, specs = multi_run
    back = tmp_path / "from-iceberg.parquet"

    iceberg.to_iceberg(source, catalog)
    assert iceberg.to_dataset(catalog, back) == runs

    restored = ParquetUCIS(back)
    assert restored.run_ids == runs
    assert walk_facts(restored) == walk_facts(ParquetUCIS(source))

    # And per-run reads still isolate a single run after the round trip.
    for run_id, (counts, _tag) in zip(runs, specs):
        one = ParquetUCIS(back, runs=run_id)
        assert walk_facts(one) == walk_facts(ParquetUCIS(source, runs=run_id))


def test_merge_still_works_after_an_iceberg_round_trip(catalog, tmp_path,
                                                       multi_run):
    from covsight.core.parquet import merge
    source, _runs, _specs = multi_run
    back = tmp_path / "from-iceberg.parquet"
    iceberg.to_iceberg(source, catalog)
    iceberg.to_dataset(catalog, back)
    assert merge.virtual(back).counts == merge.virtual(source).counts


@ucis_feature("X.13", level="L2")
def test_int8_widening_is_normalized(catalog, tmp_path, single_run):
    """Iceberg has no int8, so the reader must conform types on the way back.

    Without this the round trip would be "equal except two column types",
    which is exactly the kind of engine detail the neutrality rules exist to
    keep out of the logical schema.
    """
    _db, source, _run_id = single_run
    back = tmp_path / "from-iceberg.parquet"
    iceberg.to_iceberg(source, catalog)
    iceberg.to_dataset(catalog, back)

    restored = ParquetUCIS(back)
    properties = restored.dataset.table("properties")
    assert properties.schema.field("object_kind").type == \
        sch.table_schema("properties").field("object_kind").type


def test_write_ucis_straight_into_iceberg(catalog, tmp_path):
    """The UCIS→columnar mapping has one implementation, not two."""
    back = tmp_path / "from-iceberg.parquet"
    db = build_db()
    assert iceberg.write_ucis(db, catalog, run_id="r0") == "r0"
    iceberg.to_dataset(catalog, back)

    reference = tmp_path / "reference.parquet"
    ParquetWriter(reference).write(build_db(), run_id="r0")
    assert walk_facts(ParquetUCIS(back)) == walk_facts(ParquetUCIS(reference))


@pytest.mark.iceberg_rest
@pytest.mark.skipif(os.environ.get("COVSIGHT_ICEBERG_REST") != "1",
                    reason="set COVSIGHT_ICEBERG_REST=1 and run the "
                           "apache/iceberg-rest-fixture container")
def test_round_trip_through_rest_catalog(tmp_path, single_run):
    """Tier 3: the REST-catalog path, opt-in and Docker-dependent."""
    _db, source, _run_id = single_run
    back = tmp_path / "from-rest.parquet"
    catalog = iceberg.rest_catalog(
        os.environ.get("COVSIGHT_ICEBERG_REST_URI",
                       "http://localhost:8181"))
    namespace = "covsight_rest_test"
    iceberg.to_iceberg(source, catalog, namespace=namespace)
    iceberg.to_dataset(catalog, back, namespace=namespace)
    assert walk_facts(ParquetUCIS(back)) == walk_facts(ParquetUCIS(source))


def test_compact_rolls_runs_up_inside_iceberg(catalog, tmp_path, multi_run):
    """Upload per-run, then roll old runs down -- coverage must be unchanged."""
    from covsight.core.parquet import merge
    source, runs, _specs = multi_run
    iceberg.to_iceberg(source, catalog)

    before = merge.virtual(source).counts

    iceberg.compact(catalog, runs=list(runs), into_run_id="rollup-1")

    back = tmp_path / "after-rollup.parquet"
    iceberg.to_dataset(catalog, back)

    assert ParquetUCIS(back).run_ids == ("rollup-1",)
    assert merge.virtual(back).counts == before


def test_compact_can_keep_recent_runs(catalog, tmp_path, multi_run):
    """A rolling policy: roll up the old, leave the recent individually there."""
    from covsight.core.parquet import merge
    source, runs, specs = multi_run
    iceberg.to_iceberg(source, catalog)
    before = merge.virtual(source).counts

    iceberg.compact(catalog, runs=list(runs[:2]), into_run_id="rollup-old")

    back = tmp_path / "partial.parquet"
    iceberg.to_dataset(catalog, back)
    restored = ParquetUCIS(back)
    assert sorted(restored.run_ids) == sorted(["rollup-old", runs[2]])
    assert merge.virtual(back).counts == before

    # The retained run is still separable after the rollup.
    assert ParquetUCIS(back, runs=runs[2]).selected_runs == (runs[2],)


def test_compact_refuses_to_target_a_source_run(catalog, multi_run):
    source, runs, _specs = multi_run
    iceberg.to_iceberg(source, catalog)
    with pytest.raises(ValueError):
        iceberg.compact(catalog, runs=list(runs), into_run_id=runs[0])
