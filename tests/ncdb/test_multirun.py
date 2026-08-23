"""Phase B — multi-run NCDB: one schema, many count arrays.

The point of the format: a single-run `.cdb` repeats the whole schema, so N
runs as N files pay for it N times. These tests pin the two things that makes
true — the schema is stored once, and each run stays individually readable —
plus the guards that stop a run being double-counted or silently mixed with a
different design.

See ``docs/ncdb-multirun-scoping.md``.
"""

import os
import zipfile

import pytest

from covsight.core.api import CoverTypeT, ScopeTypeT
from covsight.core.ncdb.multirun import (
    BASELINE_RUN_ID, MEMBER_RUNS, RunTable, RunTableError, append_run,
    append_runs, drop_runs, is_multirun, read_run_table, run_ids,
    run_member_name,
)
from covsight.core.ncdb.ncdb_reader import NcdbReader
from covsight.core.ncdb.ncdb_writer import NcdbWriter

from .test_merge_fidelity import all_bins, build_db


@pytest.fixture
def single(tmp_path):
    """A one-run archive, as written today."""
    path = os.path.join(str(tmp_path), "cov.cdb")
    NcdbWriter().write(build_db(counts=(1, 0, 7), tag="a"), path)
    return path


@pytest.fixture
def multi(tmp_path):
    """A three-run archive, and the per-run covergroup counts."""
    path = os.path.join(str(tmp_path), "cov.cdb")
    NcdbWriter().write(build_db(counts=(1, 0, 7), tag="a"), path)
    append_run(path, build_db(counts=(2, 5, 0), tag="b"), run_id="r1")
    append_run(path, build_db(counts=(4, 1, 1), tag="c"), run_id="r2")
    return path, [(1, 0, 7), (2, 5, 0), (4, 1, 1)]


def cvg_counts(db):
    return [b.getCoverData().data for b in all_bins(db)
            if b.getName().startswith("bin_")]


# --------------------------------------------------------------------------
# Backward compatibility
# --------------------------------------------------------------------------

def test_existing_archives_are_unchanged(single):
    """A single-run `.cdb` has no run table and reads exactly as before."""
    assert MEMBER_RUNS not in zipfile.ZipFile(single).namelist()
    assert not is_multirun(single)
    assert cvg_counts(NcdbReader().read(single)) == [1, 0, 7]


def test_a_single_run_archive_synthesizes_a_run_table(single):
    """Callers never have to branch on "is this multi-run"."""
    table = read_run_table(single)
    assert table.run_ids == (BASELINE_RUN_ID,)


def test_runs_argument_is_harmless_on_a_single_run_archive(single):
    assert cvg_counts(NcdbReader().read(single, runs=None)) == [1, 0, 7]
    assert cvg_counts(
        NcdbReader().read(single, runs=BASELINE_RUN_ID)) == [1, 0, 7]


# --------------------------------------------------------------------------
# The schema is stored once
# --------------------------------------------------------------------------

def test_appending_a_run_adds_one_count_array_not_a_database(multi):
    path, _ = multi
    members = zipfile.ZipFile(path).namelist()
    assert sorted(n for n in members if n.startswith("counts/")) == [
        run_member_name(0), run_member_name(1)]
    # Exactly one of each schema member, however many runs there are.
    for schema_member in ("scope_tree.bin", "strings.bin", "sources.json"):
        assert members.count(schema_member) == 1


def test_multi_run_archive_is_smaller_than_separate_files(tmp_path):
    """The premise of the whole format, asserted rather than assumed."""
    separate = []
    for i in range(4):
        p = os.path.join(str(tmp_path), "sep%d.cdb" % i)
        NcdbWriter().write(build_db(counts=(i, i, i), tag=str(i)), p)
        separate.append(p)
    separate_total = sum(os.path.getsize(p) for p in separate)

    combined = os.path.join(str(tmp_path), "combined.cdb")
    NcdbWriter().write(build_db(counts=(0, 0, 0), tag="0"), combined)
    for i in range(1, 4):
        append_run(combined, build_db(counts=(i, i, i), tag=str(i)),
                   run_id="r%d" % i)

    assert os.path.getsize(combined) < separate_total


# --------------------------------------------------------------------------
# Run selection
# --------------------------------------------------------------------------

def test_all_runs_merge_by_default(multi):
    path, specs = multi
    expected = [sum(s[i] for s in specs) for i in range(3)]
    assert cvg_counts(NcdbReader().read(path)) == expected


def test_a_single_run_is_individually_readable(multi):
    """Per-run provenance: this is what N-files-merged-away cannot do."""
    path, specs = multi
    assert cvg_counts(NcdbReader().read(path, runs="r1")) == list(specs[1])
    assert cvg_counts(NcdbReader().read(path, runs="r2")) == list(specs[2])
    assert cvg_counts(
        NcdbReader().read(path, runs=BASELINE_RUN_ID)) == list(specs[0])


def test_a_subset_of_runs_merges(multi):
    path, specs = multi
    expected = [specs[1][i] + specs[2][i] for i in range(3)]
    assert cvg_counts(NcdbReader().read(path, runs=["r1", "r2"])) == expected


def test_unknown_run_is_an_error(multi):
    path, _ = multi
    with pytest.raises(RunTableError):
        NcdbReader().read(path, runs="nope")


def test_run_ids_are_reported_in_order(multi):
    path, _ = multi
    assert run_ids(path) == (BASELINE_RUN_ID, "r1", "r2")
    assert is_multirun(path)


# --------------------------------------------------------------------------
# Guards
# --------------------------------------------------------------------------

def test_reappending_a_run_is_refused(multi):
    """Idempotency: adding the same run twice must not double its counts."""
    path, specs = multi
    with pytest.raises(FileExistsError):
        append_run(path, build_db(counts=(9, 9, 9), tag="b"), run_id="r1")
    expected = [sum(s[i] for s in specs) for i in range(3)]
    assert cvg_counts(NcdbReader().read(path)) == expected


def test_replacing_a_run_is_explicit_and_keeps_its_place(multi):
    path, specs = multi
    append_run(path, build_db(counts=(0, 0, 0), tag="b"), run_id="r1",
               replace=True)
    assert run_ids(path) == (BASELINE_RUN_ID, "r1", "r2"), \
        "a replaced run must keep its position in the table"
    expected = [specs[0][i] + specs[2][i] for i in range(3)]
    assert cvg_counts(NcdbReader().read(path)) == expected


def test_a_different_design_cannot_be_appended(single):
    """Runs of one archive must share the schema, or the counts misalign."""
    with pytest.raises(ValueError, match="different design"):
        append_run(single, build_db(counts=(1, 2, 3, 4), num_bins=8),
                   run_id="other")


def test_auto_named_runs_do_not_collide(single):
    ids = [append_run(single, build_db(counts=(i, i, i), tag=str(i)))
           for i in range(3)]
    assert ids == ["run-0000", "run-0001", "run-0002"]
    assert run_ids(single) == (BASELINE_RUN_ID, "run-0000", "run-0001",
                               "run-0002")


def test_member_names_do_not_derive_from_run_ids(tmp_path):
    """Run ids come from test names and CI job ids.

    Deriving a member name from one invites path traversal, and -- worse --
    collisions: sanitizing ``a.b`` and ``a_b`` to the same member would let one
    run silently overwrite the other. Member names are indexed instead.
    """
    assert run_member_name(0) == "counts/run-00000.bin"
    assert run_member_name(7) == "counts/run-00007.bin"

    path = os.path.join(str(tmp_path), "hostile.cdb")
    NcdbWriter().write(build_db(counts=(1, 1, 1), tag="a"), path)
    append_run(path, build_db(counts=(2, 2, 2), tag="b"), run_id="a.b")
    append_run(path, build_db(counts=(4, 4, 4), tag="c"), run_id="a_b")
    append_run(path, build_db(counts=(8, 8, 8), tag="d"),
               run_id="../../etc/passwd")

    # Every run kept its own array; nothing collided or escaped counts/.
    assert run_ids(path) == (BASELINE_RUN_ID, "a.b", "a_b", "../../etc/passwd")
    assert cvg_counts(NcdbReader().read(path)) == [15, 15, 15]
    for name in zipfile.ZipFile(path).namelist():
        assert ".." not in name


def test_replacing_a_run_reuses_its_member(multi):
    """A re-uploaded run must not leave an orphan array behind."""
    path, _ = multi
    before = [n for n in zipfile.ZipFile(path).namelist()
              if n.startswith("counts/")]
    append_run(path, build_db(counts=(0, 0, 0), tag="b"), run_id="r1",
               replace=True)
    after = [n for n in zipfile.ZipFile(path).namelist()
             if n.startswith("counts/")]
    assert sorted(after) == sorted(before)


# --------------------------------------------------------------------------
# Dropping runs
# --------------------------------------------------------------------------

def test_dropping_a_run_removes_its_counts(multi):
    path, specs = multi
    member = read_run_table(path).get("r1")["member"]
    assert drop_runs(path, ["r1"]) == (BASELINE_RUN_ID, "r2")
    assert member not in zipfile.ZipFile(path).namelist(), \
        "the dropped run's array is still taking up space"
    expected = [specs[0][i] + specs[2][i] for i in range(3)]
    assert cvg_counts(NcdbReader().read(path)) == expected


def test_dropping_every_run_is_refused(multi):
    path, _ = multi
    with pytest.raises(RunTableError):
        drop_runs(path, [BASELINE_RUN_ID, "r1", "r2"])


# --------------------------------------------------------------------------
# Batch append
# --------------------------------------------------------------------------

def test_append_runs_writes_several_in_one_rewrite(single):
    """One archive rewrite instead of N -- appending is O(archive)."""
    written = append_runs(single, [
        ("r1", build_db(counts=(1, 1, 1), tag="b"), None, {}),
        ("r2", build_db(counts=(2, 2, 2), tag="c"), None, {}),
    ])
    assert written == ["r1", "r2"]
    assert cvg_counts(NcdbReader().read(single)) == [4, 3, 10]


def test_run_table_round_trips():
    table = RunTable()
    table.upsert("a", "counts/a.bin", nonzero=3)
    table.upsert("b", "counts/b.bin", nonzero=0)
    restored = RunTable.deserialize(table.serialize())
    assert restored.run_ids == ("a", "b")
    assert restored.get("a")["nonzero"] == 3
    assert restored.remove("a")["run_id"] == "a"
    assert restored.run_ids == ("b",)
