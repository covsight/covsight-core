from covsight.core.api import FlagsT
from covsight.core.api import ScopeTypeT
from covsight.core.api import SourceInfo
from covsight.core.api import SourceT
from covsight.core.mem import MemFactory
from covsight.core.visitors import UCISVisitor, traverse


def test_mem_visitor_smoke():
    db = MemFactory.create()
    fh = db.createFileHandle("design.sv", "/rtl")
    du = db.createScope(
        "work.top",
        SourceInfo(fh, 1, 0),
        1,
        SourceT.VLOG,
        ScopeTypeT.DU_MODULE,
        FlagsT.SCOPE_UNDER_DU | FlagsT.INST_ONCE,
    )
    db.createInstance(
        "top_inst",
        None,
        1,
        SourceT.VLOG,
        ScopeTypeT.INSTANCE,
        du,
        FlagsT.INST_ONCE,
    )

    class Visitor(UCISVisitor):
        def __init__(self):
            self.instances = []

        def visit_instance(self, inst):
            self.instances.append(inst.getScopeName())

    visitor = Visitor()
    traverse(db, visitor)

    assert visitor.instances == ["top_inst"]
