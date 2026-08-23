"""UCIS+ ⇄ Parquet backend.

A peer backend to NCDB and ``MemUCIS``: coverage is stored columnar (raw
Parquet, or Iceberg tables) while tools keep talking to the UCIS object API.

The normative schema is ``docs/ucis-parquet-mapping.md``; the implementation
plan is ``docs/ucis-parquet-impl-plan.md``.

A dataset represents a *database*, not a run.  Measurement tables carry a
``run_id`` and are partitioned by it, so N runs coexist in one dataset and
merge is a ``GROUP BY`` over the selected partitions (decision 1 of the plan).
"""

from covsight.core.parquet.schema import (
    SCHEMA_VERSION,
    ObjectKind,
    PropType,
    table_schema,
    TABLES,
    DEFINITION_TABLES,
    MEASUREMENT_TABLES,
)
from covsight.core.parquet.writer import (
    ParquetWriter, write_dataset, DefinitionMismatch,
)
from covsight.core.parquet.query import (
    ParquetDataset, RunSelection, DatasetError,
)
from covsight.core.parquet.backend import ParquetUCIS, open_dataset

__all__ = [
    "SCHEMA_VERSION",
    "ObjectKind",
    "PropType",
    "table_schema",
    "TABLES",
    "DEFINITION_TABLES",
    "MEASUREMENT_TABLES",
    "ParquetWriter",
    "write_dataset",
    "DefinitionMismatch",
    "ParquetDataset",
    "RunSelection",
    "DatasetError",
    "ParquetUCIS",
    "open_dataset",

]
