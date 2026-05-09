"""Verify abstract classes are importable and structured correctly."""
from covsight.core.api import UCIS, Scope, Obj, HistoryNode, CoverData, SourceInfo

def test_ucis_inherits_scope():
    assert issubclass(UCIS, Scope)

def test_scope_inherits_obj():
    assert issubclass(Scope, Obj)

def test_cover_data_instantiation():
    from covsight.core.api.enums import CoverTypeT
    cd = CoverData(type=CoverTypeT.CVGBIN, flags=0)
    assert cd is not None
