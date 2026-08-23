"""Parquet format plugin -- registered through the same entry-point registry
NCDB uses, so ``open_db`` dispatches to it with no tool changes.

Declares ``Read | Write`` but not ``Create``: the write path is a batch loader,
not the streaming UCIS create API (see :mod:`covsight.core.parquet.backend`).
Advertising ``Create`` would let a tool build a database against this backend
and fail deep inside the hierarchy walk instead of at open.
"""

from covsight.core.api import UCIS
from covsight.core.ext.format_db import (
    FormatCapabilities, FormatDbFlags, FormatDescDb, FormatIfDb,
)


class ParquetFormatPlugin:
    @staticmethod
    def describe() -> FormatDescDb:
        return FormatDescDb(
            name="parquet",
            fmt_if=ParquetFormatIf(),
            flags=FormatDbFlags.Read | FormatDbFlags.Write,
            description="Columnar UCIS+ dataset (Parquet / Iceberg), "
                        "multi-run and merge-on-read",
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


def _require_pyarrow():
    """Fail with an actionable message rather than a bare ImportError.

    ``describe()`` deliberately does not import pyarrow, so the format still
    *lists* on an install without the extra -- a tool can then report "install
    covsight-core[parquet]" instead of the format silently not existing.
    """
    try:
        import pyarrow  # noqa: F401
    except ImportError as exc:
        raise ImportError(
            "the Parquet backend needs pyarrow: pip install "
            "'covsight-core[parquet]'") from exc


class ParquetFormatIf(FormatIfDb):

    def create(self, filename=None) -> UCIS:
        from covsight.core.api.unimpl_error import UnimplError
        raise UnimplError(
            "the Parquet backend has no streaming create path; build the "
            "database in another backend and write it with ParquetWriter")

    def read(self, file_or_filename) -> UCIS:
        _require_pyarrow()
        from covsight.core.parquet.backend import ParquetUCIS
        return ParquetUCIS(file_or_filename)

    def write(self, db: UCIS, file_or_filename) -> None:
        _require_pyarrow()
        from covsight.core.parquet.writer import ParquetWriter
        ParquetWriter(file_or_filename).write(db)
