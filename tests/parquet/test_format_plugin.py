"""Plugin registration and dataset detection.

"Tools work unchanged" also means *opening* works unchanged: the backend has to
arrive through the same entry-point registry NCDB uses, and a dataset has to be
recognizable without the caller knowing which format it is.
"""

import pytest

from covsight.core.ext.format_db import FormatDbFlags
from covsight.core.ext.registry import FormatRegistry
from covsight.core.parquet import ParquetUCIS
from covsight.core.parquet import schema as sch
from covsight.core.parquet.format_detect import (
    dataset_runs, dataset_schema_version, detect_parquet_format,
    is_parquet_dataset, read_manifest,
)

from .conftest import walk_facts


def test_parquet_registers_as_a_peer_of_ncdb():
    formats = FormatRegistry().db_formats()
    assert "parquet" in formats, \
        "the entry point is missing -- reinstall the package"
    assert "ncdb" in formats


def test_declared_flags_match_reality():
    """``Create`` is not declared: there is no streaming create path."""
    desc = FormatRegistry().get_db_format("parquet")
    assert desc.flags & FormatDbFlags.Read
    assert desc.flags & FormatDbFlags.Write
    assert not desc.flags & FormatDbFlags.Create


def test_capabilities_claim_losslessness():
    caps = FormatRegistry().get_db_format("parquet").capabilities
    assert caps.can_read and caps.can_write and caps.lossless
    assert caps.functional_coverage and caps.code_coverage
    assert caps.toggle_coverage and caps.history_nodes


def test_read_through_the_registry(single_run):
    db, path, _run_id = single_run
    desc = FormatRegistry().get_db_format("parquet")
    restored = desc.fmt_if.read(str(path))
    assert isinstance(restored, ParquetUCIS)
    assert walk_facts(restored) == walk_facts(db)


def test_write_through_the_registry(tmp_path, make_db):
    desc = FormatRegistry().get_db_format("parquet")
    path = tmp_path / "viaplugin.parquet"
    desc.fmt_if.write(make_db(), str(path))
    assert is_parquet_dataset(path)
    assert dataset_runs(path) == ("run-0000",)


def test_create_fails_loudly(tmp_path):
    """Better to refuse at open than to fail deep inside a hierarchy walk."""
    from covsight.core.api.unimpl_error import UnimplError
    desc = FormatRegistry().get_db_format("parquet")
    with pytest.raises(UnimplError):
        desc.fmt_if.create(str(tmp_path / "nope.parquet"))


def test_detection_recognizes_a_dataset(single_run):
    _db, path, _run_id = single_run
    assert detect_parquet_format(path) == sch.DATASET_FORMAT
    assert is_parquet_dataset(path)
    assert dataset_schema_version(path) == sch.SCHEMA_VERSION


def test_detection_rejects_a_plain_parquet_directory(tmp_path):
    """A directory of unrelated Parquet files is not a coverage database."""
    import pyarrow as pa
    import pyarrow.parquet as pq
    directory = tmp_path / "notours"
    directory.mkdir()
    pq.write_table(pa.table({"a": [1, 2]}), directory / "data.parquet")

    assert detect_parquet_format(directory) == "unknown"
    assert not is_parquet_dataset(directory)
    assert read_manifest(directory) is None
    assert dataset_runs(directory) == ()


def test_detection_rejects_a_file_and_a_missing_path(tmp_path):
    a_file = tmp_path / "file.parquet"
    a_file.write_bytes(b"not a directory")
    assert not is_parquet_dataset(a_file)
    assert not is_parquet_dataset(tmp_path / "does-not-exist")


def test_detection_survives_a_corrupt_manifest(tmp_path):
    directory = tmp_path / "corrupt"
    directory.mkdir()
    (directory / sch.MANIFEST_NAME).write_text("{not json")
    assert not is_parquet_dataset(directory)
