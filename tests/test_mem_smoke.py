"""End-to-end smoke: create a database, add a covergroup, verify counts."""
from covsight.core.mem import MemFactory
from covsight.core.api import (
    ScopeTypeT, SourceT, CoverData, CoverTypeT, CoverFlagsT,
    HistoryNodeKind, TestStatusT,
)
from covsight.core.api.test_data import TestData


def test_create_covergroup_with_bins():
    db = MemFactory.create()

    testnode = db.createHistoryNode(None, "smoke", "smoke", HistoryNodeKind.TEST)
    testnode.setTestData(TestData(teststatus=TestStatusT.OK, toolcategory="test", date="20240101000000"))

    cg = db.createScope("cg_arith", None, 1, SourceT.SV, ScopeTypeT.COVERGROUP, 0)
    cp = cg.createScope("cp_op", None, 1, SourceT.SV, ScopeTypeT.COVERPOINT, 0)

    cd_add = CoverData(type=CoverTypeT.CVGBIN, flags=CoverFlagsT.IS_32BIT)
    cd_add.data = 5
    cp.createNextCover("add", cd_add, None)

    cd_sub = CoverData(type=CoverTypeT.CVGBIN, flags=CoverFlagsT.IS_32BIT)
    cd_sub.data = 0
    cp.createNextCover("sub", cd_sub, None)

    bins = list(cp.coverItems(CoverTypeT.CVGBIN))
    assert len(bins) == 2

    db.close()
