# UCIS+ ⇄ Parquet mapping

**Status:** Implemented — see the "Implementation notes" section for the
points where the built schema differs from this draft.
**Date:** 2026-07-24 (implementation notes added 2026-07-26)
**Related:** [`ncdb-improvements.md`](ncdb-improvements.md) (the "optimized
storage, UCIS logical view" principle this generalizes),
[`ucis-impl-plan.md`](ucis-impl-plan.md), [`adr/0001-ucis-writer-foundation.md`](adr/0001-ucis-writer-foundation.md),
[`testplan-schema.md`](testplan-schema.md).

## Purpose

Define a **versioned, engine-neutral schema** that maps the UCIS 1.0 data model
(plus our extensions — "UCIS+") to and from Apache Parquet, so that tools written
against the **UCIS API** work unchanged while the data is physically stored in a
columnar database (Apache Iceberg / Trino / DuckDB, or a commercial warehouse
such as ClickHouse / Snowflake / BigQuery).

NCDB is *one* storage backend, not the subject here. This document specifies a
*peer* backend: "Parquet-in-a-database." The invariant across both is the UCIS
logical view.

## Conformance

### Normative language

The key words **MUST**, **MUST NOT**, **REQUIRED**, **SHOULD**, **SHOULD NOT**
and **MAY** are to be interpreted as in RFC 2119 when they appear in capitals.
Lowercase uses of the same words are descriptive prose and carry no requirement.

Where this document describes what our implementation happens to do rather than
what an implementation must do, it says so — see "Implementation notes".

### Two profiles

A conforming implementation implements the **core** profile. The **extended**
profile is additive.

| Profile | Contains | A reader that omits it |
| ------- | -------- | ---------------------- |
| **core** | the scope tree, coveritems and counts, the property space, source files, history nodes, run partitioning and merge | cannot report coverage correctly |
| **extended** | formal-verification results, per-test contribution (`test_cover_assoc` and its tiers), tags, user-defined coverage | still reports every coverage number correctly, and loses only the capability itself |

The split is drawn on one question: **does omitting this change a coverage
number?** Formal results, per-test provenance, tags and user-defined bins are
all things a legitimate producer may not have — a simulation-only tool has no
proofs, and the association tiers exist precisely because recording per-test
contribution is often unaffordable. Requiring them would exclude conforming
implementations without making any two of them interoperate better.

Which profile each UCIS feature falls in is recorded per feature in
[`docs/conformance/features/`](conformance/features/) and rendered in
[`ucis-parquet-feature-map.md`](ucis-parquet-feature-map.md).

### Requirements on a reader

1. A reader MUST recognise a dataset by the `format` key of its manifest, and
   MUST refuse a `schema_version` whose major component it does not implement.
2. A reader MUST honour `properties.prop_type` when selecting a value column.
   Reading the wrong column yields NULL rather than an error, so ignoring it
   silently loses the property long tail.
3. A reader MUST treat an unrecognised `namespace` in `properties` as opaque and
   preserve it. It MUST NOT drop rows it does not understand.
4. A reader MUST NOT infer that a scope has no test associations from the
   absence of `test_cover_assoc` rows; it MUST consult the `assoc_tier`
   property first.
5. A reader MUST reject the inputs listed under "Negative rules" rather than
   repair or ignore them.
6. A reader SHOULD read a promoted property from its column when the
   corresponding `promoted_props` bit is set, from `properties` when it is not,
   and MUST NOT invent a value when neither is present.

### Requirements on a writer

1. A writer MUST make the definition tables byte-identical across appends of
   additional runs.
2. A writer MUST key measurements on `(run_id, coveritem_id)` so re-loading a
   run cannot double-count.
3. A writer MUST NOT reorder or repurpose `promoted_props` bit positions; new
   promoted properties are appended.
4. A writer MUST record every property it could not obtain by leaving both the
   promoted bit clear and the EAV row absent, rather than emitting a default.
5. A writer SHOULD emit extensions as rows in a non-`ucis` namespace, and MUST
   NOT add engine-specific columns or types to the logical schema.

## Core principle: API is the contract, storage is pluggable

```
        Tools  (report, merge, testplan-bind, query)
          │  call only the UCIS+ API
          ▼
   ┌──────────────────────────────┐
   │   UCIS+ API facade            │  stable — tools never see storage
   ├──────────────────────────────┤
   │   Backend interface (abstract)│  open/create · scope walk · cover walk
   │                               │  · get/set property · history · assoc · merge
   ├───────┬──────────┬───────────┤
   │ NCDB  │ Parquet/  │ hosted DB │  peer backends
   │       │ Iceberg   │           │
   └───────┴──────────┴───────────┘
                │
   UCIS+ ⇄ Parquet schema  ◄── this document
```

Tools compile once against the UCIS API and are agnostic to the backend.

## Two mappings, not one

1. **Logical mapping — UCIS+ objects ↔ tables.** Schema design; see below.
2. **Access mapping — UCIS iterators/handles ↔ set-oriented queries.** The UCIS
   C API is handle-based and row-at-a-time (`ucis_ScopeIterate` /
   `ucis_NextScope`, `ucis_CoverIterate` / `ucis_NextCover`, per-object
   `ucis_GetIntProperty`). A columnar store wants set-at-a-time SQL. The adapter
   **MUST NOT** translate each `NextScope`/`GetProperty` into its own query. It
   MUST prefetch subtrees in bulk, expose cursors over result sets, and
   lazy-load the property long tail — presenting a row-at-a-time API over
   columnar batch reads.

## Logical schema

Portable types only (see "Neutrality rules"). Column widths given as logical
Parquet/Arrow types.

### `scopes`
<!-- ucis-features: S4.1, S4.2, S4.3, S4.15, S4.16, S4.17, S4.18, ST.*, SF.*, F10.3, DB.5, H11.9 -->
The scope tree: design units, instances, and coverage scopes (covergroup,
coverpoint, cross, FSM, toggle, block/branch/expr/cond).

| column | type | notes |
| ------ | ---- | ----- |
| `unique_id` | STRING | **PK** — `UCIS_STR_UNIQUE_ID`, stable cross-store identity |
| `parent_id` | STRING | FK → `scopes.unique_id` (null for DB root) |
| `dfs_ordinal` | INT64 | canonical depth-first iteration order |
| `scope_type` | INT64 | UCIS scope-type enum (**INT64**: `ScopeTypeT` exceeds 32 bits) |
| `flags` | INT64 | UCIS scope flags bitfield |
| `name` | STRING | local scope name (not the full path) |
| `du_id` | STRING | FK → `scopes.unique_id` of the owning design unit — **the DU's unique id, not its name**. Gives file-id locality, since UCIS file ids are DU-local |
| `source_file_id` | INT32 | DU-local file id (null if none) |
| `source_line` | INT32 | nullable |
| `source_col` | INT32 | nullable |
| `weight` / `goal` | INT32 | hot properties promoted to columns (nullable) |

### `coveritems`
<!-- ucis-features: C6.1, C6.3, C6.4, C6.6, BT.*, DB.3, UD.1, UD.2, MT.3, CG.6 -->
Typed cover bins within a scope.

| column | type | notes |
| ------ | ---- | ----- |
| `coveritem_id` | INT64 | **PK** — dense surrogate in definition order (see Identity) |
| `scope_id` | STRING | FK → `scopes.unique_id` |
| `local_index` | INT32 | position within scope; with `scope_id` reproduces `ucis_CoverIterate` order |
| `cover_type` | INT32 | `CVGBIN`/`STMTBIN`/`BRANCHBIN`/`TOGGLEBIN`/`EXPRBIN`/`STATEBIN`/`TRANSBIN`/`USERBIN`/assertion bins |
| `flags` | INT64 | UCIS cover flags (`ucisCoverDataFlagsT`) |
| `name` | STRING | bin name (nullable — e.g. toggle) |
| `at_least` / `weight` | INT32 | hot properties (nullable) |

### `cross_operands`
<!-- ucis-features: CG.11, S4.19, PI.7, PS.12 -->
Which coverpoints a `UCIS_CROSS` scope crosses, and in what order.

A table rather than property rows because the relation is one-to-many **and
ordered**: `i` is an argument to `ucis_GetIthCrossedCvp` and selects which name
`UCIS_STR_ITH_CROSSED_CVP_NAME` reports, while `properties` is keyed by
`prop_id` alone and can hold only one value per key. Definition-side: a cross
crosses the same coverpoints in every run.

| column | type | notes |
| ------ | ---- | ----- |
| `cross_id` | STRING | FK → `scopes.unique_id`, the `UCIS_CROSS` scope |
| `operand_index` | INT32 | the `i` of `ucis_GetIthCrossedCvp`; dense from 0 |
| `coverpoint_id` | STRING | FK → `scopes.unique_id` (nullable, see below) |
| `name` | STRING | the crossed coverpoint's name |

A writer MUST emit `operand_index` dense from 0 and MUST NOT emit two rows with
the same `(cross_id, operand_index)`. `coverpoint_id` MAY be null: a source that
states only `UCIS_STR_ITH_CROSSED_CVP_NAME` has a name and no handle, and a name
without a resolvable scope is still worth carrying. A reader MUST NOT invent a
coverpoint for such a row — `ucis_GetIthCrossedCvp` returns nothing for it.

`UCIS_INT_NUM_CROSSED_CVPS` is the **count of these rows**, never a stored
number: the property is read-only in UCIS, and a stored count free to disagree
with the operand list would leave a reader no way to decide which is true.

### `counts`
<!-- ucis-features: DB.1, BT.24 -->
The columnar payload — kept separate so it compresses/scans independently and
merges cheaply.

| column | type | notes |
| ------ | ---- | ----- |
| `coveritem_id` | INT64 | FK → `coveritems.coveritem_id` |
| `count` | INT64 | hit count (RLE/dictionary-friendly; mostly small/zero) |

### `properties`
<!-- ucis-features: A9.1, A9.2, A9.3, A9.4, A9.5, DB.2, DB.6, CG.9, CG.10, CX.2, CX.3, TG.6, BR.2, UD.3 -->
The full UCIS typed-property space **and all UCIS+ extensions** — the long tail
not promoted to columns. Tall/EAV, typed, namespaced.

| column | type | notes |
| ------ | ---- | ----- |
| `object_id` | STRING | scope `unique_id` or `coveritem_id` (see `object_kind`) |
| `object_kind` | INT8 | scope \| coveritem \| history-node |
| `namespace` | STRING | `"ucis"` for standard props; `"covsight"`/vendor for UCIS+ |
| `prop_id` | STRING | property key |
| `prop_type` | INT8 | int \| int64 \| real \| string \| handle |
| `i64` / `f64` / `str` / `handle` | INT64/DOUBLE/STRING/STRING | one populated per row |

### `history_nodes`
<!-- ucis-features: H11.1, H11.3, H11.4, H11.5, H11.6, H11.7, H11.8, V3.4 -->
Per-test-run metadata and merge nodes (a tree).

| column | type | notes |
| ------ | ---- | ----- |
| `node_id` | STRING | **PK** |
| `parent_id` | STRING | merge-tree parent (null for root) |
| `kind` | INT8 | 1 = test run, 2 = merge node |
| `local_index` | INT32 | position in the history list; reproduces `ucis_HistoryIterate` order |
| `logical_name` / `physical_name` | STRING | the test's name and its run artifact |
| `tool_category` | STRING | `UCIS_SIM_TOOL`, `UCIS_FORMAL_TOOL`, … |
| `ucis_version` | STRING | the standard version the node was written under |
| `vendor_id` / `vendor_tool` / `vendor_tool_version` | STRING | writing tool identity |
| `cmdline` / `args` / `run_cwd` | STRING | invocation |
| `host_os` / `user_name` | STRING | environment |
| `seed` | STRING | the UCIS API types the seed as a string |
| `time_unit` | STRING | unit for `sim_time` |
| `comment` | STRING | free text |
| `date` **and** `date_int` | STRING / INT64 | exactly one populated — see the implementation notes |
| `compulsory` | INT32 | `UCIS_INT_TEST_COMPULSORY` |
| `same_tests` | INT32 | UOR same-test count |
| `sim_time` / `cpu_time` / `cost` | DOUBLE | nullable |
| `status` | INT8 | `ucisTestStatusT` — OK / WARNING / ERROR / FATAL / MISSING / MERGE_ERROR |
| `promoted_props` | INT64 | support bitmask; see below |

All columns except `node_id` and `kind` are nullable. The node's own property
long tail lives in `history_props`, which has the same shape as `properties` but
is run-partitioned, because a history node belongs to a run.

### `test_cover_assoc`
<!-- ucis-features: HL.1, HL.3, HL.4, HL.5, HL.6, HL.7 -->
Which tests contributed to which bins. Preserves the tiered model (NEVER / ALL /
SPARSE) — only SPARSE materializes rows; ALL/NEVER are recorded as a per-scope
tier flag so the common cases cost ~zero rows.

| column | type | notes |
| ------ | ---- | ----- |
| `test_id` | STRING | FK → `history_nodes.node_id` |
| `coveritem_id` | INT64 | FK → `coveritems.coveritem_id` |

### `source_files`
<!-- ucis-features: F10.1, F10.2, F10.4, DB.7 -->
Per-DU file table — UCIS file IDs are **DU-local**, per ADR 0001.

| column | type | notes |
| ------ | ---- | ----- |
| `du_id` | STRING | design unit |
| `file_id` | INT32 | DU-local id |
| `path` | STRING | file path |

## Dataset layout on disk
<!-- ucis-features: L1.1, L1.2, V3.2, V3.3 -->

A dataset is a **directory**, not a file. Definition tables are unpartitioned;
measurement tables use one Hive-style partition directory per run, so an engine
discovers `run_id` as a column without being told about it.

```
<dataset>/
  _covsight_parquet.json                    manifest (below)
  scopes/data.parquet                       ─┐
  coveritems/data.parquet                    │ definition tables:
  cross_operands/data.parquet                │ run-independent, never
  properties/data.parquet                    │ rewritten by a later append
  source_files/data.parquet                 ─┘
  counts/run_id=<rid>/data.parquet          ─┐
  history_nodes/run_id=<rid>/data.parquet    │ measurement tables:
  history_props/run_id=<rid>/data.parquet    │ one partition per run
  test_cover_assoc/run_id=<rid>/data.parquet │
  formal/run_id=<rid>/data.parquet          ─┘
```

A reader reads the whole of a measurement table with a recursive glob — `counts/**/*.parquet`
with Hive partitioning enabled — and one run by naming its partition directory.
The `run_id` column exists only in the partition path, which is what makes run
selection a directory prune rather than a scan.

The manifest is JSON and carries the format marker, the schema version, and the
run list:

```json
{
  "format": "covsight-parquet",
  "schema_version": "1.1",
  "compression": "snappy",
  "runs": [{"run_id": "r0", "num_counts": 37, "num_tests": 2, "num_assoc": 3}],
  "num_scopes": 19,
  "num_coveritems": 37,
  "db": {"api_version": "1.0", "path_separator": "/", "written_by": "...",
         "written_time": 1786843145, "str_properties": {}}
}
```

`schema_version` also travels in each table's Parquet key-value metadata, so a
single table file remains self-describing when it is copied out of the dataset.

## Enumerated column encodings
<!-- ucis-features: DB.6, HL.6, HL.7, H11.5 -->

Columns whose values are small integers with a meaning. Scope and cover types are
not listed here — those are UCIS 1.0 enum values and are normative in the
specification — but the following are **ours**, and a reader has no way to guess
them.

| Column | Value | Meaning |
| ------ | ----: | ------- |
| `properties.object_kind` | 0 | `object_id` is a `scopes.unique_id` |
| | 1 | `object_id` is a `coveritems.coveritem_id` |
| | 2 | `object_id` is a `history_nodes.node_id` |
| `properties.prop_type` | 0 | `int` — read `i64` |
| | 1 | `int64` — read `i64` |
| | 2 | `real` — read `f64` |
| | 3 | `string` — read `str` |
| | 4 | `handle` — read `handle` (a `unique_id` of the referenced object) |
| `history_nodes.kind` | 1 | a test run (`UCIS_HISTORYNODE_TEST`) |
| | 2 | a merge node (`UCIS_HISTORYNODE_MERGE`) |

Exactly one value column is populated per `properties` row; `prop_type` says
which. Reading the wrong one yields NULL rather than an error, so a reader that
ignores `prop_type` silently loses the property long tail.

**Namespaces and reserved `prop_id` forms.** `namespace = "ucis"` holds the
standard typed-property space, keyed by the UCIS property enum name
(`SCOPE_WEIGHT`, `TOGGLE_CANON_NAME`, …). Everything else is an extension and a
reader that does not recognise a namespace must preserve those rows rather than
drop them. Within `namespace = "covsight"`:

| `prop_id` | `prop_type` | Meaning |
| --------- | ----------- | ------- |
| `attr:<key>` | string | a user-defined attribute (§ 8.4) named `<key>` |
| `tag:<name>` | int, `i64 = 1` | the object carries tag `<name>` |
| `assoc_tier` | int | per-scope association tier: 0 = NEVER, 1 = ALL, 2 = SPARSE |

### Flag bits are type-qualified
<!-- ucis-features: SF.10, SF.11, SF.12, SF.26, C6.6, DB.3, X.1, BR.3, FS.7, CG.12, CG.13, AS.6, CG.8, CX.3, UD.4 -->

`scopes.flags` and `coveritems.flags` are `ucisFlagsT` bitfields, and UCIS
divides both into four ranges:

| Bits | Meaning |
| ---- | ------- |
| `0x0000FFFF` | **general** — the same in every scope or coveritem type |
| `0x07FF0000` | **type-qualified** — *"flag locations may be reused for non-intersecting type sets"* |
| `0x08000000` | a temporary traversal mark; not persistent state |
| `0xF0000000` | reserved for user extension |

**A reader MUST interpret the `0x07FF0000` bits together with the object's
type.** Bit `0x00010000` means `UCIS_IS_FSM_RESET` on a `UCIS_FSMBIN`,
`UCIS_IS_BR_ELSE` on a `UCIS_BRANCHBIN`, and `UCIS_BIN_IFF_EXISTS` on a
covergroup bin — three unrelated facts sharing one position. On scopes the same
is true: `0x00010000` is `UCIS_IS_TOP_NODE` on a toggle, `UCIS_IS_IMMEDIATE_ASSERT`
on an assertion, and `UCIS_SCOPE_CVG_AUTO` on a coverpoint or cross.

The mapping stores the bitfield verbatim; it does not normalize or split it.
That keeps the column faithful, and it puts the burden on the reader — which is
why it is stated here rather than left to be discovered.

**Exclusion is per-bin as well as per-scope.** `coveritems.flags` carries
`UCIS_EXCLUDE_PRAGMA` (0x20), `_FILE` (0x40), `_INST` (0x80) and `_AUTO`
(0x100); their union is `UCIS_EXCLUDED` (0x1E0). These are *general* bits, so
they need no type qualification. A consumer asking "is this bin excluded?"
tests against the union, and one asking "why?" tests the individual reasons —
the four exist separately because a report has to distinguish a user exclusion
from a tool-generated one, and a merge unions them without losing which is
which.

**A promoted property is a column *instead of* a row, never both.** This is the
rule most likely to trip a reader: querying `properties` for `SCOPE_NAME`
returns nothing, because the name is `scopes.name`. There is no duplicate row,
by design — duplicating them would double the cost the promotion exists to
avoid, and give two places for the same fact to disagree.

| UCIS property | Where to read it | Notes |
| ------------- | ---------------- | ----- |
| `UCIS_STR_SCOPE_NAME` | `scopes.name` | local name, not the path |
| `UCIS_STR_UNIQUE_ID` | `scopes.unique_id` | the primary key |
| `UCIS_STR_SCOPE_HIER_NAME` | *(derived)* | walk `parent_id`; not stored |
| `UCIS_STR_FILE_NAME` | `source_files.path` | via `source_file_id`, DU-local |
| `UCIS_INT_SCOPE_WEIGHT` | `scopes.weight` | promoted, bit 0 |
| `UCIS_INT_SCOPE_GOAL` | `scopes.goal` | promoted, bit 1 |
| `UCIS_INT_COVER_GOAL` | `coveritems.goal` | promoted, bit 2 |
| `UCIS_INT_TOGGLE_COVERED` | *(derived)* | promoted bit 3, **no column** — it is computed from the counts, so storing it in a definition table would make the definition disagree with the measurement after a merge |
| `UCIS_INT_SCOPE_SOURCE_TYPE` | `scopes.source` | promoted, bit 4 (a `ucisSourceT` value — see "The UCIS property vocabulary") |
| `UCIS_INT_IS_MODIFIED`, `_MODIFIED_SINCE_SIM`, `_NUM_TESTS` | manifest / *(derived)* | database-level, not per scope |
| `UCIS_REAL_CVG_INST_AVERAGE` | *(derived)* | computed from instance coverage |

History-node properties are promoted in the same way, in this order:
`TEST_STATUS`, `TEST_COMPULSORY`, `HIST_CMDLINE`, `HIST_RUNCWD`,
`TEST_USERNAME`, `COMMENT`, `VER_VENDOR_ID`, `VER_VENDOR_TOOL`,
`VER_VENDOR_VERSION`, `SIMTIME`, `CPUTIME`, `COST` — mapping onto the
`history_nodes` columns of the same name.

**Bit *i* of `promoted_props` is set iff the source backend actually answered
property *i***, in the orders above. Read the promoted column when the bit is
set, look for an EAV row when it is not, and raise when neither exists. Without
the mask a column cannot distinguish "the weight is 1" from "this backend does
not implement `SCOPE_WEIGHT`", and a reader would invent plausible answers where
the source raised. **Bit positions are part of the format: they MUST be appended only, and MUST NOT
be reordered.**

`assoc_tier` is what keeps `test_cover_assoc` small: only the SPARSE tier
materializes rows, so a scope whose every bin was hit by every test costs one
property row rather than one row per (test, bin) pair. A reader that ignores the
tier will conclude that an ALL-tier scope has no test associations at all, which
is the opposite of the truth.

## The UCIS property vocabulary
<!-- ucis-features: PI.*, PS.*, PR.*, PH.*, PP.*, SL.* -->

`properties` and `history_props` are keyed by `prop_id`, and for
`namespace = "ucis"` that key is **the UCIS property enum constant with its
`UCIS_INT_` / `UCIS_STR_` / `UCIS_REAL_` / `UCIS_HANDLE_` prefix removed** —
`SCOPE_WEIGHT`, `TOGGLE_CANON_NAME`, `CVG_ATLEAST`. A writer MUST use that
spelling and MUST NOT abbreviate, re-case or vendor-prefix it; a reader MUST
treat an unrecognized `prop_id` as data to preserve rather than an error.

The tables below are the whole standard key space, in UCIS 1.0's own typedef
order, with the one thing a reader actually needs: **where to read each
property from**. A property is in exactly one place. "`properties` row" means
the EAV table; a named column means the property is promoted and there is
**no** row (see "A promoted property is a column *instead of* a row"); *derived*
means the value is recomputed from structure or counts and is stored nowhere.

Properties are read through `ucis_Get{Int,Real,String,Handle}Property` and
written through the matching setters (PP.1–PP.8). The `coverindex` argument
selects the object: `-1` is the scope itself, `>= 0` is the cover at that index
within the scope, and it is ignored for history nodes (PP.9). Scope-level and
per-bin properties therefore land in different rows of `properties` —
`object_kind` is what distinguishes them.

### Integer properties (`ucisIntPropertyEnumT`)

| UCIS property | Applies to | Where to read it |
| ------------- | ---------- | ---------------- |
| `UCIS_INT_IS_MODIFIED` (PI.1) | db | *not stored* — see below |
| `UCIS_INT_MODIFIED_SINCE_SIM` (PI.2) | db | *not stored* — see below |
| `UCIS_INT_NUM_TESTS` (PI.3) | db | derived: count of `history_nodes` rows with `kind = 1` |
| `UCIS_INT_SCOPE_WEIGHT` (PI.4) | scope | `scopes.weight` (promoted, bit 0) |
| `UCIS_INT_SCOPE_GOAL` (PI.5) | scope | `scopes.goal` (promoted, bit 1) |
| `UCIS_INT_SCOPE_SOURCE_TYPE` (PI.6) | scope | `scopes.source` (promoted, bit 4) |
| `UCIS_INT_NUM_CROSSED_CVPS` (PI.7) | scope | derived (promoted, bit 5): count of `cross_operands` rows for the cross |
| `UCIS_INT_SCOPE_IS_UNDER_DU` (PI.8) | scope | `properties` row (also derivable by walking `parent_id`) |
| `UCIS_INT_SCOPE_IS_UNDER_COVERINSTANCE` (PI.9) | scope | `properties` row (also derivable by walking `parent_id`) |
| `UCIS_INT_SCOPE_NUM_COVERITEMS` (PI.10) | scope | derived: count of `coveritems` rows with that `scope_id` |
| `UCIS_INT_SCOPE_NUM_EXPR_TERMS` (PI.11) | scope | `properties` row |
| `UCIS_INT_TOGGLE_TYPE` (PI.12) | scope | `properties` row |
| `UCIS_INT_TOGGLE_DIR` (PI.13) | scope | `properties` row |
| `UCIS_INT_TOGGLE_COVERED` (PI.14) | scope | derived from `counts` (promoted bit 3, **no column**) |
| `UCIS_INT_BRANCH_HAS_ELSE` (PI.15) | scope | `properties` row; also `UCIS_IS_BR_ELSE` on the bin |
| `UCIS_INT_BRANCH_ISCASE` (PI.16) | scope | `properties` row |
| `UCIS_INT_COVER_GOAL` (PI.17) | cover | `coveritems.goal` (promoted, bit 2) |
| `UCIS_INT_COVER_LIMIT` (PI.18) | cover | `coveritems.limit` |
| `UCIS_INT_COVER_WEIGHT` (PI.19) | cover | `coveritems.weight` |
| `UCIS_INT_TEST_STATUS` (PI.20) | history | `history_nodes.status` (promoted, bit 0) |
| `UCIS_INT_TEST_COMPULSORY` (PI.21) | history | `history_nodes.compulsory` (promoted, bit 1) |
| `UCIS_INT_STMT_INDEX` (PI.22) | scope | `properties` row |
| `UCIS_INT_BRANCH_COUNT` (PI.23) | scope | `properties` row |
| `UCIS_INT_FSM_STATEVAL` (PI.24) | scope | `properties` row |
| `UCIS_INT_CVG_ATLEAST` (PI.25) | scope | `properties` row (per-bin `at_least` is `coveritems.at_least`) |
| `UCIS_INT_CVG_AUTOBINMAX` (PI.26) | scope | `properties` row |
| `UCIS_INT_CVG_DETECTOVERLAP` (PI.27) | scope | `properties` row |
| `UCIS_INT_CVG_NUMPRINTMISSING` (PI.28) | scope | `properties` row |
| `UCIS_INT_CVG_STROBE` (PI.29) | scope | `properties` row |
| `UCIS_INT_CVG_PERINSTANCE` (PI.30) | scope | `properties` row |
| `UCIS_INT_CVG_GETINSTCOV` (PI.31) | scope | `properties` row |
| `UCIS_INT_CVG_MERGEINSTANCES` (PI.32) | scope | `properties` row |
| `UCIS_INT_TOGGLE_METRIC` (PI.33) | scope | `properties` row |
| `UCIS_INT_SUPPRESS_MODIFIED` (PI.34) | db | *not stored* — see below |

### String properties (`ucisStringPropertyEnumT`)

| UCIS property | Applies to | Where to read it |
| ------------- | ---------- | ---------------- |
| `UCIS_STR_FILE_NAME` (PS.1) | db | the dataset directory itself; per-scope source files are `source_files.path` |
| `UCIS_STR_SCOPE_NAME` (PS.2) | scope | `scopes.name` |
| `UCIS_STR_SCOPE_HIER_NAME` (PS.3) | scope | derived: walk `parent_id` |
| `UCIS_STR_INSTANCE_DU_NAME` (PS.4) | scope | `scopes.instance_du_id` -> that DU's `name` |
| `UCIS_STR_UNIQUE_ID` (PS.5) | scope/cover | `scopes.unique_id` / `(scope_id, local_index)` |
| `UCIS_STR_VER_STANDARD` (PS.6) | db | the `properties` map of the `_covsight_parquet.json` manifest |
| `UCIS_STR_VER_STANDARD_VERSION` (PS.7) | db | manifest `properties` |
| `UCIS_STR_VER_VENDOR_ID` (PS.8) | db/history | `history_nodes.vendor_id` (promoted, bit 6) |
| `UCIS_STR_VER_VENDOR_TOOL` (PS.9) | db/history | `history_nodes.vendor_tool` (promoted, bit 7) |
| `UCIS_STR_VER_VENDOR_VERSION` (PS.10) | db/history | `history_nodes.vendor_tool_version` (promoted, bit 8) |
| `UCIS_STR_GENERIC` (PS.11) | any | `properties` row |
| `UCIS_STR_ITH_CROSSED_CVP_NAME` (PS.12) | scope | derived (promoted, bit 6): `cross_operands.name` at `operand_index = coverindex` |
| `UCIS_STR_HIST_CMDLINE` (PS.13) | history | `history_nodes.cmdline` (promoted, bit 2) |
| `UCIS_STR_HIST_RUNCWD` (PS.14) | history | `history_nodes.run_cwd` (promoted, bit 3) |
| `UCIS_STR_COMMENT` (PS.15) | any | `history_nodes.comment` (promoted, bit 5) for history nodes; `properties` row elsewhere |
| `UCIS_STR_TEST_TIMEUNIT` (PS.16) | history | `history_nodes.time_unit` |
| `UCIS_STR_TEST_DATE` (PS.17) | history | `history_nodes.date` or `date_int` -- exactly one |
| `UCIS_STR_TEST_SIMARGS` (PS.18) | history | `history_nodes.args` |
| `UCIS_STR_TEST_USERNAME` (PS.19) | history | `history_nodes.user_name` (promoted, bit 4) |
| `UCIS_STR_TEST_NAME` (PS.20) | history | `history_props` row |
| `UCIS_STR_TEST_SEED` (PS.21) | history | `history_nodes.seed` |
| `UCIS_STR_TEST_HOSTNAME` (PS.22) | history | **not carried** |
| `UCIS_STR_TEST_HOSTOS` (PS.23) | history | **not carried** |
| `UCIS_STR_EXPR_TERMS` (PS.24) | scope | `properties` row |
| `UCIS_STR_TOGGLE_CANON_NAME` (PS.25) | scope | `properties` row |
| `UCIS_STR_UNIQUE_ID_ALIAS` (PS.26) | scope/cover | `properties` row |
| `UCIS_STR_DESIGN_VERSION_ID` (PS.27) | scope | `properties` row |
| `UCIS_STR_DU_SIGNATURE` (PS.28) | scope | `properties` row |
| `UCIS_STR_HIST_TOOLCATEGORY` (PS.29) | history | `history_nodes.tool_category` |
| `UCIS_STR_HIST_LOG_NAME` (PS.30) | history | `history_nodes.logical_name` |
| `UCIS_STR_HIST_PHYS_NAME` (PS.31) | history | `history_nodes.physical_name` |

### Real properties (`ucisRealPropertyEnumT`)

| UCIS property | Applies to | Where to read it |
| ------------- | ---------- | ---------------- |
| `UCIS_REAL_HIST_CPUTIME` (PR.1) | history | `history_nodes.cpu_time` (promoted, bit 10) |
| `UCIS_REAL_TEST_SIMTIME` (PR.2) | history | `history_nodes.sim_time` (promoted, bit 9) |
| `UCIS_REAL_TEST_COST` (PR.3) | history | `history_nodes.cost` (promoted, bit 11) |
| `UCIS_REAL_CVG_INST_AVERAGE` (PR.4) | scope | derived from instance coverage |

### Handle properties (`ucisHandleEnumT`)

Handle properties are navigation, not payload: every one of them resolves to a foreign key that is already in the schema, so none becomes a `properties` row.

| UCIS property | Applies to | Where to read it |
| ------------- | ---------- | ---------------- |
| `UCIS_HANDLE_SCOPE_PARENT` (PH.1) | scope | `scopes.parent_id` |
| `UCIS_HANDLE_SCOPE_TOP` (PH.2) | scope | the `scopes` row whose `parent_id` is null, reached by walking `parent_id` |
| `UCIS_HANDLE_INSTANCE_DU` (PH.3) | scope | `scopes.instance_du_id` |
| `UCIS_HANDLE_HIST_NODE_PARENT` (PH.4) | history | `history_nodes.parent_id` |
| `UCIS_HANDLE_HIST_NODE_ROOT` (PH.5) | history | the `history_nodes` row whose `parent_id` is null |

### Source language (`ucisSourceT`)

`scopes.source` and `UCIS_INT_SCOPE_SOURCE_TYPE` (PI.6) carry a `ucisSourceT`
value. UCIS 1.0 numbers the enumerators consecutively from `UCIS_VHDL` = 0:
`UCIS_VHDL` 0, `UCIS_VLOG` 1, `UCIS_SV` 2, `UCIS_SYSTEMC` 3, `UCIS_PSL_VHDL` 4,
`UCIS_PSL_VLOG` 5, `UCIS_PSL_SV` 6, `UCIS_PSL_SYSTEMC` 7, `UCIS_E` 8,
`UCIS_VERA` 9, `UCIS_NONE` 10, `UCIS_OTHER` 11, `UCIS_SOURCE_ERROR` 12
(SL.1–SL.13). The value is normative in the specification, but the column that
holds it is ours, and a reader has to be told which one that is.

### Properties with no persistent form

`UCIS_INT_IS_MODIFIED`, `UCIS_INT_MODIFIED_SINCE_SIM` and
`UCIS_INT_SUPPRESS_MODIFIED` are in-memory state per UCIS 1.0 § 8.3 — they
describe an open database, not its contents, and are deliberately absent from
the dataset. A reader MUST answer them from its own session state rather than
looking for a stored value.

## Structural encoding by coverage type

The seven tables above say what the *columns* are. This section says what
*shape* to build in them for each kind of coverage — which is what an
implementation written from this document has to get right in order to
interoperate, and what the earlier drafts left unsaid.

**There is exactly one structural rule.** Every UCIS coverage construct —
covergroup, cross, FSM, toggle, assertion, branch, statement, condition,
expression — is a **scope subtree plus its coveritems**. No coverage type gets
its own table, its own columns, or its own encoding. `scopes.scope_type` and
`coveritems.cover_type` are the only discriminators, and anything that does not
fit in a promoted column becomes a row in `properties`.

That uniformity is the design, not an omission. It is what makes a new coverage
type additive (a new enum value, no schema change) and what lets one merge
implementation serve every construct. The consequence for an implementer is
that reading this section is mostly learning **which nesting shapes are legal**,
not learning eight different encodings.

Two rules apply throughout and are not repeated below:

- **Scope-level** UCIS typed properties are carried in `properties` with
  `object_kind = 0` and `namespace = "ucis"`, keyed by the property's enum name.
- **Per-bin** state lives in the `coveritems` row itself — `cover_type`,
  `flags`, `name`, `at_least`, `weight`, `goal`, `limit`, `bitlen` — and in
  `counts.count`. Per-bin *typed properties* are **not** carried; see
  "Structure this mapping does not carry" below.

### Covergroups, coverpoints and crosses
<!-- ucis-features: CG.1, CG.2, CG.3, CG.4, CG.5, CG.7, CG.8, S4.4, S4.5 -->

```
UCIS_INSTANCE                              (or UCIS_DU_* for a type-level group)
└── UCIS_COVERGROUP          "addr_cg"     the covergroup type
    ├── UCIS_COVERINSTANCE   "addr_cg#1"   per-instance specialization (optional)
    ├── UCIS_COVERPOINT      "addr"
    │   ├── UCIS_CVGBINSCOPE / UCIS_ILLEGALBINSCOPE / UCIS_IGNOREBINSCOPE
    │   │                                  optional grouping scopes; bins may
    │   │                                  also hang directly off the coverpoint
    │   └── coveritems: UCIS_CVGBIN, UCIS_IGNOREBIN, UCIS_ILLEGALBIN,
    │                   UCIS_DEFAULTBIN
    ├── UCIS_TRANSITION      "t1"          transition-bin container; the parent
    │                                      coverpoint carries
    │                                      UCIS_SCOPE_CVG_TRANSITION
    └── UCIS_CROSS           "addr_x_data"
        └── coveritems: UCIS_CVGBIN, one per cross bin
```

- **Bin classification is a `cover_type`, not a separate table.** An ignore bin
  and a hit bin are rows in `coveritems` distinguished by `cover_type`; a reader
  that filters on the type sees the same thing every other backend sees.
- **Coverpoint shape** (`UCIS_SCOPE_CVG_AUTO` / `_SCALAR` / `_VECTOR` /
  `_TRANSITION`) is in `scopes.flags`.
- **Per-bin markers** — `UCIS_IS_CROSSAUTO`, `UCIS_BIN_IFF_EXISTS`,
  `UCIS_BIN_SAMPLE_TRUE` — are in `coveritems.flags`.
- **Covergroup options** (`at_least`, `auto_bin_max`, `per_instance`,
  `detect_overlap`, the real-valued instance average) are `properties` rows on
  the covergroup or cover-instance scope.
- **A cross is an ordinary scope** of type `UCIS_CROSS` whose coveritems are
  `UCIS_CVGBIN`. Both `ucis_CreateCross` and `ucis_CreateCrossByName` produce
  exactly this shape; the mapping does not distinguish which constructor was
  used. **The crossed coverpoints are carried in `cross_operands`**, in cross
  order — a cross of `(addr, data)` and a cross of `(data, addr)` have the same
  operand set and different bin semantics, so the order is part of the data.

### FSM coverage
<!-- ucis-features: FS.1, FS.2, FS.3 -->

```
UCIS_FSM                     "state_fsm"   state-variable name in properties
                                           (UCIS_STR_FSM_STATEVAR)
├── UCIS_FSM_STATES
│   └── coveritems: UCIS_FSMBIN (= UCIS_STATEBIN), one per state
└── UCIS_FSM_TRANS
    └── coveritems: UCIS_FSMBIN (= UCIS_TRANSBIN), one per arc
```

States and transitions are **sibling child scopes** of the FSM scope, not two
kinds of thing in one scope. Both use `UCIS_FSMBIN`, so a reader distinguishes
them by the parent's `scope_type` rather than by the bin type — the one place
where the bin type alone is not enough. `UCIS_IS_FSM_RESET` and
`UCIS_IS_FSM_TRAN` are `coveritems.flags`.

Arc endpoints and per-state encoded values are not carried; see the gaps
section.

### Toggle coverage
<!-- ucis-features: TG.1, TG.2, TG.3, TG.4, X.2 -->

```
UCIS_TOGGLE                  "data_valid"
└── coveritems: UCIS_TOGGLEBIN, named for the transition ("0->1", "1->0")
```

The toggle's metric, type, direction and canonical name
(`UCIS_INT_TOGGLE_METRIC` / `_TYPE` / `_DIR`, `UCIS_STR_TOGGLE_CANON_NAME`) are
`properties` rows on the toggle scope, not columns. `UCIS_INT_TOGGLE_COVERED` is
deliberately **not** promoted to a column even though it is a hot property: it
is derived from the counts, so it is run-dependent and freezing it into a
definition table would make the definition disagree with the measurement after
a merge. Its `promoted_props` bit still records whether the source backend
answered it.

Bin names carry the transition, so a reader must not assume positional order:
NCDB's flat-bin ordering canonicalizes a toggle pair to `0->1`, `1->0` while
`coveritems.local_index` follows creation order. Cross-backend code joins on the
**name**, not the index. Vector toggles are a physical grouping in some backends
and a flat list here; the logical view must agree either way.

### Assertion and cover-property coverage
<!-- ucis-features: AS.1, AS.2, AS.4, AS.5, AS.6 -->

```
UCIS_ASSERT                  "a_req_ack"
└── coveritems: UCIS_PASSBIN, UCIS_FAILBIN, UCIS_VACUOUSBIN,
                UCIS_DISABLEDBIN, UCIS_ATTEMPTBIN, UCIS_ACTIVEBIN,
                UCIS_PEAKACTIVEBIN

UCIS_COVER                   "c_handshake"  cover property
└── coveritems: UCIS_COVERBIN, and UCIS_SCBIN for a sequence
```

An assertion's outcome is a **set of counter bins**, not a status field. That
matters for merge: the bins are ordinary rows and most of them sum, but
`UCIS_PEAKACTIVEBIN` takes `MAX` (a high-water mark summed over 64 runs is a
plausible-looking number that is simply false), and formal status merges by
precedence in the separate `formal` table. `UCIS_IS_IMMEDIATE_ASSERT` is a scope
flag.

### Statement, branch, condition and expression coverage
<!-- ucis-features: BR.1, SB.1, SB.2, CX.1 -->

```
UCIS_BLOCK                   per procedural block
└── coveritems: UCIS_STMTBIN per statement/line, UCIS_BLOCKBIN for the
                block-level aggregate

UCIS_BRANCH                  per branch point; UCIS_SCOPE_BLOCK_ISBRANCH on the
│                            enclosing block, UCIS_INT_BRANCH_HAS_ELSE and
│                            UCIS_INT_BRANCH_ISCASE as properties
└── coveritems: UCIS_BRANCHBIN per arm; UCIS_IS_BR_ELSE flags the else arm

UCIS_EXPR / UCIS_COND        per expression or condition
└── coveritems: UCIS_EXPRBIN / UCIS_CONDBIN, one per truth-table row
```

Code coverage is the least special case: the source location on the scope or
the bin (`source_file_id`, `source_line`, `source_col`, resolved through
`source_files`) is what a report joins on, and the bins are plain counters.
`UCIS_STR_EXPR_TERMS` and `UCIS_SCOPE_EXPR_ISHIERARCHICAL` are a property row
and a scope flag respectively.

### Tags and user attributes
<!-- ucis-features: T13.1, T13.2, T13.3, T13.4, T13.5, IT.8, IT.9, DB.4, X.11 -->

Both are **extension-namespace rows in `properties`**, following the "extensions
are data, not schema forks" rule. Neither gets a table.

| What | `object_kind` | `namespace` | `prop_id` | `prop_type` | value |
| ---- | ------------- | ----------- | --------- | ----------- | ----- |
| user attribute | scope / coveritem / history-node | `covsight` | `attr:<key>` | string | `str` |
| tag | scope / coveritem / history-node | `covsight` | `tag:<name>` | int | `i64 = 1` |

A tag is a set membership, so it is encoded as a **key with a constant value**
rather than a value to compare: `tag:rtl` present means tagged, absent means
not. `ucis_TaggedObjIterate` is then a predicate on `prop_id`, and
`ucis_ObjectTagsIterate` is a prefix scan of one object's rows — both are
ordinary columnar filters rather than a join against a tag table.

`object_kind` is what makes one EAV table serve every object kind, and it is
why attributes on a history node land in `history_props` rather than
`properties`: history nodes are run-scoped, and appending a run must not rewrite
a definition table.

A `UCIS_GROUP` scope used as a tag-group container is an ordinary scope; it
carries no special encoding.

### Structure this mapping does not carry

Named explicitly, because a specification that is silent about a gap is worse
than one that admits it — an implementer who writes these and finds them missing
after a round trip has no way to tell a bug from a boundary.

| Not carried | Consequence |
| ----------- | ----------- |
| **FSM arc endpoints** (`ucis_CreateNextTransition`, `ucis_GetFSMTransitionStates`) | Transition bins survive; their source and destination states do not. The object API raises `UnimplError` for the constructor, so nothing exercises this path today. |
| **Per-bin typed properties** (e.g. `UCIS_INT_FSM_STATEVAL`, `UCIS_INT_STMT_INDEX`) | Cover bins are not property-bearing objects in this API, so only the `covsight` attribute namespace reaches them. A per-bin UCIS integer property has no `ucis`-namespace representation. |
| **Host identity of a test run** (`UCIS_STR_TEST_HOSTNAME`, `UCIS_STR_TEST_HOSTOS`) | Neither reaches the dataset: there is no `history_nodes.host_name` column, and `host_os` exists but is always null because the object API exposes no accessor to fill it. A consumer reconstructing *where* a run executed has to fall back on the extension namespace. |
| **Net alias relations** (§ 4.10) | `UCIS_STR_TOGGLE_CANON_NAME` is carried as a property row, but whether aliased nets share counts or stay independent is undecided. |
| **A distinct "waiver" concept** | UCIS models exclusion, not waiver: `UCIS_EXCLUDE_PRAGMA`, `_FILE`, `_INST` and `_AUTO` record *why* something was excluded, and there is no separate signed-off-waiver notion to carry. Tools that distinguish the two must encode the difference themselves, in the extension namespace. |

Each of these has a registry entry and shows as unmapped in
[`ucis-parquet-feature-map.md`](ucis-parquet-feature-map.md); none is a silent
omission.

### Negative rules: input a reader must reject
<!-- ucis-features: N.1, N.2, N.3, N.6 -->

Conformance is not only about what a reader accepts. A writer that produces one
of the following and is not told so will ship it, and the corruption surfaces
later as a coverage number nobody can explain.

A conforming reader **must reject**, not repair or ignore:

1. **An unrecognized `scopes.scope_type`.** Treating it as a generic scope
   silently changes the shape of the tree, and every subsequent traversal is
   wrong in a way no checksum catches.
2. **A negative or non-integral `counts.count`.** There is no coverage
   interpretation of a negative hit count.
3. **A `UCIS_CROSS` or child scope whose `parent_id` does not resolve**, and
   more generally any dangling foreign key. Dangling references survive a naive
   round trip because nothing dereferences them until a query does.
4. **A history node of kind `UCIS_HISTORYNODE_MERGE` with no contributing
   nodes.** Provenance that claims a merge but names no inputs is worse than no
   provenance, because it reads as complete.

Two further rejections are specified elsewhere in this document: duplicate
`(run_id, coveritem_id)` measurement rows (see "Correctness rules") and a
`schema_version` newer than the reader understands (see "Neutrality rules").

## Identity & ordering rules
<!-- ucis-features: S4.10, S4.11, S4.12, S4.13, S4.14, S4.15, L1.10, IT.1, IT.2, IT.4, IT.5 -->

1. **`UCIS_STR_UNIQUE_ID` MUST be the primary key for scopes.** UCIS handles
   (`ucisScopeT`) are process-local pointers; the unique-id is the only stable
   identity that survives a round-trip, a merge, and a backend swap. Making it
   the PK is what lets a tool re-open a handle by identity.
2. **`coveritem_id` is a dense surrogate** assigned in definition order —
   never a row-autonumber, and (after measurement) **not a hash**. A digest of
   `(scope_unique_id, local_index)` is equally stable but *incompressible*: on a
   1.2M-bin design, hashed ids cost ~20 MB of a 24 MB dataset, because random
   integers defeat both delta encoding and the compressor. Dense ordinals cost
   almost nothing.
   Nothing is lost, because the surrogate only has to be stable across the
   writes and merges of *one definition set*, and definition equality is
   enforced before any merge. Identity that must outlive a definition change
   uses the natural key `(scope_id, local_index)` — stored as columns on
   `coveritems`, and exactly what the hash was computed from.
3. **Iteration order MUST be persisted explicitly.** Columnar tables are unordered sets;
   UCIS iteration order is defined. `scopes.dfs_ordinal` and
   `coveritems.local_index` reproduce `ScopeIterate`/`CoverIterate` canonical
   order without re-derivation.

## Extensions (UCIS+): data, not schema forks
<!-- ucis-features: DB.2, DB.6, X.9 -->

New coverage semantics MUST ship as **rows in `properties` under a non-`ucis`
namespace**, never as new engine-specific columns. Promote a property to a
first-class column only when it is hot enough to warrant it *and* portable.
A reader that does not recognise a namespace MUST preserve those rows; new
tools read them. This is the relational
analog of NCDB's "add members, don't rewrite" and the testplan format's
namespaced `custom{}`.

## Access mapping (iterators ↔ queries)
<!-- ucis-features: IT.1, IT.2, IT.4, IT.5, IT.6, IT.7 -->

- **Scope walk:** one ranged query per subtree ordered by `dfs_ordinal`, fed
  into a cursor; `ucis_NextScope` advances the cursor, not a new query.
- **Cover walk:** batch-fetch `coveritems` + `counts` for a scope (or a window
  of scopes) in one join; iterate in memory.
- **Property access:** hot props come from the promoted columns; a
  `ucis_GetIntProperty` for a long-tail key hits `properties` — prefetched per
  subtree, not per object.
- **Merge:** expressed as `GROUP BY coveritem_id SUM(count)` (or an engine
  feature such as ClickHouse `SummingMergeTree`), not row-by-row.

## Coverage merge

Merge is fundamentally `GROUP BY coveritem_id → SUM(count)` plus a set-union of
associations — the operation columnar engines are built for. The schema enables
this directly: **`counts` is a separate table keyed by `coveritem_id`**, so a
merge touches only the narrow measurement table while bin *definitions* (type,
goal, weight, name in `coveritems`/`scopes`) are carried through untouched.

### Runs, partitioning and append
<!-- ucis-features: X.8, X.14 -->

Measurement tables (`counts`, `test_cover_assoc`, `formal`, `history_nodes`,
`history_props`) carry a `run_id` and are partitioned by it. Definition tables
(`scopes`, `coveritems`, `source_files`, `properties`) do **not**: they describe
the design, which is run-independent.

That split is what makes append cheap and what makes it *correct*: adding a run
writes new partitions and must leave the definition tables byte-identical. If an
append rewrites a definition table, "append" is secretly a rewrite — the cost
model is wrong and any concurrent reader sees definitions move under it.

Both views must be coherent UCIS databases on their own: the merged view over a
set of runs, and the single-run view obtained by pruning to one partition. A
reader selecting one run reads that partition plus the shared definitions, and
nothing else.

### Three server-side merge models

1. **Virtual / query-time merge (primary).** Never physically merge. Runs are
   append-only (`counts` rows tagged with a `run_id`); the "merged DB" is a view:
   `SELECT coveritem_id, SUM(count) FROM counts WHERE run_id IN (…) GROUP BY
   coveritem_id`. Merges *any subset* (a regression, a milestone, N seeds) with
   no data rewrite; new runs are appends, not re-merges.
2. **Engine-native incremental.** Merge *is* ingestion — ClickHouse
   `SummingMergeTree`/`AggregatingMergeTree` sums duplicate keys on background
   compaction; you just `INSERT` each run. The additive hot-path becomes an
   automatic built-in. Read the exact merged value with `SUM … GROUP BY` /
   `FINAL` before compaction settles.
3. **Materialized snapshot.** `INSERT … SELECT … GROUP BY` or Iceberg
   `MERGE INTO` (DuckDB ≥1.5.3, Spark, Trino) to produce a physical running-total
   table — the server-side analog of NCDB's merged artifact, for when a
   downstream tool wants one merged snapshot.

The local fast-binary merge does not disappear — it **moves server-side** and
becomes either automatic (`SummingMergeTree`) or virtual (a `GROUP BY`), with no
data movement.

### Merge is type-aware — not everything is `SUM`
<!-- ucis-features: BT.19, AS.3, FM.1, X.1, X.3, X.4, X.5, X.10 -->

Keyed on `cover_type`:

| Object / column | Merge op |
| --------------- | -------- |
| toggle / line / branch / covergroup / FSM counts | `SUM` (or `MAX` / bitwise-`OR` in hit-bitmap / saturating mode) |
| assertion status (PASS/FAIL/ATTEMPT/…) | **precedence**, not sum (e.g. FAIL dominates) |
| goal / at_least / weight / name / type | **carried through, never merged** — must agree across runs; disagreement is a schema-drift error |
| test↔cover associations | set `UNION` (tiered ALL/NEVER stay flags) |
| exclusions / waivers | `UNION` |
| history nodes | append a merge node (the tree is just rows) |

Because definitions live in a separate table from `counts`, "carried through" is
automatic: merge `counts`, then join definitions back.

### Correctness rules
<!-- ucis-features: N.4, X.10 -->

- **Idempotency.** Measurements MUST be keyed on `(run_id, coveritem_id)` so
  re-loading a run cannot double-count.
- **Definition consistency.** Bins sharing a `unique_id` must agree on
  `cover_type`/`goal`/`weight`; a mismatch (tool-version schema drift) MUST be a merge
  error, not a silent pick.
- **Provenance preserved.** Query-time merge keeps every run — unlike
  destructive local merge — enabling per-test contribution, redundancy analysis,
  and per-testpoint coverage over data that is never thrown away.
- **Cost control.** Query-time scan grows with runs merged; bound it with
  partition pruning (by `run_id`/DU) and periodic materialized rollups
  (ClickHouse projections/MVs or an Iceberg `MERGE INTO` snapshot per milestone).

### Recommendation

Query-time (virtual) merge is the primary model — append-only runs, merge as a
`GROUP BY`, full provenance retained. Add materialized rollups for milestone
snapshots and to bound read cost. On ClickHouse, lean on `SummingMergeTree` so
additive merge is free at ingest.

## Read/write scope (decision)
<!-- ucis-features: L1.1, L1.2, L1.3 -->

- **Phase 1 — read backend (recommended first):** NCDB → Parquet → Iceberg /
  hosted DB. The adapter implements only the read side of the backend interface.
  Write path is a **batch loader** (buffer into columnar batches / Parquet
  files), not the streaming UCIS create API. Matches how coverage is actually
  consumed: build at the edge, query centrally.
- **Phase 2 — write parity (only if a tool must author into the DB):** implement
  `ucis_ScopeCreate`/`ucis_CreateNextCover` against the DB, buffering to Parquet
  — never row-by-row `INSERT`.

## Correctness bar

**UCIS → Parquet → UCIS MUST be lossless.** A golden round-trip test must
recover, for every object: scope type, flags, source info, all properties
(standard + UCIS+), counts, history tree, and test↔cover associations — the same
discipline NCDB round-trip tests enforce. Losslessness is what makes this a
faithful UCIS *backend* rather than a lossy export.

## Neutrality rules
<!-- ucis-features: V3.2, V3.3, V3.5, V3.6, DB.9, N.5 -->

Neutrality is at the **format** level, not the schema level — the schema is ours
to keep portable:

- Portable Parquet/Arrow types only: INT8/32/64, DOUBLE, STRING (BYTE_ARRAY),
  lists/structs. Engine-specific types MUST NOT appear (ClickHouse `LowCardinality`,
  Snowflake `VARIANT` semantics, BigQuery-only types). Dictionary encoding gives
  the low-cardinality benefit portably.
- Partitioning/sort keys are per-engine tuning and live in engine config, not in
  the logical schema.
- This schema is versioned (`schema_version`, in the manifest and in each
  table's key-value metadata). It MUST evolve by adding nullable columns or new
  namespaces, and a column MUST NOT be repurposed.

## Local & ephemeral testing (for tuning and benchmarks)

We want a disposable database for iteration — no server to stand up during
tuning. Options, lightest first:

1. **Raw Parquet, no catalog (tightest inner loop).** Write the tables above as
   Parquet into a `tmp_path` and query with DuckDB (`read_parquet`) or PyArrow.
   Zero services; perfect for codec/layout tuning where Iceberg table semantics
   don't matter yet.
2. **PyIceberg + SQLite catalog + tmp warehouse (ephemeral Iceberg).** A
   `SqlCatalog` backed by SQLite (file or `:memory:`) with a `file://` tmp
   warehouse gives real Iceberg tables with no server. Ideal for pytest fixtures
   (`tmp_path`), teardown is deleting the dir. SQLite catalog is single-writer —
   fine for tests, not concurrency benchmarks.
3. **`apache/iceberg-rest-fixture` Docker (REST catalog parity).** The official
   in-memory REST-catalog image on `:8181`; use when exercising the REST-catalog
   path (e.g. DuckDB's Iceberg *writes*, which require an attached REST catalog).
   For concurrent load use `jdbc:sqlite:file::memory:?cache=shared`.

Sequencing: tune on **(1)** for speed, validate Iceberg semantics on **(2)** in
CI, and reserve **(3)** for the write-path / REST-catalog integration tests.

See also the sources logged in the investigation notes accompanying this doc.

---

## Implementation notes

Where the built schema (`covsight.core.parquet`) differs from or extends this
draft. Each was forced by implementation or by measurement; see
[`adr/0003-parquet-backend.md`](adr/0003-parquet-backend.md).

### Tables added
<!-- ucis-features: FM.1, FM.2, FM.3, H11.8 -->

| Table | Why |
| ----- | --- |
| `history_props` | The history-node property long tail. Same EAV shape as `properties`, but **run-partitioned**: a history node belongs to a run, and appending a run must not rewrite a definition table. |
| `cross_operands` | The coverpoints a cross crosses, in cross order (schema 1.1). A table because the relation is one-to-many and ordered, which the EAV `properties` table cannot express: it is keyed by `prop_id` alone, so it held one operand and no index. |
| `formal` | Formal-verification results per assertion bin (`status`, `radius`, `witness`) — a UCIS+ extension NCDB already carries. Run-scoped, because a proof status comes *from* a run, and merging two statuses is a precedence decision rather than a sum. |

### Columns added
<!-- ucis-features: S4.2, DB.5 -->

* `scopes.source` — the `SourceT` language, needed to rebuild a scope faithfully.
* `scopes.instance_du_id` — the DU an INSTANCE instantiates, resolved after the
  walk because a DU may be serialized *after* an instance of it.
* `scopes.promoted_props`, `history_nodes.promoted_props` — see below.
* `coveritems.goal` / `limit` / `bitlen` — hot `CoverData` fields; promoting
  them is the sanctioned evolution path (nullable columns).
* `history_nodes.date` **and** `date_int` — exactly one populated. UCIS types the
  test date as a string property, but a backend may hold an epoch integer, and a
  type-sniffing heuristic eventually guesses wrong.
* `test_cover_assoc.count` — the contribution, not just the fact of one.

### Promoted properties need a support bitmask
<!-- ucis-features: DB.6, X.6 -->

A promoted column cannot express *"this backend does not support this
property"*. `weight = 1` and `getIntProperty(SCOPE_WEIGHT)` raising
`UnimplError` are different facts, and a lossless backend has to reproduce
both — otherwise it invents plausible answers where the source raises, which is
a quiet change to the data model.

Each promoted-property list is therefore paired with a `promoted_props`
bitmask: bit *i* is set iff the source backend actually answered property *i*.
Reading is then exact — promoted column when the bit is set, EAV row otherwise,
raise if neither. Bit positions are part of the format: append only, never
reorder.

### Physical encoding is part of the format

Identity columns are stored `DELTA_BINARY_PACKED`, not dictionary-encoded.
They are dense ascending integers; dictionary-encoding them stores every value
*plus* a dictionary, and measured **1.9 MB per run against 327 KB
delta-encoded** — more than the counts they identify. Whole-dataset effect at
zstd-19: 6.29 MB → 2.82 MB.

This is a Parquet encoding, not a format fork: same logical schema, same
portable types, no engine-specific anything. Encoding choices belong in this
document and should be reviewed like schema choices.

### Association tiers
<!-- ucis-features: HL.6, HL.7, X.7 -->

Only the SPARSE tier materializes rows, as specified. ALL and NEVER are
recorded as a per-scope `covsight:assoc_tier` property row rather than a schema
fork — consistent with "extensions are data, not schema forks".

### Merge is type-aware in practice

`counts` merges with `SUM` except for `PEAKACTIVEBIN`, which takes `MAX` — a
high-water mark summed over 64 runs is a plausible-looking number that is
simply false. Formal/assertion status merges by **precedence** (failure
dominates), with radius taking the maximum and the witness following the
winning status.

### Iceberg widens INT8
<!-- ucis-features: X.13 -->

Iceberg has no 8-bit integer, so `object_kind` and `prop_type` come back as
INT32. The reader conforms them to the declared schema, so an Iceberg round
trip compares equal rather than "equal except two column types".

### Merge speed: a near-tie, not a headline

The "local fast-binary merge moves server-side" framing above should not be read
as a performance claim. Measured on a 1.2M-bin design against NCDB's real merge
path (`NcdbMerger`, *not* `db_merger.py`): 0.64 s vs 1.03 s at four runs, 1.91 s
vs 1.63 s at sixteen. NCDB leads at small run counts, Parquet's vectorized
aggregation overtakes it around N≈8, and NCDB's numbers are its pure-Python
floor.

The query-time model earns its place on capability, not speed — merging writes
nothing, any subset merges at any time, and per-run counts survive so per-test
contribution and redundancy analysis remain answerable. See
[`ncdb-benchmark-results.md`](ncdb-benchmark-results.md).
