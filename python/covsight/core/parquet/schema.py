"""PyArrow schemas for the UCIS+ ⇄ Parquet mapping.

Portable Arrow/Parquet types only -- INT8/32/64, DOUBLE, STRING.  No
engine-specific types (``LowCardinality``, ``VARIANT``, ...): dictionary
encoding at write time gives the low-cardinality benefit portably.  See the
"Neutrality rules" section of ``docs/ucis-parquet-mapping.md``; the
``PORTABLE_TYPES`` allowlist below is what the neutrality test asserts against.

Physical layout of a dataset (a directory):

    <dataset>/
      _covsight_parquet.json           dataset manifest (schema_version, runs)
      scopes/data.parquet              definition tables -- written once,
      coveritems/data.parquet          run-independent, never rewritten by
      source_files/data.parquet        a later run append
      properties/data.parquet
      cross_operands/data.parquet
      counts/run_id=<rid>/data.parquet          measurement tables --
      history_nodes/run_id=<rid>/data.parquet   one partition per run
      history_props/run_id=<rid>/data.parquet
      test_cover_assoc/run_id=<rid>/data.parquet

Definition vs. measurement is the split that makes merge cheap: a merge
touches only the narrow measurement tables while bin *definitions* are carried
through untouched (see "Coverage merge" in the mapping doc).
"""

from enum import IntEnum

import pyarrow as pa

#: Bumped when the logical schema changes.  Evolve by adding nullable columns
#: or new namespaces -- never by repurposing an existing column.
#:
#: 1.1 added the ``cross_operands`` table.  Minor, not major: a 1.0 reader
#: sees a directory it does not know and every table it does know is
#: unchanged, so the rule in "Neutrality rules" -- refuse an unknown *major*
#: version -- keeps such a dataset readable, minus the crossed-coverpoint
#: list.  A 1.1 reader over a 1.0 dataset finds no such directory, which the
#: reader already treats as an empty table.
SCHEMA_VERSION = "1.1"

#: Marker written into the dataset manifest.
DATASET_FORMAT = "covsight-parquet"

#: Name of the per-dataset manifest file.
MANIFEST_NAME = "_covsight_parquet.json"

#: Partition column for the measurement tables.
RUN_PARTITION_COL = "run_id"


class ObjectKind(IntEnum):
    """``properties.object_kind`` -- what ``object_id`` refers to."""

    SCOPE = 0
    COVERITEM = 1
    HISTORY_NODE = 2


class PropType(IntEnum):
    """``properties.prop_type`` -- which value column is populated."""

    INT = 0
    INT64 = 1
    REAL = 2
    STRING = 3
    HANDLE = 4


#: Property namespaces.  ``ucis`` is the standard typed-property space; every
#: extension ("UCIS+") ships as rows under a non-``ucis`` namespace so old
#: tools ignore what they do not know.
NS_UCIS = "ucis"
NS_COVSIGHT = "covsight"

#: ``prop_id`` prefixes used within the ``covsight`` namespace.
PROP_ATTR_PREFIX = "attr:"
PROP_TAG_PREFIX = "tag:"

#: Per-scope test↔cover association tier (mapping doc: only SPARSE
#: materializes rows; ALL/NEVER are a per-scope flag).  Stored as a
#: ``covsight``-namespace property row rather than a schema fork.
PROP_ASSOC_TIER = "assoc_tier"

ASSOC_TIER_NEVER = 0
ASSOC_TIER_ALL = 1
ASSOC_TIER_SPARSE = 2


# --------------------------------------------------------------------------
# Promoted properties
# --------------------------------------------------------------------------
#
# A "hot" typed property is stored as a column rather than an EAV row -- that
# is what keeps the property table from dominating dataset size, and it is
# what lets an engine push a predicate down onto e.g. `weight`.
#
# But a column cannot say "this backend does not support this property":
# ``weight = 1`` and "getIntProperty(SCOPE_WEIGHT) raises UnimplError" are
# different facts, and a faithful backend has to reproduce both.  So each
# promoted-property list below is paired with a ``promoted_props`` bitmask
# column: bit *i* is set iff the source backend actually answered property
# *i*.  Reader logic is then exact -- promoted column when the bit is set,
# EAV row otherwise, raise if neither.
#
# Order is part of the format: append only, never reorder.

#: Promoted properties whose value is *derived* at read time rather than read
#: out of a column.  Named rather than ``None`` because there is now more than
#: one, and "which derivation" is not recoverable from a null.
DERIVED_TOGGLE_COVERED = "@toggle_covered"
DERIVED_NUM_CROSSED_CVPS = "@num_crossed_cvps"
DERIVED_ITH_CROSSED_CVP_NAME = "@ith_crossed_cvp_name"


def is_derived(column) -> bool:
    """True if a promoted property's "column" is a derivation, not a column."""
    return isinstance(column, str) and column.startswith("@")


def _promoted_scope():
    from covsight.core.api import IntProperty, StrProperty
    return (
        (IntProperty.SCOPE_WEIGHT, "int", "weight"),
        (IntProperty.SCOPE_GOAL, "int", "goal"),
        (IntProperty.COVER_GOAL, "int", "goal"),
        # Derived from the counts, so it is run-dependent and must never be
        # frozen into a definition table.  The bit still records whether the
        # source backend answered it at all.
        (IntProperty.TOGGLE_COVERED, "int", DERIVED_TOGGLE_COVERED),
        # Appended (bit 4) rather than inserted: `scopes.source` already
        # existed, but nothing read it and the property was *also* written as
        # an EAV row -- one fact in two places, which is the duplication the
        # promotion rule exists to prevent.
        (IntProperty.SCOPE_SOURCE_TYPE, "int", "source"),
        # Both derived from `cross_operands`, which is the one place a cross's
        # operand list lives.  `NUM_CROSSED_CVPS` is read-only in UCIS, and
        # `ITH_CROSSED_CVP_NAME` names the operand selected by `coverindex` --
        # a *row* per operand, so neither can be an EAV row keyed by prop_id
        # alone without losing the index.
        (IntProperty.NUM_CROSSED_CVPS, "int", DERIVED_NUM_CROSSED_CVPS),
        (StrProperty.ITH_CROSSED_CVP_NAME, "str",
         DERIVED_ITH_CROSSED_CVP_NAME),
    )


def _promoted_history():
    from covsight.core.api import IntProperty, RealProperty, StrProperty
    return (
        (IntProperty.TEST_STATUS, "int", "status"),
        (IntProperty.TEST_COMPULSORY, "int", "compulsory"),
        (StrProperty.HIST_CMDLINE, "str", "cmdline"),
        (StrProperty.HIST_RUNCWD, "str", "run_cwd"),
        (StrProperty.TEST_USERNAME, "str", "user_name"),
        (StrProperty.COMMENT, "str", "comment"),
        (StrProperty.VER_VENDOR_ID, "str", "vendor_id"),
        (StrProperty.VER_VENDOR_TOOL, "str", "vendor_tool"),
        (StrProperty.VER_VENDOR_VERSION, "str", "vendor_tool_version"),
        (RealProperty.SIMTIME, "real", "sim_time"),
        (RealProperty.CPUTIME, "real", "cpu_time"),
        (RealProperty.COST, "real", "cost"),
    )


def promoted_properties(kind: ObjectKind):
    """The ordered promoted-property list for *kind*, or () if none."""
    if kind == ObjectKind.SCOPE:
        return _promoted_scope()
    if kind == ObjectKind.HISTORY_NODE:
        return _promoted_history()
    return ()


# --------------------------------------------------------------------------
# Table schemas
# --------------------------------------------------------------------------

def _scopes() -> pa.Schema:
    """The scope tree: design units, instances and coverage scopes."""
    return pa.schema([
        # PK -- UCIS_STR_UNIQUE_ID.  The only identity that survives a
        # round-trip, a merge and a backend swap.
        pa.field("unique_id", pa.string(), nullable=False),
        pa.field("parent_id", pa.string()),          # FK -> scopes.unique_id
        pa.field("dfs_ordinal", pa.int64(), nullable=False),
        # int64, not int32: ScopeTypeT is a bitfield that exceeds 32 bits.
        pa.field("scope_type", pa.int64(), nullable=False),
        pa.field("flags", pa.int64()),
        pa.field("name", pa.string()),               # local name, not the path
        pa.field("du_id", pa.string()),              # owning DU (file-id locality)
        pa.field("source_file_id", pa.int32()),      # DU-local file id
        pa.field("source_line", pa.int32()),
        pa.field("source_col", pa.int32()),
        pa.field("weight", pa.int32()),              # hot property, promoted
        pa.field("goal", pa.int32()),                # hot property, promoted
        pa.field("source", pa.int32()),              # SourceT
        # For INSTANCE scopes: unique_id of the design unit it instantiates.
        pa.field("instance_du_id", pa.string()),
        # Which promoted properties the source backend answered -- see
        # `promoted_properties`.
        pa.field("promoted_props", pa.int32()),
    ])


def _coveritems() -> pa.Schema:
    """Typed cover bins within a scope."""
    return pa.schema([
        # PK -- a dense surrogate assigned in definition order.  Dense rather
        # than a digest of the natural key because random integers are
        # incompressible: on a 1.2M-bin design, hashed ids cost ~20 MB of a
        # 24 MB dataset.  The natural key `(scope_id, local_index)` is stored
        # right here, so identity that must outlive a definition change does
        # not depend on the surrogate.
        pa.field("coveritem_id", pa.int64(), nullable=False),
        pa.field("scope_id", pa.string(), nullable=False),
        # With scope_id, reproduces ucis_CoverIterate order.
        pa.field("local_index", pa.int32(), nullable=False),
        pa.field("cover_type", pa.int32(), nullable=False),
        pa.field("flags", pa.int64()),
        pa.field("name", pa.string()),               # nullable (e.g. toggle)
        pa.field("at_least", pa.int32()),
        pa.field("weight", pa.int32()),
        pa.field("goal", pa.int32()),
        pa.field("limit", pa.int32()),
        pa.field("bitlen", pa.int32()),
        pa.field("source_file_id", pa.int32()),
        pa.field("source_line", pa.int32()),
        pa.field("source_col", pa.int32()),
    ])


def _cross_operands() -> pa.Schema:
    """Which coverpoints a ``UCIS_CROSS`` scope crosses, in cross order.

    A cross's operand list is one-to-many and *ordered* -- ``i`` is an argument
    to ``ucis_GetIthCrossedCvp`` and selects the name reported by
    ``UCIS_STR_ITH_CROSSED_CVP_NAME`` -- so it cannot live in the EAV
    ``properties`` table, which is keyed by ``prop_id`` alone and would hold
    only one operand per cross.  Definition-side: a cross crosses the same
    coverpoints in every run.
    """
    return pa.schema([
        pa.field("cross_id", pa.string(), nullable=False),   # FK -> scopes
        pa.field("operand_index", pa.int32(), nullable=False),
        # FK -> scopes.unique_id.  Nullable: a source that answers only
        # `UCIS_STR_ITH_CROSSED_CVP_NAME` gives a name and no handle, and a
        # name recorded without a resolved scope is still worth carrying.
        pa.field("coverpoint_id", pa.string()),
        pa.field("name", pa.string()),
    ])


def _counts() -> pa.Schema:
    """The columnar payload -- narrow, run-partitioned, cheap to merge."""
    return pa.schema([
        pa.field("coveritem_id", pa.int64(), nullable=False),
        pa.field("count", pa.int64(), nullable=False),
    ])


def _properties() -> pa.Schema:
    """The full typed-property space and all UCIS+ extensions (tall/EAV).

    Definition-side only (scopes and coveritems) -- run-independent, so it is
    written once per definition set.  History-node properties live in
    ``history_props``, which is run-partitioned.
    """
    return pa.schema([
        pa.field("object_id", pa.string(), nullable=False),
        pa.field("object_kind", pa.int8(), nullable=False),
        pa.field("namespace", pa.string(), nullable=False),
        pa.field("prop_id", pa.string(), nullable=False),
        pa.field("prop_type", pa.int8(), nullable=False),
        pa.field("i64", pa.int64()),
        pa.field("f64", pa.float64()),
        pa.field("str", pa.string()),
        pa.field("handle", pa.string()),
    ])


def _history_nodes() -> pa.Schema:
    """Per-test-run metadata and merge nodes (a tree)."""
    return pa.schema([
        pa.field("node_id", pa.string(), nullable=False),
        pa.field("parent_id", pa.string()),
        pa.field("kind", pa.int32()),                # HistoryNodeKind
        pa.field("local_index", pa.int32(), nullable=False),
        pa.field("logical_name", pa.string()),
        pa.field("physical_name", pa.string()),
        pa.field("tool_category", pa.string()),
        pa.field("ucis_version", pa.string()),
        pa.field("vendor_id", pa.string()),
        pa.field("vendor_tool", pa.string()),
        pa.field("vendor_tool_version", pa.string()),
        pa.field("cmdline", pa.string()),
        pa.field("args", pa.string()),
        pa.field("run_cwd", pa.string()),
        pa.field("host_os", pa.string()),
        pa.field("user_name", pa.string()),
        pa.field("seed", pa.string()),
        pa.field("time_unit", pa.string()),
        pa.field("comment", pa.string()),
        # UCIS types the test date as a *string* property, but a backend may
        # hold an epoch int.  Two nullable columns, exactly one populated, so
        # the value round-trips without a type-sniffing heuristic.
        pa.field("date", pa.string()),
        pa.field("date_int", pa.int64()),
        pa.field("compulsory", pa.int64()),
        pa.field("same_tests", pa.int64()),
        pa.field("sim_time", pa.float64()),
        pa.field("cpu_time", pa.float64()),
        pa.field("cost", pa.float64()),
        pa.field("status", pa.int32()),              # TestStatusT
        pa.field("promoted_props", pa.int32()),
    ])


def _history_props() -> pa.Schema:
    """History-node property long tail -- same EAV shape as ``properties``.

    Split out because it is run-scoped: appending a run must not rewrite a
    definition table.
    """
    return _properties()


def _test_cover_assoc() -> pa.Schema:
    """Which tests contributed to which bins (SPARSE tier only).

    ALL / NEVER tiers cost zero rows: they are recorded as a per-scope
    ``covsight:assoc_tier`` property row instead.
    """
    return pa.schema([
        pa.field("test_id", pa.string(), nullable=False),
        pa.field("coveritem_id", pa.int64(), nullable=False),
        pa.field("count", pa.int64()),
    ])


def _formal() -> pa.Schema:
    """Formal-verification results per assertion bin (a UCIS+ extension).

    Run-scoped: a proof status comes *from* a run, and merging two runs of the
    same assertion is a precedence decision, not a sum -- which is why this is
    a table of its own rather than more columns on ``coveritems``.
    """
    return pa.schema([
        pa.field("coveritem_id", pa.int64(), nullable=False),
        pa.field("status", pa.int32()),              # FormalStatusT
        pa.field("radius", pa.int64()),
        pa.field("witness", pa.string()),
    ])


def _source_files() -> pa.Schema:
    """Per-DU file table -- UCIS file ids are DU-local (ADR 0001)."""
    return pa.schema([
        pa.field("du_id", pa.string(), nullable=False),
        pa.field("file_id", pa.int32(), nullable=False),
        pa.field("path", pa.string(), nullable=False),
    ])


_BUILDERS = {
    "scopes": _scopes,
    "coveritems": _coveritems,
    "cross_operands": _cross_operands,
    "counts": _counts,
    "properties": _properties,
    "history_nodes": _history_nodes,
    "history_props": _history_props,
    "test_cover_assoc": _test_cover_assoc,
    "formal": _formal,
    "source_files": _source_files,
}

#: Every table in the dataset.
TABLES = tuple(_BUILDERS.keys())

#: Run-independent: written once per definition set, never rewritten when a
#: later run is appended.
DEFINITION_TABLES = ("scopes", "coveritems", "cross_operands", "properties",
                     "source_files")

#: Run-scoped: carry ``run_id`` and are partitioned by it.
MEASUREMENT_TABLES = ("counts", "history_nodes", "history_props",
                      "test_cover_assoc", "formal")

assert set(DEFINITION_TABLES) | set(MEASUREMENT_TABLES) == set(TABLES)


# --------------------------------------------------------------------------
# Type-aware merge
# --------------------------------------------------------------------------
#
# Merge is *not* uniformly SUM.  Getting this wrong is silent: a peak-active
# assertion count that is summed across 64 runs reads as a plausible number
# that is simply false.  Keyed on `coveritems.cover_type`.

MERGE_SUM = "sum"
MERGE_MAX = "max"


def _merge_ops():
    from covsight.core.api import CoverTypeT
    return {
        # A peak is a high-water mark, not a total.
        int(CoverTypeT.PEAKACTIVEBIN): MERGE_MAX,
    }


#: ``cover_type`` -> merge op.  Anything absent is additive, which is the
#: right default for hit counts (toggle, line, branch, covergroup, FSM).
MERGE_OPS = None


def merge_op(cover_type) -> str:
    """The merge operation for *cover_type*."""
    global MERGE_OPS
    if MERGE_OPS is None:
        MERGE_OPS = _merge_ops()
    return MERGE_OPS.get(int(cover_type or 0), MERGE_SUM)


def merge_formal_statuses(statuses) -> int:
    """Merge formal statuses across runs.

    Delegates to :func:`covsight.core.api.enums.merge_formal_statuses` so this
    backend and NCDB cannot drift apart on what a merged assertion status
    means -- the same question must not get two answers depending on where the
    data happens to be stored.

    Note the rule is not a simple precedence: UCIS reserves ``CONFLICT`` for a
    merge disagreement, so a bin proved in one run and failed in another
    becomes ``CONFLICT`` rather than letting either side win.
    """
    from covsight.core.api.enums import merge_formal_statuses as _merge
    return _merge(statuses)


def formal_status_rank(status) -> int:
    """How informative a ``FormalStatusT`` is; higher wins absent a conflict."""
    from covsight.core.api.enums import formal_status_rank as _rank
    return _rank(status)


# --------------------------------------------------------------------------
# Physical encoding
# --------------------------------------------------------------------------
#
# Logical schema and physical encoding are different things, and the default
# encoding is badly wrong for this data.  Identity columns are dense ascending
# integers; dictionary-encoding them stores every value *and* a dictionary,
# which is why `counts.coveritem_id` measured 1.9 MB per run of a 1.2M-bin
# design -- three times the actual counts it accompanies.
#
# DELTA_BINARY_PACKED stores the successive differences instead.  For a column
# that is literally `range(n)` those are all 1, and the column costs almost
# nothing.  Same bytes on the wire, same logical schema, no engine-specific
# types: this is a Parquet encoding, not a format fork.

DELTA_ENCODED = {
    "counts": ("coveritem_id",),
    "coveritems": ("coveritem_id", "local_index", "source_line",
                   "source_file_id"),
    "scopes": ("dfs_ordinal", "source_line", "source_file_id"),
    "test_cover_assoc": ("coveritem_id",),
    "formal": ("coveritem_id",),
    "history_nodes": ("local_index",),
    "source_files": ("file_id",),
    "cross_operands": ("operand_index",),
}


def delta_columns(table: str) -> tuple:
    """Columns of *table* to encode as DELTA_BINARY_PACKED."""
    return DELTA_ENCODED.get(table, ())


def dictionary_columns(table: str) -> list:
    """Columns of *table* to dictionary-encode -- everything else.

    Parquet rejects specifying both a dictionary and an explicit encoding for
    the same column, so the two lists have to partition the schema.
    """
    delta = set(delta_columns(table))
    return [f.name for f in table_schema(table) if f.name not in delta]


#: Types the schema is allowed to use.  Test 1 asserts every field of every
#: table is in this set, so a non-portable type cannot creep in for perf.
PORTABLE_TYPES = (
    pa.int8(), pa.int32(), pa.int64(),
    pa.float64(), pa.string(),
)


def table_schema(name: str, *, with_run_id: bool = False) -> pa.Schema:
    """Return the Arrow schema for table *name*.

    Args:
        name: One of :data:`TABLES`.
        with_run_id: Append the ``run_id`` partition column.  The column is
            implicit in the on-disk layout (a Hive partition directory), so it
            is only materialized when a caller wants the flattened form -- for
            instance when handing a table to an engine that has no partition
            awareness, or when writing Iceberg tables.

    Raises:
        KeyError: If *name* is not a known table.
    """
    schema = _BUILDERS[name]()
    if with_run_id:
        if name not in MEASUREMENT_TABLES:
            raise ValueError(
                "%s is a definition table and carries no run_id" % name)
        schema = schema.append(
            pa.field(RUN_PARTITION_COL, pa.string(), nullable=False))
    return schema


def with_metadata(schema: pa.Schema, table: str, **extra) -> pa.Schema:
    """Attach ``schema_version`` and table identity to *schema*'s metadata."""
    meta = {
        b"covsight.schema_version": SCHEMA_VERSION.encode(),
        b"covsight.format": DATASET_FORMAT.encode(),
        b"covsight.table": table.encode(),
    }
    for key, value in extra.items():
        meta[("covsight.%s" % key).encode()] = str(value).encode()
    return schema.with_metadata(meta)


def empty_table(name: str, *, with_run_id: bool = False) -> pa.Table:
    """An empty, correctly-typed table -- used when a run has no such rows."""
    schema = with_metadata(table_schema(name, with_run_id=with_run_id), name)
    return pa.table({f.name: pa.array([], type=f.type) for f in schema},
                    schema=schema)
