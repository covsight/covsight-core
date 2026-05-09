# TypeScript UCIS Implementation Plan

> Companion to `ts/DESIGN.md`.  This document breaks the design into
> concrete, ordered work items with enough per-file detail that each item
> can be executed independently.  It covers **implementation**, **testing**,
> and **documentation** for every component.

---

## Table of Contents

1. [Repository & Toolchain Setup](#1-repository--toolchain-setup)
2. [Phase 1 — Enumerations & Data Classes](#2-phase-1--enumerations--data-classes)
3. [Phase 2 — Abstract API Classes](#3-phase-2--abstract-api-classes)
4. [Phase 3 — In-Memory Backend](#4-phase-3--in-memory-backend)
5. [Phase 4 — NCDB Reader](#5-phase-4--ncdb-reader)
6. [Phase 5 — NCDB Writer](#6-phase-5--ncdb-writer)
7. [Phase 6 — Optional NCDB Members](#7-phase-6--optional-ncdb-members)
8. [Phase 7 — Visitor & Format Registry](#8-phase-7--visitor--format-registry)
9. [Test Plan (complete inventory)](#9-test-plan)
10. [Documentation Plan](#10-documentation-plan)
11. [Cross-cutting Constraints](#11-cross-cutting-constraints)

---

## 1. Repository & Toolchain Setup

### 1.1 `ts/package.json`

```jsonc
{
  "name": "@covsight/core",
  "version": "0.1.0",
  "type": "module",
  "exports": {
    ".":          { "import": "./dist/index.js",      "types": "./dist/index.d.ts" },
    "./api":      { "import": "./dist/api/index.js",  "types": "./dist/api/index.d.ts" },
    "./mem":      { "import": "./dist/mem/index.js",  "types": "./dist/mem/index.d.ts" },
    "./ncdb":     { "import": "./dist/ncdb/index.js", "types": "./dist/ncdb/index.d.ts" }
  },
  "scripts": {
    "build":      "tsc",
    "build:watch":"tsc --watch",
    "test":       "node --experimental-vm-modules node_modules/.bin/jest",
    "lint":       "eslint src tests --ext .ts"
  },
  "dependencies": {
    "jszip": "^3.10.1"
  },
  "devDependencies": {
    "typescript":       "^5.4.0",
    "@types/node":      "^20.0.0",
    "jest":             "^29.0.0",
    "ts-jest":          "^29.0.0",
    "@types/jest":      "^29.0.0",
    "eslint":           "^8.0.0",
    "@typescript-eslint/eslint-plugin": "^7.0.0",
    "@typescript-eslint/parser":        "^7.0.0"
  }
}
```

### 1.2 `ts/tsconfig.json`

```jsonc
{
  "compilerOptions": {
    "target":           "ES2022",
    "module":           "NodeNext",
    "moduleResolution": "NodeNext",
    "lib":              ["ES2022"],
    "outDir":           "dist",
    "rootDir":          "src",
    "declaration":      true,
    "declarationMap":   true,
    "sourceMap":        true,
    "strict":           true,
    "noUncheckedIndexedAccess": true,
    "exactOptionalPropertyTypes": true,
    "useDefineForClassFields": true
  },
  "include": ["src/**/*"],
  "exclude": ["node_modules", "dist"]
}
```

### 1.3 `ts/jest.config.cjs`

```js
module.exports = {
  preset: 'ts-jest/presets/default-esm',
  testEnvironment: 'node',
  extensionsToTreatAsEsm: ['.ts'],
  moduleNameMapper: { '^(\\.{1,2}/.*)\\.js$': '$1' },
  testMatch: ['**/tests/**/*.test.ts'],
};
```

### 1.4 `.eslintrc.cjs` (in `ts/`)

Standard `@typescript-eslint` ruleset with `no-explicit-any: error` and
`strict-boolean-expressions: error`.

---

## 2. Phase 1 — Enumerations & Data Classes

All files are pure TypeScript with no runtime dependencies.

### 2.1 `src/api/enums/ScopeTypeT.ts`

**Content:** `bigint` constants (not `const enum` — TypeScript `const enum`
with `bigint` initializers is unsupported in TS 5.x; use a plain `const`
object instead).

```typescript
export const ScopeTypeT = Object.freeze({
  TOGGLE:          0x0000000000000001n,
  BRANCH:          0x0000000000000002n,
  EXPR:            0x0000000000000004n,
  COND:            0x0000000000000008n,
  INSTANCE:        0x0000000000000010n,
  PROCESS:         0x0000000000000020n,
  BLOCK:           0x0000000000000040n,
  FUNCTION:        0x0000000000000080n,
  FORKJOIN:        0x0000000000000100n,
  GENERATE:        0x0000000000000200n,
  GENERIC:         0x0000000000000400n,
  CLASS:           0x0000000000000800n,
  COVERGROUP:      0x0000000000001000n,
  COVERINSTANCE:   0x0000000000002000n,
  COVERPOINT:      0x0000000000004000n,
  CROSS:           0x0000000000008000n,
  COVER:           0x0000000000010000n,
  ASSERT:          0x0000000000020000n,
  PROGRAM:         0x0000000000040000n,
  PACKAGE:         0x0000000000080000n,
  TASK:            0x0000000000100000n,
  INTERFACE:       0x0000000000200000n,
  FSM:             0x0000000000400000n,
  DU_MODULE:       0x0000000001000000n,
  DU_ARCH:         0x0000000002000000n,
  DU_PACKAGE:      0x0000000004000000n,
  DU_PROGRAM:      0x0000000008000000n,
  DU_INTERFACE:    0x0000000010000000n,
  FSM_STATES:      0x0000000020000000n,
  FSM_TRANS:       0x0000000040000000n,
  COVBLOCK:        0x0000000080000000n,
  CVGBINSCOPE:     0x0000000100000000n,
  ILLEGALBINSCOPE: 0x0000000200000000n,
  IGNOREBINSCOPE:  0x0000000400000000n,
  RESERVEDSCOPE:   0xFF00000000000000n,
  ALL:             0x0000FFFFFFFFFFFFn,
} as const);

export type ScopeTypeTValue = typeof ScopeTypeT[keyof typeof ScopeTypeT];

export const DU_MASK = 0x000000001F000000n;
export function isDUScope(t: bigint): boolean { return (t & DU_MASK) !== 0n; }
```

**Implementation notes:**
- Values must be identical to `python/covsight/core/api/enums/scope_type.py`.
- Use `Object.freeze` so accidental mutation is caught at runtime.
- Export a `ScopeTypeTValue` union type for use in function signatures
  instead of bare `bigint`.

### 2.2 `src/api/enums/CoverTypeT.ts`

Same pattern using `number` (all values fit within 32-bit safe integers):

```typescript
export const CoverTypeT = Object.freeze({
  CVGBIN:       0x00000001,
  COVERBIN:     0x00000002,
  ASSERTBIN:    0x00000004,
  STMTBIN:      0x00000020,
  BRANCHBIN:    0x00000040,
  EXPRBIN:      0x00000080,
  CONDBIN:      0x00000100,
  TOGGLEBIN:    0x00000200,
  PASSBIN:      0x00000400,
  FSMBIN:       0x00000800,
  USERBIN:      0x00001000,
  GENERICBIN:   0x00001000,   // alias
  COUNT:        0x00002000,
  FAILBIN:      0x00004000,
  VACUOUSBIN:   0x00008000,
  DISABLEDBIN:  0x00010000,
  ATTEMPTBIN:   0x00020000,
  ACTIVEBIN:    0x00040000,
  IGNOREBIN:    0x00080000,
  ILLEGALBIN:   0x00100000,
  DEFAULTBIN:   0x00200000,
  PEAKACTIVEBIN:0x00400000,
  BLOCKBIN:     0x01000000,
  ALL:          0xFFFFFFFF,
} as const);
export type CoverTypeTValue = typeof CoverTypeT[keyof typeof CoverTypeT];
```

### 2.3 `src/api/enums/CoverFlagsT.ts`

```typescript
export const CoverFlagsT = Object.freeze({
  IS_32BIT:   0x00000001,
  IS_64BIT:   0x00000002,
  IS_VECTOR:  0x00000004,
  HAS_GOAL:   0x00000008,
  HAS_WEIGHT: 0x00000010,
  HAS_LIMIT:  0x00000400,
  HAS_COUNT:  0x00000800,
  IS_COVERED: 0x00001000,
} as const);
export type CoverFlagsTValue = typeof CoverFlagsT[keyof typeof CoverFlagsT];
```

### 2.4 `src/api/enums/FlagsT.ts`

```typescript
export const FlagsT = Object.freeze({
  INST_ONCE:            0x00000001,
  ENABLED_STMT:         0x00000002,
  ENABLED_BRANCH:       0x00000004,
  ENABLED_COND:         0x00000008,
  ENABLED_EXPR:         0x00000010,
  ENABLED_FSM:          0x00000020,
  ENABLED_TOGGLE:       0x00000040,
  SCOPE_UNDER_DU:       0x00000100,
  SCOPE_EXCLUDED:       0x00000200,
  SCOPE_PRAGMA_EXCLUDED:0x00000400,
  SCOPE_PRAGMA_CLEARED: 0x00000800,
  SCOPE_SPECIALIZED:    0x00001000,
  UOR_SAFE_SCOPE:       0x00002000,
  UOR_SAFE_SCOPE_ALLCOVERS: 0x00004000,
  IS_TOP_NODE:          0x00010000,
  SCOPE_CVG_AUTO:       0x00010000,
  SCOPE_CVG_SCALAR:     0x00020000,
  SCOPE_CVG_VECTOR:     0x00040000,
  SCOPE_CVG_TRANSITION: 0x00080000,
  SCOPE_IFF_EXISTS:     0x00100000,
  ENABLED_BLOCK:        0x00800000,
  SCOPE_BLOCK_ISBRANCH: 0x01000000,
} as const);
export type FlagsTValue = typeof FlagsT[keyof typeof FlagsT];
```

### 2.5 `src/api/enums/SourceT.ts`

Plain numeric `enum` is fine here (no bigint required):

```typescript
export enum SourceT {
  VHDL = 0, VLOG, SV, SYSTEMC,
  PSL_VHDL, PSL_VLOG, PSL_SV, PSL_SYSTEMC,
  E, VERA, NONE, OTHER, SOURCE_ERROR,
}
```

### 2.6 `src/api/enums/HistoryNodeKind.ts`

```typescript
export enum HistoryNodeKind { NONE = -1, ALL = 0, TEST = 1, MERGE = 2 }
```

### 2.7 `src/api/enums/TestStatusT.ts`

```typescript
export enum TestStatusT { OK = 0, FAILED, ERROR, FATAL, COMPILE }
```

### 2.8 Toggle enums — `ToggleMetricT.ts`, `ToggleTypeT.ts`, `ToggleDirT.ts`

```typescript
// ToggleMetricT.ts
export enum ToggleMetricT { MEASURE = 0, NOFILL, COUNT }

// ToggleTypeT.ts
export enum ToggleTypeT { REG = 0, NET, PORT }

// ToggleDirT.ts
export enum ToggleDirT { INTERNAL = 0, UP, DOWN, BOTH }
```

### 2.9 `src/api/enums/IntProperty.ts`

Mirror of `python/covsight/core/api/enums/int_property.py`.  Include at
minimum the properties referenced in the Python `getIntProperty`/`setIntProperty`
dispatch logic:

```typescript
export enum IntProperty {
  // Database-level
  IS_MODIFIED = 0, MODIFIED_SINCE_SIM, NUM_TESTS,
  // Scope-level
  SCOPE_GOAL, SCOPE_WEIGHT, SCOPE_SOURCE_TYPE, SCOPE_EXCLUDE,
  // Cover item
  COVER_DATA, COVER_GOAL, COVER_WEIGHT, COVER_AT_LEAST,
  // Covergroup
  CVG_PERINSTANCE, CVG_MERGEINSTANCES, CVG_GETINSTCOVERAGE,
  CVG_ATLEAST, CVG_AUTOBINMAX,
}
```

### 2.10 `src/api/enums/index.ts`

Re-exports all enums for single-import convenience:

```typescript
export * from './ScopeTypeT.js';
export * from './CoverTypeT.js';
export * from './CoverFlagsT.js';
export * from './FlagsT.js';
export * from './SourceT.js';
export * from './HistoryNodeKind.js';
export * from './TestStatusT.js';
export * from './ToggleMetricT.js';
export * from './ToggleTypeT.js';
export * from './ToggleDirT.js';
export * from './IntProperty.js';
```

### 2.11 Data classes

**`src/api/FileHandle.ts`**
```typescript
export class FileHandle {
  constructor(
    public readonly fileName: string,
    public readonly workDir: string = '',
  ) {}
}
```

**`src/api/SourceInfo.ts`**
```typescript
import type { FileHandle } from './FileHandle.js';
export class SourceInfo {
  constructor(
    public readonly file:  FileHandle | null,
    public readonly line:  number,
    public readonly token: number = 0,
  ) {}
}
```

**`src/api/CoverData.ts`**

Must include all fields from the Python `CoverData`, plus the
`COVER_TYPE_DEFAULTS` lookup table embedded as a static helper:

```typescript
import type { CoverTypeTValue } from './enums/CoverTypeT.js';
export class CoverData {
  data     = 0;
  goal     = 0;
  weight   = 0;
  limit    = 0;
  bitlen   = 0;
  atLeast  = 1;
  constructor(
    public readonly type:  CoverTypeTValue,
    public flags: number,
  ) {}
}
```

**`src/api/CoverIndex.ts`**
```typescript
export class CoverIndex {
  constructor(public readonly index: number) {}
}
```

**`src/api/TestData.ts`**
```typescript
import type { TestStatusT } from './enums/TestStatusT.js';
export class TestData {
  toolCategory = ''; date = ''; simTime = 0; timeUnit = '';
  runCwd = ''; cpuTime = 0; seed = ''; cmd = '';
  args: string[] = []; compulsory: string[] = [];
  user = ''; cost = 0;
  constructor(public testStatus: TestStatusT) {}
}
```

---

## 3. Phase 2 — Abstract API Classes

### 3.1 `src/api/Obj.ts`

- Fields: `_goal: number = 100`
- `getGoal() / setGoal(n)`
- `getIntProperty(coverIndex, property)` — throws by default
- `setIntProperty(coverIndex, property, value)` — throws by default

### 3.2 `src/api/Scope.ts`

Extends `Obj`.  Every method is `abstract` unless a default can be expressed
in terms of other abstract methods (none for Scope itself).

**Abstract methods to define:**

| Method | Returns | Notes |
|---|---|---|
| `getScopeName()` | `string` | |
| `getScopeType()` | `bigint` | |
| `getParent()` | `Scope \| null` | |
| `getSourceInfo()` | `SourceInfo \| null` | |
| `getWeight()` | `number` | |
| `getFlags()` | `number` | |
| `scopes(typeMask: bigint)` | `Iterable<Scope>` | |
| `coverItems()` | `Iterable<CoverItem>` | see type below |
| `createScope(name, srcinfo, weight, source, type, flags)` | `Scope` | |
| `createInstance(name, srcinfo, weight, source, type, duScope, flags)` | `Scope` | |
| `createCovergroup(name, srcinfo, weight, source)` | `Covergroup` | |
| `createToggle(name, canonicalName, flags, metric, type, dir)` | `Scope` | |
| `createNextCover(name, data, srcinfo)` | `CoverIndex` | |

**Exported helper type:**
```typescript
export interface CoverItem {
  name:    string;
  data:    CoverData;
  index:   CoverIndex;
  srcinfo: SourceInfo | null;
}
```

### 3.3 `src/api/UCIS.ts`

Extends `Scope`.  Adds database-level operations:

**Additional abstract methods:**

| Method | Returns |
|---|---|
| `read(path)` | `Promise<void>` |
| `write(path)` | `Promise<void>` |
| `close()` | `void` |
| `isModified()` | `boolean` |
| `modifiedSinceSim()` | `boolean` |
| `getNumTests()` | `number` |
| `getAPIVersion()` | `string` |
| `getWrittenBy()` | `string` |
| `setWrittenBy(by)` | `void` |
| `getWrittenTime()` | `number` |
| `setWrittenTime(t)` | `void` |
| `createHistoryNode(parent, logicalName, physicalName, kind)` | `HistoryNode` |
| `historyNodes(kindMask)` | `Iterable<HistoryNode>` |
| `createFileHandle(fileName, workDir)` | `FileHandle` |
| `getFileHandles()` | `Iterable<FileHandle>` |

**Override `getIntProperty`** to dispatch `IS_MODIFIED`, `MODIFIED_SINCE_SIM`,
`NUM_TESTS` to the abstract method equivalents (mirrors Python).

### 3.4 `src/api/Covergroup.ts`

Extends `Scope`.

**Additional abstract methods:**

| Method | Returns |
|---|---|
| `getPerInstance()` | `boolean` |
| `setPerInstance(v)` | `void` |
| `getMergeInstances()` | `boolean` |
| `setMergeInstances(v)` | `void` |
| `getGetInstCoverage()` | `boolean` |
| `setGetInstCoverage(v)` | `void` |
| `createCoverpoint(name, srcinfo, weight, source)` | `Coverpoint` |
| `createCross(name, srcinfo, weight, source, points)` | `Cross` |
| `createCoverInstance(name, srcinfo, weight, source)` | `Covergroup` |

**Override `getIntProperty` / `setIntProperty`** to dispatch
`CVG_PERINSTANCE`, `CVG_MERGEINSTANCES`, `CVG_GETINSTCOVERAGE`.

### 3.5 `src/api/Coverpoint.ts`

Extends `Scope`.

**Additional abstract methods:**

| Method | Returns |
|---|---|
| `getAtLeast()` | `number` |
| `setAtLeast(n)` | `void` |
| `createBin(name, srcinfo, atLeast, count, rhs, kind)` | `CoverIndex` |

### 3.6 `src/api/Cross.ts`

Extends `Scope`.

| Method | Returns |
|---|---|
| `getCrossedPoints()` | `Coverpoint[]` |

### 3.7 `src/api/HistoryNode.ts`

Extends `Obj`.

All getter/setter pairs for every history field (matches Python
`HistoryNode` exactly — see `DESIGN.md §HistoryNode`).

**Concrete helper:**
```typescript
setTestData(td: TestData): void {
  this.setTestStatus(td.testStatus);
  this.setToolCategory(td.toolCategory);
  // ... all fields
}
```

### 3.8 `src/api/index.ts`

Re-exports all abstract classes, data classes, and enums.

---

## 4. Phase 3 — In-Memory Backend

### 4.1 `src/mem/MemCoverItem.ts`

Internal value object storing one cover item:

```typescript
export class MemCoverItem {
  constructor(
    public name:    string,
    public data:    CoverData,
    public index:   CoverIndex,
    public srcinfo: SourceInfo | null = null,
  ) {}
}
```

### 4.2 `src/mem/MemScope.ts`

Concrete implementation of `Scope`.

**Fields:**
```
_name:      string
_type:      bigint
_parent:    MemScope | null
_srcinfo:   SourceInfo | null
_source:    SourceT
_flags:     number
_weight:    number
_goal:      number (from Obj)
_children:  MemScope[]
_covers:    MemCoverItem[]
```

**Key implementation notes:**

- `scopes(typeMask)`: generator that yields `_children` matching
  `(child._type & typeMask) !== 0n`.
- `coverItems()`: generator over `_covers`.
- `createScope()`: constructs a `MemScope`, appends to `_children`,
  returns it.  **Must dispatch on `type` to return the correct subclass**
  (e.g. `ScopeTypeT.COVERGROUP` → `MemCovergroup`).
- `createInstance()`: constructs `MemInstanceScope` (subclass holding
  `_duScope` reference), appends.
- `createCovergroup()`: calls `createScope()` with type
  `ScopeTypeT.COVERGROUP`, returns result cast to `MemCovergroup`.
- `createToggle()`: constructs `MemToggleScope` (subclass holding
  canonical name, metric, type, dir).
- `createNextCover()`: wraps data in `MemCoverItem`, assigns a
  sequential `CoverIndex`, appends to `_covers`.
- `getIntProperty()` / `setIntProperty()`: dispatch on `IntProperty`
  values for `SCOPE_GOAL`, `SCOPE_WEIGHT`, `COVER_DATA`, `COVER_AT_LEAST`.

### 4.3 `src/mem/MemInstanceScope.ts`

Extends `MemScope`, adds:
```typescript
constructor(..., duScope: MemScope) { ... }
getDuScope(): MemScope { return this._duScope; }
```

### 4.4 `src/mem/MemToggleScope.ts`

Extends `MemScope`, adds:
```typescript
constructor(..., canonicalName: string,
  metric: ToggleMetricT, toggleType: ToggleTypeT, dir: ToggleDirT) { ... }
```

### 4.5 `src/mem/MemCovergroup.ts`

Extends `MemScope`, implements `Covergroup`.

**Fields:**
```
_perInstance:     boolean = false
_mergeInstances:  boolean = true
_getInstCoverage: boolean = false
```

**`createCoverpoint()`**: creates a child `MemCoverpoint`.

**`createCross()`**: creates a child `MemCross` holding the
`Coverpoint[]` reference list.

**`createCoverInstance()`**: creates a child `MemCovergroup` with
type `ScopeTypeT.COVERINSTANCE`.

### 4.6 `src/mem/MemCoverpoint.ts`

Extends `MemScope`, implements `Coverpoint`.

**Fields:**
```
_atLeast: number = 1
```

**`createBin()`**: creates a `CoverData` from `kind`, calls
`createNextCover()`, returns the resulting `CoverIndex`.

### 4.7 `src/mem/MemCross.ts`

Extends `MemScope`, implements `Cross`.

**Fields:**
```
_crossedPoints: Coverpoint[]
```

### 4.8 `src/mem/MemHistoryNode.ts`

Implements `HistoryNode`.

**Fields:** one field per getter/setter pair:
```
_logicalName, _physicalName, _kind, _testStatus,
_simTime, _timeUnit, _runCwd, _cpuTime, _seed,
_cmd, _args, _compulsory, _date, _userName, _cost,
_toolCategory, _vendorId, _vendorTool, _vendorToolVersion,
_comment, _parent, _sameTests
```

All getters return the field; all setters assign it.

### 4.9 `src/mem/MemUCIS.ts`

Extends `MemScope`, implements `UCIS`.

**Fields:**
```
_historyNodes: MemHistoryNode[]
_fileHandles:  FileHandle[]
_writtenBy:    string = ''
_writtenTime:  number = 0
_modified:     boolean = false
_modSinceSim:  boolean = false
```

**Constructor:**
```typescript
constructor() {
  super(null, '', null, 1, SourceT.NONE, ScopeTypeT.RESERVEDSCOPE, 0);
}
```

**`read(path)`**: delegates to `NcdbReader.readInto(path, this)`.

**`write(path)`**: delegates to `NcdbWriter.write(path, this)`.

**`createHistoryNode()`**: constructs a `MemHistoryNode`, pushes to
`_historyNodes`, returns it.

**`historyNodes(kindMask)`**: generator over `_historyNodes` filtered
by kind.

**`createFileHandle()`**: constructs `FileHandle`, pushes to
`_fileHandles`, returns it.

**`getFileHandles()`**: returns `_fileHandles` as iterable.

**`getNumTests()`**: counts `_historyNodes` with
`kind === HistoryNodeKind.TEST`.

### 4.10 `src/mem/MemFactory.ts`

```typescript
export class MemFactory {
  static create(): MemUCIS { return new MemUCIS(); }
}
```

### 4.11 `src/mem/index.ts`

Re-exports `MemUCIS`, `MemFactory`, `MemScope`, `MemCovergroup`,
`MemCoverpoint`, `MemCross`, `MemHistoryNode`.

---

## 5. Phase 4 — NCDB Reader

### 5.1 `src/ncdb/constants.ts`

**All constants from `python/covsight/core/ncdb/constants.py`:**

- Format identity: `NCDB_FORMAT`, `NCDB_VERSION`, `NCDB_GENERATOR`
- ZIP member names: all `MEMBER_*` strings
- History format versions: `HISTORY_FORMAT_V1`, `HISTORY_FORMAT_V2`
- Scope tree markers: `SCOPE_MARKER_REGULAR = 0`, `SCOPE_MARKER_TOGGLE_PAIR = 1`
- Presence flags: `PRESENCE_FLAGS = 0x01`, `PRESENCE_SOURCE = 0x02`, etc.
- Counts modes: `COUNTS_MODE_UINT32 = 0`, `COUNTS_MODE_VARINT = 1`
- Toggle bin names: `TOGGLE_BIN_0_TO_1 = '0 -> 1'`, `TOGGLE_BIN_1_TO_0 = '1 -> 0'`
- Cover-type defaults table:

```typescript
export const COVER_TYPE_DEFAULTS: ReadonlyMap<number, readonly [flags: number, atLeast: number, weight: number]>
```

Initialized with the same entries as `COVER_TYPE_DEFAULTS` in `constants.py`.

### 5.2 `src/ncdb/varint.ts`

Pure TypeScript LEB128 (unsigned) codec.

```typescript
/** Encode non-negative integer as unsigned LEB128. */
export function encodeVarUint(value: number): Uint8Array { ... }

/** Decode one unsigned LEB128 varint from buf at offset.
 *  Returns [value, newOffset]. */
export function decodeVarUint(buf: Uint8Array, offset: number): [number, number] { ... }

/** Encode an array of non-negative integers. */
export function encodeVarUints(values: number[]): Uint8Array { ... }

/** Decode count integers from buf starting at offset.
 *  Returns [values, newOffset]. */
export function decodeVarUints(buf: Uint8Array, count: number, offset: number): [number[], number] { ... }
```

**Implementation notes:**
- Handle values that require up to 64 bits by accepting `bigint` input in
  an overloaded `encodeVarUint64` / `decodeVarUint64` variant used only by
  `ScopeTreeReader` for scope type fields.
- The 32-bit variants use `number` throughout (safe for counts < 2^32).

### 5.3 `src/ncdb/StringTable.ts`

```typescript
export class StringTable {
  private _strings: string[] = [];
  private _index:   Map<string, number> = new Map();

  /** Return index for s, adding if not present. */
  intern(s: string): number { ... }

  get(idx: number): string { ... }

  get length(): number { return this._strings.length; }

  static decode(buf: Uint8Array): StringTable { ... }
  encode(): Uint8Array { ... }
}
```

**`decode()`:** reads `[count: varint]` then `count × [len: varint][utf8 bytes]`.

**`encode()`:** writes same layout.

**Important:** `intern('')` must return index 0 and must be called in the
constructor so index 0 is always the empty string (matches Python).

### 5.4 `src/ncdb/SourcesReader.ts` / `SourcesWriter.ts`

**Reader:**
```typescript
export class SourcesReader {
  read(json: string, db: MemUCIS): void {
    // parse JSON array of filename strings
    // call db.createFileHandle(filename) for each
    // return the array so ScopeTreeReader can index by position
  }
  readToArray(json: string, db: MemUCIS): FileHandle[] { ... }
}
```

**Writer:**
```typescript
export class SourcesWriter {
  write(db: MemUCIS): string {
    return JSON.stringify([...db.getFileHandles()].map(fh => fh.fileName));
  }
}
```

### 5.5 `src/ncdb/ScopeTreeReader.ts`

**Constructor:**
```typescript
constructor(private strings: StringTable, private fileHandles: FileHandle[]) {}
```

**`read(buf, parent, countsIter)`:**

DFS loop over `buf`:
1. Read 1-byte marker.
2. If `SCOPE_MARKER_TOGGLE_PAIR`: read `name_ref` varint, consume 2 counts
   from `countsIter`, create a `BRANCH` scope under `parent` with two
   `TOGGLEBIN` cover items.
3. If `SCOPE_MARKER_REGULAR`: read full record:
   - `scope_type_val` as two 32-bit varints combined to bigint
   - `name_ref` varint
   - `presence` varint
   - Optional fields depending on presence bits:
     - `PRESENCE_FLAGS` → `flags` varint
     - `PRESENCE_SOURCE` → `file_id`, `line`, `token` varints → `SourceInfo`
     - `PRESENCE_WEIGHT` → `weight` varint
     - `PRESENCE_AT_LEAST` → `atLeast` varint
     - `PRESENCE_GOAL` → `goal` varint
     - `PRESENCE_SOURCE_TYPE` → `source_type` varint
   - `num_child_scopes` varint
   - `num_cover_items` varint
   - If `num_cover_items > 0`: `child_cover_type` varint, then
     `num_cover_items × name_ref` varints; consume one count per cover item
     from `countsIter`.
   - Recurse: read `num_child_scopes` scope records under the new scope.

**Scope factory dispatch:**

Use a helper that calls `parent.createScope()` normally, but for
`COVERGROUP`/`COVERINSTANCE` types calls `createCovergroup()` instead to
get the proper `Covergroup` subclass.  Similarly for `COVERPOINT` →
`createCoverpoint()` (need a reference to the parent covergroup), and
`CROSS`.

Because `ScopeTreeReader` receives a generic `MemUCIS` / `MemScope` parent,
the simplest approach is to directly construct the appropriate `MemScope`
subclass and `_children.push()` rather than going through the abstract
`createScope` API — this avoids type-dispatch brittleness.

**DU linkage:** After the full tree is decoded, call `fixupDuLinks(db)` to
replace placeholder DU references on INSTANCE scopes with the actual DU
sibling (mirrors Python `_fixup_instance_du_links`).

### 5.6 `src/ncdb/ScopeTreeWriter.ts`

**Constructor:**
```typescript
constructor(private strings: StringTable, private fileHandles: FileHandle[]) {}
```

**`write(db)`:** Returns `{ scopeTree: Uint8Array, counts: number[] }`.

DFS walk mirroring Python `ScopeTreeWriter._write_scope`:

1. For each top-level scope call `_writeScope(scope, buf, counts)`.
2. `_isTogglePair(scope)`: returns true when scope is BRANCH with exactly 2
   TOGGLEBIN cover items and no child scopes, and names are `'0 -> 1'` /
   `'1 -> 0'`.
3. `_writeTogglePair()`: emit `SCOPE_MARKER_TOGGLE_PAIR` + `name_ref` varint;
   push two counts.
4. `_writeRegularScope()`: compute presence bitfield, emit marker + fields +
   child counts, recurse children.

File handle tracking: maintain a `Map<string, number>` for `filename → fileId`
populated during the write pass; the resulting ordered array is the source
for `SourcesWriter`.

### 5.7 `src/ncdb/CountsReader.ts` / `CountsWriter.ts`

**Reader:**
```typescript
export class CountsReader {
  /** Decode counts.bin and return an iterator of count values. */
  decode(buf: Uint8Array): IterableIterator<number> { ... }
}
```

Layout: `[mode: 1 byte][count: varint][data...]`
- Mode 0 (UINT32): read `count × 4` bytes, parse as LE uint32.
- Mode 1 (VARINT): call `decodeVarUints(buf, count, offset)`.

**Writer:**
```typescript
export class CountsWriter {
  /** Encode an array of counts, choosing the smaller mode. */
  encode(counts: number[]): Uint8Array { ... }
}
```

Choose mode 1 if any count > 0xFFFFFFFF or if varint total bytes < count×4.

### 5.8 `src/ncdb/HistoryReader.ts` / `HistoryWriter.ts`

**Reader:** parses `history.json` array, creates `MemHistoryNode` objects on
the db.  Field name mapping must match Python `HistoryWriter.serialize()`:

| JSON key | HistoryNode setter |
|---|---|
| `logical_name` | `setLogicalName` |
| `physical_name` | `setPhysicalName` |
| `kind` | parse string → `HistoryNodeKind` enum |
| `test_status` | parse int → `TestStatusT` enum |
| `sim_time` | `setSimTime` |
| `time_unit` | `setTimeUnit` |
| `run_cwd` | `setRunCwd` |
| `cpu_time` | `setCpuTime` |
| `seed` | `setSeed` |
| `cmd` | `setCmd` |
| `args` | `setArgs` |
| `compulsory` | (store array) |
| `date` | `setDate` |
| `user_name` | `setUserName` |
| `cost` | `setCost` |
| `tool_category` | `setToolCategory` |
| `vendor_id` | `setVendorId` |
| `vendor_tool` | `setVendorTool` |
| `vendor_tool_version` | `setVendorToolVersion` |
| `same_tests` | `setSameTests` |
| `comment` | `setComment` |

**Writer:** serialises all `HistoryNode` objects from `db.historyNodes(ALL)`
to a JSON array with the same field names.

### 5.9 `src/ncdb/NcdbReader.ts`

```typescript
import JSZip from 'jszip';
import { readFile } from 'node:fs/promises';

export class NcdbReader {
  async read(path: string): Promise<MemUCIS> {
    const db = new MemUCIS();
    await this.readInto(path, db);
    return db;
  }

  async readInto(path: string, db: MemUCIS): Promise<void> {
    const buf = await readFile(path);
    const zip = await JSZip.loadAsync(buf);

    // 1. Validate manifest
    const manifestEntry = zip.file(MEMBER_MANIFEST);
    if (!manifestEntry) throw new Error('Missing manifest.json in NCDB file');
    const manifest = JSON.parse(await manifestEntry.async('string'));
    if (manifest.format !== NCDB_FORMAT)
      throw new Error(`Expected format=${NCDB_FORMAT}, got ${manifest.format}`);

    // 2. String table
    const strEntry = zip.file(MEMBER_STRINGS);
    if (!strEntry) throw new Error('Missing strings.bin');
    const strBuf  = await strEntry.async('uint8array');
    const strings = StringTable.decode(strBuf);

    // 3. Sources (before scope tree — provides FileHandle array)
    const srcEntry = zip.file(MEMBER_SOURCES);
    const fileHandles: FileHandle[] = srcEntry
      ? new SourcesReader().readToArray(
          await srcEntry.async('string'), db)
      : [];

    // 4. Scope tree + counts (coupled — counts iterator fed into scope tree reader)
    const treeEntry   = zip.file(MEMBER_SCOPE_TREE);
    const countsEntry = zip.file(MEMBER_COUNTS);
    if (!treeEntry)   throw new Error('Missing scope_tree.bin');
    if (!countsEntry) throw new Error('Missing counts.bin');
    const treeBuf   = await treeEntry.async('uint8array');
    const countsBuf = await countsEntry.async('uint8array');

    const countsIter = new CountsReader().decode(countsBuf);
    new ScopeTreeReader(strings, fileHandles).read(treeBuf, db, countsIter);

    // 5. History
    const histEntry = zip.file(MEMBER_HISTORY);
    if (histEntry) {
      new HistoryReader().read(await histEntry.async('string'), db);
    }
    // Phases 6+ add toggle, fsm, cross, attrs readers here
  }
}
```

---

## 6. Phase 5 — NCDB Writer

### 6.1 `src/ncdb/NcdbWriter.ts`

```typescript
import JSZip from 'jszip';
import { writeFile } from 'node:fs/promises';

export class NcdbWriter {
  async write(path: string, db: MemUCIS): Promise<void> {
    const zip     = new JSZip();
    const strings = new StringTable();
    strings.intern('');   // ensure index 0 = ''

    const fileHandles: FileHandle[] = [];
    const stWriter = new ScopeTreeWriter(strings, fileHandles);
    const { scopeTree, counts } = stWriter.write(db);

    zip.file(MEMBER_MANIFEST, JSON.stringify({
      format:    NCDB_FORMAT,
      version:   NCDB_VERSION,
      generator: NCDB_GENERATOR,
    }, null, 2));

    zip.file(MEMBER_STRINGS,    strings.encode());
    zip.file(MEMBER_SCOPE_TREE, scopeTree);
    zip.file(MEMBER_COUNTS,     new CountsWriter().encode(counts));
    zip.file(MEMBER_SOURCES,    new SourcesWriter().writeFromHandles(fileHandles));
    zip.file(MEMBER_HISTORY,    new HistoryWriter().write(db));

    const buf = await zip.generateAsync({
      type:        'nodebuffer',
      compression: 'DEFLATE',
      compressionOptions: { level: 6 },
    });
    await writeFile(path, buf);
  }
}
```

**`SourcesWriter.writeFromHandles(handles)`:** accepts a pre-built ordered
array (from `ScopeTreeWriter`) rather than walking the db, ensuring IDs
match what was embedded in `scope_tree.bin`.

---

## 7. Phase 6 — Optional NCDB Members

These members are decoded/encoded as needed.  Each has its own reader/writer
pair.  Initial implementation can **skip writing** these members and just
ignore them on read (they are optional per the NCDB spec); full support
can be added incrementally.

| Member | Reader | Writer | Python source |
|---|---|---|---|
| `toggle.bin` | `ToggleReader` | `ToggleWriter` | `toggle.py` |
| `fsm.bin` | `FsmReader` | `FsmWriter` | `fsm.py` |
| `cross.bin` | `CrossReader` | `CrossWriter` | `cross.py` |
| `attrs.bin` | `AttrsReader` | `AttrsWriter` | `attrs.py` |
| `design_units.json` | `DesignUnitsReader` | `DesignUnitsWriter` | `design_units.py` |
| `properties.json` | `PropertiesReader` | `PropertiesWriter` | `properties.py` |
| `formal.bin` | `FormalReader` | `FormalWriter` | `formal.py` |

**Toggle reader/writer:** `toggle.bin` stores additional per-signal metadata
(canonical name, metric, type, dir) for `MemToggleScope` objects.  On read,
use the DFS order of TOGGLE scopes to match entries.

**Design units reader/writer:** `design_units.json` lists DU scope names and
their types.  Used to reconstruct DU→instance linkage for scopes that were
instantiated before their DU was defined.

**Cross reader/writer:** `cross.bin` stores the list of coverpoint indices
that form each cross.  On read, resolve to actual `Coverpoint` objects.

---

## 8. Phase 7 — Visitor & Format Registry

### 8.1 `src/visitors.ts`

```typescript
export interface UCISVisitor {
  /** Return false to skip children of this scope. */
  enterScope?(scope: Scope, depth: number): boolean | void;
  exitScope?(scope:  Scope, depth: number): void;
  visitCoverItem?(scope: Scope, item: CoverItem): void;
}

export function traverse(
  root:     Scope,
  visitor:  UCISVisitor,
  typeMask: bigint = ScopeTypeT.ALL,
): void {
  function visit(scope: Scope, depth: number): void {
    const descend = visitor.enterScope?.(scope, depth);
    if (descend === false) return;
    for (const item of scope.coverItems()) {
      visitor.visitCoverItem?.(scope, item);
    }
    for (const child of scope.scopes(typeMask)) {
      visit(child, depth + 1);
    }
    visitor.exitScope?.(scope, depth);
  }
  visit(root, 0);
}
```

### 8.2 `src/ext.ts` — Format Registry

```typescript
export interface DbFormat {
  name:  string;
  read(path:  string):             Promise<MemUCIS>;
  write(path: string, db: MemUCIS): Promise<void>;
}

export class FormatRegistry {
  private _formats = new Map<string, DbFormat>();
  register(fmt: DbFormat): void { this._formats.set(fmt.name, fmt); }
  getDbFormat(name: string): DbFormat { ... }
  dbFormats(): ReadonlyMap<string, DbFormat> { return this._formats; }
}

export const ncdbFormat: DbFormat = {
  name:  'ncdb',
  read:  path      => new NcdbReader().read(path),
  write: (path, db) => new NcdbWriter().write(path, db),
};
```

### 8.3 `src/index.ts` (top-level barrel)

```typescript
export * from './api/index.js';
export * from './mem/index.js';
export * from './ncdb/index.js';
export * from './visitors.js';
export * from './ext.js';
```

---

## 9. Test Plan

All tests live in `ts/tests/`.  Test runner is Jest + ts-jest.

### 9.1 Phase 1 — Enum & Data Class Tests (`tests/api/`)

**`tests/api/enums.test.ts`**

| Test | Assertion |
|---|---|
| `ScopeTypeT.COVERGROUP` equals Python value | `=== 0x1000n` |
| `ScopeTypeT.ALL` includes all non-reserved type bits | `ScopeTypeT.ALL & ScopeTypeT.COVERGROUP !== 0n` |
| `isDUScope(ScopeTypeT.DU_MODULE)` | `true` |
| `isDUScope(ScopeTypeT.COVERGROUP)` | `false` |
| `CoverTypeT.CVGBIN` value | `=== 1` |
| `CoverFlagsT.HAS_GOAL \| CoverFlagsT.HAS_WEIGHT` | `=== 0x18` |
| `HistoryNodeKind.TEST` | `=== 1` |
| `SourceT.SV` | `=== 2` (auto-incremented from 0) |
| All scope type values unique | set size equals key count |
| All cover type values unique | set size equals key count |

**`tests/api/data_classes.test.ts`**

| Test | Assertion |
|---|---|
| `new CoverData(CoverTypeT.CVGBIN, 0)` defaults | `data=0, goal=0, atLeast=1` |
| `new SourceInfo(null, 42, 0)` | fields accessible |
| `new TestData(TestStatusT.OK)` | `testStatus === TestStatusT.OK` |
| `CoverIndex` wraps index correctly | `new CoverIndex(3).index === 3` |
| `FileHandle` stores name/workdir | fields accessible |

### 9.2 Phase 2 — Abstract API Shape Tests (`tests/api/`)

**`tests/api/abstract_shape.test.ts`**

These tests verify the TypeScript abstract class signatures compile and that
concrete stubs reject unimplemented methods correctly.

Use a minimal stub that extends each abstract class with `override`
implementations throwing `new Error('stub')`:

```typescript
class StubScope extends Scope { /* minimal overrides */ }
```

| Test | Assertion |
|---|---|
| `StubScope.getScopeName()` throws | expected |
| `StubUCIS.historyNodes()` iterates empty | compiles and runs |
| `HistoryNode.setTestData()` dispatches to abstract setters | `setCalled === true` on spy |
| `Covergroup.getIntProperty(_, CVG_PERINSTANCE)` delegates | calls `getPerInstance()` |

### 9.3 Phase 3 — In-Memory Backend Tests (`tests/mem/`)

**`tests/mem/mem_scope.test.ts`**

| Test | Assertion |
|---|---|
| Create DU_MODULE scope | `getScopeType() === ScopeTypeT.DU_MODULE` |
| Create child INSTANCE scope | parent iterator yields it |
| `scopes(ScopeTypeT.INSTANCE)` filters by mask | only instances returned |
| `scopes(ScopeTypeT.ALL)` yields all children | |
| `createNextCover()` assigns sequential indices | 0, 1, 2, … |
| `coverItems()` yields all covers | count matches |
| `getParent()` on child scope | returns parent |

**`tests/mem/mem_covergroup.test.ts`**

| Test | Assertion |
|---|---|
| Create covergroup under instance | type `COVERGROUP` |
| `setPerInstance(true)` / `getPerInstance()` round-trips | |
| `createCoverpoint()` returns `Coverpoint` | `instanceof MemCoverpoint` |
| `createCross()` with two coverpoints | `getCrossedPoints().length === 2` |
| `createCoverInstance()` returns sub-`Covergroup` | type `COVERINSTANCE` |
| `getIntProperty(_, CVG_PERINSTANCE)` delegates | `=== 0` or `=== 1` |

**`tests/mem/mem_coverpoint.test.ts`**

| Test | Assertion |
|---|---|
| `createBin()` creates CVGBIN cover item | `data.type === CoverTypeT.CVGBIN` |
| `setAtLeast(2)` / `getAtLeast()` | round-trips |
| Multiple bins with `count > 0` | counts preserved |

**`tests/mem/mem_ucis.test.ts`**

| Test | Assertion |
|---|---|
| `MemFactory.create()` returns `MemUCIS` | not null |
| `createFileHandle()` returns `FileHandle` | |
| `getFileHandles()` iterates created handles | count matches |
| `createHistoryNode(_, 'test1', _, TEST)` | in `historyNodes(TEST)` |
| `historyNodes(MERGE)` excludes TEST nodes | |
| `historyNodes(ALL)` includes all | |
| `getNumTests()` counts TEST nodes only | |
| `getWrittenBy()` / `setWrittenBy()` round-trips | |

**`tests/mem/mem_hierarchy.test.ts`**

Full hierarchy construction test mirroring Python `test_mem_hierarchy.py`:

1. Create DB
2. Create DU_MODULE scope `'counter'`
3. Create INSTANCE `'top'` under DB root (linked to DU)
4. Create covergroup `'cg'` under instance
5. Create coverpoints `'cp_addr'`, `'cp_data'`
6. Add bins to each
7. Create cross of the two coverpoints
8. Verify DFS traversal yields expected scope names in order
9. Verify cover item counts per scope

**`tests/mem/mem_history_node.test.ts`**

| Test | Assertion |
|---|---|
| `setTestData()` sets all fields | each getter returns set value |
| `getKind()` returns `TEST` | |
| `getLogicalName()` after `setLogicalName()` | round-trip |

### 9.4 Phase 4 — NCDB Binary Codec Tests (`tests/ncdb/`)

**`tests/ncdb/varint.test.ts`**

| Test | Input → expected output |
|---|---|
| Encode 0 | `[0x00]` |
| Encode 127 | `[0x7F]` |
| Encode 128 | `[0x80, 0x01]` |
| Encode 300 | `[0xAC, 0x02]` |
| Encode max safe int | correct bytes |
| Decode round-trip for 1000 random values | value === original |
| `decodeVarUints` with count=3 | returns 3 values + correct new offset |
| Truncated buffer | throws |

**`tests/ncdb/string_table.test.ts`**

| Test | Assertion |
|---|---|
| Empty table: `intern('')` → 0 | |
| `intern('foo')` → 1, second call → 1 (dedup) | |
| `get(1)` → `'foo'` | |
| `encode()` / `StringTable.decode()` round-trip (10 strings) | all strings preserved |
| Decode of known byte sequence | matches expected strings |
| UTF-8 strings with non-ASCII | preserved through encode/decode |

**`tests/ncdb/counts.test.ts`**

| Test | Assertion |
|---|---|
| Encode `[]` | 3 bytes: mode + count varint + empty |
| Encode `[0,0,0]` mode selection | picks smaller encoding |
| Encode `[0,1,2,…,255]` | uint32 vs varint sizes compared correctly |
| Large count (> 0xFFFFFFFF) | forces varint mode |
| `decode(encode(values))` round-trip for `[0,0,0,1,0,0,5]` | exact match |
| Mode 0 decode | correct values |
| Mode 1 decode | correct values |

**`tests/ncdb/sources.test.ts`**

| Test | Assertion |
|---|---|
| `SourcesWriter.write(db)` with 2 file handles | JSON array with 2 filenames |
| `SourcesReader.readToArray(json, db)` | returns 2 `FileHandle` objects |
| Empty sources | `[]` JSON |

**`tests/ncdb/history.test.ts`**

| Test | Assertion |
|---|---|
| `HistoryWriter.write(db)` with one TEST node | JSON has `logical_name`, `kind: "TEST"` |
| `HistoryReader.read(json, db)` populates node | `getLogicalName()` matches |
| Kind `"MERGE"` deserialized | `getKind() === HistoryNodeKind.MERGE` |
| `test_status` int ↔ `TestStatusT` round-trip | |
| All fields round-trip | every field set/get matches |

**`tests/ncdb/scope_tree.test.ts`**

| Test | Assertion |
|---|---|
| Write + read empty db | no scopes, no counts |
| Write + read single INSTANCE scope | name, type preserved |
| Write + read toggle pair | 2 TOGGLEBIN cover items, counts = `[3, 7]` |
| Write + read COVERGROUP with COVERPOINT and 3 bins | hierarchy preserved |
| Presence bits: scope with source info | `srcinfo.line === 42` after read |
| Presence bits: non-default weight | `weight === 5` after read |
| Presence bits: non-default goal | `goal === 80` after read |
| DU linkage fixup | INSTANCE scope `getDuScope()` points to actual DU |
| 100-scope stress test | all names preserved in order |

### 9.5 Phase 4/5 — Integration Tests (`tests/ncdb/`)

**`tests/ncdb/roundtrip.test.ts`**

Construct a `MemUCIS` programmatically, write to a temp `.cdb` file,
read back, compare scope structure:

```
DB
├── DU_MODULE 'counter'
├── INSTANCE 'top' → counter
│   ├── COVERGROUP 'cg_addr'
│   │   ├── COVERPOINT 'cp_addr'
│   │   │   ├── CVGBIN 'low'   count=5
│   │   │   ├── CVGBIN 'mid'   count=0
│   │   │   └── CVGBIN 'high'  count=1
│   │   └── CROSS 'x_addr_data'
│   ├── BRANCH 'sig_valid' (toggle pair)
│   │   (implicit: '0 -> 1' count=3, '1 -> 0' count=2)
└── HistoryNode TEST 'smoke' status=OK seed='42'
```

Assertions:
1. Scope count matches
2. Cover item names and counts preserved exactly
3. Toggle pair bin names and counts preserved
4. History node logical name, kind, seed match
5. File handles preserved
6. Source info (line numbers) preserved

**`tests/ncdb/cross_language.test.ts`** *(requires Python to be installed)*

Skip in CI if Python unavailable.  Uses `child_process.execSync` to:
1. Write a `.cdb` with Python's `NcdbWriter`
2. Read it with the TS `NcdbReader` — assert expected scope names
3. Write a `.cdb` with TS `NcdbWriter`
4. Read it with Python's `NcdbReader` (via a small Python helper script)
   and assert expected output on stdout

This is the most critical correctness gate for format compatibility.

### 9.6 Phase 7 — Visitor Tests (`tests/`)

**`tests/visitors.test.ts`**

| Test | Assertion |
|---|---|
| `traverse` visits all scopes in DFS order | order matches expected |
| `enterScope` returning `false` skips subtree | child not visited |
| `visitCoverItem` called for each cover item | count matches |
| `typeMask = ScopeTypeT.COVERGROUP` limits traversal | only covergroups entered |
| Empty DB traversal | no errors |

**`tests/ext.test.ts`**

| Test | Assertion |
|---|---|
| `FormatRegistry.register(ncdbFormat)` | `getDbFormat('ncdb')` returns it |
| `getDbFormat('unknown')` throws | error message meaningful |
| `dbFormats()` returns read-only map | `.set()` throws |

---

## 10. Documentation Plan

### 10.1 `ts/README.md`

Must cover:
- Installation (`npm install` within `ts/`)
- Build (`npm run build`)
- Test (`npm test`)
- Quick-start example: create a `MemUCIS`, add covergroup, write to NCDB
- Quick-start example: read an NCDB file, traverse scopes
- Reference links to DESIGN.md and the Python implementation

### 10.2 TSDoc comments

Every `public` class, interface, method, and exported constant must have a
TSDoc comment.  Minimum content per element:

| Element | Required TSDoc sections |
|---|---|
| Class / abstract class | `@summary`, `@remarks` (design intent), `@example` |
| `abstract` method | `@param` per param, `@returns`, `@throws` |
| Concrete method | `@param`, `@returns` |
| Enum / const-object value | Inline description comment |
| Data class constructor | `@param` per param |

**Priority order for documentation (write in this order):**

1. All enum values in `src/api/enums/` — one-line descriptions
2. `Obj`, `Scope`, `UCIS` class + all methods
3. `Covergroup`, `Coverpoint`, `Cross`, `HistoryNode`
4. Data classes: `CoverData`, `SourceInfo`, `TestData`
5. `MemUCIS`, `MemScope` (reference Python docstrings)
6. `NcdbReader`, `NcdbWriter` — explain the ZIP member pipeline
7. `StringTable`, `varint` — explain encoding formats
8. `traverse`, `UCISVisitor`

### 10.3 `ts/src/ncdb/FORMAT.md` (internal reference)

Inline format documentation for contributors:
- NCDB ZIP member list and their roles
- Scope tree binary layout (presence bitfield table)
- Toggle-pair encoding
- Counts.bin mode selection
- History JSON schema

This mirrors `python/covsight/core/ncdb/constants.py` documentation comments
but in a format more accessible for TypeScript contributors.

---

## 11. Cross-cutting Constraints

### 11.1 Format compatibility

The TypeScript implementation MUST produce `.cdb` files bit-for-bit
compatible with the Python implementation.  The cross-language integration
test (§9.5) is the gate for this.

### 11.2 `bigint` scope type handling

The NCDB binary format stores scope types as two consecutive unsigned
LEB128 varints (low 32 bits, high 32 bits).  The reader must reconstruct
the full `bigint` as:
```typescript
const scopeType = BigInt(lo) | (BigInt(hi) << 32n);
```
The writer must split a `bigint` the same way.  This is the primary place
where `bigint` crosses the wire boundary.

### 11.3 Index alignment (scope tree ↔ counts)

The `ScopeTreeReader` and `CountsReader` are **decoupled**: the scope tree
reader drives a DFS and pulls values from a `counts` iterator.  The counts
are stored in DFS pre-order.  Any deviation breaks count alignment silently.
The integration roundtrip test (§9.5) detects this.

### 11.4 String table pre-population

The empty string `''` must always be at index 0.  Call `strings.intern('')`
in the `StringTable` constructor.  Any code that uses string index 0 as
"no name" relies on this invariant.

### 11.5 ESM module paths

All intra-package imports must include the `.js` extension (TypeScript ESM
convention):
```typescript
import { MemScope } from './MemScope.js';  // ✓
import { MemScope } from './MemScope';     // ✗ breaks at runtime
```

### 11.6 No circular dependencies

The dependency graph must be acyclic:
```
enums ← data-classes ← api ← mem ← ncdb ← ext/visitors
```
`api` must not import from `mem` or `ncdb`.  `mem` must not import from
`ncdb`.  Use dependency-cruiser or `madge` to enforce this in CI.

### 11.7 Strict null safety

All optional fields (`srcinfo`, `parent`, etc.) use `T | null` (not
`T | undefined`).  Callers must null-check before use.  The `tsconfig` option
`noUncheckedIndexedAccess: true` enforces this for array access.
