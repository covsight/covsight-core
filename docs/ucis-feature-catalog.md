# UCIS 1.0 Feature Catalog

Comprehensive enumeration of every feature defined by the Accellera UCIS 1.0
specification (June 2012). Built to be a checklist against which the NCDB
data model and each language API (C / Python / TypeScript) can be scored.

Section references (§ x.y) point into
[`UCIS_Version_1.0_Final_June-2012.md`](../UCIS_Version_1.0_Final_June-2012.md).
Spec-mandated identifiers (function names, enum tokens, bit values) are
reproduced verbatim — they are normative and must match across all impls.

Scoring columns are intentionally empty here; this file is *the catalog* — the
human-readable narrative. Two other files derive from it:

- **The registry**, [`docs/conformance/features/`](conformance/features/), is
  authoritative for the **ID key space**. Every ID in the tables below has an
  entry there recording what each backend is expected to do with it.
  `tools/sync_catalog.py --check` fails CI if the two drift apart.
- **The matrix**, [`docs/ucis-parquet-feature-map.md`](ucis-parquet-feature-map.md),
  is generated: it joins the registry to tagged tests and to the mapping
  document's claims. Never hand-edited.

Adding a row here means adding a registry entry in the same commit. See
[`docs/conformance/README.md`](conformance/README.md).

---

## 1. Database lifecycle and I/O  (§ 8.1)

| ID    | Feature                                                                       | Spec ref |
| ----- | ----------------------------------------------------------------------------- | -------- |
| L1.1  | `ucis_Open(name)` — in-memory DB, optionally populated from a file            | § 8.1.5  |
| L1.2  | `ucis_Close(db)` — invalidate handle, release memory                          | § 8.1.4  |
| L1.3  | `ucis_Write(db, file, scope, recurse, covertype)` — persist subset to file    | § 8.1.13 |
| L1.4  | `ucis_OpenFromInterchangeFormat(file)` — load from XML interchange            | § 8.1.6  |
| L1.5  | `ucis_WriteToInterchangeFormat(db, file)` — emit XML interchange              | § 8.1.14 |
| L1.6  | `ucis_OpenReadStream(name)` — read-streaming open                             | § 8.1.9  |
| L1.7  | `ucis_OpenWriteStream(name)` — write-streaming open                           | § 8.1.10 |
| L1.8  | `ucis_WriteStream(db)` — flush in-flight object in write-streaming            | § 8.1.11 |
| L1.9  | `ucis_WriteStreamScope(db)` — flush current scope and pop to parent           | § 8.1.12 |
| L1.10 | `ucis_GetPathSeparator(db)` / `ucis_SetPathSeparator(db, sep)`                | § 8.1.7–8 |

**Streaming-mode semantics (§ 3.2.1, § 4.5):** read-streaming serves objects
in document order without random access; write-streaming requires objects to
be emitted in valid parent→child order and uses the `WriteStream*` flush
calls to commit each step. In-memory mode is the random-access alternative.

---

## 2. Error handling  (§ 8.2)

| ID    | Feature                                                          | Spec ref |
| ----- | ---------------------------------------------------------------- | -------- |
| E2.1  | `ucis_RegisterErrorHandler(handler, userdata)`                   | § 8.2.1  |
| E2.2  | `ucisErrorT { msgno, severity, msgstr }`                         | § 8.2    |
| E2.3  | Severity enum: `INFO`, `WARNING`, `ERROR`                        | § 8.2    |

---

## 3. Versioning  (§ 7, § 8.18)

| ID    | Feature                                                                 | Spec ref |
| ----- | ----------------------------------------------------------------------- | -------- |
| V3.1  | `ucis_GetAPIVersion()` — version of the linked API library              | § 8.18.1 |
| V3.2  | `ucis_GetDBVersion(db)` — version of the in-memory DB schema            | § 8.18.2 |
| V3.3  | `ucis_GetFileVersion(file)` — version recorded in a file                | § 8.18.3 |
| V3.4  | `ucis_GetHistoryNodeVersion(node)` — version a history node was written under | § 8.18.4 |
| V3.5  | `ucis_GetVersionStringProperty(versionH, prop)`                         | § 8.18.5 |
| V3.6  | Version string properties: `UCIS_STR_VER_STANDARD`, `VER_STANDARD_VERSION`, `VER_VENDOR_ID`, `VER_VENDOR_TOOL`, `VER_VENDOR_VERSION` | § 7 |

---

## 4. Scope creation and management  (§ 8.5)

### 4.1 Constructors

| ID    | Feature                                                                       | Spec ref |
| ----- | ----------------------------------------------------------------------------- | -------- |
| S4.1  | `ucis_CreateScope(db, parent, name, srcinfo, weight, source, type, flags)`     | § 8.5.2  |
| S4.2  | `ucis_CreateInstance(db, parent, name, srcinfo, weight, source, type, du, flags)` — DU-linked instance | § 8.5.9 |
| S4.3  | `ucis_CreateInstanceByName(...du_name, flags)` — write-streaming variant by name | § 8.5.10 |
| S4.4  | `ucis_CreateCross(db, parent, name, srcinfo, weight, source, n, points[])`     | § 8.5.7  |
| S4.5  | `ucis_CreateCrossByName(...n, point_names[])` — write-streaming variant         | § 8.5.8  |
| S4.6  | `ucis_CreateNextTransition(db, parent, name, srcinfo, weight, src_state, dst_state)` — FSM arc | § 8.5.11 |
| S4.7  | `ucis_RemoveScope(db, scope)`                                                   | § 8.5.3  |

(Note: toggle scopes are created via `ucis_CreateScope(type=UCIS_TOGGLE)`
and configured via integer/string properties; some impls also expose a
`ucis_CreateToggle` convenience constructor that bundles those calls.)

### 4.2 DU-name composition / DU lookup

| ID    | Feature                                                  | Spec ref |
| ----- | -------------------------------------------------------- | -------- |
| S4.8  | `ucis_ComposeDUName(lib, primary, secondary)` → string   | § 8.5.5  |
| S4.9  | `ucis_ParseDUName(name, &lib, &primary, &secondary)`     | § 8.5.6  |
| S4.10 | `ucis_MatchDU(db, name)` → DU scope                      | § 8.5.17 |

### 4.3 Unique-ID matching  (§ 5.5, § 8.5)

| ID    | Feature                                                          | Spec ref |
| ----- | ---------------------------------------------------------------- | -------- |
| S4.11 | `ucis_MatchScopeByUniqueID(db, uid_list)` — case-insensitive     | § 8.5.13 |
| S4.12 | `ucis_CaseAwareMatchScopeByUniqueID(db, uid_list)`               | § 8.5.14 |
| S4.13 | `ucis_MatchCoverByUniqueID(db, uid_list)`                        | § 8.5.15 |
| S4.14 | `ucis_CaseAwareMatchCoverByUniqueID(db, uid_list)`               | § 8.5.16 |
| S4.15 | `UCIS_STR_UNIQUE_ID` / `UCIS_STR_UNIQUE_ID_ALIAS` string properties (computation per § 5.4) | § 5.4 |

### 4.4 Source-info and type queries

| ID    | Feature                                                          | Spec ref |
| ----- | ---------------------------------------------------------------- | -------- |
| S4.16 | `ucis_SetScopeSourceInfo` / `ucis_GetScopeSourceInfo`            | § 8.5.19, 8.5.24 |
| S4.17 | `ucis_GetScopeType(db, scope)` → `ucisScopeTypeT`                | § 8.5.25 |
| S4.18 | `ucis_GetObjType(db, obj)` → scope or history-node kind          | § 8.5.26 |
| S4.19 | `ucis_GetIthCrossedCvp(db, cross, i)` → coverpoint scope          | § 8.5.18 |

### 4.5 Callback traversal

| ID    | Feature                                                          | Spec ref |
| ----- | ---------------------------------------------------------------- | -------- |
| S4.20 | `ucis_CallBack(db, scope, mask, kind, fn, userdata)` — scope-callback walk | § 8.5.4 |

### 4.6 Scope-type bits (one-hot, normative values)  (§ 6, header reproduced in [`c/ucis/include/ucis.h:200`](../c/ucis/include/ucis.h#L200))

| ID    | Token                          | Notes / spec section |
| ----- | ------------------------------ | -------------------- |
| ST.1  | `UCIS_TOGGLE`                  | § 6.5.5 |
| ST.2  | `UCIS_BRANCH`                  | § 6.5.2 |
| ST.3  | `UCIS_EXPR`                    | § 6.5.4 |
| ST.4  | `UCIS_COND`                    | § 6.5.4 |
| ST.5  | `UCIS_INSTANCE`                | § 6.3 — HDL instance scope, requires DU link |
| ST.6  | `UCIS_PROCESS`                 | § 6.3.5 |
| ST.7  | `UCIS_BLOCK`                   | § 6.5.3 — statement/block coverage container |
| ST.8  | `UCIS_FUNCTION`                | § 6.3.5 |
| ST.9  | `UCIS_FORKJOIN`                | § 6.3.5 |
| ST.10 | `UCIS_GENERATE`                | § 6.3.5 |
| ST.11 | `UCIS_GENERIC`                 | § 6.3.5 |
| ST.12 | `UCIS_CLASS`                   | § 6.3.5 |
| ST.13 | `UCIS_COVERGROUP`              | § 6.4.2 — covergroup type |
| ST.14 | `UCIS_COVERINSTANCE`           | § 6.4.2 — per-instance specialization of a covergroup |
| ST.15 | `UCIS_COVERPOINT`              | § 6.4.2 |
| ST.16 | `UCIS_CROSS`                   | § 6.4.2 |
| ST.17 | `UCIS_COVER`                   | § 6.6.1 — SVA/PSL cover-property container |
| ST.18 | `UCIS_ASSERT`                  | § 6.6.2 — SVA/PSL assert container |
| ST.19 | `UCIS_PROGRAM`                 | § 6.3.5 |
| ST.20 | `UCIS_PACKAGE`                 | § 6.3.5 |
| ST.21 | `UCIS_TASK`                    | § 6.3.5 |
| ST.22 | `UCIS_INTERFACE`               | § 6.3.5 |
| ST.23 | `UCIS_FSM`                     | § 6.5.6 |
| ST.24 | ~~`UCIS_TESTPLAN`~~            | **Not in UCIS 1.0** — no such `ucisScopeTypeT` value. Testplan linkage is covsight's own (`docs/testplan-schema.md`) |
| ST.25 | `UCIS_DU_MODULE`               | § 6.3.3 — Verilog/SV module DU |
| ST.26 | `UCIS_DU_ARCH`                 | § 6.3.3 — VHDL architecture |
| ST.27 | `UCIS_DU_PACKAGE`              | § 6.3.3 |
| ST.28 | `UCIS_DU_PROGRAM`              | § 6.3.3 |
| ST.29 | `UCIS_DU_INTERFACE`            | § 6.3.3 |
| ST.30 | `UCIS_FSM_STATES`              | § 6.5.6 — child of FSM holding STATEBINs |
| ST.31 | `UCIS_FSM_TRANS`               | § 6.5.6 — child of FSM holding TRANSBINs |
| ST.32 | `UCIS_COVBLOCK`                | Block sub-bucket for cover statements |
| ST.33 | `UCIS_CVGBINSCOPE`             | § 6.4.2 — coverpoint bin scope |
| ST.34 | `UCIS_ILLEGALBINSCOPE`         | § 6.4.2 — coverpoint illegal-bin scope |
| ST.35 | `UCIS_IGNOREBINSCOPE`          | § 6.4.2 — coverpoint ignore-bin scope |
| ST.36 | ~~`UCIS_BBLOCKSCOPE`~~         | **Misnamed** — the spec's basic-block scope is `UCIS_BBLOCK` (§ 6.5.3), the same value ST.32 records as `UCIS_COVBLOCK`. Duplicate of ST.32 |
| ST.37 | ~~`UCIS_GROUP`~~               | **Not in UCIS 1.0** — no such `ucisScopeTypeT` value. Tags attach to objects directly (§ 8.16) |
| ST.38 | ~~`UCIS_TRANSITION`~~          | **Not in UCIS 1.0** — no such `ucisScopeTypeT` value. Transition bins hang off the coverpoint, flagged `UCIS_SCOPE_CVG_TRANSITION` |
| ST.39 | `UCIS_RESERVEDSCOPE`           | Vendor-reserved high byte |

**Aggregate masks** (also normative, used for `ScopeIterate` and
`ucis_CallBack`): `UCIS_FSM_SCOPE`, `UCIS_CODE_COV_SCOPE`, `UCIS_DU_ANY`,
`UCIS_CVG_SCOPE`, `UCIS_FUNC_COV_SCOPE`, `UCIS_COV_SCOPE`,
`UCIS_VERIF_SCOPE`, `UCIS_HDL_SUBSCOPE`, `UCIS_HDL_INST_SCOPE`,
`UCIS_HDL_DU_SCOPE`, `UCIS_HDL_SCOPE`, `UCIS_NO_SCOPES`, `UCIS_ALL_SCOPES`.

### 4.7 Scope flags  (§ 6, header reproduced in [`c/ucis/include/ucis.h:289`](../c/ucis/include/ucis.h#L289))

| ID    | Flag                                | Meaning |
| ----- | ----------------------------------- | ------- |
| SF.1  | `UCIS_INST_ONCE`                    | Instance contents emitted once; cross-link other instances |
| SF.2  | `UCIS_ENABLED_STMT`                 | Statement coverage enabled |
| SF.3  | `UCIS_ENABLED_BRANCH`               | Branch coverage enabled |
| SF.4  | `UCIS_ENABLED_COND`                 | Condition coverage enabled |
| SF.5  | `UCIS_ENABLED_EXPR`                 | Expression coverage enabled |
| SF.6  | `UCIS_ENABLED_FSM`                  | FSM coverage enabled |
| SF.7  | `UCIS_ENABLED_TOGGLE`               | Toggle coverage enabled |
| SF.8  | `UCIS_ENABLED_BLOCK`                | Block coverage enabled |
| SF.9  | `UCIS_SCOPE_UNDER_DU`               | Scope is the direct child of a DU |
| SF.10 | `UCIS_SCOPE_EXCLUDED`               | Excluded |
| SF.11 | `UCIS_SCOPE_PRAGMA_EXCLUDED`        | Excluded by source-pragma |
| SF.12 | `UCIS_SCOPE_PRAGMA_CLEARED`         | Pragma-cleared |
| SF.13 | `UCIS_SCOPE_SPECIALIZED`            | Specialization of a generic covergroup/class |
| SF.14 | `UCIS_UOR_SAFE_SCOPE`               | Universal-Object-Recognition safe (§ 6.1.1) |
| SF.15 | `UCIS_UOR_SAFE_SCOPE_ALLCOVERS`     | All children also UOR-safe |
| SF.16 | `UCIS_IS_TOP_NODE`                  | Top-level node of the hierarchy |
| SF.17 | `UCIS_IS_IMMEDIATE_ASSERT`          | Immediate-assertion semantics |
| SF.18 | `UCIS_SCOPE_CVG_AUTO`               | Auto-bin covergroup |
| SF.19 | `UCIS_SCOPE_CVG_SCALAR`             | Scalar coverpoint |
| SF.20 | `UCIS_SCOPE_CVG_VECTOR`             | Vector coverpoint |
| SF.21 | `UCIS_SCOPE_CVG_TRANSITION`         | Transition coverpoint |
| SF.22 | `UCIS_SCOPE_IFF_EXISTS`             | Coverpoint has `iff` guard |
| SF.23 | `UCIS_SCOPE_SAMPLE_TRUE`            | Coverpoint sampled on true |
| SF.24 | `UCIS_SCOPE_BLOCK_ISBRANCH`         | Block is the implicit-branch container |
| SF.25 | `UCIS_SCOPE_EXPR_ISHIERARCHICAL`    | Expression decomposes hierarchically |
| SF.26 | `UCIS_SCOPEFLAG_MARK` / `UCIS_SCOPE_INTERNAL` | Tool-private mark bits |

Flag get/set: `ucis_GetScopeFlags`, `ucis_SetScopeFlags`,
`ucis_SetScopeFlag(mask, bitvalue)` (§ 8.5.20–23).

---

## 5. Source language enum  (`ucisSourceT`)

The value of `UCIS_INT_SCOPE_SOURCE_TYPE` (PI.6) and of the `source`
argument to the scope constructors (§ 8.5). The enumerators are
consecutive from `UCIS_VHDL` = 0 in the order below, so the numbering is
part of the interchange and a stored value is meaningless without it.

| ID | Token | Value | Meaning |
| --- | --- | ---: | --- |
| SL.1 | `UCIS_VHDL` | 0 | VHDL |
| SL.2 | `UCIS_VLOG` | 1 | Verilog |
| SL.3 | `UCIS_SV` | 2 | SystemVerilog |
| SL.4 | `UCIS_SYSTEMC` | 3 | SystemC |
| SL.5 | `UCIS_PSL_VHDL` | 4 | assert/cover in PSL VHDL |
| SL.6 | `UCIS_PSL_VLOG` | 5 | assert/cover in PSL Verilog |
| SL.7 | `UCIS_PSL_SV` | 6 | assert/cover in PSL SystemVerilog |
| SL.8 | `UCIS_PSL_SYSTEMC` | 7 | assert/cover in PSL SystemC |
| SL.9 | `UCIS_E` | 8 | e |
| SL.10 | `UCIS_VERA` | 9 | Vera |
| SL.11 | `UCIS_NONE` | 10 | not important / unspecified |
| SL.12 | `UCIS_OTHER` | 11 | user-defined; also used for user-defined attributes |
| SL.13 | `UCIS_SOURCE_ERROR` | 12 | error cases |

---

## 6. Coveritems  (§ 8.11)

### 6.1 Create / modify / read

| ID    | Feature                                                                            | Spec ref |
| ----- | ---------------------------------------------------------------------------------- | -------- |
| C6.1  | `ucis_CreateNextCover(db, parent, name, data, srcinfo)` → cover index              | § 8.11.1 |
| C6.2  | `ucis_RemoveCover(db, parent, coverindex)`                                          | § 8.11.2 |
| C6.3  | `ucis_GetCoverData(db, parent, coverindex, &name, &data, &srcinfo)`                | § 8.11.3 |
| C6.4  | `ucis_SetCoverData(db, parent, coverindex, data)`                                   | § 8.11.4 |
| C6.5  | `ucis_IncrementCover(db, parent, coverindex, delta)` — incremental update           | § 8.11.5 |
| C6.6  | `ucis_GetCoverFlag` / `ucis_SetCoverFlag` / `ucis_GetCoverFlags`                    | § 8.11.6–8 |

### 6.2 `ucisCoverDataT` value union

`int32` (if `UCIS_IS_32BIT`), `int64` (if `UCIS_IS_64BIT`),
`bytevector + bitlen` (if `UCIS_IS_VECTOR`).

### 6.3 Cover (bin) types  (one-hot bits — § 6, header at [`c/ucis/include/ucis.h:254`](../c/ucis/include/ucis.h#L254))

| ID    | Token              | Used for |
| ----- | ------------------ | -------- |
| BT.1  | `UCIS_CVGBIN`      | Covergroup coverpoint/cross bin (§ 6.4.2) |
| BT.2  | `UCIS_COVERBIN`    | SVA/PSL cover hit  (§ 6.6.1) |
| BT.3  | `UCIS_ASSERTBIN`   | Generic assertion bin (§ 6.6.2) |
| BT.4  | `UCIS_SCBIN`       | Sequence-cover bin |
| BT.5  | ~~`UCIS_ZINBIN`~~  | **Not in UCIS 1.0** — no such `ucisCoverTypeT` value |
| BT.6  | `UCIS_STMTBIN`     | Statement coverage (§ 6.5.3) |
| BT.7  | `UCIS_BRANCHBIN`   | Branch coverage (§ 6.5.2) |
| BT.8  | `UCIS_EXPRBIN`     | Expression-truth-table row (§ 6.5.4) |
| BT.9  | `UCIS_CONDBIN`     | Condition coverage (§ 6.5.4) |
| BT.10 | `UCIS_TOGGLEBIN`   | Toggle direction (§ 6.5.5) |
| BT.11 | `UCIS_FSMBIN` (= `UCIS_STATEBIN` / `UCIS_TRANSBIN`) | FSM state or arc (§ 6.5.6) |
| BT.12 | `UCIS_USERBIN` (= `UCIS_GENERICBIN`) | User-defined bin |
| BT.13 | `UCIS_PASSBIN`     | Assertion pass counter |
| BT.14 | `UCIS_FAILBIN`     | Assertion fail counter |
| BT.15 | `UCIS_VACUOUSBIN`  | Assertion vacuous-pass counter |
| BT.16 | `UCIS_DISABLEDBIN` | Assertion disabled counter |
| BT.17 | `UCIS_ATTEMPTBIN`  | Assertion attempt counter |
| BT.18 | `UCIS_ACTIVEBIN`   | Assertion active counter |
| BT.19 | `UCIS_PEAKACTIVEBIN` | Assertion peak-active counter |
| BT.20 | `UCIS_IGNOREBIN`   | Ignore-bin (covergroup) |
| BT.21 | `UCIS_ILLEGALBIN`  | Illegal-bin (covergroup) |
| BT.22 | `UCIS_DEFAULTBIN`  | Default-bin (covergroup) |
| BT.23 | `UCIS_BLOCKBIN`    | Block-level rollup bin |
| BT.24 | `UCIS_COUNT`       | Generic counter |
| BT.25 | `UCIS_USERBITS`    | Reserved bit-range for vendor extensions |
| BT.26 | `UCIS_RESERVEDBIN` | High-byte reserved range |

### 6.4 Coveritem flags  (header at [`c/ucis/include/ucis.h:319`](../c/ucis/include/ucis.h#L319))

`UCIS_IS_32BIT`, `UCIS_IS_64BIT`, `UCIS_IS_VECTOR`, `UCIS_HAS_GOAL`,
`UCIS_HAS_WEIGHT`, `UCIS_HAS_LIMIT`, `UCIS_HAS_COUNT`, `UCIS_HAS_ACTION`,
`UCIS_EXCLUDE_PRAGMA`, `UCIS_EXCLUDE_FILE`, `UCIS_EXCLUDE_INST`,
`UCIS_EXCLUDE_AUTO` (and the aggregate `UCIS_EXCLUDED`), `UCIS_ENABLED`,
`UCIS_IS_COVERED`, `UCIS_UOR_SAFE_COVERITEM`, `UCIS_CLEAR_PRAGMA`,
`UCIS_IS_TLW_ENABLED`, `UCIS_LOG_ON`, `UCIS_IS_EOS_NOTE`,
`UCIS_IS_FSM_RESET`, `UCIS_IS_FSM_TRAN`, `UCIS_IS_BR_ELSE`,
`UCIS_BIN_IFF_EXISTS`, `UCIS_BIN_SAMPLE_TRUE`, `UCIS_IS_CROSSAUTO`.

### 6.5 Source-info on coveritems

`ucis_GetCoverSourceInfo(db, parent, coverindex, &srcinfo)` mirrors the
scope-level srcinfo accessor.

---

## 7. Iteration / traversal  (§ 8.6–8.10)

| ID    | Feature                                                     | Spec ref |
| ----- | ----------------------------------------------------------- | -------- |
| IT.1  | `ucis_ScopeIterate(db, parent, mask)` → iterator             | § 8.6.1 |
| IT.2  | `ucis_ScopeScan(db, iter)` → next scope                      | § 8.6.2 |
| IT.3  | `ucis_FreeIterator(db, iter)`                                | § 8.6.3 |
| IT.4  | `ucis_CoverIterate(db, scope, covermask)`                    | § 8.7.1 |
| IT.5  | `ucis_CoverScan(db, iter, &index)`                           | § 8.7.2 |
| IT.6  | `ucis_HistoryIterate(db, parent, kind)`                      | § 8.8.1 |
| IT.7  | `ucis_HistoryScan(db, iter)`                                 | § 8.8.2 |
| IT.8  | `ucis_TaggedObjIterate(db, tag)` / `TaggedObjScan`           | § 8.9   |
| IT.9  | `ucis_ObjectTagsIterate(db, obj)` / `ObjectTagsScan`         | § 8.10  |

**Scope-mask category bits** (modifier on a scope-type mask, § 6):
`UCIS_SCOPEMASK_GENERAL`, `UCIS_SCOPEMASK_TYPED`, `UCIS_SCOPEMASK_MARK`,
`UCIS_SCOPEMASK_USER`.

---

## 8. Properties  (§ 8.3)

Polymorphic typed get/set on (DB | scope | cover | history-node), keyed by
enum constant. Coverindex semantics: `-1` ⇒ the scope, `>=0` ⇒ the cover at
that index, ignored for history nodes.

The four enums below are given in normative typedef order. Entries past the
end of a typedef are marked as such: UCIS 1.0 references them normatively in
prose or in an example without listing them in the enumeration, and one
catalog entry per enum named a token the specification does not define at
all.

### 8.1 Integer properties (`ucisIntPropertyEnumT`)

| ID | Property | Notes |
| --- | --- | --- |
| PI.1 | `UCIS_INT_IS_MODIFIED` | modified since the stored UCISDB was opened (read-only) |
| PI.2 | `UCIS_INT_MODIFIED_SINCE_SIM` | modified since the end of the simulation run (read-only) |
| PI.3 | `UCIS_INT_NUM_TESTS` | number of UCIS_HISTORYNODE_TEST nodes (read-only) |
| PI.4 | `UCIS_INT_SCOPE_WEIGHT` | scope weight |
| PI.5 | `UCIS_INT_SCOPE_GOAL` | scope goal |
| PI.6 | `UCIS_INT_SCOPE_SOURCE_TYPE` | source language of the scope (`ucisSourceT`, SL.*) |
| PI.7 | `UCIS_INT_NUM_CROSSED_CVPS` | number of coverpoints in a cross (read-only) |
| PI.8 | `UCIS_INT_SCOPE_IS_UNDER_DU` | scope is underneath a design-unit scope (read-only) |
| PI.9 | `UCIS_INT_SCOPE_IS_UNDER_COVERINSTANCE` | scope is underneath a covergroup instance (read-only) |
| PI.10 | `UCIS_INT_SCOPE_NUM_COVERITEMS` | number of coveritems under the scope (read-only) |
| PI.11 | `UCIS_INT_SCOPE_NUM_EXPR_TERMS` | number of '#'-delimited expression terms |
| PI.12 | `UCIS_INT_TOGGLE_TYPE` | toggle type (`ucisToggleTypeT`) |
| PI.13 | `UCIS_INT_TOGGLE_DIR` | toggle direction (`ucisToggleDirT`) |
| PI.14 | `UCIS_INT_TOGGLE_COVERED` | the toggle object is covered |
| PI.15 | `UCIS_INT_BRANCH_HAS_ELSE` | branch has an 'else' coveritem |
| PI.16 | `UCIS_INT_BRANCH_ISCASE` | branch represents a 'case' statement |
| PI.17 | `UCIS_INT_COVER_GOAL` | coveritem goal |
| PI.18 | `UCIS_INT_COVER_LIMIT` | coverage count limit for the coveritem |
| PI.19 | `UCIS_INT_COVER_WEIGHT` | coveritem weight |
| PI.20 | `UCIS_INT_TEST_STATUS` | test run status (`ucisTestStatusT`) |
| PI.21 | `UCIS_INT_TEST_COMPULSORY` | test run is compulsory |
| PI.22 | `UCIS_INT_STMT_INDEX` | index of the statement on a line |
| PI.23 | `UCIS_INT_BRANCH_COUNT` | total branch execution count |
| PI.24 | `UCIS_INT_FSM_STATEVAL` | FSM state value |
| PI.25 | `UCIS_INT_CVG_ATLEAST` | covergroup `at_least` option |
| PI.26 | `UCIS_INT_CVG_AUTOBINMAX` | covergroup `auto_bin_max` option |
| PI.27 | `UCIS_INT_CVG_DETECTOVERLAP` | covergroup `detect_overlap` option |
| PI.28 | `UCIS_INT_CVG_NUMPRINTMISSING` | covergroup `cross_num_print_missing` option |
| PI.29 | `UCIS_INT_CVG_STROBE` | covergroup `strobe` option |
| PI.30 | `UCIS_INT_CVG_PERINSTANCE` | covergroup `per_instance` option |
| PI.31 | `UCIS_INT_CVG_GETINSTCOV` | covergroup `get_inst_coverage` option |
| PI.32 | `UCIS_INT_CVG_MERGEINSTANCES` | covergroup `merge_instances` option |
| PI.33 | `UCIS_INT_TOGGLE_METRIC` | toggle metric specialization (`ucisToggleMetricT`) |
| PI.34 | `UCIS_INT_SUPPRESS_MODIFIED` | suppress the in-memory modification flag |
| PI.35 | ~~UCIS_INT_SCOPE_PERINSTANCE~~ | **Not in UCIS 1.0** — Not defined by UCIS 1.0: the token appears nowhere in the specification. The per-instance option is CVG_PERINSTANCE (PI.30). |

### 8.2 String properties (`ucisStringPropertyEnumT`)

| ID | Property | Notes |
| --- | --- | --- |
| PS.1 | `UCIS_STR_FILE_NAME` | UCISDB file/directory name (read-only) |
| PS.2 | `UCIS_STR_SCOPE_NAME` | scope name |
| PS.3 | `UCIS_STR_SCOPE_HIER_NAME` | hierarchical scope name |
| PS.4 | `UCIS_STR_INSTANCE_DU_NAME` | the instance's design-unit name |
| PS.5 | `UCIS_STR_UNIQUE_ID` | scope or coveritem unique id (read-only) |
| PS.6 | `UCIS_STR_VER_STANDARD` | standard, fixed to "UCIS" |
| PS.7 | `UCIS_STR_VER_STANDARD_VERSION` | version of the standard |
| PS.8 | `UCIS_STR_VER_VENDOR_ID` | vendor id |
| PS.9 | `UCIS_STR_VER_VENDOR_TOOL` | vendor tool |
| PS.10 | `UCIS_STR_VER_VENDOR_VERSION` | vendor tool version |
| PS.11 | `UCIS_STR_GENERIC` | miscellaneous string data |
| PS.12 | `UCIS_STR_ITH_CROSSED_CVP_NAME` | i-th crossed coverpoint name of a cross |
| PS.13 | `UCIS_STR_HIST_CMDLINE` | test run command line |
| PS.14 | `UCIS_STR_HIST_RUNCWD` | test run working directory |
| PS.15 | `UCIS_STR_COMMENT` | comment |
| PS.16 | `UCIS_STR_TEST_TIMEUNIT` | test run simulation time unit |
| PS.17 | `UCIS_STR_TEST_DATE` | test run date |
| PS.18 | `UCIS_STR_TEST_SIMARGS` | test run simulator arguments |
| PS.19 | `UCIS_STR_TEST_USERNAME` | test run user name |
| PS.20 | `UCIS_STR_TEST_NAME` | test run name |
| PS.21 | `UCIS_STR_TEST_SEED` | test run seed |
| PS.22 | `UCIS_STR_TEST_HOSTNAME` | test run hostname |
| PS.23 | `UCIS_STR_TEST_HOSTOS` | test run host OS |
| PS.24 | `UCIS_STR_EXPR_TERMS` | input-ordered expression terms delimited by '#' |
| PS.25 | `UCIS_STR_TOGGLE_CANON_NAME` | toggle object canonical name |
| PS.26 | `UCIS_STR_UNIQUE_ID_ALIAS` | scope or coveritem unique-id alias |
| PS.27 | `UCIS_STR_DESIGN_VERSION_ID` | version of the design / elaboration id |
| PS.28 | `UCIS_STR_DU_SIGNATURE` | design-unit signature, for source-change detection |
| PS.29 | `UCIS_STR_HIST_TOOLCATEGORY` | tool category of the history node |
| PS.30 | `UCIS_STR_HIST_LOG_NAME` | logical name of the history node |
| PS.31 | `UCIS_STR_HIST_PHYS_NAME` | history node physical name |
| PS.32 | ~~UCIS_STR_FSM_STATEVAR~~ | **Not in UCIS 1.0** — Not defined by UCIS 1.0: the token appears nowhere in the specification. The FSM state variable is carried as UCIS_STR_GENERIC (PS.11) on the FSM scope, which is what the specification's own example does. |

### 8.3 Real properties (`ucisRealPropertyEnumT`)

| ID | Property | Notes |
| --- | --- | --- |
| PR.1 | `UCIS_REAL_HIST_CPUTIME` | test run CPU time |
| PR.2 | `UCIS_REAL_TEST_SIMTIME` | test run simulation time |
| PR.3 | `UCIS_REAL_TEST_COST` | relative cost of re-running the test |
| PR.4 | `UCIS_REAL_CVG_INST_AVERAGE` | average coverage across covergroup instances |

### 8.4 Handle properties (`ucisHandleEnumT`)

| ID | Property | Notes |
| --- | --- | --- |
| PH.1 | `UCIS_HANDLE_SCOPE_PARENT` | parent scope |
| PH.2 | `UCIS_HANDLE_SCOPE_TOP` | top (root) scope |
| PH.3 | `UCIS_HANDLE_INSTANCE_DU` | the instance's design-unit scope |
| PH.4 | `UCIS_HANDLE_HIST_NODE_PARENT` | parent history node |
| PH.5 | `UCIS_HANDLE_HIST_NODE_ROOT` | top (root) history node |
| PH.6 | ~~UCIS_HANDLE_SCOPE_DU~~ | **Not in UCIS 1.0** — Not defined by UCIS 1.0: the token appears only in a s 8.3 example. The enumerated constant is UCIS_HANDLE_INSTANCE_DU (PH.3). |

### 8.5 Get/set functions

| ID | Feature | Spec ref |
| --- | --- | --- |
| PP.1 | `ucis_GetIntProperty(db, obj, coverindex, property)` | § 8.3.5 |
| PP.2 | `ucis_SetIntProperty(db, obj, coverindex, property, value)` | § 8.3.6 |
| PP.3 | `ucis_GetRealProperty(db, obj, coverindex, property)` | § 8.3.7 |
| PP.4 | `ucis_SetRealProperty(db, obj, coverindex, property, value)` | § 8.3.8 |
| PP.5 | `ucis_GetStringProperty(db, obj, coverindex, property)` | § 8.3.9 |
| PP.6 | `ucis_SetStringProperty(db, obj, coverindex, property, value)` | § 8.3.10 |
| PP.7 | `ucis_GetHandleProperty(db, obj, property)` | § 8.3.11 |
| PP.8 | `ucis_SetHandleProperty(db, obj, property, value)` | § 8.3.12 |
| PP.9 | coverindex semantics: `-1` = the scope, `>= 0` = the cover at that index, ignored for history nodes | § 8.3.5 |

---

## 9. User-defined attributes  (§ 3.4.2, § 8.4)

| ID    | Feature                                                                          | Spec ref |
| ----- | -------------------------------------------------------------------------------- | -------- |
| A9.1  | `ucis_AttrAdd(db, obj, coverindex, key, value)`                                   | § 8.4.1 |
| A9.2  | `ucis_AttrMatch(db, obj, coverindex, key)` → iterator-style match                 | § 8.4.2 |
| A9.3  | `ucis_AttrNext(db, obj, coverindex, prev_key, &key, &value)`                      | § 8.4.3 |
| A9.4  | `ucis_AttrRemove(db, obj, coverindex, key)`                                       | § 8.4.4 |
| A9.5  | Attr value types: `INT`, `INT64`, `FLOAT`, `DOUBLE`, `STRING`, `MEMBLK`           | § 8.4   |

---

## 10. Source files  (§ 8.12)

| ID    | Feature                                                                | Spec ref |
| ----- | ---------------------------------------------------------------------- | -------- |
| F10.1 | `ucis_CreateFileHandle(db, filename, fileworkdir)`                     | § 8.12.2 |
| F10.2 | `ucis_GetFileName(db, fh)`                                              | § 8.12.3 |
| F10.3 | `ucisSourceInfoT { filehandle, line, token }` — per-scope/per-cover loc | § 3.4.7 |
| F10.4 | Per-DU file table (file IDs are local to a DU)                          | § 6.3.3 |

(`ucis_CreateSrcFileHandle(db, du, filename, fileworkdir)` is a documented
specialization used by writers that emit per-DU file tables — D1 in
[`docs/adr/0001-ucis-writer-foundation.md`](adr/0001-ucis-writer-foundation.md).)

---

## 11. History nodes and test data  (§ 4.3, § 8.13, § 8.14)

| ID    | Feature                                                                                  | Spec ref |
| ----- | ---------------------------------------------------------------------------------------- | -------- |
| H11.1 | `ucis_CreateHistoryNode(db, parent, logicalname, physicalname, kind)`                     | § 8.13.1 |
| H11.2 | `ucis_RemoveHistoryNode(db, node)`                                                        | § 8.13.2 |
| H11.3 | `ucis_GetHistoryKind(db, node)`                                                           | § 8.13.3 |
| H11.4 | `ucis_SetTestData(db, node, &data)` / `ucis_GetTestData`                                   | § 8.14.2–3 |
| H11.5 | History-node kinds: `UCIS_HISTORYNODE_NONE`, `_ALL`, `_TEST`, `_MERGE`                     | § 4.3.1  |
| H11.6 | Tool-category strings: `UCIS_SIM_TOOL`, `UCIS_FORMAL_TOOL`, `UCIS_ANALOG_TOOL`, `UCIS_EMULATOR_TOOL`, `UCIS_MERGE_TOOL` | § 4.3 |
| H11.7 | Test-status enum: `OK`, `WARNING`, `ERROR`, `FATAL`, `MISSING`, `MERGE_ERROR`              | § 8.14.1 |
| H11.8 | Test-data record fields: `teststatus`, `simtime`, `timeunit`, `runcwd`, `cputime`, `seed`, `cmd`, `args`, `compulsory`, `date`, `username`, `cost`, `toolcategory` | § 8.14 |
| H11.9 | Universal-Object-Recognition compliance flags (UOR safe scopes/coveritems)                | § 4.3.2, § 6.1.1 |

---

## 12. History-node lists / test↔coveritem association  (§ 8.17)

| ID    | Feature                                                            | Spec ref |
| ----- | ------------------------------------------------------------------ | -------- |
| HL.1  | `ucis_CreateHistoryNodeList(db)`                                    | § 8.17.2 |
| HL.2  | `ucis_FreeHistoryNodeList(db, list)`                                | § 8.17.3 |
| HL.3  | `ucis_AddToHistoryNodeList(list, node)`                             | § 8.17.4 |
| HL.4  | `ucis_RemoveFromHistoryNodeList(list, node)`                        | § 8.17.5 |
| HL.5  | `ucis_HistoryNodeListIterate(db, list)`                             | § 8.17.6 |
| HL.6  | `ucis_SetHistoryNodeListAssoc(db, list, obj, coverindex)`           | § 8.17.7 |
| HL.7  | `ucis_GetHistoryNodeListAssoc(db, obj, coverindex)` → list           | § 8.17.8 |

---

## 13. Tags  (§ 3.4.4, § 8.16)

| ID    | Feature                                                  | Spec ref |
| ----- | -------------------------------------------------------- | -------- |
| T13.1 | `ucis_ObjKind(db, obj)`                                  | § 8.16.1 |
| T13.2 | `ucis_AddObjTag(db, obj, tagname)`                       | § 8.16.2 |
| T13.3 | `ucis_RemoveObjTag(db, obj, tagname)`                    | § 8.16.3 |
| T13.4 | Tag traversal (see IT.8, IT.9)                            | § 8.9, 8.10 |
| T13.5 | `UCIS_GROUP` scope as tag-group container                 | § 6      |

---

## 14. Toggle coverage  (§ 6.5.5, § 8.15)

| ID    | Feature                                                                              | Spec ref |
| ----- | ------------------------------------------------------------------------------------ | -------- |
| TG.1  | `UCIS_TOGGLE` scope with `UCIS_TOGGLEBIN` children                                    | § 6.5.5 |
| TG.2  | `ucisToggleMetricT`: `NOBINS`, `ENUM`, `TRANSITION`, `2STOGGLE`, `ZTOGGLE`, `XTOGGLE` | § 6.5.5 |
| TG.3  | `ucisToggleTypeT`: `NET`, `REG`                                                       | § 6.5.5 |
| TG.4  | `ucisToggleDirT`: `INTERNAL`, `IN`, `OUT`, `INOUT`                                    | § 6.5.5 |
| TG.5  | `UCIS_STR_TOGGLE_CANON_NAME` for net-aliasing (§ 4.10)                                | § 4.10  |
| TG.6  | Toggle properties: `UCIS_INT_TOGGLE_TYPE/DIR/METRIC/COVERED`                          | § 6.5.5 |
| TG.7  | Toggle XML schema: `TOGGLE_OBJECT`, `TOGGLE_BIT`, `TOGGLE`                            | § 9.7   |

---

## 15. FSM coverage  (§ 6.5.6)

| ID    | Feature                                                                | Spec ref |
| ----- | ---------------------------------------------------------------------- | -------- |
| FS.1  | `UCIS_FSM` scope with `UCIS_STR_FSM_STATEVAR` (state-var name)         | § 6.5.6  |
| FS.2  | `UCIS_FSM_STATES` child scope with `UCIS_STATEBIN` per state            | § 6.5.6  |
| FS.3  | `UCIS_FSM_TRANS` child scope with `UCIS_TRANSBIN` per arc               | § 6.5.6  |
| FS.4  | `UCIS_INT_FSM_STATEVAL` integer property on state bins                  | § 6.5.6  |
| FS.5  | `ucis_CreateNextTransition` constructor for arcs                        | § 8.5.11 |
| FS.6  | `ucis_GetFSMTransitionStates(db, trans, &src, &dst)`                     | § 8.5.12 |
| FS.7  | Coveritem flags: `UCIS_IS_FSM_RESET`, `UCIS_IS_FSM_TRAN`                | § 6      |
| FS.8  | XML schema: `FSM`, `FSM_STATE`, `FSM_TRANSITION`                        | § 9.11   |

---

## 16. Branch coverage  (§ 6.5.2, § 9.13)

| ID    | Feature                                                          | Spec ref |
| ----- | ---------------------------------------------------------------- | -------- |
| BR.1  | `UCIS_BRANCH` scope with `UCIS_BRANCHBIN` children                | § 6.5.2 |
| BR.2  | `UCIS_INT_BRANCH_HAS_ELSE`, `UCIS_INT_BRANCH_ISCASE`              | § 6.5.2 |
| BR.3  | `UCIS_IS_BR_ELSE` coveritem flag (else-branch marker)             | § 6.5.2 |
| BR.4  | XML schema: `BRANCH_STATEMENT`, `BRANCH`                          | § 9.13   |

---

## 17. Statement / block coverage  (§ 6.5.3, § 9.12)

| ID    | Feature                                                          | Spec ref |
| ----- | ---------------------------------------------------------------- | -------- |
| SB.1  | `UCIS_BLOCK` scope + `UCIS_STMTBIN` per line/statement            | § 6.5.3 |
| SB.2  | `UCIS_BLOCKBIN` for block-level aggregate                         | § 6.5.3 |
| SB.3  | `UCIS_INT_STMT_INDEX` integer property                            | § 6.5.3 |
| SB.4  | XML schema: `STATEMENT`, `PROCESS_BLOCK`, `BLOCK`                 | § 9.12   |

(These were `ST.1`–`ST.4` before the conformance registry existed; the prefix
collided with the scope-type tokens in § 4.2, which own `ST.*`. Renamed at
registry bootstrap. They are *not* recorded as aliases, because `ST.1`–`ST.4`
remain live IDs for the scope-type tokens — an alias would be ambiguous, which
is the exact failure being repaired. Nothing referenced the old statement IDs
outside this file.)

---

## 18. Condition and expression coverage  (§ 6.5.4, § 9.9)

| ID    | Feature                                                          | Spec ref |
| ----- | ---------------------------------------------------------------- | -------- |
| CX.1  | `UCIS_EXPR` / `UCIS_COND` scope with `UCIS_EXPRBIN` / `UCIS_CONDBIN` children (truth-table rows) | § 6.5.4 |
| CX.2  | `UCIS_INT_SCOPE_NUM_EXPR_TERMS` + `UCIS_STR_EXPR_TERMS`           | § 6.5.4 |
| CX.3  | `UCIS_SCOPE_EXPR_ISHIERARCHICAL` flag                              | § 6.5.4 |
| CX.4  | XML schema: `EXPR`                                                | § 9.9   |

---

## 19. Covergroup coverage  (§ 6.4, § 9.8)

| ID    | Feature                                                                            | Spec ref |
| ----- | ---------------------------------------------------------------------------------- | -------- |
| CG.1  | `UCIS_COVERGROUP` type scope                                                        | § 6.4.2 |
| CG.2  | `UCIS_COVERINSTANCE` per-instance specialization                                    | § 6.4.2 |
| CG.3  | `UCIS_COVERPOINT` scope with `UCIS_CVGBIN` children                                  | § 6.4.2 |
| CG.4  | `UCIS_CROSS` scope referencing N coverpoints (built via `CreateCross`)              | § 6.4.2 |
| CG.5  | Bin-scope variants: `UCIS_CVGBINSCOPE`, `UCIS_ILLEGALBINSCOPE`, `UCIS_IGNOREBINSCOPE` | § 6.4.2 |
| CG.6  | Per-bin classification: `UCIS_IGNOREBIN`, `UCIS_ILLEGALBIN`, `UCIS_DEFAULTBIN`       | § 6.4.2 |
| CG.7  | Transition bins via `UCIS_TRANSITION` scope + `UCIS_SCOPE_CVG_TRANSITION` flag       | § 6.4.2 |
| CG.8  | Coverpoint shape flags: `UCIS_SCOPE_CVG_AUTO/SCALAR/VECTOR/TRANSITION`               | § 6.4.2 |
| CG.9  | Covergroup options: `UCIS_INT_CVG_ATLEAST`, `AUTOBINMAX`, `DETECTOVERLAP`, `NUMPRINTMISSING`, `STROBE`, `PERINSTANCE`, `GETINSTCOV`, `MERGEINSTANCES` | § 6.4.2 |
| CG.10 | `UCIS_REAL_CVG_INST_AVERAGE`                                                        | § 6.4.2 |
| CG.11 | Cross helpers: `UCIS_INT_NUM_CROSSED_CVPS`, `UCIS_STR_ITH_CROSSED_CVP_NAME`, `ucis_GetIthCrossedCvp` | § 8.5.18 |
| CG.12 | `UCIS_IS_CROSSAUTO` per-bin flag (auto-generated cross bin)                          | § 6.4.2 |
| CG.13 | Bin guards: `UCIS_BIN_IFF_EXISTS`, `UCIS_BIN_SAMPLE_TRUE`                            | § 6.4.2 |
| CG.14 | XML schema: `CGINSTANCE`, `CG_ID`, `CGINST_OPTIONS`, `COVERPOINT`, `COVERPOINT_OPTIONS`, `COVERPOINT_BIN`, `RANGE_VALUE`, `SEQUENCE`, `CROSS`, `CROSS_OPTIONS`, `CROSS_BIN` | § 9.8   |

---

## 20. Assertion and cover (SVA/PSL)  (§ 6.6, § 9.10)

| ID    | Feature                                                          | Spec ref |
| ----- | ---------------------------------------------------------------- | -------- |
| AS.1  | `UCIS_ASSERT` scope                                              | § 6.6.2 |
| AS.2  | `UCIS_COVER` scope (cover-property)                              | § 6.6.1 |
| AS.3  | Assertion counter bins: `PASSBIN`, `FAILBIN`, `VACUOUSBIN`, `DISABLEDBIN`, `ATTEMPTBIN`, `ACTIVEBIN`, `PEAKACTIVEBIN` | § 6.6.2 |
| AS.4  | Cover-property bin: `UCIS_COVERBIN`                              | § 6.6.1 |
| AS.5  | Sequence bin: `UCIS_SCBIN`                                       | § 6.6.1 |
| AS.6  | Immediate-assertion marker: `UCIS_IS_IMMEDIATE_ASSERT`           | § 6.6.2 |
| AS.7  | XML schema: `ASSERTION`                                          | § 9.10  |

---

## 21. User-defined coverage  (§ 6.6.3)

| ID    | Feature                                                          | Spec ref |
| ----- | ---------------------------------------------------------------- | -------- |
| UD.1  | `UCIS_USERBIN` / `UCIS_GENERICBIN` for arbitrary user-defined bins | § 6.6.3 |
| UD.2  | `UCIS_USERBITS` reserved bit-range on cover types                 | § 6      |
| UD.3  | `UCIS_STR_GENERIC` generic string property                        | § 8.3.2 |
| UD.4  | `UCIS_HAS_ACTION` / `UCIS_HAS_COUNT` cover flags                  | § 6      |

---

## 22. Formal verification  (§ 8.19)

| ID    | Feature                                                                                  | Spec ref |
| ----- | ---------------------------------------------------------------------------------------- | -------- |
| FM.1  | Formal-status enum + `ucis_SetFormalStatus` / `ucis_GetFormalStatus`                      | § 8.19.3–5 |
| FM.2  | Formal-radius accessors (`SetFormalRadius`, `GetFormalRadius`)                           | § 8.19.6–7 |
| FM.3  | Formal-witness accessors (`SetFormalWitness`, `GetFormalWitness`)                        | § 8.19.8–9 |
| FM.4  | Formally-unreachable coveritem: `Set/GetFormallyUnreachableCoverTest`                     | § 8.19.11–12 |
| FM.5  | Formal-env API: `AddFormalEnv`, `FormalEnvGetData`, `NextFormalEnv`                       | § 8.19.15–17 |
| FM.6  | Test↔env association: `AssocFormalInfoTest`                                               | § 8.19.18 |
| FM.7  | Per-test formal info: `FormalTestGetInfo`                                                 | § 8.19.20 |
| FM.8  | Assumption↔env association: `AssocAssumptionFormalEnv`, `NextFormalEnvAssumption`         | § 8.19.21–22 |
| FM.9  | Formal-coverage-context constants: `STIMULUS`, `RESPONSE`, `TARGETED`, `ANCILLARY`, `INCONCLUSIVE_ANALYSIS` | § 8.19.19 |

---

## 23. Net aliasing  (§ 4.10)

| ID    | Feature                                                          | Spec ref |
| ----- | ---------------------------------------------------------------- | -------- |
| NA.1  | Canonical-name property for nets: `UCIS_STR_TOGGLE_CANON_NAME`   | § 4.10  |
| NA.2  | Multiple-instance roll-up via `UCIS_INST_ONCE`                    | § 4.10  |

---

## 24. Metrics  (§ 4.9)

| ID    | Feature                                                          | Spec ref |
| ----- | ---------------------------------------------------------------- | -------- |
| MT.1  | Metric naming model (URL-style)                                  | § 4.9.2 |
| MT.2  | Metric criteria (covered / excluded / pragma)                    | § 4.9.3 |
| MT.3  | Metric excluded-value coveritem flags                            | § 4.9.4 |
| MT.4  | Built-in metric definitions (per coverage type)                  | § 4.9.5 |
| MT.5  | XML `METRIC_MODE` and metric attributes                          | § 9.3.10–11 |

---

## 25. XML interchange  (§ 9)

| ID    | Feature                                                          | Spec ref |
| ----- | ---------------------------------------------------------------- | -------- |
| X25.1 | Round-trip: `ucis_OpenFromInterchangeFormat` ⇄ `ucis_WriteToInterchangeFormat` | § 8.1 |
| X25.2 | Complete UCIS XML schema (top-level)                              | § 9.4, § 9.14 |
| X25.3 | Schema fragments per coverage type (TOGGLE / CVG / COND / ASSERT / FSM / BLOCK / BRANCH) | § 9.7–9.13 |
| X25.4 | Common complex types: `NAME_VALUE`, `SOURCE_FILE`, `LINE_ID`, `STATEMENT_ID`, `DIMENSION`, `BIN`, `BIN_CONTENTS` | § 9.3 |
| X25.5 | History-node XML schema                                           | § 9.5  |
| X25.6 | Instance-coverage XML schema                                      | § 9.6  |
| X25.7 | User-defined attribute encoding                                   | § 9.3.12 |

---

## 26. Database-level objects (§ 3.4)

| ID    | Feature                                                          | Spec ref |
| ----- | ---------------------------------------------------------------- | -------- |
| DB.1  | Counts on coveritems                                              | § 3.4.1 |
| DB.2  | Attributes (typed, user-defined) on any object                    | § 3.4.2 |
| DB.3  | Flags on scopes and coveritems                                    | § 3.4.3 |
| DB.4  | Tags on objects                                                   | § 3.4.4 |
| DB.5  | Weights, goals, limits                                            | § 3.4.5 |
| DB.6  | Typed properties                                                  | § 3.4.6 |
| DB.7  | Source-file tables                                                | § 3.4.7 |
| DB.8  | Error handler                                                     | § 3.4.8 |
| DB.9  | DB-level vendor / standard / version identifying strings (manifest) | § 7    |

---

## How to use this catalog

For each downstream comparison file (e.g. `ucis-feature-matrix.md`),
clone the ID column and add scoring columns:

```
| ID    | Feature       | NCDB schema | C UCIS shim | Python API | TS API | Notes |
```

Use these scoring values per cell:

- **✅ full** — feature is fully modeled / exposed and tested
- **🟡 partial** — modeled but with caveats (writer-only, no read-back,
  no merge semantics, lossy, etc.)
- **❌ missing** — not present
- **n/a** — not applicable to that surface (e.g. XML interchange against
  the writer-only C shim)

Cross-reference NCDB on-disk shape via [`docs/ucis-impl-status.md`](ucis-impl-status.md)
and the per-phase task list in [`docs/ucis-impl-plan.md`](ucis-impl-plan.md).
The C writer shim's current surface is in
[`c/ucis/include/ucis.h`](../c/ucis/include/ucis.h); use that as the
ground-truth inventory for the C column.
