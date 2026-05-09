from covsight.core.api import CoverData
from covsight.core.api import CoverTypeT
from covsight.core.api import FlagsT
from covsight.core.api import HistoryNodeKind
from covsight.core.api import ScopeTypeT
from covsight.core.api import SourceInfo
from covsight.core.api import SourceT
from covsight.core.api import TestStatusT as _TestStatusT
from covsight.core.mem import MemFactory
from covsight.core.visitors import UCISVisitor, traverse


class CountingVisitor(UCISVisitor):
    def __init__(self):
        super().__init__()
        self.dbs = []
        self.history = []
        self.instances = []
        self.covergroups = []
        self.coverpoints = []
        self.cover_items = []

    def visit_db(self, db):
        self.dbs.append(db)

    def visit_history_node(self, node):
        self.history.append(node.getLogicalName())

    def visit_instance(self, inst):
        self.instances.append(inst.getScopeName())

    def visit_covergroup(self, cg):
        self.covergroups.append(cg.getScopeName())

    def visit_coverpoint(self, cp):
        self.coverpoints.append(cp.getScopeName())

    def visit_cover_item(self, item):
        self.cover_items.append(item.getName())


def _create_instance_db(create_db):
    db = create_db()
    fh = db.createFileHandle("design.sv", "/rtl")
    du = db.createScope(
        "work.top",
        SourceInfo(fh, 1, 0),
        1,
        SourceT.VLOG,
        ScopeTypeT.DU_MODULE,
        FlagsT.SCOPE_UNDER_DU | FlagsT.INST_ONCE,
    )
    inst = db.createInstance(
        "top_inst",
        None,
        1,
        SourceT.VLOG,
        ScopeTypeT.INSTANCE,
        du,
        FlagsT.INST_ONCE,
    )
    return db, fh, inst


def test_traverse_empty_db(backend):
    _, create_db, _, _, _ = backend

    visitor = CountingVisitor()
    traverse(create_db(), visitor)

    assert len(visitor.dbs) == 1
    assert visitor.history == []
    assert visitor.instances == []


def test_traverse_single_instance_and_history(backend):
    _, create_db, _, _, _ = backend
    db, _, _ = _create_instance_db(create_db)

    history = db.createHistoryNode(None, "test_a", "test_a.ucis", HistoryNodeKind.TEST)
    history.setTestStatus(_TestStatusT.OK)

    visitor = CountingVisitor()
    traverse(db, visitor)

    assert visitor.history == ["test_a"]
    assert "top_inst" in visitor.instances


def test_traverse_coverpoint_bins(backend):
    _, create_db, _, _, _ = backend
    db, fh, inst = _create_instance_db(create_db)
    cg = inst.createCovergroup("cg", SourceInfo(fh, 5, 0), 1, SourceT.OTHER)
    cgi = cg.createCoverInstance("cg", SourceInfo(fh, 5, 0), 1, SourceT.OTHER)
    cp = cgi.createCoverpoint("cp", SourceInfo(fh, 6, 0), 1, SourceT.VLOG)

    cp.createNextCover("bin_a", CoverData(CoverTypeT.CVGBIN, 0), None)
    cp.createNextCover("bin_b", CoverData(CoverTypeT.CVGBIN, 0), None)

    visitor = CountingVisitor()
    traverse(db, visitor)

    assert "cg" in visitor.covergroups
    assert "cp" in visitor.coverpoints
    assert visitor.cover_items == ["bin_a", "bin_b"]


def test_custom_visitor_counts_instances(backend):
    _, create_db, _, _, _ = backend
    db, _, _ = _create_instance_db(create_db)
    root_du = next(db.scopes(ScopeTypeT.DU_MODULE))
    for idx in range(2):
        db.createInstance(
            f"child_{idx}",
            None,
            1,
            SourceT.VLOG,
            ScopeTypeT.INSTANCE,
            root_du,
            FlagsT.INST_ONCE,
        )

    class InstanceCounter(UCISVisitor):
        def __init__(self):
            super().__init__()
            self.count = 0

        def visit_instance(self, inst):
            self.count += 1

    visitor = InstanceCounter()
    traverse(db, visitor)

    assert visitor.count == 3


def test_clone_database_traversal(backend):
    backend_name, create_db, _, _, _ = backend
    if backend_name != "memory":
        return

    db, _, inst = _create_instance_db(create_db)
    cg = inst.createCovergroup("cg", None, 1, SourceT.OTHER)
    cp = cg.createCoverpoint("cp", None, 1, SourceT.OTHER)
    cp.createNextCover("bin", CoverData(CoverTypeT.CVGBIN, 0), None)

    cloned = MemFactory.clone(db)

    original = CountingVisitor()
    copied = CountingVisitor()
    traverse(db, original)
    traverse(cloned, copied)

    assert original.instances == copied.instances
    assert original.covergroups == copied.covergroups
    assert original.cover_items == copied.cover_items
