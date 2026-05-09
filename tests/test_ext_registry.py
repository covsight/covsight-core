from covsight.core.ext import FormatRegistry


def test_registry_returns_empty_when_no_plugins():
    r = FormatRegistry()
    assert isinstance(r.db_formats(), dict)
    assert isinstance(r.rpt_formats(), dict)


def test_get_missing_format_raises():
    import pytest
    r = FormatRegistry()
    with pytest.raises(KeyError, match="not found"):
        r.get_db_format("nonexistent")


def test_get_missing_rpt_format_raises():
    import pytest
    r = FormatRegistry()
    with pytest.raises(KeyError, match="not found"):
        r.get_rpt_format("nonexistent")
