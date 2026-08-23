<!-- GENERATED FILE -- DO NOT EDIT.
     Regenerate with:  python3 tools/gen_conformance_matrix.py
     Sources: docs/conformance/features/*.yaml (intent)
              docs/ucis-parquet-mapping.md (documentation claims)
              tests/conformance/coverage.json (test evidence)
     See docs/ucis-conformance-structure-plan.md. -->

# UCIS -> parquet conformance matrix

Every UCIS 1.0 feature in [`ucis-feature-catalog.md`](ucis-feature-catalog.md), scored against the `parquet` backend. **Status is computed**, from three independent inputs: a human-declared scope in the registry, a documentation claim in the mapping document, and the existence of a tagged test. Nothing here is hand-written.

| Badge | Meaning |
| ----- | ------- |
| ✅ | mapped + tested |
| 🟡 | mapped, untested |
| 🟠 | tested, undocumented |
| ❌ | unmapped |
| ⚪ | out of scope / deferred |

The **Profile** column is a separate axis from status: `core` is what a conforming implementation must support, `*ext*` is what it MAY omit while still reporting every coverage number correctly. See the "Conformance" section of the mapping document.

🟠 is the state worth reading first: the implementation handles it and a test proves it, but the specification does not say so — so a third-party implementation written from the document alone will not interoperate.

## Totals

| Status | Count | Share |
| ------ | ----: | ----: |
| ✅ mapped + tested | 178 | 47.1% |
| 🟡 mapped, untested | 135 | 35.7% |
| 🟠 tested, undocumented | 0 | 0.0% |
| ❌ unmapped | 5 | 1.3% |
| ⚪ out of scope / deferred | 60 | 15.9% |
| **total** | **378** | |

## By catalog section

| § | Section | ✅ | 🟡 | 🟠 | ❌ | ⚪ | Total |
| - | ------- | --: | --: | --: | --: | --: | ----: |
| 1 | Database lifecycle and I/O | 0 | 3 | 0 | 0 | 7 | 10 |
| 2 | Error handling | 0 | 0 | 0 | 0 | 3 | 3 |
| 3 | Versioning | 2 | 3 | 0 | 0 | 1 | 6 |
| 4 | Scope creation and management | 28 | 48 | 0 | 1 | 8 | 85 |
| 5 | Source language enum | 2 | 11 | 0 | 0 | 0 | 13 |
| 6 | Coveritems | 23 | 6 | 0 | 0 | 3 | 32 |
| 7 | Iteration / traversal | 5 | 3 | 0 | 0 | 1 | 9 |
| 8 | Properties | 46 | 34 | 0 | 0 | 6 | 86 |
| 9 | User-defined attributes | 5 | 0 | 0 | 0 | 0 | 5 |
| 10 | Source files | 4 | 0 | 0 | 0 | 0 | 4 |
| 11 | History nodes and test data | 6 | 2 | 0 | 0 | 1 | 9 |
| 12 | History-node lists / test↔coveritem association | 2 | 4 | 0 | 0 | 1 | 7 |
| 13 | Tags | 2 | 3 | 0 | 0 | 0 | 5 |
| 14 | Toggle coverage | 5 | 0 | 0 | 0 | 2 | 7 |
| 15 | FSM coverage | 4 | 0 | 0 | 3 | 1 | 8 |
| 16 | Branch coverage | 2 | 1 | 0 | 0 | 1 | 4 |
| 17 | Statement / block coverage | 2 | 0 | 0 | 1 | 1 | 4 |
| 18 | Condition and expression coverage | 2 | 1 | 0 | 0 | 1 | 4 |
| 19 | Covergroup coverage | 10 | 3 | 0 | 0 | 1 | 14 |
| 20 | Assertion and cover (SVA/PSL) | 4 | 2 | 0 | 0 | 1 | 7 |
| 21 | User-defined coverage | 2 | 2 | 0 | 0 | 0 | 4 |
| 22 | Formal verification | 3 | 0 | 0 | 0 | 6 | 9 |
| 23 | Net aliasing | 0 | 0 | 0 | 0 | 2 | 2 |
| 24 | Metrics | 0 | 1 | 0 | 0 | 4 | 5 |
| 25 | XML interchange | 0 | 0 | 0 | 0 | 7 | 7 |
| 26 | Database-level objects | 7 | 1 | 0 | 0 | 1 | 9 |
| 90 | Feature combinations | 11 | 2 | 0 | 0 | 1 | 14 |
| 91 | Negative conformance | 1 | 5 | 0 | 0 | 0 | 6 |

## Features

### 1. Database lifecycle and I/O (§ 8.1)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `L1.1` | `ucis_Open(name)` — in-memory DB, optionally populated from a file | § 8.1.5 | core | [Dataset layout on disk](ucis-parquet-mapping.md#dataset-layout-on-disk) [Read/write scope (decision)](ucis-parquet-mapping.md#readwrite-scope-decision) | — | 🟡 |
| `L1.2` | `ucis_Close(db)` — invalidate handle, release memory | § 8.1.4 | core | [Dataset layout on disk](ucis-parquet-mapping.md#dataset-layout-on-disk) [Read/write scope (decision)](ucis-parquet-mapping.md#readwrite-scope-decision) | — | 🟡 |
| `L1.3` | `ucis_Write(db, file, scope, recurse, covertype)` — persist subset to file — *deferred* | § 8.1.13 | — | [Read/write scope (decision)](ucis-parquet-mapping.md#readwrite-scope-decision) | — | ⚪ |
| `L1.4` | `ucis_OpenFromInterchangeFormat(file)` — load from XML interchange — *out of scope* | § 8.1.6 | — | — | — | ⚪ |
| `L1.5` | `ucis_WriteToInterchangeFormat(db, file)` — emit XML interchange — *out of scope* | § 8.1.14 | — | — | — | ⚪ |
| `L1.6` | `ucis_OpenReadStream(name)` — read-streaming open — *out of scope* | § 8.1.9 | — | — | — | ⚪ |
| `L1.7` | `ucis_OpenWriteStream(name)` — write-streaming open — *out of scope* | § 8.1.10 | — | — | — | ⚪ |
| `L1.8` | `ucis_WriteStream(db)` — flush in-flight object in write-streaming — *out of scope* | § 8.1.11 | — | — | — | ⚪ |
| `L1.9` | `ucis_WriteStreamScope(db)` — flush current scope and pop to parent — *out of scope* | § 8.1.12 | — | — | — | ⚪ |
| `L1.10` | `ucis_GetPathSeparator(db)` / `ucis_SetPathSeparator(db, sep)` | § 8.1.7–8 | core | [Identity & ordering rules](ucis-parquet-mapping.md#identity--ordering-rules) | — | 🟡 |

### 2. Error handling (§ 8.2)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `E2.1` | `ucis_RegisterErrorHandler(handler, userdata)` — *out of scope* | § 8.2.1 | — | — | — | ⚪ |
| `E2.2` | `ucisErrorT { msgno, severity, msgstr }` — *out of scope* | § 8.2 | — | — | — | ⚪ |
| `E2.3` | Severity enum: `INFO`, `WARNING`, `ERROR` — *out of scope* | § 8.2 | — | — | — | ⚪ |

### 3. Versioning (§ 7, § 8.18)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `V3.1` | `ucis_GetAPIVersion()` — version of the linked API library — *out of scope* | § 8.18.1 | — | — | — | ⚪ |
| `V3.2` | `ucis_GetDBVersion(db)` — version of the in-memory DB schema | § 8.18.2 | core | [Dataset layout on disk](ucis-parquet-mapping.md#dataset-layout-on-disk) [Neutrality rules](ucis-parquet-mapping.md#neutrality-rules) | L0: `test_schema_version_in_metadata[counts]`, `test_schema_version_in_metadata[coveritems]`, `test_schema_version_in_metadata[cross_operands]` +7 | ✅ |
| `V3.3` | `ucis_GetFileVersion(file)` — version recorded in a file | § 8.18.3 | core | [Dataset layout on disk](ucis-parquet-mapping.md#dataset-layout-on-disk) [Neutrality rules](ucis-parquet-mapping.md#neutrality-rules) | L0: `test_schema_version_in_metadata[counts]`, `test_schema_version_in_metadata[coveritems]`, `test_schema_version_in_metadata[cross_operands]` +7 | ✅ |
| `V3.4` | `ucis_GetHistoryNodeVersion(node)` — version a history node was written under | § 8.18.4 | core | [`history_nodes`](ucis-parquet-mapping.md#history_nodes) | — | 🟡 |
| `V3.5` | `ucis_GetVersionStringProperty(versionH, prop)` | § 8.18.5 | core | [Neutrality rules](ucis-parquet-mapping.md#neutrality-rules) | — | 🟡 |
| `V3.6` | Version string properties: `UCIS_STR_VER_STANDARD`, `VER_STANDARD_VERSION`, `VER_VENDOR_ID`, `VER_VENDOR_TOOL`, `VER_VENDOR_VERSION` | § 7 | core | [Neutrality rules](ucis-parquet-mapping.md#neutrality-rules) | — | 🟡 |

### 4. Scope creation and management (§ 8.5)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `S4.1` | `ucis_CreateScope(db, parent, name, srcinfo, weight, source, type, flags)` | § 8.5.2 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | L1: `test_round_trip_loses_nothing`, `test_scope_count_and_order`<br>L2: `test_scope_and_bin_structure_agrees`<br>L4: `test_q1_total_and_hit_bins_for_a_design_unit` | ✅ |
| `S4.2` | `ucis_CreateInstance(db, parent, name, srcinfo, weight, source, type, du, flags)` — DU-linked instance | § 8.5.9 | core | [`scopes`](ucis-parquet-mapping.md#scopes) [Columns added](ucis-parquet-mapping.md#columns-added) | L1: `test_instance_du_link_survives`, `test_round_trip_loses_nothing` | ✅ |
| `S4.3` | `ucis_CreateInstanceByName(...du_name, flags)` — write-streaming variant by name | § 8.5.10 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | L1: `test_instance_du_link_survives` | ✅ |
| `S4.4` | `ucis_CreateCross(db, parent, name, srcinfo, weight, source, n, points[])` | § 8.5.7 | core | [Covergroups, coverpoints and crosses](ucis-parquet-mapping.md#covergroups-coverpoints-and-crosses) | L1: `test_covergroup_shape_survives` | ✅ |
| `S4.5` | `ucis_CreateCrossByName(...n, point_names[])` — write-streaming variant | § 8.5.8 | core | [Covergroups, coverpoints and crosses](ucis-parquet-mapping.md#covergroups-coverpoints-and-crosses) | — | 🟡 |
| `S4.6` | `ucis_CreateNextTransition(db, parent, name, srcinfo, weight, src_state, dst_state)` — FSM arc | § 8.5.11 | core | — | — | ❌ |
| `S4.7` | `ucis_RemoveScope(db, scope)` — *deferred* | § 8.5.3 | — | — | — | ⚪ |
| `S4.8` | `ucis_ComposeDUName(lib, primary, secondary)` → string — *out of scope* | § 8.5.5 | — | — | — | ⚪ |
| `S4.9` | `ucis_ParseDUName(name, &lib, &primary, &secondary)` — *out of scope* | § 8.5.6 | — | — | — | ⚪ |
| `S4.10` | `ucis_MatchDU(db, name)` → DU scope | § 8.5.17 | core | [Identity & ordering rules](ucis-parquet-mapping.md#identity--ordering-rules) | L1: `test_instance_du_link_survives` | ✅ |
| `S4.11` | `ucis_MatchScopeByUniqueID(db, uid_list)` — case-insensitive | § 8.5.13 | core | [Identity & ordering rules](ucis-parquet-mapping.md#identity--ordering-rules) | L1: `test_unique_id_is_the_primary_key`<br>L2: `test_identity_lookup_resolves_to_the_same_object` | ✅ |
| `S4.12` | `ucis_CaseAwareMatchScopeByUniqueID(db, uid_list)` | § 8.5.14 | core | [Identity & ordering rules](ucis-parquet-mapping.md#identity--ordering-rules) | — | 🟡 |
| `S4.13` | `ucis_MatchCoverByUniqueID(db, uid_list)` | § 8.5.15 | core | [Identity & ordering rules](ucis-parquet-mapping.md#identity--ordering-rules) | — | 🟡 |
| `S4.14` | `ucis_CaseAwareMatchCoverByUniqueID(db, uid_list)` | § 8.5.16 | core | [Identity & ordering rules](ucis-parquet-mapping.md#identity--ordering-rules) | — | 🟡 |
| `S4.15` | `UCIS_STR_UNIQUE_ID` / `UCIS_STR_UNIQUE_ID_ALIAS` string properties (computation per § 5.4) | § 5.4 | core | [`scopes`](ucis-parquet-mapping.md#scopes) [Identity & ordering rules](ucis-parquet-mapping.md#identity--ordering-rules) | L1: `test_unique_id_is_the_primary_key`<br>L2: `test_identity_lookup_resolves_to_the_same_object`<br>L4: `test_q2_full_scope_path_of_a_bin` | ✅ |
| `S4.16` | `ucis_SetScopeSourceInfo` / `ucis_GetScopeSourceInfo` | § 8.5.19, 8.5.24 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `S4.17` | `ucis_GetScopeType(db, scope)` → `ucisScopeTypeT` | § 8.5.25 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | L0: `test_scope_type_is_64_bit`<br>L1: `test_every_scope_type_in_the_corpus_survives`<br>L2: `test_scope_type_mask_filtering_agrees` | ✅ |
| `S4.18` | `ucis_GetObjType(db, obj)` → scope or history-node kind | § 8.5.26 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `S4.19` | `ucis_GetIthCrossedCvp(db, cross, i)` → coverpoint scope | § 8.5.18 | core | [`cross_operands`](ucis-parquet-mapping.md#cross_operands) | L1: `test_the_crossed_coverpoint_list_survives`<br>L2: `test_a_cross_resolves_its_operands_to_the_same_scopes`<br>L4: `test_a_cross_names_its_operands_in_order` | ✅ |
| `S4.20` | `ucis_CallBack(db, scope, mask, kind, fn, userdata)` — scope-callback walk — *out of scope* | § 8.5.4 | — | — | — | ⚪ |
| `SF.1` | `UCIS_INST_ONCE` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `SF.2` | `UCIS_ENABLED_STMT` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `SF.3` | `UCIS_ENABLED_BRANCH` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `SF.4` | `UCIS_ENABLED_COND` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `SF.5` | `UCIS_ENABLED_EXPR` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `SF.6` | `UCIS_ENABLED_FSM` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `SF.7` | `UCIS_ENABLED_TOGGLE` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `SF.8` | `UCIS_ENABLED_BLOCK` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `SF.9` | `UCIS_SCOPE_UNDER_DU` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `SF.10` | `UCIS_SCOPE_EXCLUDED` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) [Flag bits are type-qualified](ucis-parquet-mapping.md#flag-bits-are-type-qualified) | L4: `test_q6_excluded_bins` | ✅ |
| `SF.11` | `UCIS_SCOPE_PRAGMA_EXCLUDED` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) [Flag bits are type-qualified](ucis-parquet-mapping.md#flag-bits-are-type-qualified) | L4: `test_q6_excluded_bins` | ✅ |
| `SF.12` | `UCIS_SCOPE_PRAGMA_CLEARED` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) [Flag bits are type-qualified](ucis-parquet-mapping.md#flag-bits-are-type-qualified) | — | 🟡 |
| `SF.13` | `UCIS_SCOPE_SPECIALIZED` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `SF.14` | `UCIS_UOR_SAFE_SCOPE` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `SF.15` | `UCIS_UOR_SAFE_SCOPE_ALLCOVERS` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `SF.16` | `UCIS_IS_TOP_NODE` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `SF.17` | `UCIS_IS_IMMEDIATE_ASSERT` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `SF.18` | `UCIS_SCOPE_CVG_AUTO` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `SF.19` | `UCIS_SCOPE_CVG_SCALAR` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `SF.20` | `UCIS_SCOPE_CVG_VECTOR` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `SF.21` | `UCIS_SCOPE_CVG_TRANSITION` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `SF.22` | `UCIS_SCOPE_IFF_EXISTS` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `SF.23` | `UCIS_SCOPE_SAMPLE_TRUE` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `SF.24` | `UCIS_SCOPE_BLOCK_ISBRANCH` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `SF.25` | `UCIS_SCOPE_EXPR_ISHIERARCHICAL` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `SF.26` | `UCIS_SCOPEFLAG_MARK` / `UCIS_SCOPE_INTERNAL` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) [Flag bits are type-qualified](ucis-parquet-mapping.md#flag-bits-are-type-qualified) | — | 🟡 |
| `ST.1` | `UCIS_TOGGLE` | § 6.5.5 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | L1: `test_every_scope_type_in_the_corpus_survives` | ✅ |
| `ST.2` | `UCIS_BRANCH` | § 6.5.2 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | L1: `test_every_scope_type_in_the_corpus_survives` | ✅ |
| `ST.3` | `UCIS_EXPR` | § 6.5.4 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | L1: `test_every_scope_type_in_the_corpus_survives` | ✅ |
| `ST.4` | `UCIS_COND` | § 6.5.4 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | L1: `test_every_scope_type_in_the_corpus_survives` | ✅ |
| `ST.5` | `UCIS_INSTANCE` | § 6.3 — HDL instance scope, requires DU link | core | [`scopes`](ucis-parquet-mapping.md#scopes) | L1: `test_every_scope_type_in_the_corpus_survives` | ✅ |
| `ST.6` | `UCIS_PROCESS` | § 6.3.5 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `ST.7` | `UCIS_BLOCK` | § 6.5.3 — statement/block coverage container | core | [`scopes`](ucis-parquet-mapping.md#scopes) | L1: `test_every_scope_type_in_the_corpus_survives` | ✅ |
| `ST.8` | `UCIS_FUNCTION` | § 6.3.5 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `ST.9` | `UCIS_FORKJOIN` | § 6.3.5 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `ST.10` | `UCIS_GENERATE` | § 6.3.5 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `ST.11` | `UCIS_GENERIC` | § 6.3.5 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | L1: `test_every_scope_type_in_the_corpus_survives` | ✅ |
| `ST.12` | `UCIS_CLASS` | § 6.3.5 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `ST.13` | `UCIS_COVERGROUP` | § 6.4.2 — covergroup type | core | [`scopes`](ucis-parquet-mapping.md#scopes) | L1: `test_every_scope_type_in_the_corpus_survives` | ✅ |
| `ST.14` | `UCIS_COVERINSTANCE` | § 6.4.2 — per-instance specialization of a covergroup | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `ST.15` | `UCIS_COVERPOINT` | § 6.4.2 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | L1: `test_every_scope_type_in_the_corpus_survives` | ✅ |
| `ST.16` | `UCIS_CROSS` | § 6.4.2 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | L1: `test_every_scope_type_in_the_corpus_survives` | ✅ |
| `ST.17` | `UCIS_COVER` | § 6.6.1 — SVA/PSL cover-property container | core | [`scopes`](ucis-parquet-mapping.md#scopes) | L1: `test_every_scope_type_in_the_corpus_survives` | ✅ |
| `ST.18` | `UCIS_ASSERT` | § 6.6.2 — SVA/PSL assert container | core | [`scopes`](ucis-parquet-mapping.md#scopes) | L1: `test_every_scope_type_in_the_corpus_survives` | ✅ |
| `ST.19` | `UCIS_PROGRAM` | § 6.3.5 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `ST.20` | `UCIS_PACKAGE` | § 6.3.5 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `ST.21` | `UCIS_TASK` | § 6.3.5 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `ST.22` | `UCIS_INTERFACE` | § 6.3.5 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `ST.23` | `UCIS_FSM` | § 6.5.6 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | L1: `test_every_scope_type_in_the_corpus_survives` | ✅ |
| `ST.24` | `UCIS_TESTPLAN` — *out of scope* | — | — | [`scopes`](ucis-parquet-mapping.md#scopes) | — | ⚪ |
| `ST.25` | `UCIS_DU_MODULE` | § 6.3.3 — Verilog/SV module DU | core | [`scopes`](ucis-parquet-mapping.md#scopes) | L1: `test_every_scope_type_in_the_corpus_survives` | ✅ |
| `ST.26` | `UCIS_DU_ARCH` | § 6.3.3 — VHDL architecture | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `ST.27` | `UCIS_DU_PACKAGE` | § 6.3.3 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `ST.28` | `UCIS_DU_PROGRAM` | § 6.3.3 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `ST.29` | `UCIS_DU_INTERFACE` | § 6.3.3 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `ST.30` | `UCIS_FSM_STATES` | § 6.5.6 — child of FSM holding STATEBINs | core | [`scopes`](ucis-parquet-mapping.md#scopes) | L1: `test_every_scope_type_in_the_corpus_survives` | ✅ |
| `ST.31` | `UCIS_FSM_TRANS` | § 6.5.6 — child of FSM holding TRANSBINs | core | [`scopes`](ucis-parquet-mapping.md#scopes) | L1: `test_every_scope_type_in_the_corpus_survives` | ✅ |
| `ST.32` | `UCIS_COVBLOCK` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `ST.33` | `UCIS_CVGBINSCOPE` | § 6.4.2 — coverpoint bin scope | core | [`scopes`](ucis-parquet-mapping.md#scopes) | L1: `test_every_scope_type_in_the_corpus_survives` | ✅ |
| `ST.34` | `UCIS_ILLEGALBINSCOPE` | § 6.4.2 — coverpoint illegal-bin scope | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `ST.35` | `UCIS_IGNOREBINSCOPE` | § 6.4.2 — coverpoint ignore-bin scope | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |
| `ST.36` | `UCIS_BBLOCKSCOPE` — *out of scope* | — | — | [`scopes`](ucis-parquet-mapping.md#scopes) | — | ⚪ |
| `ST.37` | `UCIS_GROUP` — *out of scope* | — | — | [`scopes`](ucis-parquet-mapping.md#scopes) | — | ⚪ |
| `ST.38` | `UCIS_TRANSITION` — *out of scope* | — | — | [`scopes`](ucis-parquet-mapping.md#scopes) | — | ⚪ |
| `ST.39` | `UCIS_RESERVEDSCOPE` | — | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |

### 5. Source language enum (§ 5, § 8.5)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `SL.1` | `UCIS_VHDL` — VHDL | § 5 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `SL.2` | `UCIS_VLOG` — Verilog | § 5 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `SL.3` | `UCIS_SV` — SystemVerilog | § 5 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_the_source_language_of_a_scope_round_trips` | ✅ |
| `SL.4` | `UCIS_SYSTEMC` — SystemC | § 5 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `SL.5` | `UCIS_PSL_VHDL` — assert/cover in PSL VHDL | § 5 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `SL.6` | `UCIS_PSL_VLOG` — assert/cover in PSL Verilog | § 5 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `SL.7` | `UCIS_PSL_SV` — assert/cover in PSL SystemVerilog | § 5 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `SL.8` | `UCIS_PSL_SYSTEMC` — assert/cover in PSL SystemC | § 5 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `SL.9` | `UCIS_E` — e | § 5 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `SL.10` | `UCIS_VERA` — Vera | § 5 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `SL.11` | `UCIS_NONE` — not important / unspecified | § 5 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_the_source_language_of_a_scope_round_trips` | ✅ |
| `SL.12` | `UCIS_OTHER` — user-defined; also used for user-defined attributes | § 5 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `SL.13` | `UCIS_SOURCE_ERROR` — error cases | § 5 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |

### 6. Coveritems (§ 8.11)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `BT.1` | `UCIS_CVGBIN` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_every_bin_type_in_the_corpus_survives`<br>L3: `test_ordinary_counters_do_sum` | ✅ |
| `BT.2` | `UCIS_COVERBIN` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_every_bin_type_in_the_corpus_survives` | ✅ |
| `BT.3` | `UCIS_ASSERTBIN` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | — | 🟡 |
| `BT.4` | `UCIS_SCBIN` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | — | 🟡 |
| `BT.5` | `UCIS_ZINBIN` — *out of scope* | — | — | [`coveritems`](ucis-parquet-mapping.md#coveritems) | — | ⚪ |
| `BT.6` | `UCIS_STMTBIN` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_every_bin_type_in_the_corpus_survives`<br>L3: `test_ordinary_counters_do_sum` | ✅ |
| `BT.7` | `UCIS_BRANCHBIN` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_every_bin_type_in_the_corpus_survives` | ✅ |
| `BT.8` | `UCIS_EXPRBIN` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_every_bin_type_in_the_corpus_survives` | ✅ |
| `BT.9` | `UCIS_CONDBIN` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_every_bin_type_in_the_corpus_survives` | ✅ |
| `BT.10` | `UCIS_TOGGLEBIN` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_every_bin_type_in_the_corpus_survives` | ✅ |
| `BT.11` | `UCIS_FSMBIN` (= `UCIS_STATEBIN` / `UCIS_TRANSBIN`) | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_every_bin_type_in_the_corpus_survives` | ✅ |
| `BT.12` | `UCIS_USERBIN` (= `UCIS_GENERICBIN`) | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_every_bin_type_in_the_corpus_survives` | ✅ |
| `BT.13` | `UCIS_PASSBIN` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_every_bin_type_in_the_corpus_survives`<br>L3: `test_ordinary_counters_do_sum` | ✅ |
| `BT.14` | `UCIS_FAILBIN` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_every_bin_type_in_the_corpus_survives` | ✅ |
| `BT.15` | `UCIS_VACUOUSBIN` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_every_bin_type_in_the_corpus_survives` | ✅ |
| `BT.16` | `UCIS_DISABLEDBIN` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_every_bin_type_in_the_corpus_survives` | ✅ |
| `BT.17` | `UCIS_ATTEMPTBIN` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_every_bin_type_in_the_corpus_survives` | ✅ |
| `BT.18` | `UCIS_ACTIVEBIN` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_every_bin_type_in_the_corpus_survives` | ✅ |
| `BT.19` | `UCIS_PEAKACTIVEBIN` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) [Merge is type-aware — not everything is `SUM`](ucis-parquet-mapping.md#merge-is-type-aware--not-everything-is-sum) | L0: `test_merge_is_not_uniformly_sum`<br>L1: `test_every_bin_type_in_the_corpus_survives`<br>L3: `test_peak_active_takes_the_max_not_the_sum`, `test_peak_active_takes_the_maximum` | ✅ |
| `BT.20` | `UCIS_IGNOREBIN` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_every_bin_type_in_the_corpus_survives` | ✅ |
| `BT.21` | `UCIS_ILLEGALBIN` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_every_bin_type_in_the_corpus_survives` | ✅ |
| `BT.22` | `UCIS_DEFAULTBIN` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_every_bin_type_in_the_corpus_survives` | ✅ |
| `BT.23` | `UCIS_BLOCKBIN` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_every_bin_type_in_the_corpus_survives` | ✅ |
| `BT.24` | `UCIS_COUNT` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) [`counts`](ucis-parquet-mapping.md#counts) | — | 🟡 |
| `BT.25` | `UCIS_USERBITS` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | — | 🟡 |
| `BT.26` | `UCIS_RESERVEDBIN` | — | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | — | 🟡 |
| `C6.1` | `ucis_CreateNextCover(db, parent, name, data, srcinfo)` → cover index | § 8.11.1 | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_round_trip_loses_nothing`<br>L2: `test_scope_and_bin_structure_agrees`<br>L4: `test_q1_total_and_hit_bins_for_a_design_unit` | ✅ |
| `C6.2` | `ucis_RemoveCover(db, parent, coverindex)` — *deferred* | § 8.11.2 | — | — | — | ⚪ |
| `C6.3` | `ucis_GetCoverData(db, parent, coverindex, &name, &data, &srcinfo)` | § 8.11.3 | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_every_api_fact_survives`, `test_round_trip_loses_nothing`<br>L2: `test_cover_type_mask_filtering_agrees` | ✅ |
| `C6.4` | `ucis_SetCoverData(db, parent, coverindex, data)` | § 8.11.4 | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | — | 🟡 |
| `C6.5` | `ucis_IncrementCover(db, parent, coverindex, delta)` — incremental update — *deferred* | § 8.11.5 | — | — | — | ⚪ |
| `C6.6` | `ucis_GetCoverFlag` / `ucis_SetCoverFlag` / `ucis_GetCoverFlags` | § 8.11.6–8 | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) [Flag bits are type-qualified](ucis-parquet-mapping.md#flag-bits-are-type-qualified) | L1: `test_every_api_fact_survives`, `test_every_bin_type_in_the_corpus_survives`<br>L4: `test_q6_excluded_bins`, `test_the_same_flag_bit_means_different_things_by_type` | ✅ |

### 7. Iteration / traversal (§ 8.6–8.10)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `IT.1` | `ucis_ScopeIterate(db, parent, mask)` → iterator | § 8.6.1 | core | [Identity & ordering rules](ucis-parquet-mapping.md#identity--ordering-rules) [Access mapping (iterators ↔ queries)](ucis-parquet-mapping.md#access-mapping-iterators--queries) | L1: `test_scope_count_and_order`<br>L2: `test_scope_iteration_order_agrees`, `test_scope_mask_filtering_agrees`, `test_scope_type_mask_filtering_agrees`<br>L4: `test_q2_full_scope_path_of_a_bin` | ✅ |
| `IT.2` | `ucis_ScopeScan(db, iter)` → next scope | § 8.6.2 | core | [Identity & ordering rules](ucis-parquet-mapping.md#identity--ordering-rules) [Access mapping (iterators ↔ queries)](ucis-parquet-mapping.md#access-mapping-iterators--queries) | L1: `test_scope_count_and_order`<br>L2: `test_scope_iteration_order_agrees` | ✅ |
| `IT.3` | `ucis_FreeIterator(db, iter)` — *out of scope* | § 8.6.3 | — | — | — | ⚪ |
| `IT.4` | `ucis_CoverIterate(db, scope, covermask)` | § 8.7.1 | core | [Identity & ordering rules](ucis-parquet-mapping.md#identity--ordering-rules) [Access mapping (iterators ↔ queries)](ucis-parquet-mapping.md#access-mapping-iterators--queries) | L2: `test_cover_iteration_order_agrees`, `test_cover_type_mask_filtering_agrees`, `test_type_mask_filtering_agrees` | ✅ |
| `IT.5` | `ucis_CoverScan(db, iter, &index)` | § 8.7.2 | core | [Identity & ordering rules](ucis-parquet-mapping.md#identity--ordering-rules) [Access mapping (iterators ↔ queries)](ucis-parquet-mapping.md#access-mapping-iterators--queries) | L2: `test_cover_iteration_order_agrees` | ✅ |
| `IT.6` | `ucis_HistoryIterate(db, parent, kind)` | § 8.8.1 | core | [Access mapping (iterators ↔ queries)](ucis-parquet-mapping.md#access-mapping-iterators--queries) | — | 🟡 |
| `IT.7` | `ucis_HistoryScan(db, iter)` | § 8.8.2 | core | [Access mapping (iterators ↔ queries)](ucis-parquet-mapping.md#access-mapping-iterators--queries) | — | 🟡 |
| `IT.8` | `ucis_TaggedObjIterate(db, tag)` / `TaggedObjScan` | § 8.9 | *ext* | [Tags and user attributes](ucis-parquet-mapping.md#tags-and-user-attributes) | — | 🟡 |
| `IT.9` | `ucis_ObjectTagsIterate(db, obj)` / `ObjectTagsScan` | § 8.10 | *ext* | [Tags and user attributes](ucis-parquet-mapping.md#tags-and-user-attributes) | L1: `test_attributes_and_tags_survive`, `test_tags_and_attributes_survive_on_every_object_kind` | ✅ |

### 8. Properties (§ 8.3)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `PH.1` | `UCIS_HANDLE_SCOPE_PARENT` — parent scope | § 8.3.4 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L4: `test_handle_properties_resolve_through_foreign_keys` | ✅ |
| `PH.2` | `UCIS_HANDLE_SCOPE_TOP` — top (root) scope | § 8.3.4 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L4: `test_handle_properties_resolve_through_foreign_keys` | ✅ |
| `PH.3` | `UCIS_HANDLE_INSTANCE_DU` — the instance's design-unit scope | § 8.3.4 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L4: `test_handle_properties_resolve_through_foreign_keys` | ✅ |
| `PH.4` | `UCIS_HANDLE_HIST_NODE_PARENT` — parent history node | § 8.3.4 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L4: `test_handle_properties_resolve_through_foreign_keys` | ✅ |
| `PH.5` | `UCIS_HANDLE_HIST_NODE_ROOT` — top (root) history node | § 8.3.4 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L4: `test_handle_properties_resolve_through_foreign_keys` | ✅ |
| `PH.6` | UCIS_HANDLE_SCOPE_DU (not defined by UCIS 1.0) — *out of scope* | § 8.3.4 | — | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | ⚪ |
| `PI.1` | `UCIS_INT_IS_MODIFIED` — modified since the stored UCISDB was opened (read-only) — *out of scope* | § 8.3.1 | — | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | ⚪ |
| `PI.2` | `UCIS_INT_MODIFIED_SINCE_SIM` — modified since the end of the simulation run (read-only) — *out of scope* | § 8.3.1 | — | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | ⚪ |
| `PI.3` | `UCIS_INT_NUM_TESTS` — number of UCIS_HISTORYNODE_TEST nodes (read-only) | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PI.4` | `UCIS_INT_SCOPE_WEIGHT` — scope weight | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_a_promoted_property_is_a_column_instead_of_a_row`, `test_every_scope_property_the_source_answers_survives` | ✅ |
| `PI.5` | `UCIS_INT_SCOPE_GOAL` — scope goal | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_a_promoted_property_is_a_column_instead_of_a_row`, `test_every_scope_property_the_source_answers_survives` | ✅ |
| `PI.6` | `UCIS_INT_SCOPE_SOURCE_TYPE` — source language of the scope (`ucisSourceT`, SL.*) | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_a_promoted_property_is_a_column_instead_of_a_row`, `test_every_scope_property_the_source_answers_survives`, `test_the_source_language_of_a_scope_round_trips` | ✅ |
| `PI.7` | `UCIS_INT_NUM_CROSSED_CVPS` — number of coverpoints in a cross (read-only) | § 8.3.1 | core | [`cross_operands`](ucis-parquet-mapping.md#cross_operands) [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_the_crossed_coverpoint_list_survives`, `test_the_operand_count_is_the_list_and_not_a_stored_number`<br>L4: `test_a_cross_names_its_operands_in_order` | ✅ |
| `PI.8` | `UCIS_INT_SCOPE_IS_UNDER_DU` — scope is underneath a design-unit scope (read-only) | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_every_scope_property_the_source_answers_survives` | ✅ |
| `PI.9` | `UCIS_INT_SCOPE_IS_UNDER_COVERINSTANCE` — scope is underneath a covergroup instance (read-only) | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PI.10` | `UCIS_INT_SCOPE_NUM_COVERITEMS` — number of coveritems under the scope (read-only) | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PI.11` | `UCIS_INT_SCOPE_NUM_EXPR_TERMS` — number of '#'-delimited expression terms | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_every_scope_property_the_source_answers_survives` | ✅ |
| `PI.12` | `UCIS_INT_TOGGLE_TYPE` — toggle type (`ucisToggleTypeT`) | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_every_scope_property_the_source_answers_survives` | ✅ |
| `PI.13` | `UCIS_INT_TOGGLE_DIR` — toggle direction (`ucisToggleDirT`) | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_every_scope_property_the_source_answers_survives` | ✅ |
| `PI.14` | `UCIS_INT_TOGGLE_COVERED` — the toggle object is covered | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PI.15` | `UCIS_INT_BRANCH_HAS_ELSE` — branch has an 'else' coveritem | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PI.16` | `UCIS_INT_BRANCH_ISCASE` — branch represents a 'case' statement | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PI.17` | `UCIS_INT_COVER_GOAL` — coveritem goal | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_per_bin_properties_are_addressed_by_cover_index` | ✅ |
| `PI.18` | `UCIS_INT_COVER_LIMIT` — coverage count limit for the coveritem | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_per_bin_properties_are_addressed_by_cover_index` | ✅ |
| `PI.19` | `UCIS_INT_COVER_WEIGHT` — coveritem weight | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_per_bin_properties_are_addressed_by_cover_index` | ✅ |
| `PI.20` | `UCIS_INT_TEST_STATUS` — test run status (`ucisTestStatusT`) | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_history_node_properties_survive` | ✅ |
| `PI.21` | `UCIS_INT_TEST_COMPULSORY` — test run is compulsory | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PI.22` | `UCIS_INT_STMT_INDEX` — index of the statement on a line | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_every_scope_property_the_source_answers_survives` | ✅ |
| `PI.23` | `UCIS_INT_BRANCH_COUNT` — total branch execution count | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PI.24` | `UCIS_INT_FSM_STATEVAL` — FSM state value | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PI.25` | `UCIS_INT_CVG_ATLEAST` — covergroup `at_least` option | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_every_scope_property_the_source_answers_survives` | ✅ |
| `PI.26` | `UCIS_INT_CVG_AUTOBINMAX` — covergroup `auto_bin_max` option | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_every_scope_property_the_source_answers_survives` | ✅ |
| `PI.27` | `UCIS_INT_CVG_DETECTOVERLAP` — covergroup `detect_overlap` option | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_every_scope_property_the_source_answers_survives` | ✅ |
| `PI.28` | `UCIS_INT_CVG_NUMPRINTMISSING` — covergroup `cross_num_print_missing` option | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PI.29` | `UCIS_INT_CVG_STROBE` — covergroup `strobe` option | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_every_scope_property_the_source_answers_survives` | ✅ |
| `PI.30` | `UCIS_INT_CVG_PERINSTANCE` — covergroup `per_instance` option | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_every_scope_property_the_source_answers_survives` | ✅ |
| `PI.31` | `UCIS_INT_CVG_GETINSTCOV` — covergroup `get_inst_coverage` option | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PI.32` | `UCIS_INT_CVG_MERGEINSTANCES` — covergroup `merge_instances` option | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_every_scope_property_the_source_answers_survives` | ✅ |
| `PI.33` | `UCIS_INT_TOGGLE_METRIC` — toggle metric specialization (`ucisToggleMetricT`) | § 8.3.1 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_every_scope_property_the_source_answers_survives` | ✅ |
| `PI.34` | `UCIS_INT_SUPPRESS_MODIFIED` — suppress the in-memory modification flag — *out of scope* | § 8.3.1 | — | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | ⚪ |
| `PI.35` | UCIS_INT_SCOPE_PERINSTANCE (not defined by UCIS 1.0) — *out of scope* | § 8.3.1 | — | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | ⚪ |
| `PP.1` | `ucis_GetIntProperty(db, obj, coverindex, property)` | § 8.3.5 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_every_scope_property_the_source_answers_survives`, `test_prop_id_is_the_enum_name_without_its_prefix` | ✅ |
| `PP.2` | `ucis_SetIntProperty(db, obj, coverindex, property, value)` | § 8.3.6 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PP.3` | `ucis_GetRealProperty(db, obj, coverindex, property)` | § 8.3.7 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PP.4` | `ucis_SetRealProperty(db, obj, coverindex, property, value)` | § 8.3.8 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PP.5` | `ucis_GetStringProperty(db, obj, coverindex, property)` | § 8.3.9 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_every_scope_property_the_source_answers_survives`, `test_prop_id_is_the_enum_name_without_its_prefix` | ✅ |
| `PP.6` | `ucis_SetStringProperty(db, obj, coverindex, property, value)` | § 8.3.10 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PP.7` | `ucis_GetHandleProperty(db, obj, property)` | § 8.3.11 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PP.8` | `ucis_SetHandleProperty(db, obj, property, value)` | § 8.3.12 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PP.9` | coverindex semantics: `-1` = the scope, `>= 0` = the cover at that index, ignored for history nodes | § 8.3.5 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_per_bin_properties_are_addressed_by_cover_index` | ✅ |
| `PR.1` | `UCIS_REAL_HIST_CPUTIME` — test run CPU time | § 8.3.3 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PR.2` | `UCIS_REAL_TEST_SIMTIME` — test run simulation time | § 8.3.3 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_history_node_properties_survive` | ✅ |
| `PR.3` | `UCIS_REAL_TEST_COST` — relative cost of re-running the test | § 8.3.3 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PR.4` | `UCIS_REAL_CVG_INST_AVERAGE` — average coverage across covergroup instances | § 8.3.3 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PS.1` | `UCIS_STR_FILE_NAME` — UCISDB file/directory name (read-only) | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PS.2` | `UCIS_STR_SCOPE_NAME` — scope name | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_a_promoted_property_is_a_column_instead_of_a_row`, `test_every_scope_property_the_source_answers_survives` | ✅ |
| `PS.3` | `UCIS_STR_SCOPE_HIER_NAME` — hierarchical scope name | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_every_scope_property_the_source_answers_survives` | ✅ |
| `PS.4` | `UCIS_STR_INSTANCE_DU_NAME` — the instance's design-unit name | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PS.5` | `UCIS_STR_UNIQUE_ID` — scope or coveritem unique id (read-only) | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_a_promoted_property_is_a_column_instead_of_a_row`, `test_every_scope_property_the_source_answers_survives` | ✅ |
| `PS.6` | `UCIS_STR_VER_STANDARD` — standard, fixed to "UCIS" | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PS.7` | `UCIS_STR_VER_STANDARD_VERSION` — version of the standard | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PS.8` | `UCIS_STR_VER_VENDOR_ID` — vendor id | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PS.9` | `UCIS_STR_VER_VENDOR_TOOL` — vendor tool | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PS.10` | `UCIS_STR_VER_VENDOR_VERSION` — vendor tool version | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PS.11` | `UCIS_STR_GENERIC` — miscellaneous string data | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_every_scope_property_the_source_answers_survives` | ✅ |
| `PS.12` | `UCIS_STR_ITH_CROSSED_CVP_NAME` — i-th crossed coverpoint name of a cross | § 8.3.2 | core | [`cross_operands`](ucis-parquet-mapping.md#cross_operands) [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_a_cross_with_only_a_name_still_round_trips`, `test_every_scope_property_the_source_answers_survives`, `test_the_crossed_coverpoint_list_survives`<br>L4: `test_a_cross_names_its_operands_in_order` | ✅ |
| `PS.13` | `UCIS_STR_HIST_CMDLINE` — test run command line | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_history_node_properties_survive` | ✅ |
| `PS.14` | `UCIS_STR_HIST_RUNCWD` — test run working directory | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_history_node_properties_survive` | ✅ |
| `PS.15` | `UCIS_STR_COMMENT` — comment | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_every_scope_property_the_source_answers_survives` | ✅ |
| `PS.16` | `UCIS_STR_TEST_TIMEUNIT` — test run simulation time unit | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_history_node_properties_survive` | ✅ |
| `PS.17` | `UCIS_STR_TEST_DATE` — test run date | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_history_node_properties_survive` | ✅ |
| `PS.18` | `UCIS_STR_TEST_SIMARGS` — test run simulator arguments | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PS.19` | `UCIS_STR_TEST_USERNAME` — test run user name | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_history_node_properties_survive` | ✅ |
| `PS.20` | `UCIS_STR_TEST_NAME` — test run name | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PS.21` | `UCIS_STR_TEST_SEED` — test run seed | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_history_node_properties_survive` | ✅ |
| `PS.22` | `UCIS_STR_TEST_HOSTNAME` — test run hostname | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PS.23` | `UCIS_STR_TEST_HOSTOS` — test run host OS | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PS.24` | `UCIS_STR_EXPR_TERMS` — input-ordered expression terms delimited by '#' | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_every_scope_property_the_source_answers_survives` | ✅ |
| `PS.25` | `UCIS_STR_TOGGLE_CANON_NAME` — toggle object canonical name | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_every_scope_property_the_source_answers_survives` | ✅ |
| `PS.26` | `UCIS_STR_UNIQUE_ID_ALIAS` — scope or coveritem unique-id alias | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PS.27` | `UCIS_STR_DESIGN_VERSION_ID` — version of the design / elaboration id | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PS.28` | `UCIS_STR_DU_SIGNATURE` — design-unit signature, for source-change detection | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | 🟡 |
| `PS.29` | `UCIS_STR_HIST_TOOLCATEGORY` — tool category of the history node | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_history_node_properties_survive` | ✅ |
| `PS.30` | `UCIS_STR_HIST_LOG_NAME` — logical name of the history node | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_history_node_properties_survive` | ✅ |
| `PS.31` | `UCIS_STR_HIST_PHYS_NAME` — history node physical name | § 8.3.2 | core | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | L1: `test_history_node_properties_survive` | ✅ |
| `PS.32` | UCIS_STR_FSM_STATEVAR (not defined by UCIS 1.0) — *out of scope* | § 8.3.2 | — | [The UCIS property vocabulary](ucis-parquet-mapping.md#the-ucis-property-vocabulary) | — | ⚪ |

### 9. User-defined attributes (§ 3.4.2, § 8.4)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `A9.1` | `ucis_AttrAdd(db, obj, coverindex, key, value)` | § 8.4.1 | core | [`properties`](ucis-parquet-mapping.md#properties) | L1: `test_attributes_and_tags_survive`, `test_coveritem_attributes_survive`, `test_tags_and_attributes_survive_on_every_object_kind`<br>L4: `test_q5_standard_and_extension_properties_on_a_scope` | ✅ |
| `A9.2` | `ucis_AttrMatch(db, obj, coverindex, key)` → iterator-style match | § 8.4.2 | core | [`properties`](ucis-parquet-mapping.md#properties) | L1: `test_attributes_and_tags_survive` | ✅ |
| `A9.3` | `ucis_AttrNext(db, obj, coverindex, prev_key, &key, &value)` | § 8.4.3 | core | [`properties`](ucis-parquet-mapping.md#properties) | L1: `test_attributes_and_tags_survive` | ✅ |
| `A9.4` | `ucis_AttrRemove(db, obj, coverindex, key)` | § 8.4.4 | core | [`properties`](ucis-parquet-mapping.md#properties) | L1: `test_coveritem_attributes_survive` | ✅ |
| `A9.5` | Attr value types: `INT`, `INT64`, `FLOAT`, `DOUBLE`, `STRING`, `MEMBLK` | § 8.4 | core | [`properties`](ucis-parquet-mapping.md#properties) | L1: `test_attributes_and_tags_survive`, `test_tags_and_attributes_survive_on_every_object_kind` | ✅ |

### 10. Source files (§ 8.12)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `F10.1` | `ucis_CreateFileHandle(db, filename, fileworkdir)` | § 8.12.2 | core | [`source_files`](ucis-parquet-mapping.md#source_files) | L1: `test_source_files_survive`, `test_source_locations_resolve_through_the_file_table` | ✅ |
| `F10.2` | `ucis_GetFileName(db, fh)` | § 8.12.3 | core | [`source_files`](ucis-parquet-mapping.md#source_files) | L1: `test_source_files_survive` | ✅ |
| `F10.3` | `ucisSourceInfoT { filehandle, line, token }` — per-scope/per-cover loc | § 3.4.7 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | L1: `test_source_locations_resolve_through_the_file_table` | ✅ |
| `F10.4` | Per-DU file table (file IDs are local to a DU) | § 6.3.3 | core | [`source_files`](ucis-parquet-mapping.md#source_files) | L1: `test_source_files_survive`, `test_source_locations_resolve_through_the_file_table` | ✅ |

### 11. History nodes and test data (§ 4.3, § 8.13, § 8.14)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `H11.1` | `ucis_CreateHistoryNode(db, parent, logicalname, physicalname, kind)` | § 8.13.1 | core | [`history_nodes`](ucis-parquet-mapping.md#history_nodes) | L1: `test_each_run_keeps_its_own_history`, `test_history_and_test_data_survive`, `test_history_tree_and_test_data_survive`<br>L2: `test_history_agrees`<br>L3: `test_identity_a_single_run_merges_to_itself`<br>L4: `test_q3_per_run_and_merged_counts` | ✅ |
| `H11.2` | `ucis_RemoveHistoryNode(db, node)` — *deferred* | § 8.13.2 | — | — | — | ⚪ |
| `H11.3` | `ucis_GetHistoryKind(db, node)` | § 8.13.3 | core | [`history_nodes`](ucis-parquet-mapping.md#history_nodes) | L1: `test_history_tree_and_test_data_survive`<br>L2: `test_history_agrees` | ✅ |
| `H11.4` | `ucis_SetTestData(db, node, &data)` / `ucis_GetTestData` | § 8.14.2–3 | core | [`history_nodes`](ucis-parquet-mapping.md#history_nodes) | L1: `test_history_and_test_data_survive`, `test_history_tree_and_test_data_survive` | ✅ |
| `H11.5` | History-node kinds: `UCIS_HISTORYNODE_NONE`, `_ALL`, `_TEST`, `_MERGE` | § 4.3.1 | core | [`history_nodes`](ucis-parquet-mapping.md#history_nodes) [Enumerated column encodings](ucis-parquet-mapping.md#enumerated-column-encodings) | L1: `test_history_tree_and_test_data_survive`<br>L2: `test_history_agrees`<br>L3: `test_materialized_merge_gains_a_merge_node` | ✅ |
| `H11.6` | Tool-category strings: `UCIS_SIM_TOOL`, `UCIS_FORMAL_TOOL`, `UCIS_ANALOG_TOOL`, `UCIS_EMULATOR_TOOL`, `UCIS_MERGE_TOOL` | § 4.3 | core | [`history_nodes`](ucis-parquet-mapping.md#history_nodes) | — | 🟡 |
| `H11.7` | Test-status enum: `OK`, `WARNING`, `ERROR`, `FATAL`, `MISSING`, `MERGE_ERROR` | § 8.14.1 | core | [`history_nodes`](ucis-parquet-mapping.md#history_nodes) | L1: `test_history_and_test_data_survive`, `test_history_tree_and_test_data_survive` | ✅ |
| `H11.8` | Test-data record fields: `teststatus`, `simtime`, `timeunit`, `runcwd`, `cputime`, `seed`, `cmd`, `args`, `compulsory`, `date`, `username`, `cost`, `toolcategory` | § 8.14 | core | [`history_nodes`](ucis-parquet-mapping.md#history_nodes) [Tables added](ucis-parquet-mapping.md#tables-added) | L1: `test_history_and_test_data_survive`, `test_history_tree_and_test_data_survive` | ✅ |
| `H11.9` | Universal-Object-Recognition compliance flags (UOR safe scopes/coveritems) | § 4.3.2, § 6.1.1 | core | [`scopes`](ucis-parquet-mapping.md#scopes) | — | 🟡 |

### 12. History-node lists / test↔coveritem association (§ 8.17)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `HL.1` | `ucis_CreateHistoryNodeList(db)` | § 8.17.2 | *ext* | [`test_cover_assoc`](ucis-parquet-mapping.md#test_cover_assoc) | — | 🟡 |
| `HL.2` | `ucis_FreeHistoryNodeList(db, list)` — *out of scope* | § 8.17.3 | — | — | — | ⚪ |
| `HL.3` | `ucis_AddToHistoryNodeList(list, node)` | § 8.17.4 | *ext* | [`test_cover_assoc`](ucis-parquet-mapping.md#test_cover_assoc) | — | 🟡 |
| `HL.4` | `ucis_RemoveFromHistoryNodeList(list, node)` | § 8.17.5 | *ext* | [`test_cover_assoc`](ucis-parquet-mapping.md#test_cover_assoc) | — | 🟡 |
| `HL.5` | `ucis_HistoryNodeListIterate(db, list)` | § 8.17.6 | *ext* | [`test_cover_assoc`](ucis-parquet-mapping.md#test_cover_assoc) | — | 🟡 |
| `HL.6` | `ucis_SetHistoryNodeListAssoc(db, list, obj, coverindex)` | § 8.17.7 | *ext* | [`test_cover_assoc`](ucis-parquet-mapping.md#test_cover_assoc) [Enumerated column encodings](ucis-parquet-mapping.md#enumerated-column-encodings) [Association tiers](ucis-parquet-mapping.md#association-tiers) | L1: `test_test_cover_associations_survive`<br>L2: `test_test_associations_agree`<br>L3: `test_associations_union_across_runs`<br>L4: `test_q4_contributing_tests_including_the_tier` | ✅ |
| `HL.7` | `ucis_GetHistoryNodeListAssoc(db, obj, coverindex)` → list | § 8.17.8 | *ext* | [`test_cover_assoc`](ucis-parquet-mapping.md#test_cover_assoc) [Enumerated column encodings](ucis-parquet-mapping.md#enumerated-column-encodings) [Association tiers](ucis-parquet-mapping.md#association-tiers) | L1: `test_test_cover_associations_survive`<br>L2: `test_contributing_tests_are_queryable`, `test_test_associations_agree`<br>L4: `test_q4_contributing_tests_including_the_tier` | ✅ |

### 13. Tags (§ 3.4.4, § 8.16)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `T13.1` | `ucis_ObjKind(db, obj)` | § 8.16.1 | *ext* | [Tags and user attributes](ucis-parquet-mapping.md#tags-and-user-attributes) | — | 🟡 |
| `T13.2` | `ucis_AddObjTag(db, obj, tagname)` | § 8.16.2 | *ext* | [Tags and user attributes](ucis-parquet-mapping.md#tags-and-user-attributes) | L1: `test_attributes_and_tags_survive`, `test_tags_and_attributes_survive_on_every_object_kind` | ✅ |
| `T13.3` | `ucis_RemoveObjTag(db, obj, tagname)` | § 8.16.3 | *ext* | [Tags and user attributes](ucis-parquet-mapping.md#tags-and-user-attributes) | — | 🟡 |
| `T13.4` | Tag traversal (see IT.8, IT.9) | § 8.9, 8.10 | *ext* | [Tags and user attributes](ucis-parquet-mapping.md#tags-and-user-attributes) | — | 🟡 |
| `T13.5` | `UCIS_GROUP` scope as tag-group container | § 6 | *ext* | [Tags and user attributes](ucis-parquet-mapping.md#tags-and-user-attributes) | L1: `test_attributes_and_tags_survive`, `test_tags_and_attributes_survive_on_every_object_kind` | ✅ |

### 14. Toggle coverage (§ 6.5.5, § 8.15)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `TG.1` | `UCIS_TOGGLE` scope with `UCIS_TOGGLEBIN` children | § 6.5.5 | core | [Toggle coverage](ucis-parquet-mapping.md#toggle-coverage) | L1: `test_toggle_bins_are_addressed_by_name` | ✅ |
| `TG.2` | `ucisToggleMetricT`: `NOBINS`, `ENUM`, `TRANSITION`, `2STOGGLE`, `ZTOGGLE`, `XTOGGLE` | § 6.5.5 | core | [Toggle coverage](ucis-parquet-mapping.md#toggle-coverage) | L1: `test_toggle_metadata_lands_in_the_properties_table`, `test_toggle_properties_do_not_depend_on_the_parent_scope` | ✅ |
| `TG.3` | `ucisToggleTypeT`: `NET`, `REG` | § 6.5.5 | core | [Toggle coverage](ucis-parquet-mapping.md#toggle-coverage) | L1: `test_toggle_metadata_lands_in_the_properties_table`, `test_toggle_properties_do_not_depend_on_the_parent_scope` | ✅ |
| `TG.4` | `ucisToggleDirT`: `INTERNAL`, `IN`, `OUT`, `INOUT` | § 6.5.5 | core | [Toggle coverage](ucis-parquet-mapping.md#toggle-coverage) | L1: `test_toggle_metadata_lands_in_the_properties_table`, `test_toggle_properties_do_not_depend_on_the_parent_scope` | ✅ |
| `TG.5` | `UCIS_STR_TOGGLE_CANON_NAME` for net-aliasing (§ 4.10) — *deferred* | § 4.10 | — | — | — | ⚪ |
| `TG.6` | Toggle properties: `UCIS_INT_TOGGLE_TYPE/DIR/METRIC/COVERED` | § 6.5.5 | core | [`properties`](ucis-parquet-mapping.md#properties) | L1: `test_toggle_metadata_lands_in_the_properties_table`, `test_toggle_properties_do_not_depend_on_the_parent_scope` | ✅ |
| `TG.7` | Toggle XML schema: `TOGGLE_OBJECT`, `TOGGLE_BIT`, `TOGGLE` — *out of scope* | § 9.7 | — | — | — | ⚪ |

### 15. FSM coverage (§ 6.5.6)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `FS.1` | `UCIS_FSM` scope with `UCIS_STR_FSM_STATEVAR` (state-var name) | § 6.5.6 | core | [FSM coverage](ucis-parquet-mapping.md#fsm-coverage) | L1: `test_fsm_states_and_transitions_are_sibling_scopes` | ✅ |
| `FS.2` | `UCIS_FSM_STATES` child scope with `UCIS_STATEBIN` per state | § 6.5.6 | core | [FSM coverage](ucis-parquet-mapping.md#fsm-coverage) | L1: `test_fsm_states_and_transitions_are_sibling_scopes` | ✅ |
| `FS.3` | `UCIS_FSM_TRANS` child scope with `UCIS_TRANSBIN` per arc | § 6.5.6 | core | [FSM coverage](ucis-parquet-mapping.md#fsm-coverage) | L1: `test_fsm_states_and_transitions_are_sibling_scopes` | ✅ |
| `FS.4` | `UCIS_INT_FSM_STATEVAL` integer property on state bins | § 6.5.6 | core | — | — | ❌ |
| `FS.5` | `ucis_CreateNextTransition` constructor for arcs | § 8.5.11 | core | — | — | ❌ |
| `FS.6` | `ucis_GetFSMTransitionStates(db, trans, &src, &dst)` | § 8.5.12 | core | — | — | ❌ |
| `FS.7` | Coveritem flags: `UCIS_IS_FSM_RESET`, `UCIS_IS_FSM_TRAN` | § 6 | core | [Flag bits are type-qualified](ucis-parquet-mapping.md#flag-bits-are-type-qualified) | L4: `test_the_same_flag_bit_means_different_things_by_type` | ✅ |
| `FS.8` | XML schema: `FSM`, `FSM_STATE`, `FSM_TRANSITION` — *out of scope* | § 9.11 | — | — | — | ⚪ |

### 16. Branch coverage (§ 6.5.2, § 9.13)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `BR.1` | `UCIS_BRANCH` scope with `UCIS_BRANCHBIN` children | § 6.5.2 | core | [Statement, branch, condition and expression coverage](ucis-parquet-mapping.md#statement-branch-condition-and-expression-coverage) | L1: `test_code_coverage_shapes_survive` | ✅ |
| `BR.2` | `UCIS_INT_BRANCH_HAS_ELSE`, `UCIS_INT_BRANCH_ISCASE` | § 6.5.2 | core | [`properties`](ucis-parquet-mapping.md#properties) | — | 🟡 |
| `BR.3` | `UCIS_IS_BR_ELSE` coveritem flag (else-branch marker) | § 6.5.2 | core | [Flag bits are type-qualified](ucis-parquet-mapping.md#flag-bits-are-type-qualified) | L4: `test_the_same_flag_bit_means_different_things_by_type` | ✅ |
| `BR.4` | XML schema: `BRANCH_STATEMENT`, `BRANCH` — *out of scope* | § 9.13 | — | — | — | ⚪ |

### 17. Statement / block coverage (§ 6.5.3, § 9.12)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `SB.1` | `UCIS_BLOCK` scope + `UCIS_STMTBIN` per line/statement | § 6.5.3 | core | [Statement, branch, condition and expression coverage](ucis-parquet-mapping.md#statement-branch-condition-and-expression-coverage) | L1: `test_code_coverage_shapes_survive` | ✅ |
| `SB.2` | `UCIS_BLOCKBIN` for block-level aggregate | § 6.5.3 | core | [Statement, branch, condition and expression coverage](ucis-parquet-mapping.md#statement-branch-condition-and-expression-coverage) | L1: `test_code_coverage_shapes_survive` | ✅ |
| `SB.3` | `UCIS_INT_STMT_INDEX` integer property | § 6.5.3 | core | — | — | ❌ |
| `SB.4` | XML schema: `STATEMENT`, `PROCESS_BLOCK`, `BLOCK` — *out of scope* | § 9.12 | — | — | — | ⚪ |

### 18. Condition and expression coverage (§ 6.5.4, § 9.9)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `CX.1` | `UCIS_EXPR` / `UCIS_COND` scope with `UCIS_EXPRBIN` / `UCIS_CONDBIN` children (truth-table rows) | § 6.5.4 | core | [Statement, branch, condition and expression coverage](ucis-parquet-mapping.md#statement-branch-condition-and-expression-coverage) | L1: `test_code_coverage_shapes_survive` | ✅ |
| `CX.2` | `UCIS_INT_SCOPE_NUM_EXPR_TERMS` + `UCIS_STR_EXPR_TERMS` | § 6.5.4 | core | [`properties`](ucis-parquet-mapping.md#properties) | L1: `test_code_coverage_shapes_survive` | ✅ |
| `CX.3` | `UCIS_SCOPE_EXPR_ISHIERARCHICAL` flag | § 6.5.4 | core | [`properties`](ucis-parquet-mapping.md#properties) [Flag bits are type-qualified](ucis-parquet-mapping.md#flag-bits-are-type-qualified) | — | 🟡 |
| `CX.4` | XML schema: `EXPR` — *out of scope* | § 9.9 | — | — | — | ⚪ |

### 19. Covergroup coverage (§ 6.4, § 9.8)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `CG.1` | `UCIS_COVERGROUP` type scope | § 6.4.2 | core | [Covergroups, coverpoints and crosses](ucis-parquet-mapping.md#covergroups-coverpoints-and-crosses) | L1: `test_covergroup_shape_survives` | ✅ |
| `CG.2` | `UCIS_COVERINSTANCE` per-instance specialization | § 6.4.2 | core | [Covergroups, coverpoints and crosses](ucis-parquet-mapping.md#covergroups-coverpoints-and-crosses) | — | 🟡 |
| `CG.3` | `UCIS_COVERPOINT` scope with `UCIS_CVGBIN` children | § 6.4.2 | core | [Covergroups, coverpoints and crosses](ucis-parquet-mapping.md#covergroups-coverpoints-and-crosses) | L1: `test_covergroup_shape_survives` | ✅ |
| `CG.4` | `UCIS_CROSS` scope referencing N coverpoints (built via `CreateCross`) | § 6.4.2 | core | [Covergroups, coverpoints and crosses](ucis-parquet-mapping.md#covergroups-coverpoints-and-crosses) | L1: `test_covergroup_shape_survives` | ✅ |
| `CG.5` | Bin-scope variants: `UCIS_CVGBINSCOPE`, `UCIS_ILLEGALBINSCOPE`, `UCIS_IGNOREBINSCOPE` | § 6.4.2 | core | [Covergroups, coverpoints and crosses](ucis-parquet-mapping.md#covergroups-coverpoints-and-crosses) | L1: `test_covergroup_shape_survives` | ✅ |
| `CG.6` | Per-bin classification: `UCIS_IGNOREBIN`, `UCIS_ILLEGALBIN`, `UCIS_DEFAULTBIN` | § 6.4.2 | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_covergroup_shape_survives` | ✅ |
| `CG.7` | Transition bins via `UCIS_TRANSITION` scope + `UCIS_SCOPE_CVG_TRANSITION` flag | § 6.4.2 | core | [Covergroups, coverpoints and crosses](ucis-parquet-mapping.md#covergroups-coverpoints-and-crosses) | — | 🟡 |
| `CG.8` | Coverpoint shape flags: `UCIS_SCOPE_CVG_AUTO/SCALAR/VECTOR/TRANSITION` | § 6.4.2 | core | [Flag bits are type-qualified](ucis-parquet-mapping.md#flag-bits-are-type-qualified) [Covergroups, coverpoints and crosses](ucis-parquet-mapping.md#covergroups-coverpoints-and-crosses) | — | 🟡 |
| `CG.9` | Covergroup options: `UCIS_INT_CVG_ATLEAST`, `AUTOBINMAX`, `DETECTOVERLAP`, `NUMPRINTMISSING`, `STROBE`, `PERINSTANCE`, `GETINSTCOV`, `MERGEINSTANCES` | § 6.4.2 | core | [`properties`](ucis-parquet-mapping.md#properties) | L1: `test_covergroup_options_survive`, `test_covergroup_shape_survives`<br>L2: `test_covergroup_properties_agree` | ✅ |
| `CG.10` | `UCIS_REAL_CVG_INST_AVERAGE` | § 6.4.2 | core | [`properties`](ucis-parquet-mapping.md#properties) | L1: `test_covergroup_options_survive` | ✅ |
| `CG.11` | Cross helpers: `UCIS_INT_NUM_CROSSED_CVPS`, `UCIS_STR_ITH_CROSSED_CVP_NAME`, `ucis_GetIthCrossedCvp` | § 8.5.18 | core | [`cross_operands`](ucis-parquet-mapping.md#cross_operands) | L1: `test_the_crossed_coverpoint_list_survives`, `test_the_operand_count_is_the_list_and_not_a_stored_number`<br>L2: `test_a_cross_resolves_its_operands_to_the_same_scopes`<br>L4: `test_a_cross_names_its_operands_in_order` | ✅ |
| `CG.12` | `UCIS_IS_CROSSAUTO` per-bin flag (auto-generated cross bin) | § 6.4.2 | core | [Flag bits are type-qualified](ucis-parquet-mapping.md#flag-bits-are-type-qualified) | L4: `test_the_same_flag_bit_means_different_things_by_type` | ✅ |
| `CG.13` | Bin guards: `UCIS_BIN_IFF_EXISTS`, `UCIS_BIN_SAMPLE_TRUE` | § 6.4.2 | core | [Flag bits are type-qualified](ucis-parquet-mapping.md#flag-bits-are-type-qualified) | L4: `test_the_same_flag_bit_means_different_things_by_type` | ✅ |
| `CG.14` | XML schema: `CGINSTANCE`, `CG_ID`, `CGINST_OPTIONS`, `COVERPOINT`, `COVERPOINT_OPTIONS`, `COVERPOINT_BIN`, `RANGE_VALUE`, `SEQUENCE`, `CROSS`, `CROSS_OPTIONS`, `CROSS_BIN` — *out of scope* | § 9.8 | — | — | — | ⚪ |

### 20. Assertion and cover (SVA/PSL) (§ 6.6, § 9.10)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `AS.1` | `UCIS_ASSERT` scope | § 6.6.2 | core | [Assertion and cover-property coverage](ucis-parquet-mapping.md#assertion-and-cover-property-coverage) | L1: `test_assertion_outcome_is_a_set_of_counter_bins` | ✅ |
| `AS.2` | `UCIS_COVER` scope (cover-property) | § 6.6.1 | core | [Assertion and cover-property coverage](ucis-parquet-mapping.md#assertion-and-cover-property-coverage) | L1: `test_assertion_outcome_is_a_set_of_counter_bins` | ✅ |
| `AS.3` | Assertion counter bins: `PASSBIN`, `FAILBIN`, `VACUOUSBIN`, `DISABLEDBIN`, `ATTEMPTBIN`, `ACTIVEBIN`, `PEAKACTIVEBIN` | § 6.6.2 | core | [Merge is type-aware — not everything is `SUM`](ucis-parquet-mapping.md#merge-is-type-aware--not-everything-is-sum) | L1: `test_assertion_outcome_is_a_set_of_counter_bins`<br>L3: `test_peak_active_takes_the_max_not_the_sum` | ✅ |
| `AS.4` | Cover-property bin: `UCIS_COVERBIN` | § 6.6.1 | core | [Assertion and cover-property coverage](ucis-parquet-mapping.md#assertion-and-cover-property-coverage) | L1: `test_assertion_outcome_is_a_set_of_counter_bins` | ✅ |
| `AS.5` | Sequence bin: `UCIS_SCBIN` | § 6.6.1 | core | [Assertion and cover-property coverage](ucis-parquet-mapping.md#assertion-and-cover-property-coverage) | — | 🟡 |
| `AS.6` | Immediate-assertion marker: `UCIS_IS_IMMEDIATE_ASSERT` | § 6.6.2 | core | [Flag bits are type-qualified](ucis-parquet-mapping.md#flag-bits-are-type-qualified) [Assertion and cover-property coverage](ucis-parquet-mapping.md#assertion-and-cover-property-coverage) | — | 🟡 |
| `AS.7` | XML schema: `ASSERTION` — *out of scope* | § 9.10 | — | — | — | ⚪ |

### 21. User-defined coverage (§ 6.6.3)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `UD.1` | `UCIS_USERBIN` / `UCIS_GENERICBIN` for arbitrary user-defined bins | § 6.6.3 | *ext* | [`coveritems`](ucis-parquet-mapping.md#coveritems) | L1: `test_code_coverage_shapes_survive` | ✅ |
| `UD.2` | `UCIS_USERBITS` reserved bit-range on cover types | § 6 | *ext* | [`coveritems`](ucis-parquet-mapping.md#coveritems) | — | 🟡 |
| `UD.3` | `UCIS_STR_GENERIC` generic string property | § 8.3.2 | *ext* | [`properties`](ucis-parquet-mapping.md#properties) | L1: `test_code_coverage_shapes_survive` | ✅ |
| `UD.4` | `UCIS_HAS_ACTION` / `UCIS_HAS_COUNT` cover flags | § 6 | *ext* | [Flag bits are type-qualified](ucis-parquet-mapping.md#flag-bits-are-type-qualified) | — | 🟡 |

### 22. Formal verification (§ 8.19)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `FM.1` | Formal-status enum + `ucis_SetFormalStatus` / `ucis_GetFormalStatus` | § 8.19.3–5 | *ext* | [Merge is type-aware — not everything is `SUM`](ucis-parquet-mapping.md#merge-is-type-aware--not-everything-is-sum) [Tables added](ucis-parquet-mapping.md#tables-added) | L1: `test_formal_data_survives`<br>L2: `test_formal_data_agrees`<br>L3: `test_contradictory_formal_status_merges_to_conflict`, `test_formal_status_merge_is_order_independent` | ✅ |
| `FM.2` | Formal-radius accessors (`SetFormalRadius`, `GetFormalRadius`) | § 8.19.6–7 | *ext* | [Tables added](ucis-parquet-mapping.md#tables-added) | L1: `test_formal_data_survives`<br>L2: `test_formal_data_agrees` | ✅ |
| `FM.3` | Formal-witness accessors (`SetFormalWitness`, `GetFormalWitness`) | § 8.19.8–9 | *ext* | [Tables added](ucis-parquet-mapping.md#tables-added) | L1: `test_formal_data_survives`<br>L2: `test_formal_data_agrees` | ✅ |
| `FM.4` | Formally-unreachable coveritem: `Set/GetFormallyUnreachableCoverTest` — *deferred* | § 8.19.11–12 | — | — | — | ⚪ |
| `FM.5` | Formal-env API: `AddFormalEnv`, `FormalEnvGetData`, `NextFormalEnv` — *deferred* | § 8.19.15–17 | — | — | — | ⚪ |
| `FM.6` | Test↔env association: `AssocFormalInfoTest` — *deferred* | § 8.19.18 | — | — | — | ⚪ |
| `FM.7` | Per-test formal info: `FormalTestGetInfo` — *deferred* | § 8.19.20 | — | — | — | ⚪ |
| `FM.8` | Assumption↔env association: `AssocAssumptionFormalEnv`, `NextFormalEnvAssumption` — *deferred* | § 8.19.21–22 | — | — | — | ⚪ |
| `FM.9` | Formal-coverage-context constants: `STIMULUS`, `RESPONSE`, `TARGETED`, `ANCILLARY`, `INCONCLUSIVE_ANALYSIS` — *deferred* | § 8.19.19 | — | — | — | ⚪ |

### 23. Net aliasing (§ 4.10)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `NA.1` | Canonical-name property for nets: `UCIS_STR_TOGGLE_CANON_NAME` — *deferred* | § 4.10 | — | — | — | ⚪ |
| `NA.2` | Multiple-instance roll-up via `UCIS_INST_ONCE` — *deferred* | § 4.10 | — | — | — | ⚪ |

### 24. Metrics (§ 4.9)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `MT.1` | Metric naming model (URL-style) — *deferred* | § 4.9.2 | — | — | — | ⚪ |
| `MT.2` | Metric criteria (covered / excluded / pragma) — *deferred* | § 4.9.3 | — | — | — | ⚪ |
| `MT.3` | Metric excluded-value coveritem flags | § 4.9.4 | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) | — | 🟡 |
| `MT.4` | Built-in metric definitions (per coverage type) — *deferred* | § 4.9.5 | — | — | — | ⚪ |
| `MT.5` | XML `METRIC_MODE` and metric attributes — *out of scope* | § 9.3.10–11 | — | — | — | ⚪ |

### 25. XML interchange (§ 9)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `X25.1` | Round-trip: `ucis_OpenFromInterchangeFormat` ⇄ `ucis_WriteToInterchangeFormat` — *out of scope* | § 8.1 | — | — | — | ⚪ |
| `X25.2` | Complete UCIS XML schema (top-level) — *out of scope* | § 9.4, § 9.14 | — | — | — | ⚪ |
| `X25.3` | Schema fragments per coverage type (TOGGLE / CVG / COND / ASSERT / FSM / BLOCK / BRANCH) — *out of scope* | § 9.7–9.13 | — | — | — | ⚪ |
| `X25.4` | Common complex types: `NAME_VALUE`, `SOURCE_FILE`, `LINE_ID`, `STATEMENT_ID`, `DIMENSION`, `BIN`, `BIN_CONTENTS` — *out of scope* | § 9.3 | — | — | — | ⚪ |
| `X25.5` | History-node XML schema — *out of scope* | § 9.5 | — | — | — | ⚪ |
| `X25.6` | Instance-coverage XML schema — *out of scope* | § 9.6 | — | — | — | ⚪ |
| `X25.7` | User-defined attribute encoding — *out of scope* | § 9.3.12 | — | — | — | ⚪ |

### 26. Database-level objects (§ 3.4)

| ID | Feature | Spec | Profile | Mapping | Tests | Status |
| -- | ------- | ---- | ------- | ------- | ----- | ------ |
| `DB.1` | Counts on coveritems | § 3.4.1 | core | [`counts`](ucis-parquet-mapping.md#counts) | L1: `test_every_api_fact_survives`, `test_round_trip_loses_nothing`<br>L2: `test_engine_merge_matches_the_object_api`<br>L3: `test_a_materialized_merge_equals_the_virtual_one`, `test_a_subset_merge_is_bounded_by_the_whole`, `test_associativity_grouping_does_not_matter` +5<br>L4: `test_q1_total_and_hit_bins_for_a_design_unit`, `test_q3_per_run_and_merged_counts` | ✅ |
| `DB.2` | Attributes (typed, user-defined) on any object | § 3.4.2 | core | [`properties`](ucis-parquet-mapping.md#properties) [Extensions (UCIS+): data, not schema forks](ucis-parquet-mapping.md#extensions-ucis-data-not-schema-forks) | L1: `test_prop_id_is_the_enum_name_without_its_prefix`<br>L4: `test_q5_standard_and_extension_properties_on_a_scope` | ✅ |
| `DB.3` | Flags on scopes and coveritems | § 3.4.3 | core | [`coveritems`](ucis-parquet-mapping.md#coveritems) [Flag bits are type-qualified](ucis-parquet-mapping.md#flag-bits-are-type-qualified) | L1: `test_every_api_fact_survives`, `test_round_trip_loses_nothing` | ✅ |
| `DB.4` | Tags on objects | § 3.4.4 | *ext* | [Tags and user attributes](ucis-parquet-mapping.md#tags-and-user-attributes) | L1: `test_tags_and_attributes_survive_on_every_object_kind` | ✅ |
| `DB.5` | Weights, goals, limits | § 3.4.5 | core | [`scopes`](ucis-parquet-mapping.md#scopes) [Columns added](ucis-parquet-mapping.md#columns-added) | L1: `test_round_trip_loses_nothing`<br>L3: `test_definitions_are_carried_through`, `test_definitions_are_carried_through_untouched` | ✅ |
| `DB.6` | Typed properties | § 3.4.6 | core | [`properties`](ucis-parquet-mapping.md#properties) [Enumerated column encodings](ucis-parquet-mapping.md#enumerated-column-encodings) [Extensions (UCIS+): data, not schema forks](ucis-parquet-mapping.md#extensions-ucis-data-not-schema-forks) [Promoted properties need a support bitmask](ucis-parquet-mapping.md#promoted-properties-need-a-support-bitmask) | L0: `test_promoted_property_order_is_stable`<br>L1: `test_a_promoted_property_is_a_column_instead_of_a_row`, `test_toggle_metadata_lands_in_the_properties_table`, `test_unsupported_property_still_raises`<br>L2: `test_property_support_agrees`<br>L4: `test_q5_standard_and_extension_properties_on_a_scope` | ✅ |
| `DB.7` | Source-file tables | § 3.4.7 | core | [`source_files`](ucis-parquet-mapping.md#source_files) | L1: `test_source_files_survive`, `test_source_locations_resolve_through_the_file_table` | ✅ |
| `DB.8` | Error handler — *out of scope* | § 3.4.8 | — | — | — | ⚪ |
| `DB.9` | DB-level vendor / standard / version identifying strings (manifest) | § 7 | core | [Neutrality rules](ucis-parquet-mapping.md#neutrality-rules) | — | 🟡 |

## Feature combinations

Interactions that are wrong even when every composed feature is individually right — normally because the merge operator differs per feature while the rows join on the same key.

| ID | Combination | Composes | Mapping | Tests | Status |
| -- | ----------- | -------- | ------- | ----- | ------ |
| `X.1` | cross × per-bin exclusion × merge | `S4.4`, `C6.6`, `SF.10` | [Flag bits are type-qualified](ucis-parquet-mapping.md#flag-bits-are-type-qualified) [Merge is type-aware — not everything is `SUM`](ucis-parquet-mapping.md#merge-is-type-aware--not-everything-is-sum) | L4: `test_q6_excluded_bins` | ✅ |
| `X.2` | toggle scope × vector grouping × multi-run | `TG.1`, `TG.6`, `H11.1` | [Toggle coverage](ucis-parquet-mapping.md#toggle-coverage) | — | 🟡 |
| `X.3` | FSM states + transitions × merge | `FS.2`, `FS.3`, `S4.6` | [Merge is type-aware — not everything is `SUM`](ucis-parquet-mapping.md#merge-is-type-aware--not-everything-is-sum) | — | 🟡 |
| `X.4` | assertion bins × formal results × merge | `AS.3`, `FM.1`, `FM.2` | [Merge is type-aware — not everything is `SUM`](ucis-parquet-mapping.md#merge-is-type-aware--not-everything-is-sum) | L3: `test_contradictory_formal_status_merges_to_conflict`, `test_formal_status_merge_is_order_independent` | ✅ |
| `X.5` | PEAKACTIVEBIN × merge at N=64 | `BT.19`, `H11.1` | [Merge is type-aware — not everything is `SUM`](ucis-parquet-mapping.md#merge-is-type-aware--not-everything-is-sum) | L0: `test_merge_is_not_uniformly_sum`<br>L3: `test_associativity_grouping_does_not_matter`, `test_commutativity_run_order_does_not_matter`, `test_peak_active_takes_the_max_not_the_sum` +1 | ✅ |
| `X.6` | promoted property unsupported by source × round-trip | `DB.5`, `DB.6` | [Promoted properties need a support bitmask](ucis-parquet-mapping.md#promoted-properties-need-a-support-bitmask) | L0: `test_promoted_property_order_is_stable`<br>L1: `test_a_promoted_property_is_a_column_instead_of_a_row`, `test_unsupported_property_still_raises`<br>L2: `test_property_support_agrees` | ✅ |
| `X.7` | assoc tier ALL/NEVER × merge with a SPARSE-tier run | `HL.6`, `HL.7` | [Association tiers](ucis-parquet-mapping.md#association-tiers) | L3: `test_associations_union_across_runs`<br>L4: `test_q4_contributing_tests_including_the_tier` | ✅ |
| `X.8` | history tree depth > 1 × append × merge node | `H11.1`, `H11.5` | [Runs, partitioning and append](ucis-parquet-mapping.md#runs-partitioning-and-append) | L1: `test_definition_tables_are_not_partitioned`, `test_each_run_keeps_its_own_history`<br>L3: `test_definitions_are_carried_through_untouched`, `test_materialized_merge_gains_a_merge_node` | ✅ |
| `X.9` | UCIS+ extension namespace × unknown-namespace passthrough | `A9.1`, `DB.2` | [Extensions (UCIS+): data, not schema forks](ucis-parquet-mapping.md#extensions-ucis-data-not-schema-forks) | L4: `test_q5_standard_and_extension_properties_on_a_scope` | ✅ |
| `X.11` | user-defined attributes on every object kind × EAV encoding | `A9.1`, `A9.5`, `T13.1` | [Tags and user attributes](ucis-parquet-mapping.md#tags-and-user-attributes) | L1: `test_attributes_and_tags_survive`, `test_coveritem_attributes_survive`, `test_tags_and_attributes_survive_on_every_object_kind`<br>L4: `test_q5_standard_and_extension_properties_on_a_scope` | ✅ |
| `X.12` | net aliasing × toggle counts | `NA.1`, `TG.5` | — | — | ⚪ |
| `X.13` | Iceberg round trip × narrow integer columns | `C6.6`, `DB.3` | [Iceberg widens INT8](ucis-parquet-mapping.md#iceberg-widens-int8) | L2: `test_int8_widening_is_normalized`, `test_round_trip_through_iceberg_is_lossless` | ✅ |
| `X.14` | multi-run dataset × single-run selection × partition pruning | `H11.1`, `IT.6` | [Runs, partitioning and append](ucis-parquet-mapping.md#runs-partitioning-and-append) | L1: `test_merged_view_sums_across_runs`, `test_runs_land_in_separate_partitions`, `test_single_run_selection_reads_only_that_partition`<br>L2: `test_per_run_provenance_is_queryable`<br>L3: `test_a_materialized_merge_equals_the_virtual_one`, `test_idempotence_merging_the_same_selection_twice_agrees`<br>L4: `test_q3_per_run_and_merged_counts` | ✅ |

## Negative conformance

Input the mapping must **reject**. A reader that accepts these produces a database that is wrong in a way no downstream check can detect.

| ID | Must be rejected | Mapping | Tests | Status |
| -- | ---------------- | ------- | ----- | ------ |
| `X.10` | same unique_id with a drifted definition (goal changed) must fail the merge | [Merge is type-aware — not everything is `SUM`](ucis-parquet-mapping.md#merge-is-type-aware--not-everything-is-sum) [Correctness rules](ucis-parquet-mapping.md#correctness-rules) | L1: `test_mismatched_definitions_are_refused`<br>L3: `test_definitions_are_carried_through_untouched`, `test_drifted_definitions_are_refused` | ✅ |
| `N.1` | unknown scope_type value on read is an error, not a silent pass-through | [Negative rules: input a reader must reject](ucis-parquet-mapping.md#negative-rules-input-a-reader-must-reject) | — | 🟡 |
| `N.2` | negative or non-integral coveritem count is rejected on write | [Negative rules: input a reader must reject](ucis-parquet-mapping.md#negative-rules-input-a-reader-must-reject) | — | 🟡 |
| `N.3` | cross scope referencing a coverpoint that does not exist is rejected | [Negative rules: input a reader must reject](ucis-parquet-mapping.md#negative-rules-input-a-reader-must-reject) | — | 🟡 |
| `N.4` | duplicate (run_id, coveritem_id) in a measurement table is rejected | [Correctness rules](ucis-parquet-mapping.md#correctness-rules) | L3: `test_reingesting_a_run_is_refused` | ✅ |
| `N.5` | schema_version newer than the reader understands is refused, not guessed | [Neutrality rules](ucis-parquet-mapping.md#neutrality-rules) | — | 🟡 |
| `N.6` | history node claiming UCIS_HISTORYNODE_MERGE with no contributing nodes is rejected | [Negative rules: input a reader must reject](ucis-parquet-mapping.md#negative-rules-input-a-reader-must-reject) | — | 🟡 |

## Out of scope and deferred

The part a third-party implementer needs most, and the part a hand-written matrix always omits: what this mapping deliberately does **not** carry, and why.

| ID | Feature | Decision | Reason | Reference |
| -- | ------- | -------- | ------ | --------- |
| `L1.3` | `ucis_Write(db, file, scope, recurse, covertype)` — persist subset to file | deferred | Filtered write (subset by scope, recurse and covertype) is not implemented; the loader writes a whole dataset. The filter is a query the mapping can express, so this is unbuilt rather than unrepresentable. | [0003-parquet-backend.md](../docs/adr/0003-parquet-backend.md) |
| `L1.4` | `ucis_OpenFromInterchangeFormat(file)` — load from XML interchange | out of scope | XML interchange is a separate backend (covsight.core.xml) with its own format plugin. Conversion runs through the UCIS object API, so the Parquet mapping neither reads nor writes XML. | — |
| `L1.5` | `ucis_WriteToInterchangeFormat(db, file)` — emit XML interchange | out of scope | XML interchange is a separate backend (covsight.core.xml) with its own format plugin. Conversion runs through the UCIS object API, so the Parquet mapping neither reads nor writes XML. | — |
| `L1.6` | `ucis_OpenReadStream(name)` — read-streaming open | out of scope | Read-streaming serves objects in document order without random access. The access mapping is the opposite by construction -- ranged bulk prefetch over dfs_ordinal, fed into a cursor. | — |
| `L1.7` | `ucis_OpenWriteStream(name)` — write-streaming open | out of scope | The write path is a batch loader by design (see 'Read/write scope' in the mapping document): objects are buffered into columnar batches, never emitted row-by-row. Write-streaming flush semantics have no counterpart. | — |
| `L1.8` | `ucis_WriteStream(db)` — flush in-flight object in write-streaming | out of scope | The write path is a batch loader by design (see 'Read/write scope' in the mapping document): objects are buffered into columnar batches, never emitted row-by-row. Write-streaming flush semantics have no counterpart. | — |
| `L1.9` | `ucis_WriteStreamScope(db)` — flush current scope and pop to parent | out of scope | The write path is a batch loader by design (see 'Read/write scope' in the mapping document): objects are buffered into columnar batches, never emitted row-by-row. Write-streaming flush semantics have no counterpart. | — |
| `E2.1` | `ucis_RegisterErrorHandler(handler, userdata)` | out of scope | API-level error reporting, registered per process. No persistent representation in the dataset. | — |
| `E2.2` | `ucisErrorT { msgno, severity, msgstr }` | out of scope | API-level error reporting, registered per process. No persistent representation in the dataset. | — |
| `E2.3` | Severity enum: `INFO`, `WARNING`, `ERROR` | out of scope | API-level error reporting, registered per process. No persistent representation in the dataset. | — |
| `V3.1` | `ucis_GetAPIVersion()` — version of the linked API library | out of scope | A property of the linked API library, not of the data. Two readers of the same dataset legitimately answer differently. | — |
| `S4.7` | `ucis_RemoveScope(db, scope)` | deferred | In-place removal from an append-only columnar dataset. The write path is a batch loader and has no delete; removal would be expressed as rewriting the definition tables, which is a separate feature from the UCIS call. | [0003-parquet-backend.md](../docs/adr/0003-parquet-backend.md) |
| `S4.8` | `ucis_ComposeDUName(lib, primary, secondary)` → string | out of scope | A pure string helper over the DU naming convention. It touches no database state. | — |
| `S4.9` | `ucis_ParseDUName(name, &lib, &primary, &secondary)` | out of scope | A pure string helper over the DU naming convention. It touches no database state. | — |
| `S4.20` | `ucis_CallBack(db, scope, mask, kind, fn, userdata)` — scope-callback walk | out of scope | A callback-driven traversal idiom over an in-memory tree. The access mapping presents cursors over batched reads; the data it would walk is already covered by IT.1-IT.5. | — |
| `ST.24` | `UCIS_TESTPLAN` | out of scope | UCIS 1.0 defines no UCIS_TESTPLAN scope type; the catalog row was an error. Testplan linkage is a covsight concept with its own schema (docs/testplan-schema.md), not a UCIS scope. | — |
| `ST.36` | `UCIS_BBLOCKSCOPE` | out of scope | UCIS 1.0 defines no UCIS_BBLOCKSCOPE. The spec's basic-block scope is UCIS_BBLOCK, which is the same value ST.32 already records as UCIS_COVBLOCK, so this entry is a duplicate under a wrong name. | — |
| `ST.37` | `UCIS_GROUP` | out of scope | UCIS 1.0 defines no UCIS_GROUP scope type; tags attach to objects directly (§ 8.16) rather than through a container scope. | — |
| `ST.38` | `UCIS_TRANSITION` | out of scope | UCIS 1.0 defines no UCIS_TRANSITION scope type. Transition bins hang off the coverpoint, which carries UCIS_SCOPE_CVG_TRANSITION. | — |
| `BT.5` | `UCIS_ZINBIN` | out of scope | UCIS 1.0 defines no UCIS_ZINBIN cover type; the catalog row was an error. | — |
| `C6.2` | `ucis_RemoveCover(db, parent, coverindex)` | deferred | In-place removal from an append-only columnar dataset. The write path is a batch loader and has no delete; removal would be expressed as rewriting the definition tables, which is a separate feature from the UCIS call. | [0003-parquet-backend.md](../docs/adr/0003-parquet-backend.md) |
| `C6.5` | `ucis_IncrementCover(db, parent, coverindex, delta)` — incremental update | deferred | Additive update is expressed as an appended run plus GROUP BY SUM, not as in-place increment. Whether the backend should emulate the call over that model is undecided. | [0003-parquet-backend.md](../docs/adr/0003-parquet-backend.md) |
| `IT.3` | `ucis_FreeIterator(db, iter)` | out of scope | Process-local handle lifetime. Nothing about it is persisted, so there is nothing for the mapping to represent. | — |
| `PH.6` | UCIS_HANDLE_SCOPE_DU (not defined by UCIS 1.0) | out of scope | Not defined by UCIS 1.0: the token appears only in a s 8.3 example. The enumerated constant is UCIS_HANDLE_INSTANCE_DU (PH.3). | — |
| `PI.1` | `UCIS_INT_IS_MODIFIED` — modified since the stored UCISDB was opened (read-only) | out of scope | UCIS 1.0 s 8.3 states this property is in-memory only and is not stored in the database, so no persistent representation can exist. | — |
| `PI.2` | `UCIS_INT_MODIFIED_SINCE_SIM` — modified since the end of the simulation run (read-only) | out of scope | In-memory only per UCIS 1.0 s 8.3, like IS_MODIFIED. | — |
| `PI.34` | `UCIS_INT_SUPPRESS_MODIFIED` — suppress the in-memory modification flag | out of scope | Write-side control of the in-memory IS_MODIFIED flag; it appears in a UCIS 1.0 s 8.3 example rather than the typedef, and like the flag it controls it has no persistent representation. | — |
| `PI.35` | UCIS_INT_SCOPE_PERINSTANCE (not defined by UCIS 1.0) | out of scope | Not defined by UCIS 1.0: the token appears nowhere in the specification. The per-instance option is CVG_PERINSTANCE (PI.30). | — |
| `PS.32` | UCIS_STR_FSM_STATEVAR (not defined by UCIS 1.0) | out of scope | Not defined by UCIS 1.0: the token appears nowhere in the specification. The FSM state variable is carried as UCIS_STR_GENERIC (PS.11) on the FSM scope, which is what the specification's own example does. | — |
| `H11.2` | `ucis_RemoveHistoryNode(db, node)` | deferred | In-place removal from an append-only columnar dataset. The write path is a batch loader and has no delete; removal would be expressed as rewriting the definition tables, which is a separate feature from the UCIS call. | [0003-parquet-backend.md](../docs/adr/0003-parquet-backend.md) |
| `HL.2` | `ucis_FreeHistoryNodeList(db, list)` | out of scope | Process-local handle lifetime. Nothing about it is persisted, so there is nothing for the mapping to represent. | — |
| `TG.5` | `UCIS_STR_TOGGLE_CANON_NAME` for net-aliasing (§ 4.10) | deferred | Blocked on the § 4.10 net-aliasing scope decision: whether the mapping preserves the alias relation or flattens aliased nets into shared counts. Freezing current behaviour by testing it would pre-empt that decision. | [0003-parquet-backend.md](../docs/adr/0003-parquet-backend.md) |
| `TG.7` | Toggle XML schema: `TOGGLE_OBJECT`, `TOGGLE_BIT`, `TOGGLE` | out of scope | XML interchange is a separate backend (covsight.core.xml) with its own format plugin. Conversion runs through the UCIS object API, so the Parquet mapping neither reads nor writes XML. | — |
| `FS.8` | XML schema: `FSM`, `FSM_STATE`, `FSM_TRANSITION` | out of scope | XML interchange is a separate backend (covsight.core.xml) with its own format plugin. Conversion runs through the UCIS object API, so the Parquet mapping neither reads nor writes XML. | — |
| `BR.4` | XML schema: `BRANCH_STATEMENT`, `BRANCH` | out of scope | XML interchange is a separate backend (covsight.core.xml) with its own format plugin. Conversion runs through the UCIS object API, so the Parquet mapping neither reads nor writes XML. | — |
| `SB.4` | XML schema: `STATEMENT`, `PROCESS_BLOCK`, `BLOCK` | out of scope | XML interchange is a separate backend (covsight.core.xml) with its own format plugin. Conversion runs through the UCIS object API, so the Parquet mapping neither reads nor writes XML. | — |
| `CX.4` | XML schema: `EXPR` | out of scope | XML interchange is a separate backend (covsight.core.xml) with its own format plugin. Conversion runs through the UCIS object API, so the Parquet mapping neither reads nor writes XML. | — |
| `CG.14` | XML schema: `CGINSTANCE`, `CG_ID`, `CGINST_OPTIONS`, `COVERPOINT`, `COVERPOINT_OPTIONS`, `COVERPOINT_BIN`, `RANGE_VALUE`, `SEQUENCE`, `CROSS`, `CROSS_OPTIONS`, `CROSS_BIN` | out of scope | XML interchange is a separate backend (covsight.core.xml) with its own format plugin. Conversion runs through the UCIS object API, so the Parquet mapping neither reads nor writes XML. | — |
| `AS.7` | XML schema: `ASSERTION` | out of scope | XML interchange is a separate backend (covsight.core.xml) with its own format plugin. Conversion runs through the UCIS object API, so the Parquet mapping neither reads nor writes XML. | — |
| `FM.4` | Formally-unreachable coveritem: `Set/GetFormallyUnreachableCoverTest` | deferred | The formal table carries per-bin status, radius and witness. Formal environments, their test and assumption associations, and the coverage-context constants have no representation yet; adding them is a schema extension, not a test. | [0003-parquet-backend.md](../docs/adr/0003-parquet-backend.md) |
| `FM.5` | Formal-env API: `AddFormalEnv`, `FormalEnvGetData`, `NextFormalEnv` | deferred | The formal table carries per-bin status, radius and witness. Formal environments, their test and assumption associations, and the coverage-context constants have no representation yet; adding them is a schema extension, not a test. | [0003-parquet-backend.md](../docs/adr/0003-parquet-backend.md) |
| `FM.6` | Test↔env association: `AssocFormalInfoTest` | deferred | The formal table carries per-bin status, radius and witness. Formal environments, their test and assumption associations, and the coverage-context constants have no representation yet; adding them is a schema extension, not a test. | [0003-parquet-backend.md](../docs/adr/0003-parquet-backend.md) |
| `FM.7` | Per-test formal info: `FormalTestGetInfo` | deferred | The formal table carries per-bin status, radius and witness. Formal environments, their test and assumption associations, and the coverage-context constants have no representation yet; adding them is a schema extension, not a test. | [0003-parquet-backend.md](../docs/adr/0003-parquet-backend.md) |
| `FM.8` | Assumption↔env association: `AssocAssumptionFormalEnv`, `NextFormalEnvAssumption` | deferred | The formal table carries per-bin status, radius and witness. Formal environments, their test and assumption associations, and the coverage-context constants have no representation yet; adding them is a schema extension, not a test. | [0003-parquet-backend.md](../docs/adr/0003-parquet-backend.md) |
| `FM.9` | Formal-coverage-context constants: `STIMULUS`, `RESPONSE`, `TARGETED`, `ANCILLARY`, `INCONCLUSIVE_ANALYSIS` | deferred | The formal table carries per-bin status, radius and witness. Formal environments, their test and assumption associations, and the coverage-context constants have no representation yet; adding them is a schema extension, not a test. | [0003-parquet-backend.md](../docs/adr/0003-parquet-backend.md) |
| `NA.1` | Canonical-name property for nets: `UCIS_STR_TOGGLE_CANON_NAME` | deferred | Blocked on the § 4.10 net-aliasing scope decision: whether the mapping preserves the alias relation or flattens aliased nets into shared counts. Freezing current behaviour by testing it would pre-empt that decision. | [0003-parquet-backend.md](../docs/adr/0003-parquet-backend.md) |
| `NA.2` | Multiple-instance roll-up via `UCIS_INST_ONCE` | deferred | Blocked on the § 4.10 net-aliasing scope decision: whether the mapping preserves the alias relation or flattens aliased nets into shared counts. Freezing current behaviour by testing it would pre-empt that decision. | [0003-parquet-backend.md](../docs/adr/0003-parquet-backend.md) |
| `MT.1` | Metric naming model (URL-style) | deferred | The UCIS metric model (naming, criteria, built-in definitions) is a derived view over stored coverage rather than stored data. Whether the mapping should persist metric definitions or compute them is undecided. | [0003-parquet-backend.md](../docs/adr/0003-parquet-backend.md) |
| `MT.2` | Metric criteria (covered / excluded / pragma) | deferred | The UCIS metric model (naming, criteria, built-in definitions) is a derived view over stored coverage rather than stored data. Whether the mapping should persist metric definitions or compute them is undecided. | [0003-parquet-backend.md](../docs/adr/0003-parquet-backend.md) |
| `MT.4` | Built-in metric definitions (per coverage type) | deferred | The UCIS metric model (naming, criteria, built-in definitions) is a derived view over stored coverage rather than stored data. Whether the mapping should persist metric definitions or compute them is undecided. | [0003-parquet-backend.md](../docs/adr/0003-parquet-backend.md) |
| `MT.5` | XML `METRIC_MODE` and metric attributes | out of scope | XML interchange is a separate backend (covsight.core.xml) with its own format plugin. Conversion runs through the UCIS object API, so the Parquet mapping neither reads nor writes XML. | — |
| `X25.1` | Round-trip: `ucis_OpenFromInterchangeFormat` ⇄ `ucis_WriteToInterchangeFormat` | out of scope | XML interchange is a separate backend (covsight.core.xml) with its own format plugin. Conversion runs through the UCIS object API, so the Parquet mapping neither reads nor writes XML. | — |
| `X25.2` | Complete UCIS XML schema (top-level) | out of scope | XML interchange is a separate backend (covsight.core.xml) with its own format plugin. Conversion runs through the UCIS object API, so the Parquet mapping neither reads nor writes XML. | — |
| `X25.3` | Schema fragments per coverage type (TOGGLE / CVG / COND / ASSERT / FSM / BLOCK / BRANCH) | out of scope | XML interchange is a separate backend (covsight.core.xml) with its own format plugin. Conversion runs through the UCIS object API, so the Parquet mapping neither reads nor writes XML. | — |
| `X25.4` | Common complex types: `NAME_VALUE`, `SOURCE_FILE`, `LINE_ID`, `STATEMENT_ID`, `DIMENSION`, `BIN`, `BIN_CONTENTS` | out of scope | XML interchange is a separate backend (covsight.core.xml) with its own format plugin. Conversion runs through the UCIS object API, so the Parquet mapping neither reads nor writes XML. | — |
| `X25.5` | History-node XML schema | out of scope | XML interchange is a separate backend (covsight.core.xml) with its own format plugin. Conversion runs through the UCIS object API, so the Parquet mapping neither reads nor writes XML. | — |
| `X25.6` | Instance-coverage XML schema | out of scope | XML interchange is a separate backend (covsight.core.xml) with its own format plugin. Conversion runs through the UCIS object API, so the Parquet mapping neither reads nor writes XML. | — |
| `X25.7` | User-defined attribute encoding | out of scope | XML interchange is a separate backend (covsight.core.xml) with its own format plugin. Conversion runs through the UCIS object API, so the Parquet mapping neither reads nor writes XML. | — |
| `DB.8` | Error handler | out of scope | API-level error reporting, registered per process. No persistent representation in the dataset. Same decision as E2.1-E2.3; listed separately in § 3.4 because the spec counts the handler among the database-level objects. | — |
| `X.12` | net aliasing × toggle counts | deferred | Blocked on the § 4.10 net-aliasing scope decision: whether the mapping preserves the alias relation or flattens aliased nets into shared counts. Testing the combination before that is settled would freeze whichever behaviour the implementation happens to have. | [0003-parquet-backend.md](../docs/adr/0003-parquet-backend.md) |
