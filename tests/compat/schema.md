# Canonical JSON Schema for NCDB Dump Helpers

Version: `ncdb-dump-v1`

This schema defines the JSON format emitted by all dump helpers
(`ts_dump.mjs`, `c_dump`).  pytest compares documents in this format
to verify cross-implementation compatibility.

---

## Top-level document

```json
{
  "format": "ncdb-dump-v1",
  "scopes":  [ <scope>, ... ],
  "history": [ <history_node>, ... ]
}
```

- `format` — always the literal string `"ncdb-dump-v1"` (version guard)
- `scopes` — top-level scopes only; children are nested inside each scope
- `history` — history nodes in file order

---

## `<scope>`

```json
{
  "children":  [ <scope>, ... ],
  "items":     [ <item>, ... ],
  "name":      "string",
  "type":      integer
}
```

| Field | Type | Description |
|-------|------|-------------|
| `children` | array | Nested child scopes (recursive) |
| `items` | array | Cover items belonging to this scope |
| `name` | string | Logical scope name |
| `type` | integer | `ScopeTypeT` numeric value (e.g. `COVERGROUP = 0x800000`) |

Fields are always present (empty arrays when absent).  Field order is
**alphabetical** for deterministic diffs.

---

## `<item>`

```json
{
  "atLeast":   integer,
  "count":     integer,
  "coverType": integer,
  "name":      "string"
}
```

| Field | Type | Description |
|-------|------|-------------|
| `atLeast` | integer | Pass threshold (always present, even when default) |
| `count` | integer | Hit count (non-negative) |
| `coverType` | integer | `CoverTypeT` numeric value (e.g. `CVGBIN = 0x20000`) |
| `name` | string | Cover item name |

Fields are **alphabetical**.  `count` and `atLeast` are always emitted as
JSON numbers.  Values up to 2^53 are safe; test scenarios keep counts ≤ 10^6.

---

## `<history_node>`

```json
{
  "kind": "TEST",
  "name": "string"
}
```

| Field | Type | Description |
|-------|------|-------------|
| `kind` | string | One of `"TEST"`, `"MERGE"`, `"ACCUMULATED"`, `"GENERICBIN"` |
| `name` | string | Logical test name |

Fields are **alphabetical**.  Additional `TestData` fields (`userName`,
`seed`, `toolCategory`) are not yet included (tracked in
`docs/cross-impl-todo.md`).

---

## Rules

1. All integers are decimal (not hex, not scientific notation).
2. All strings are UTF-8.
3. Field order within objects is always alphabetical (enables `assert obj1 == obj2`
   without ordering normalization).
4. Arrays preserve the file-order of items (children, items, history nodes).
5. The `format` field must be checked first; a mismatch is a fatal error.

---

## Known `ScopeTypeT` values

| Name | Value (hex) | Value (dec) |
|------|-------------|-------------|
| `TOGGLE` | `0x00000001` | `1` |
| `BRANCH` | `0x00000002` | `2` |
| `INSTANCE` | `0x00000010` | `16` |
| `COVERGROUP` | `0x00001000` | `4096` |
| `COVERINSTANCE` | `0x00002000` | `8192` |
| `COVERPOINT` | `0x00004000` | `16384` |
| `CROSS` | `0x00008000` | `32768` |
| `FSM` | `0x00400000` | `4194304` |
| `DU_MODULE` | `0x01000000` | `16777216` |
| `DU_ARCH` | `0x02000000` | `33554432` |
| `FSM_STATES` | `0x20000000` | `536870912` |
| `FSM_TRANS` | `0x40000000` | `1073741824` |

## Known `CoverTypeT` values

| Name | Value (hex) | Value (dec) |
|------|-------------|-------------|
| `CVGBIN` | `0x00000001` | `1` |
| `BRANCHBIN` | `0x00000040` | `64` |
| `TOGGLEBIN` | `0x00000200` | `512` |
| `IGNOREBIN` | `0x00080000` | `524288` |
| `ILLEGALBIN` | `0x00100000` | `1048576` |
| `DEFAULTBIN` | `0x00200000` | `2097152` |
