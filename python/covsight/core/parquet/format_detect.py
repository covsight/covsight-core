"""Recognize a covsight Parquet dataset.

A dataset is a *directory*, not a file -- so detection cannot sniff magic
bytes the way ``ncdb/format_detect.py`` does.  The marker is the dataset
manifest, which also carries the schema version, so a directory of unrelated
Parquet files is never mistaken for a coverage database.
"""

import json
import os

from covsight.core.parquet.schema import DATASET_FORMAT, MANIFEST_NAME


def is_parquet_dataset(path) -> bool:
    """True if *path* is a directory holding a covsight Parquet dataset."""
    return detect_parquet_format(path) == DATASET_FORMAT


def detect_parquet_format(path) -> str:
    """Return :data:`DATASET_FORMAT` for a covsight dataset, else ``"unknown"``."""
    manifest = read_manifest(path)
    if manifest is None:
        return "unknown"
    return DATASET_FORMAT if manifest.get("format") == DATASET_FORMAT \
        else "unknown"


def read_manifest(path):
    """The parsed dataset manifest, or None if *path* has none."""
    if not os.path.isdir(str(path)):
        return None
    manifest_path = os.path.join(str(path), MANIFEST_NAME)
    try:
        with open(manifest_path) as fp:
            manifest = json.load(fp)
    except (OSError, ValueError):
        return None
    return manifest if isinstance(manifest, dict) else None


def dataset_schema_version(path):
    """Schema version of the dataset at *path*, or None."""
    manifest = read_manifest(path)
    return None if manifest is None else manifest.get("schema_version")


def dataset_runs(path):
    """Run ids present in the dataset at *path* (empty tuple if none)."""
    manifest = read_manifest(path)
    if manifest is None:
        return ()
    return tuple(r["run_id"] for r in manifest.get("runs", []))
