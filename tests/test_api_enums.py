"""Verify all enums are importable and have expected members."""
from covsight.core.api.enums import (
    ScopeTypeT, CoverTypeT, CoverFlagsT, FlagsT, SourceT,
    ToggleMetricT, ToggleTypeT, ToggleDirT,
    HistoryNodeKind, IntProperty, StrProperty, RealProperty,
    HandleProperty, TestStatusT, FormalStatusT,
)

def test_scope_type_has_covergroup():
    assert hasattr(ScopeTypeT, 'COVERGROUP')

def test_cover_type_has_cvgbin():
    assert hasattr(CoverTypeT, 'CVGBIN')

def test_history_node_kind_has_test():
    assert hasattr(HistoryNodeKind, 'TEST')

def test_flags_has_inst_once():
    assert hasattr(FlagsT, 'INST_ONCE')

def test_source_has_sv():
    assert hasattr(SourceT, 'SV')

def test_test_status_has_ok():
    assert hasattr(TestStatusT, 'OK')
