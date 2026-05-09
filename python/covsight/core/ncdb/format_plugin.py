"""NCDB format plugin -- ships with covsight-core (always available)."""
from covsight.core.ext.format_db import FormatIfDb, FormatDescDb, FormatDbFlags, FormatCapabilities
from covsight.core.api import UCIS


class NcdbFormatPlugin:
    @staticmethod
    def describe() -> FormatDescDb:
        return FormatDescDb(
            name="ncdb",
            fmt_if=NcdbFormatIf(),
            flags=FormatDbFlags.Read | FormatDbFlags.Write | FormatDbFlags.Create,
            description="NCDB ZIP-based binary coverage format",
            capabilities=FormatCapabilities(
                can_read=True, can_write=True,
                functional_coverage=True, cross_coverage=True,
                ignore_illegal_bins=True,
                code_coverage=True, toggle_coverage=True,
                fsm_coverage=True, assertions=True,
                history_nodes=True, design_hierarchy=True,
                lossless=True,
            ),
        )


class NcdbFormatIf(FormatIfDb):
    def create(self, filename=None) -> UCIS:
        from covsight.core.ncdb.ncdb_ucis import NcdbUCIS
        return NcdbUCIS(filename)

    def read(self, file_or_filename) -> UCIS:
        from covsight.core.ncdb.ncdb_reader import NcdbReader
        return NcdbReader().read(file_or_filename)

    def write(self, db: UCIS, file_or_filename) -> None:
        from covsight.core.ncdb.ncdb_writer import NcdbWriter
        NcdbWriter().write(db, file_or_filename)
