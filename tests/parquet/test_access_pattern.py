"""Test 7 -- access-pattern regression: no query per object.

The UCIS API is row-at-a-time; a columnar store is set-at-a-time.  The failure
mode is not incorrectness but an adapter that quietly issues one query per
``NextScope`` and reads a thousand times slower than NCDB.  A bound on *read
count* catches that in a unit test, where a timing assertion would be flaky.

The bound is: reads depend on the dataset's **shape** -- tables touched, times
selected run partitions -- and never on how many objects the walk visits.
"""

import pytest

from covsight.core.api import CoverTypeT, ScopeTypeT
from covsight.core.mem import MemFactory
from covsight.core.parquet import ParquetDataset, ParquetUCIS, ParquetWriter
from covsight.core.parquet import schema as sch

from .conftest import build_db


def _wide_db(num_scopes, bins_per_scope=4):
    """A database whose object count is a parameter."""
    from covsight.core.api import CoverData, SourceT

    db = MemFactory.create()
    du = db.createScope("work.wide", None, 1, SourceT.SV,
                        ScopeTypeT.DU_MODULE, 0)
    inst = db.createInstance("top", None, 1, SourceT.SV, ScopeTypeT.INSTANCE,
                             du, 0)
    for i in range(num_scopes):
        cg = inst.createCovergroup("cg_%d" % i, None, 1, SourceT.SV)
        cp = cg.createCoverpoint("cp_%d" % i, None, 1, SourceT.SV)
        for j in range(bins_per_scope):
            data = CoverData(CoverTypeT.CVGBIN, 0)
            data.data = (i + j) % 3
            cp.createNextCover("bin_%d" % j, data, None)
    return db


def _full_walk(db):
    """Touch every scope, every bin and every count."""
    visited = 0
    total = 0

    def visit(scope):
        nonlocal visited, total
        visited += 1
        scope.getScopeName()
        scope.getScopeType()
        scope.getSourceInfo()
        for item in scope.coverItems(CoverTypeT.ALL):
            total += item.getCoverData().data
        for child in scope.scopes(ScopeTypeT.ALL):
            visit(child)

    for top in db.scopes(ScopeTypeT.ALL):
        visit(top)
    return visited, total


@pytest.mark.parametrize("num_scopes", [4, 64, 256])
def test_read_count_does_not_grow_with_object_count(tmp_path, num_scopes):
    """The N+1 guard: a bigger database must not mean more reads."""
    path = tmp_path / ("wide%d.parquet" % num_scopes)
    ParquetWriter(path).write(_wide_db(num_scopes), run_id="r0")

    db = ParquetUCIS(path)
    visited, _total = _full_walk(db)

    # 2 covergroup-tree scopes per unit, plus the DU and the instance.
    assert visited == 2 * num_scopes + 2
    # scopes + coveritems + counts + source_files: one read each.
    assert db.dataset.read_count <= 4, (
        "full walk of %d scopes issued %d reads -- a per-object query crept in"
        % (visited, db.dataset.read_count))


def test_read_count_is_identical_across_sizes(tmp_path):
    """Same shape, different sizes, same number of reads."""
    counts = []
    for num_scopes in (4, 128):
        path = tmp_path / ("w%d.parquet" % num_scopes)
        ParquetWriter(path).write(_wide_db(num_scopes), run_id="r0")
        db = ParquetUCIS(path)
        _full_walk(db)
        counts.append(db.dataset.read_count)
    assert counts[0] == counts[1]


def test_repeated_walks_do_not_reread(tmp_path):
    """A cursor advances over a materialized batch; it does not re-query."""
    path = tmp_path / "repeat.parquet"
    ParquetWriter(path).write(_wide_db(32), run_id="r0")

    db = ParquetUCIS(path)
    _full_walk(db)
    after_first = db.dataset.read_count
    for _ in range(5):
        _full_walk(db)
    assert db.dataset.read_count == after_first


def test_property_table_is_not_read_until_a_property_is_asked_for(tmp_path):
    """Lazy long tail: a walk that touches no EAV property never pays for it."""
    path = tmp_path / "lazy.parquet"
    ParquetWriter(path).write(build_db(), run_id="r0")

    db = ParquetUCIS(path)
    _full_walk(db)
    without_properties = db.dataset.read_count

    du = next(s for s in db.scopes(ScopeTypeT.ALL)
              if ScopeTypeT.DU_ANY(s.getScopeType()))
    assert du.getAttributes() == {"origin": "verilator"}
    assert db.dataset.read_count == without_properties + 1

    # ...and only once, however many objects ask.
    for scope in db.scopes(ScopeTypeT.ALL):
        scope.getAttributes()
    assert db.dataset.read_count == without_properties + 1


def test_counts_are_not_read_until_a_count_is_asked_for(tmp_path):
    """A structural query pays nothing for the measurement table."""
    path = tmp_path / "structure.parquet"
    ParquetWriter(path).write(_wide_db(16), run_id="r0")

    dataset = ParquetDataset(path)
    dataset.scope_rows()
    structural = dataset.read_count
    dataset.counts()
    assert dataset.read_count > structural


@pytest.mark.parametrize("num_runs", [1, 4, 16])
def test_read_count_grows_only_with_selected_partitions(tmp_path, num_runs):
    """Merging more runs costs one read per partition -- not per bin.

    This is the bound that makes query-time merge affordable: the scan grows
    with runs merged, which is why partition pruning is the cost control.
    """
    path = tmp_path / ("runs%d.parquet" % num_runs)
    writer = ParquetWriter(path)
    for i in range(num_runs):
        writer.write(build_db(counts=(1, 1, 1), tag=str(i)),
                     run_id="r%04d" % i)

    db = ParquetUCIS(path)
    _full_walk(db)
    # Definition tables: one read each.  Measurement: one per selected run.
    assert db.dataset.read_count <= len(sch.DEFINITION_TABLES) + num_runs

    one = ParquetUCIS(path, runs="r0000")
    _full_walk(one)
    assert one.dataset.read_count <= len(sch.DEFINITION_TABLES) + 1


def test_unselected_partitions_are_never_read(tmp_path):
    """Run selection prunes partitions; it does not read then filter."""
    path = tmp_path / "prune.parquet"
    writer = ParquetWriter(path)
    for i in range(8):
        writer.write(build_db(tag=str(i)), run_id="r%d" % i)

    one = ParquetDataset(path, runs="r3")
    one.table("counts")
    assert one.read_count == 1

    everything = ParquetDataset(path)
    everything.table("counts")
    assert everything.read_count == 8
