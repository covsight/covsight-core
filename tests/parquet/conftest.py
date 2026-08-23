"""Fixtures for the Parquet-backend suite.

Everything here runs on ``tmp_path`` with no services: the default job needs
only ``pip install covsight-core[parquet]``.
"""

import pytest

pytest.importorskip("pyarrow", reason="Parquet backend needs pyarrow")

from covsight.core.api import (                                   # noqa: E402
    CoverData, CoverTypeT, FlagsT, HistoryNodeKind, ScopeTypeT, SourceInfo,
    SourceT, TestStatusT,
)
from covsight.core.api.test_data import TestData                  # noqa: E402
from covsight.core.mem import MemFactory                          # noqa: E402


def build_db(counts=(1, 0, 7), tag="t0", *, with_formal=True):
    """A small but feature-diverse in-memory database.

    Covers the things a lossless round-trip has to survive: a DU/instance pair
    with a real link, functional coverage under a covergroup, code and toggle
    coverage, source locations, attributes and tags (the UCIS+ namespace),
    history with test data, sparse per-test associations, and formal results.

    Args:
        counts: Hit counts for the three covergroup bins.  Varying them per
            run is what makes a merge observable.
        tag: Suffix for the test name, so runs are distinguishable.
        with_formal: Attach formal-verification results.
    """
    db = MemFactory.create()
    fh = db.createFileHandle("counter.sv", "/rtl")

    hist = db.createHistoryNode(None, "test_" + tag, "./run.sh",
                                HistoryNodeKind.TEST)
    hist.setTestData(TestData(teststatus=TestStatusT.OK,
                              toolcategory="simulator", date="2026-07-26",
                              simtime=1.5, timeunit="ns", seed="42",
                              cmd="./run.sh", user="ci"))

    du = db.createScope("work.counter", SourceInfo(fh, 1, 0), 1, SourceT.SV,
                        ScopeTypeT.DU_MODULE, 0)
    du.setAttribute("origin", "verilator")
    du.addTag("rtl")

    inst = db.createInstance("top", SourceInfo(fh, 10, 0), 1, SourceT.SV,
                             ScopeTypeT.INSTANCE, du,
                             FlagsT.ENABLED_STMT | FlagsT.ENABLED_BRANCH)

    cg = inst.createCovergroup("addr_cg", SourceInfo(fh, 20, 0), 1, SourceT.SV)
    cg.setAtLeast(2)
    cg.setPerInstance(True)
    cp = cg.createCoverpoint("addr", SourceInfo(fh, 21, 0), 1, SourceT.SV)
    for i, count in enumerate(counts):
        data = CoverData(CoverTypeT.CVGBIN, 0)
        data.data = count
        data.at_least = 1
        data.weight = 1
        idx = cp.createNextCover("bin_%d" % i, data, SourceInfo(fh, 22 + i, 0))
        idx.setAttribute("bin_kind", "auto")

    block = inst.createScope("block1", SourceInfo(fh, 30, 0), 1, SourceT.SV,
                             ScopeTypeT.BLOCK, FlagsT.ENABLED_STMT)
    stmt = CoverData(CoverTypeT.STMTBIN, 0)
    stmt.data = 5
    block.createNextCover("stmt_30", stmt, SourceInfo(fh, 30, 0))

    toggle = inst.createToggle("data_valid", "top.data_valid",
                               FlagsT.ENABLED_TOGGLE, None, None, None)
    for name, count in (("0->1", 3), ("1->0", 0)):
        data = CoverData(CoverTypeT.TOGGLEBIN, 0)
        data.data = count
        toggle.createNextCover(name, data, None)

    # Sparse test<->cover associations: flat bin indices, as recorded by a
    # simulator front end.
    db.record_test_association(0, 0, max(counts[0], 1))
    db.record_test_association(0, 3, 5)

    if with_formal:
        from covsight.core.api.enums import FormalStatusT
        db.set_formal_data(0, status=FormalStatusT.PROOF, radius=7,
                           witness="w0.vcd")

    return db


@pytest.fixture
def make_db():
    return build_db


@pytest.fixture
def single_run(tmp_path):
    """A dataset holding one run, plus the source database."""
    from covsight.core.parquet import ParquetWriter
    db = build_db()
    path = tmp_path / "single.parquet"
    run_id = ParquetWriter(path).write(db, run_id="r0")
    return db, path, run_id


@pytest.fixture
def multi_run(tmp_path):
    """A dataset holding three runs with differing counts.

    The counts are chosen so a merge is not confusable with any single run:
    each bin's total differs from every run's value.
    """
    from covsight.core.parquet import ParquetWriter
    path = tmp_path / "multi.parquet"
    writer = ParquetWriter(path)
    specs = [((1, 0, 7), "a"), ((2, 5, 0), "b"), ((4, 1, 1), "c")]
    for i, (counts, tag) in enumerate(specs):
        writer.write(build_db(counts=counts, tag=tag), run_id="r%d" % i)
    return path, tuple("r%d" % i for i in range(len(specs))), specs


def walk_facts(db):
    """Every fact the object API exposes about *db*, in iteration order.

    Used as the comparison basis for the round-trip and cross-backend tests: a
    list, not a set, so a reordered iteration fails rather than passing
    quietly.
    """
    facts = []

    def visit(scope, path):
        facts.append(("scope", path, scope.getScopeName(),
                      int(scope.getScopeType()), scope.getWeight(),
                      scope.getGoal()))
        info = scope.getSourceInfo()
        facts.append(("srcinfo", path,
                      info.file.getFileName() if info and info.file else None,
                      info.line if info else None,
                      info.token if info else None))
        for item in scope.coverItems(CoverTypeT.ALL):
            data = item.getCoverData()
            facts.append(("bin", path, item.getName(), int(data.type),
                          data.data, data.at_least, data.weight, data.goal,
                          data.limit, data.flags))
        for child in scope.scopes(ScopeTypeT.ALL):
            visit(child, "%s/%s" % (path, child.getScopeName() or ""))

    for top in db.scopes(ScopeTypeT.ALL):
        visit(top, top.getScopeName() or "")

    for node in db.historyNodes(HistoryNodeKind.ALL):
        facts.append(("history", node.getLogicalName(),
                      node.getPhysicalName(), int(node.getKind()),
                      node.getSeed(), node.getSimTime(),
                      node.getToolCategory(), node.getDate(),
                      node.getRunCwd(), node.getUserName(),
                      int(node.getTestStatus())))
    return facts


def shape_facts(db):
    """Scope/bin structure and counts only -- the cross-backend subset.

    Deliberately narrower than :func:`walk_facts`: two different backends are
    required to agree on hierarchy, iteration order, bin names/types and
    counts.  Metadata each backend is free to normalize (source paths, history
    detail) is compared separately where it is actually guaranteed.
    """
    facts = []

    def visit(scope, path):
        facts.append(("scope", path, int(scope.getScopeType())))
        for item in scope.coverItems(CoverTypeT.ALL):
            data = item.getCoverData()
            facts.append(("bin", path, item.getName(), int(data.type),
                          data.data))
        for child in scope.scopes(ScopeTypeT.ALL):
            visit(child, "%s/%s" % (path, child.getScopeName() or ""))

    for top in db.scopes(ScopeTypeT.ALL):
        visit(top, top.getScopeName() or "")
    return facts
