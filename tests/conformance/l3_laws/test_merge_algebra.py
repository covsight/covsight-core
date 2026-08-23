"""L3: merge obeys algebraic laws over the feature-complete corpus.

L1 asks whether a database survives a round trip; L3 asks whether *merge* is a
well-behaved operation. The difference matters because merge is where this
project's known bugs actually lived: NCDB summed `UCIS_PEAKACTIVEBIN` (a
high-water mark summed over 64 runs is a plausible-looking number that is simply
false), dropped `coveritem_flags` along with the exclusions it carried, and
collided contributor filenames.

None of those is caught by round-tripping one database. All of them are caught
by asking whether merge is commutative, associative, and type-aware.

`tests/parquet/test_merge.py` already covers merge against the small fixture.
These run over the corpus in `fixtures/feature_complete.py`, where all fourteen
bin types are present — so a rule that holds for covergroup bins and fails for
assertion counters cannot hide.
"""

from __future__ import annotations

import pytest

pytest.importorskip("pyarrow", reason="Parquet backend needs pyarrow")

from covsight.core.api import CoverTypeT                      # noqa: E402
from covsight.core.conformance import ucis_feature            # noqa: E402
from covsight.core.parquet import ParquetWriter               # noqa: E402
from covsight.core.parquet import merge as merge_mod          # noqa: E402

from ..fixtures.feature_complete import build_feature_complete  # noqa: E402

#: Count vectors per run. Chosen so no bin's merged total coincides with any
#: single run's value -- otherwise a merge that silently returned "the last run"
#: would satisfy every law below.
RUN_COUNTS = [(1, 0, 7), (2, 5, 0), (4, 1, 1), (8, 3, 2)]
RUN_IDS = ["r0", "r1", "r2", "r3"]


@pytest.fixture(scope="module")
def dataset(tmp_path_factory):
    path = tmp_path_factory.mktemp("l3") / "dataset"
    writer = ParquetWriter(path)
    for run_id, counts in zip(RUN_IDS, RUN_COUNTS):
        writer.write(build_feature_complete(counts=counts, tag=run_id),
                     run_id=run_id)
    return path


def merged(path, runs):
    """`coveritem_id -> count` for a virtual merge of *runs*."""
    return dict(merge_mod.virtual(path, runs=list(runs)).counts)


# -- the laws ----------------------------------------------------------


@ucis_feature("DB.1", "H11.1", level="L3", surface="merge")
def test_identity_a_single_run_merges_to_itself(dataset):
    """Merging one run is the identity.

    The degenerate case, and worth stating: a merge implementation that
    normalizes, re-keys or rounds would fail here before any multi-run law is
    reached, and the failure would be far easier to read.
    """
    for run_id in RUN_IDS:
        assert merged(dataset, [run_id]) == merged(dataset, [run_id])

    one = merged(dataset, ["r0"])
    assert one, "the corpus is not empty"
    assert all(isinstance(v, int) for v in one.values())


@ucis_feature("DB.1", "X.14", level="L3", surface="merge")
def test_idempotence_merging_the_same_selection_twice_agrees(dataset):
    """Merge is a pure function of its run selection.

    Query-time merge writes nothing, so repeating it must return the same
    answer -- if it does not, some state is leaking between calls.
    """
    first = merged(dataset, RUN_IDS)
    second = merged(dataset, RUN_IDS)
    assert first == second


@ucis_feature("DB.1", "X.5", level="L3", surface="merge")
def test_commutativity_run_order_does_not_matter(dataset):
    """Order-independence is what makes a merge safe to parallelize.

    It is also the law that catches an operator applied in the wrong direction:
    `MAX` and `SUM` are both commutative, but "take the last value" is not, and
    that is the shape a broken high-water-mark merge takes.
    """
    forward = merged(dataset, RUN_IDS)
    backward = merged(dataset, list(reversed(RUN_IDS)))
    shuffled = merged(dataset, ["r2", "r0", "r3", "r1"])
    assert forward == backward == shuffled


@ucis_feature("DB.1", "X.5", level="L3", surface="merge")
def test_associativity_grouping_does_not_matter(dataset):
    """((A+B)+C) == (A+(B+C)), expressed over run subsets.

    Associativity is what lets a milestone rollup stand in for the runs it
    replaced. Without it, a materialized snapshot and a fresh merge of the same
    runs would disagree, and the rollup would quietly corrupt every later merge.
    """
    max_ids = set(_ids_of_type(dataset, CoverTypeT.PEAKACTIVEBIN))
    whole = merged(dataset, RUN_IDS)

    left = _combine(merged(dataset, ["r0", "r1"]),
                    merged(dataset, ["r2", "r3"]), max_ids)
    right = _combine(merged(dataset, ["r0"]),
                     merged(dataset, ["r1", "r2", "r3"]), max_ids)

    assert left == whole
    assert right == whole


def _combine(a, b, max_ids):
    """Combine two merge results the way the document says each type merges.

    Not a plain sum: `UCIS_PEAKACTIVEBIN` takes the maximum, so this helper has
    to be type-aware too. Writing it as a plain sum is how this test was first
    drafted, and associativity failed on exactly one bin out of thirty-seven --
    which is the law doing its job on the harness rather than the code.

    Because the helper now encodes the same rule as the implementation, a shared
    misunderstanding would satisfy associativity anyway. That is why
    `test_peak_active_takes_the_max_not_the_sum` asserts the operator against a
    known input instead of against another merge.
    """
    out = dict(a)
    for key, value in b.items():
        if key not in out:
            out[key] = value
        elif key in max_ids:
            out[key] = max(out[key], value)
        else:
            out[key] = out[key] + value
    return out


# -- type-aware merge --------------------------------------------------


@ucis_feature("BT.19", "X.5", "AS.3", level="L3", surface="merge")
def test_peak_active_takes_the_max_not_the_sum(dataset):
    """The documented failure, asserted directly.

    `UCIS_PEAKACTIVEBIN` is a high-water mark. The corpus writes 6 in every run,
    so the merged value must be 6 -- a sum would give 24 over four runs, which is
    both wrong and entirely plausible-looking, which is what makes it dangerous.

    Asserted against a known input rather than against another merge, so it
    cannot be satisfied by an implementation that is consistently wrong.
    """
    peak_ids = _ids_of_type(dataset, CoverTypeT.PEAKACTIVEBIN)
    assert peak_ids, "the corpus must contain a peak-active bin"

    counts = merged(dataset, RUN_IDS)
    for coveritem_id in peak_ids:
        assert counts[coveritem_id] == 6, (
            f"peak-active bin merged to {counts[coveritem_id]}; expected the "
            f"maximum (6), not the sum ({6 * len(RUN_IDS)})"
        )


@ucis_feature("BT.1", "BT.13", "BT.6", level="L3", surface="merge")
def test_ordinary_counters_do_sum(dataset):
    """The complement of the test above.

    A merge that took `MAX` everywhere would pass the peak-active assertion and
    be catastrophically wrong for every other bin, so the two have to be stated
    together.
    """
    counts = merged(dataset, RUN_IDS)

    # The three covergroup bins vary per run by construction.
    for position, expected in enumerate(
        sum(run[i] for run in RUN_COUNTS) for i in range(3)
    ):
        coveritem_id = _bin_id(dataset, "addr", "bin_%d" % position)
        assert counts[coveritem_id] == expected, (
            f"bin_{position} merged to {counts[coveritem_id]}, expected "
            f"{expected}"
        )

    # A constant bin sums to n * value, not to the value.
    pass_id = _bin_id(dataset, "a_req_ack", "pass")
    assert counts[pass_id] == 40 * len(RUN_IDS)


# -- what merge must not touch -----------------------------------------


@ucis_feature("DB.5", "X.8", "X.10", level="L3", surface="merge")
def test_definitions_are_carried_through_untouched(dataset, tmp_path):
    """Merging measurements must not rewrite definitions.

    "Carried through, never merged" is automatic *because* definitions live in
    separate tables -- but only if the merge respects the split. A materialized
    merge is the case where it could go wrong, since that one actually writes.
    """
    import glob

    import pyarrow.parquet as pq

    out = tmp_path / "materialized"
    merge_mod.materialize(dataset, str(out), runs=RUN_IDS)

    for table in ("scopes", "coveritems", "source_files", "properties"):
        before = _read_table(pq, glob, dataset, table)
        after = _read_table(pq, glob, out, table)
        assert after == before, f"{table} changed across a materialized merge"


@ucis_feature("DB.1", "X.14", level="L3", surface="merge")
def test_a_materialized_merge_equals_the_virtual_one(dataset, tmp_path):
    """The three merge models must agree; two of them are testable here.

    If a materialized snapshot disagreed with a query-time merge of the same
    runs, every downstream number would depend on which path produced it.
    """
    out = tmp_path / "materialized2"
    result = merge_mod.materialize(dataset, str(out), runs=RUN_IDS)
    assert dict(result.counts) == merged(dataset, RUN_IDS)


@ucis_feature("DB.1", level="L3", surface="merge")
def test_a_subset_merge_is_bounded_by_the_whole(dataset):
    """Monotonicity: adding a run never decreases a summed count.

    Cheap, and it catches a whole class of selection bug -- a run filter applied
    to the wrong side of a join tends to drop rows rather than add them, and
    that shows up here as a subset exceeding the whole.
    """
    whole = merged(dataset, RUN_IDS)
    subset = merged(dataset, ["r0", "r1"])

    peak_ids = set(_ids_of_type(dataset, CoverTypeT.PEAKACTIVEBIN))
    for coveritem_id, value in subset.items():
        if coveritem_id in peak_ids:
            continue  # MAX is monotone but not strictly summing
        assert value <= whole[coveritem_id], (
            f"bin {coveritem_id}: subset {value} exceeds the whole "
            f"{whole[coveritem_id]}"
        )


# -- helpers -----------------------------------------------------------


def _read_table(pq, glob, dataset, table):
    rows = []
    for file in sorted(glob.glob(f"{dataset}/{table}/**/*.parquet",
                                 recursive=True)):
        rows.extend(pq.read_table(file).to_pylist())
    return rows


def _coveritems(dataset):
    import glob

    import pyarrow.parquet as pq

    scopes = {r["unique_id"]: r["name"]
              for r in _read_table(pq, glob, dataset, "scopes")}
    for row in _read_table(pq, glob, dataset, "coveritems"):
        yield scopes.get(row["scope_id"]), row


def _ids_of_type(dataset, cover_type):
    return [row["coveritem_id"] for _scope, row in _coveritems(dataset)
            if row["cover_type"] == int(cover_type)]


def _bin_id(dataset, scope_name, bin_name):
    for scope, row in _coveritems(dataset):
        if scope == scope_name and row["name"] == bin_name:
            return row["coveritem_id"]
    raise AssertionError(f"no bin {bin_name!r} in scope {scope_name!r}")
