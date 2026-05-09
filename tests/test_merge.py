from covsight.core.api import CoverData
from covsight.core.api import CoverTypeT
from covsight.core.api import FlagsT
from covsight.core.api import HistoryNodeKind
from covsight.core.api import ScopeTypeT
from covsight.core.api import SourceInfo
from covsight.core.api import SourceT
from covsight.core.api import TestStatusT as _TestStatusT
from covsight.core.mem import MemFactory
from covsight.core.merge import DbMerger


def _find_scope(parent, scope_type, name):
    for scope in parent.scopes(scope_type):
        if scope.getScopeName() == name:
            return scope
    raise AssertionError(f"Missing scope {name} of type {scope_type}")


def _cover_counts(scope, cover_type):
    return {
        item.getName(): item.getCoverData().data
        for item in scope.coverItems(cover_type)
    }


def _create_instance(db, name):
    fh = db.createFileHandle(f"{name}.sv", "/rtl")
    du = db.createScope(
        f"work.{name}",
        SourceInfo(fh, 1, 0),
        1,
        SourceT.VLOG,
        ScopeTypeT.DU_MODULE,
        FlagsT.SCOPE_UNDER_DU | FlagsT.INST_ONCE,
    )
    inst = db.createInstance(
        name,
        SourceInfo(fh, 2, 0),
        1,
        SourceT.VLOG,
        ScopeTypeT.INSTANCE,
        du,
        FlagsT.INST_ONCE,
    )
    return fh, inst


def _add_covergroup(instance, fh, cg_name, ci_name, cp_bins, cross_bins=None):
    cg = instance.createCovergroup(cg_name, SourceInfo(fh, 10, 0), 1, SourceT.OTHER)
    top_coverpoints = {}
    for idx, (cp_name, bins) in enumerate(cp_bins.items()):
        cp = cg.createCoverpoint(cp_name, SourceInfo(fh, 11 + idx, 0), 1, SourceT.SV)
        for bin_name, count in bins.items():
            cp.createBin(bin_name, None, 1, count, bin_name, CoverTypeT.CVGBIN)
        top_coverpoints[cp_name] = cp

    if cross_bins is not None:
        cross = cg.createCross(
            "cp1xcp2",
            SourceInfo(fh, 20, 0),
            1,
            SourceT.SV,
            [top_coverpoints["cp1"], top_coverpoints["cp2"]],
        )
        for bin_name, count in cross_bins.items():
            cross.createBin(bin_name, None, 1, count, bin_name, CoverTypeT.CVGBIN)

    cgi = cg.createCoverInstance(ci_name, SourceInfo(fh, 30, 0), 1, SourceT.OTHER)
    inst_coverpoints = {}
    for idx, (cp_name, bins) in enumerate(cp_bins.items()):
        cp = cgi.createCoverpoint(cp_name, SourceInfo(fh, 31 + idx, 0), 1, SourceT.SV)
        for bin_name, count in bins.items():
            cp.createBin(bin_name, None, 1, count, bin_name, CoverTypeT.CVGBIN)
        inst_coverpoints[cp_name] = cp

    if cross_bins is not None:
        cross = cgi.createCross(
            "cp1xcp2",
            SourceInfo(fh, 40, 0),
            1,
            SourceT.SV,
            [inst_coverpoints["cp1"], inst_coverpoints["cp2"]],
        )
        for bin_name, count in cross_bins.items():
            cross.createBin(bin_name, None, 1, count, bin_name, CoverTypeT.CVGBIN)


def _add_block_scope(instance, fh, name, count):
    block = instance.createScope(
        name,
        SourceInfo(fh, 50, 0),
        1,
        SourceT.VLOG,
        ScopeTypeT.BLOCK,
        FlagsT.ENABLED_STMT,
    )
    data = CoverData(CoverTypeT.STMTBIN, 0)
    data.data = count
    data.goal = 1
    block.createNextCover("stmt", data, SourceInfo(fh, 51, 0))


def test_merge_combines_coverpoint_bins_for_same_instance():
    src1 = MemFactory.create()
    fh1, inst1 = _create_instance(src1, "inst1")
    _add_covergroup(inst1, fh1, "cvg", "inst1", {"cp1": {"b0": 0, "b1": 1}})

    src2 = MemFactory.create()
    fh2, inst2 = _create_instance(src2, "inst1")
    _add_covergroup(inst2, fh2, "cvg", "inst1", {"cp1": {"b0": 1, "b1": 0}})

    dst = MemFactory.create()
    DbMerger().merge(dst, [src1, src2])

    merged_inst = _find_scope(dst, ScopeTypeT.INSTANCE, "inst1")
    merged_cg = _find_scope(merged_inst, ScopeTypeT.COVERGROUP, "cvg")
    merged_cp = _find_scope(merged_cg, ScopeTypeT.COVERPOINT, "cp1")
    merged_cgi = _find_scope(merged_cg, ScopeTypeT.COVERINSTANCE, "inst1")
    merged_cgi_cp = _find_scope(merged_cgi, ScopeTypeT.COVERPOINT, "cp1")

    assert _cover_counts(merged_cp, CoverTypeT.CVGBIN) == {"b0": 1, "b1": 1}
    assert _cover_counts(merged_cgi_cp, CoverTypeT.CVGBIN) == {"b0": 1, "b1": 1}


def test_merge_keeps_distinct_instances_and_crosses():
    src1 = MemFactory.create()
    fh1, inst1 = _create_instance(src1, "inst1")
    _add_covergroup(
        inst1,
        fh1,
        "cvg",
        "inst1",
        {"cp1": {"b0": 1, "b1": 0}, "cp2": {"b0": 1, "b1": 0}},
        {"<b0,b0>": 1, "<b0,b1>": 0, "<b1,b0>": 0, "<b1,b1>": 0},
    )

    src2 = MemFactory.create()
    fh2, inst2 = _create_instance(src2, "inst2")
    _add_covergroup(
        inst2,
        fh2,
        "cvg",
        "inst2",
        {"cp1": {"b0": 0, "b1": 1}, "cp2": {"b0": 0, "b1": 1}},
        {"<b0,b0>": 0, "<b0,b1>": 0, "<b1,b0>": 0, "<b1,b1>": 1},
    )

    dst = MemFactory.create()
    DbMerger().merge(dst, [src1, src2])

    assert sorted(scope.getScopeName() for scope in dst.scopes(ScopeTypeT.INSTANCE)) == ["inst1", "inst2"]

    merged_inst1 = _find_scope(dst, ScopeTypeT.INSTANCE, "inst1")
    merged_inst2 = _find_scope(dst, ScopeTypeT.INSTANCE, "inst2")
    cross1 = _find_scope(_find_scope(merged_inst1, ScopeTypeT.COVERGROUP, "cvg"), ScopeTypeT.CROSS, "cp1xcp2")
    cross2 = _find_scope(_find_scope(merged_inst2, ScopeTypeT.COVERGROUP, "cvg"), ScopeTypeT.CROSS, "cp1xcp2")

    assert _cover_counts(cross1, CoverTypeT.CVGBIN)["<b0,b0>"] == 1
    assert _cover_counts(cross2, CoverTypeT.CVGBIN)["<b1,b1>"] == 1


def test_merge_sums_cross_bins_for_matching_instance():
    src1 = MemFactory.create()
    fh1, inst1 = _create_instance(src1, "inst1")
    _add_covergroup(
        inst1,
        fh1,
        "cvg",
        "inst1",
        {"cp1": {"b0": 1, "b1": 0}, "cp2": {"b0": 1, "b1": 0}},
        {"<b0,b0>": 1, "<b0,b1>": 0, "<b1,b0>": 0, "<b1,b1>": 0},
    )

    src2 = MemFactory.create()
    fh2, inst2 = _create_instance(src2, "inst1")
    _add_covergroup(
        inst2,
        fh2,
        "cvg",
        "inst1",
        {"cp1": {"b0": 0, "b1": 1}, "cp2": {"b0": 1, "b1": 0}},
        {"<b0,b0>": 0, "<b0,b1>": 0, "<b1,b0>": 1, "<b1,b1>": 0},
    )

    dst = MemFactory.create()
    DbMerger().merge(dst, [src1, src2])

    merged_inst = _find_scope(dst, ScopeTypeT.INSTANCE, "inst1")
    merged_cg = _find_scope(merged_inst, ScopeTypeT.COVERGROUP, "cvg")
    merged_cross = _find_scope(merged_cg, ScopeTypeT.CROSS, "cp1xcp2")

    assert _cover_counts(merged_cross, CoverTypeT.CVGBIN) == {
        "<b0,b0>": 1,
        "<b0,b1>": 0,
        "<b1,b0>": 1,
        "<b1,b1>": 0,
    }


def test_merge_copies_history_nodes_and_code_coverage():
    src1 = MemFactory.create()
    fh1, inst1 = _create_instance(src1, "inst1")
    _add_block_scope(inst1, fh1, "blk", 2)
    root = src1.createHistoryNode(None, "merge-root", "root.ucis", HistoryNodeKind.MERGE)
    child = src1.createHistoryNode(root, "test-1", "test-1.ucis", HistoryNodeKind.TEST)
    child.setTestStatus(_TestStatusT.OK)

    src2 = MemFactory.create()
    fh2, inst2 = _create_instance(src2, "inst1")
    _add_block_scope(inst2, fh2, "blk", 3)

    dst = MemFactory.create()
    DbMerger().merge(dst, [src1, src2])

    history = list(dst.historyNodes(HistoryNodeKind.ALL))
    assert [node.getLogicalName() for node in history] == ["merge-root", "test-1"]
    assert history[1].getParent() is history[0]

    merged_inst = _find_scope(dst, ScopeTypeT.INSTANCE, "inst1")
    merged_block = _find_scope(merged_inst, ScopeTypeT.BLOCK, "blk")
    assert _cover_counts(merged_block, CoverTypeT.STMTBIN) == {"stmt": 5}
