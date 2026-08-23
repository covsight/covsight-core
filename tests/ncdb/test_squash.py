"""Phase C — squash: fold runs into the baseline and reclaim their arrays.

The operational shape: sims append complete per-run count arrays as they
finish, and a periodic job folds the old ones into ``counts.bin``. Storage then
tracks retained runs, not runs ever uploaded.

The invariant these tests defend is that a squash never changes *what the
coverage is* — only how much per-run detail is still available.

See ``docs/ncdb-multirun-scoping.md``.
"""

import os
import zipfile

import pytest

from covsight.core.ncdb.multirun import (
    BASELINE_RUN_ID, RunTableError, append_run, read_run_table, run_ids,
)
from covsight.core.ncdb.ncdb_reader import NcdbReader
from covsight.core.ncdb.ncdb_writer import NcdbWriter
from covsight.core.ncdb.squash import (
    POLICY_ALL, POLICY_PASS_ONLY, read_squash_log, squash, squash_plan,
)

from .test_merge_fidelity import all_bins, build_db


def cvg_counts(db):
    return [b.getCoverData().data for b in all_bins(db)
            if b.getName().startswith("bin_")]


@pytest.fixture
def multi(tmp_path):
    path = os.path.join(str(tmp_path), "cov.cdb")
    NcdbWriter().write(build_db(counts=(1, 0, 7), tag="a"), path)
    append_run(path, build_db(counts=(2, 5, 0), tag="b"), run_id="r1")
    append_run(path, build_db(counts=(4, 1, 1), tag="c"), run_id="r2")
    return path, [(1, 0, 7), (2, 5, 0), (4, 1, 1)]


# --------------------------------------------------------------------------
# Coverage is preserved exactly
# --------------------------------------------------------------------------

def test_squash_preserves_total_coverage(multi):
    path, specs = multi
    before = cvg_counts(NcdbReader().read(path))

    result = squash(path)

    assert result.num_squashed == 2
    assert run_ids(path) == (BASELINE_RUN_ID,)
    assert cvg_counts(NcdbReader().read(path)) == before


def test_squash_reclaims_the_run_arrays(multi):
    path, _ = multi
    members_before = [n for n in zipfile.ZipFile(path).namelist()
                      if n.startswith("counts/")]
    assert members_before

    squash(path)

    members_after = [n for n in zipfile.ZipFile(path).namelist()
                     if n.startswith("counts/")]
    assert members_after == [], "squashed runs still occupy space"


def test_squash_shrinks_the_archive(multi):
    path, _ = multi
    before = os.path.getsize(path)
    squash(path)
    assert os.path.getsize(path) < before


def test_squash_is_repeatable_over_rounds(tmp_path):
    """Append, squash, append, squash — totals must still be right."""
    path = os.path.join(str(tmp_path), "rolling.cdb")
    NcdbWriter().write(build_db(counts=(1, 1, 1), tag="0"), path)
    for i in range(1, 4):
        append_run(path, build_db(counts=(1, 1, 1), tag=str(i)),
                   run_id="r%d" % i)
    squash(path)
    assert cvg_counts(NcdbReader().read(path)) == [4, 4, 4]

    for i in range(4, 7):
        append_run(path, build_db(counts=(1, 1, 1), tag=str(i)),
                   run_id="r%d" % i)
    squash(path)
    assert cvg_counts(NcdbReader().read(path)) == [7, 7, 7]
    assert run_ids(path) == (BASELINE_RUN_ID,)


def test_squash_can_keep_recent_runs(multi):
    """A retention policy: fold the old, keep the recent individually."""
    path, specs = multi
    total = cvg_counts(NcdbReader().read(path))

    squash(path, keep_recent=1)

    assert run_ids(path) == (BASELINE_RUN_ID, "r2")
    assert cvg_counts(NcdbReader().read(path)) == total
    # The retained run is still separable.
    assert cvg_counts(NcdbReader().read(path, runs="r2")) == list(specs[2])


def test_squash_keep_runs_loses_nothing(multi):
    """`keep_runs=True` folds in without dropping — saves nothing, loses
    nothing, and is how you check a squash before committing to it."""
    path, specs = multi
    before = cvg_counts(NcdbReader().read(path))

    squash(path, keep_runs=True)

    assert run_ids(path) == (BASELINE_RUN_ID, "r1", "r2")
    assert cvg_counts(NcdbReader().read(path, runs="r1")) == list(specs[1])
    # The baseline now already contains them, so a full read must not add
    # them a second time... which is exactly why keep_runs is not the default.
    assert cvg_counts(NcdbReader().read(path, runs=BASELINE_RUN_ID)) == before


# --------------------------------------------------------------------------
# Planning
# --------------------------------------------------------------------------

def test_squash_plan_splits_by_retention(multi):
    path, _ = multi
    assert squash_plan(path, keep_recent=1) == (("r1",), ("r2",))
    assert squash_plan(path, keep_recent=0) == (("r1", "r2"), ())
    assert squash_plan(path, keep_recent=99) == ((), ("r1", "r2"))


def test_squash_plan_never_squashes_the_baseline(multi):
    """The baseline is what runs are folded *into*."""
    path, _ = multi
    to_squash, _keep = squash_plan(path)
    assert BASELINE_RUN_ID not in to_squash


def test_squashing_nothing_is_an_error(tmp_path):
    path = os.path.join(str(tmp_path), "single.cdb")
    NcdbWriter().write(build_db(counts=(1, 1, 1)), path)
    with pytest.raises(RunTableError):
        squash(path)


def test_unknown_run_is_an_error(multi):
    path, _ = multi
    with pytest.raises(RunTableError):
        squash(path, runs=["nope"])


# --------------------------------------------------------------------------
# Policy
# --------------------------------------------------------------------------

def test_pass_only_policy_excludes_failing_runs(tmp_path):
    """Coverage from a failing test must not prop up a closure number."""
    path = os.path.join(str(tmp_path), "policy.cdb")
    NcdbWriter().write(build_db(counts=(0, 0, 0), tag="base"), path)
    append_run(path, build_db(counts=(1, 1, 1), tag="ok"), run_id="good",
               status="pass")
    append_run(path, build_db(counts=(9, 9, 9), tag="bad"), run_id="failed",
               status="fail")

    result = squash(path, policy=POLICY_PASS_ONLY)

    assert result.squashed == ("good",)
    assert "failed" in run_ids(path), "a failing run is skipped, not deleted"
    assert cvg_counts(NcdbReader().read(path, runs=BASELINE_RUN_ID)) == [1, 1, 1]


def test_policy_all_folds_in_everything(tmp_path):
    path = os.path.join(str(tmp_path), "policy-all.cdb")
    NcdbWriter().write(build_db(counts=(0, 0, 0), tag="base"), path)
    append_run(path, build_db(counts=(1, 1, 1), tag="ok"), run_id="good",
               status="pass")
    append_run(path, build_db(counts=(9, 9, 9), tag="bad"), run_id="failed",
               status="fail")

    result = squash(path, policy=POLICY_ALL)
    assert sorted(result.squashed) == ["failed", "good"]
    assert cvg_counts(
        NcdbReader().read(path, runs=BASELINE_RUN_ID)) == [10, 10, 10]


def test_runs_without_a_status_are_admitted(multi):
    """Absence of evidence is not a failure — otherwise pass-only would be
    unusable on archives that never recorded a status."""
    path, _ = multi
    result = squash(path, policy=POLICY_PASS_ONLY)
    assert result.num_squashed == 2


# --------------------------------------------------------------------------
# Audit trail
# --------------------------------------------------------------------------

def test_squash_is_recorded_in_the_log(multi):
    path, _ = multi
    assert read_squash_log(path) == []

    squash(path, policy=POLICY_PASS_ONLY, timestamp=1700000000)

    entries = read_squash_log(path)
    assert len(entries) == 1
    assert entries[0].ts == 1700000000
    assert entries[0].policy == POLICY_PASS_ONLY
    assert entries[0].num_runs == 2


def test_the_log_is_append_only(tmp_path):
    """Provenance: every squash stays on the record."""
    path = os.path.join(str(tmp_path), "audit.cdb")
    NcdbWriter().write(build_db(counts=(1, 1, 1), tag="0"), path)
    for round_index in range(3):
        append_run(path, build_db(counts=(1, 1, 1), tag=str(round_index)),
                   run_id="r%d" % round_index)
        squash(path, timestamp=1700000000 + round_index)

    entries = read_squash_log(path)
    assert [e.ts for e in entries] == [1700000000, 1700000001, 1700000002]
    assert cvg_counts(NcdbReader().read(path)) == [4, 4, 4]
