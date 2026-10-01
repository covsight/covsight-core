"""Verilator ``coverage.dat`` format plugin (read-only)."""
from covsight.core.api import UCIS
from covsight.core.ext.format_db import (
    FormatCapabilities, FormatDbFlags, FormatDescDb, FormatIfDb,
)


class VltcovFormatPlugin:
    @staticmethod
    def describe() -> FormatDescDb:
        return FormatDescDb(
            name="vltcov",
            fmt_if=VltcovFormatIf(),
            flags=FormatDbFlags.Read,
            description="Verilator coverage.dat (SystemC::Coverage-3)",
            capabilities=FormatCapabilities(
                can_read=True,
                functional_coverage=True, cross_coverage=True,
                code_coverage=True, toggle_coverage=True,
                fsm_coverage=True, assertions=True,
                design_hierarchy=True,
            ),
        )


class VltcovFormatIf(FormatIfDb):
    def read(self, file_or_filename) -> UCIS:
        from covsight.core.vltcov import read_vlt_coverage
        return read_vlt_coverage(file_or_filename)
