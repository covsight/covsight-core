"""
Unified format registry with setuptools entry_points discovery.

DB format plugins register under:    covsight.formats.db
Report format plugins register under: covsight.formats.rpt
CLI plugins register under:           covsight.cli
"""
from importlib.metadata import entry_points
from typing import Dict, Optional
from covsight.core.ext.format_db import FormatDescDb, FormatIfDb
from covsight.core.ext.format_rpt import FormatDescRpt, FormatIfRpt


class FormatRegistry:
    """Discovers and caches format plugins."""

    def __init__(self):
        self._db_formats: Optional[Dict[str, FormatDescDb]] = None
        self._rpt_formats: Optional[Dict[str, FormatDescRpt]] = None

    def db_formats(self) -> Dict[str, FormatDescDb]:
        if self._db_formats is None:
            self._db_formats = {}
            for ep in entry_points(group="covsight.formats.db"):
                try:
                    plugin = ep.load()
                    desc = plugin.describe()
                    self._db_formats[desc.name] = desc
                except Exception:
                    pass
        return self._db_formats

    def rpt_formats(self) -> Dict[str, FormatDescRpt]:
        if self._rpt_formats is None:
            self._rpt_formats = {}
            for ep in entry_points(group="covsight.formats.rpt"):
                try:
                    plugin = ep.load()
                    desc = plugin.describe()
                    self._rpt_formats[desc.name] = desc
                except Exception:
                    pass
        return self._rpt_formats

    def get_db_format(self, name: str) -> FormatDescDb:
        fmts = self.db_formats()
        if name not in fmts:
            available = ", ".join(sorted(fmts.keys())) or "(none installed)"
            raise KeyError(
                f"Database format '{name}' not found. "
                f"Available: {available}"
            )
        return fmts[name]

    def get_rpt_format(self, name: str) -> FormatDescRpt:
        fmts = self.rpt_formats()
        if name not in fmts:
            available = ", ".join(sorted(fmts.keys())) or "(none installed)"
            raise KeyError(
                f"Report format '{name}' not found. "
                f"Available: {available}"
            )
        return fmts[name]
