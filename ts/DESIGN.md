# TypeScript UCIS API & NCDB Infrastructure — Design

## Overview

This document describes the design for a TypeScript implementation of the
UCIS (Unified Coverage Interoperability Standard) object model and supporting
infrastructure for reading/writing the NCDB `.cdb` file format.

The design mirrors `python/covsight/core/` closely so that the two
implementations stay in alignment, but it follows idiomatic TypeScript
conventions throughout (interfaces, abstract classes, strict typing, `bigint`
for 64-bit masks, `AsyncIterable` for lazy I/O, etc.).

---

## Directory Layout

```
ts/
├── package.json
├── tsconfig.json
├── src/
│   ├── api/                  # Abstract UCIS object model (no I/O)
│   │   ├── enums/
│   │   │   ├── ScopeTypeT.ts
│   │   │   ├── CoverTypeT.ts
│   │   │   ├── CoverFlagsT.ts
│   │   │   ├── FlagsT.ts
│   │   │   ├── SourceT.ts
│   │   │   ├── HistoryNodeKind.ts
│   │   │   ├── TestStatusT.ts
│   │   │   ├── ToggleMetricT.ts
│   │   │   ├── ToggleTypeT.ts
│   │   │   ├── ToggleDirT.ts
│   │   │   ├── IntProperty.ts
│   │   │   └── index.ts
│   │   ├── SourceInfo.ts
│   │   ├── FileHandle.ts
│   │   ├── CoverData.ts
│   │   ├── CoverIndex.ts
│   │   ├── TestData.ts
│   │   ├── Obj.ts            # abstract base
│   │   ├── Scope.ts          # abstract, extends Obj
│   │   ├── UCIS.ts           # abstract root db, extends Scope
│   │   ├── Covergroup.ts     # abstract, extends Scope
│   │   ├── Coverpoint.ts     # abstract, extends Scope
│   │   ├── Cross.ts          # abstract, extends Scope
│   │   ├── HistoryNode.ts    # abstract, extends Obj
│   │   └── index.ts
│   ├── mem/                  # In-memory UCIS implementation
│   │   ├── MemUCIS.ts
│   │   ├── MemScope.ts
│   │   ├── MemCovergroup.ts
│   │   ├── MemCoverpoint.ts
│   │   ├── MemCross.ts
│   │   ├── MemHistoryNode.ts
│   │   └── index.ts
│   ├── ncdb/                 # NCDB .cdb ZIP format
│   │   ├── constants.ts      # member names, magic bytes, format version
│   │   ├── StringTable.ts    # strings.bin encode/decode
│   │   ├── ScopeTreeReader.ts
│   │   ├── ScopeTreeWriter.ts
│   │   ├── CountsReader.ts
│   │   ├── CountsWriter.ts
│   │   ├── HistoryReader.ts
│   │   ├── HistoryWriter.ts
│   │   ├── SourcesReader.ts
│   │   ├── SourcesWriter.ts
│   │   ├── ManifestReader.ts
│   │   ├── ManifestWriter.ts
│   │   ├── NcdbReader.ts     # top-level: ZIP → MemUCIS
│   │   ├── NcdbWriter.ts     # top-level: MemUCIS → ZIP
│   │   └── index.ts
│   └── index.ts              # re-exports api + mem + ncdb
└── tests/
    ├── api/
    │   └── scope.test.ts
    ├── mem/
    │   └── mem_ucis.test.ts
    └── ncdb/
        └── roundtrip.test.ts
```

---

## Package Configuration

```jsonc
// package.json
{
  "name": "@covsight/core",
  "version": "0.1.0",
  "type": "module",
  "main": "dist/index.js",
  "types": "dist/index.d.ts",
  "scripts": {
    "build": "tsc",
    "test":  "node --experimental-vm-modules node_modules/.bin/jest"
  },
  "dependencies": {
    "jszip": "^3.10.1"          // ZIP container for NCDB .cdb files
  },
  "devDependencies": {
    "typescript": "^5.4.0",
    "@types/node": "^20.0.0",
    "jest": "^29.0.0",
    "ts-jest": "^29.0.0",
    "@types/jest": "^29.0.0"
  }
}
```

> **Note on ZIP library:** `jszip` works in both Node.js and browsers, making
> the ncdb layer portable.  For high-throughput CLI use cases a native
> binding to the C `libncdb` (from `c/`) via `node-addon-api` can replace the
> pure-TS reader/writer in a future phase without changing the public API.

---

## Enumerations (`src/api/enums/`)

All enumerations use TypeScript `const enum` (or plain numeric `enum`) with
`bigint` **only** where bitmask values exceed 2^53 − 1.  The scope/cover type
masks use `bigint` because the Python originals use 64-bit flags.

### ScopeTypeT.ts

```typescript
export const enum ScopeTypeT {
  TOGGLE           = 0x0000000000000001n,  // bigint literal
  BRANCH           = 0x0000000000000002n,
  EXPR             = 0x0000000000000004n,
  COND             = 0x0000000000000008n,
  INSTANCE         = 0x0000000000000010n,
  PROCESS          = 0x0000000000000020n,
  BLOCK            = 0x0000000000000040n,
  FUNCTION         = 0x0000000000000080n,
  FORKJOIN         = 0x0000000000000100n,
  GENERATE         = 0x0000000000000200n,
  GENERIC          = 0x0000000000000400n,
  CLASS            = 0x0000000000000800n,
  COVERGROUP       = 0x0000000000001000n,
  COVERINSTANCE    = 0x0000000000002000n,
  COVERPOINT       = 0x0000000000004000n,
  CROSS            = 0x0000000000008000n,
  COVER            = 0x0000000000010000n,
  ASSERT           = 0x0000000000020000n,
  PROGRAM          = 0x0000000000040000n,
  PACKAGE          = 0x0000000000080000n,
  TASK             = 0x0000000000100000n,
  INTERFACE        = 0x0000000000200000n,
  FSM              = 0x0000000000400000n,
  DU_MODULE        = 0x0000000001000000n,
  DU_ARCH          = 0x0000000002000000n,
  DU_PACKAGE       = 0x0000000004000000n,
  DU_PROGRAM       = 0x0000000008000000n,
  DU_INTERFACE     = 0x0000000010000000n,
  FSM_STATES       = 0x0000000020000000n,
  FSM_TRANS        = 0x0000000040000000n,
  COVBLOCK         = 0x0000000080000000n,
  CVGBINSCOPE      = 0x0000000100000000n,
  ILLEGALBINSCOPE  = 0x0000000200000000n,
  IGNOREBINSCOPE   = 0x0000000400000000n,
  ALL              = 0x0000FFFFFFFFFFFFn,
}

export const DU_MASK = 0x000000001F000000n;
export function isDUScope(t: bigint): boolean {
  return (t & DU_MASK) !== 0n;
}
```

> Because TypeScript `const enum` with `bigint` initializers requires
> `"useDefineForClassFields": false` and recent tsc, an alternative is a
> plain `object` namespace with `bigint` constants — either approach is fine.

### CoverTypeT.ts  /  CoverFlagsT.ts

Same pattern; values fit in 32-bit safe integers for these enums so regular
`const enum` with number literals is used.

### Other enums (FlagsT, SourceT, HistoryNodeKind, TestStatusT, toggle enums)

Plain `const enum` with `number` values, mirroring the Python counterparts.

---

## Core Data Classes

### SourceInfo.ts

```typescript
import type { FileHandle } from './FileHandle.js';

export class SourceInfo {
  constructor(
    public readonly file: FileHandle | null,
    public readonly line: number,
    public readonly token: number,
  ) {}
}
```

### FileHandle.ts

```typescript
export class FileHandle {
  constructor(
    public readonly fileName: string,
    public readonly workDir: string,
  ) {}
}
```

### CoverData.ts

```typescript
import type { CoverTypeT } from './enums/CoverTypeT.js';

export class CoverData {
  data   = 0;
  goal   = 0;
  weight = 0;
  limit  = 0;
  bitlen = 0;
  atLeast = 1;

  constructor(
    public readonly type: CoverTypeT,
    public flags: number,
  ) {}
}
```

### CoverIndex.ts

```typescript
/** Opaque index to a cover item within a scope. */
export class CoverIndex {
  constructor(public readonly index: number) {}
}
```

### TestData.ts

```typescript
import type { TestStatusT } from './enums/TestStatusT.js';

export class TestData {
  toolCategory = '';
  date         = '';
  simTime      = 0;
  timeUnit     = '';
  runCwd       = '';
  cpuTime      = 0;
  seed         = '';
  cmd          = '';
  args: string[] = [];
  compulsory: string[] = [];
  user  = '';
  cost  = 0;

  constructor(public testStatus: TestStatusT) {}
}
```

---

## Abstract API Classes (`src/api/`)

TypeScript uses `abstract class` to replicate Python's ABC pattern.
All methods that lack a universal implementation throw `new Error('Not implemented')`.

### Obj.ts

```typescript
import type { IntProperty } from './enums/IntProperty.js';

export abstract class Obj {
  getIntProperty(coverIndex: number, property: IntProperty): number {
    throw new Error(`getIntProperty(${property}) not implemented`);
  }
  setIntProperty(coverIndex: number, property: IntProperty, value: number): void {
    throw new Error(`setIntProperty(${property}) not implemented`);
  }
  getGoal(): number { return this._goal; }
  setGoal(goal: number): void { this._goal = goal; }
  private _goal = 100;
}
```

### Scope.ts

```typescript
import { Obj }         from './Obj.js';
import type { SourceInfo }  from './SourceInfo.js';
import type { CoverData }   from './CoverData.js';
import type { CoverIndex }  from './CoverIndex.js';
import type { ScopeTypeT }  from './enums/ScopeTypeT.js';
import type { SourceT }     from './enums/SourceT.js';
import type { FlagsT }      from './enums/FlagsT.js';
import type { CoverTypeT }  from './enums/CoverTypeT.js';
import type { ToggleMetricT, ToggleTypeT, ToggleDirT } from './enums/index.js';
import type { Covergroup }  from './Covergroup.js';

export abstract class Scope extends Obj {
  /** Iterate child scopes matching the given type mask. */
  abstract scopes(typeMask: bigint): Iterable<Scope>;

  /** Iterate cover items in this scope. */
  abstract coverItems(): Iterable<{ name: string; data: CoverData; index: CoverIndex }>;

  abstract getScopeName(): string;
  abstract getScopeType(): bigint;
  abstract getParent(): Scope | null;

  abstract createScope(
    name:    string,
    srcinfo: SourceInfo | null,
    weight:  number,
    source:  SourceT,
    type:    bigint,
    flags:   number,
  ): Scope;

  abstract createInstance(
    name:    string,
    srcinfo: SourceInfo | null,
    weight:  number,
    source:  SourceT,
    type:    bigint,
    duScope: Scope,
    flags:   number,
  ): Scope;

  abstract createCovergroup(
    name:    string,
    srcinfo: SourceInfo | null,
    weight:  number,
    source:  SourceT,
  ): Covergroup;

  abstract createToggle(
    name:          string,
    canonicalName: string,
    flags:         number,
    metric:        ToggleMetricT,
    type:          ToggleTypeT,
    dir:           ToggleDirT,
  ): Scope;

  abstract createNextCover(
    name:    string,
    data:    CoverData,
    srcinfo: SourceInfo | null,
  ): CoverIndex;
}
```

### UCIS.ts

```typescript
import { Scope }          from './Scope.js';
import type { HistoryNode }    from './HistoryNode.js';
import type { HistoryNodeKind } from './enums/HistoryNodeKind.js';
import type { FileHandle }     from './FileHandle.js';
import type { IntProperty }    from './enums/IntProperty.js';

export abstract class UCIS extends Scope {
  abstract read(path: string):  Promise<void>;
  abstract write(path: string): Promise<void>;
  abstract close(): void;

  abstract isModified(): boolean;
  abstract modifiedSinceSim(): boolean;
  abstract getNumTests(): number;

  abstract getAPIVersion():  string;
  abstract getWrittenBy():   string;
  abstract setWrittenBy(by: string): void;
  abstract getWrittenTime(): number;
  abstract setWrittenTime(t: number): void;

  abstract createHistoryNode(
    parent:       HistoryNode | null,
    logicalName:  string,
    physicalName: string,
    kind:         HistoryNodeKind,
  ): HistoryNode;

  abstract historyNodes(kindMask: HistoryNodeKind): Iterable<HistoryNode>;

  abstract createFileHandle(fileName: string, workDir: string): FileHandle;
}
```

### Covergroup.ts

```typescript
import { Scope }          from './Scope.js';
import type { Coverpoint }     from './Coverpoint.js';
import type { Cross }          from './Cross.js';
import type { SourceInfo }     from './SourceInfo.js';
import type { SourceT }        from './enums/SourceT.js';

export abstract class Covergroup extends Scope {
  abstract getPerInstance(): boolean;
  abstract setPerInstance(v: boolean): void;
  abstract getMergeInstances(): boolean;
  abstract setMergeInstances(v: boolean): void;
  abstract getGetInstCoverage(): boolean;
  abstract setGetInstCoverage(v: boolean): void;

  abstract createCoverpoint(
    name:    string,
    srcinfo: SourceInfo | null,
    weight:  number,
    source:  SourceT,
  ): Coverpoint;

  abstract createCross(
    name:    string,
    srcinfo: SourceInfo | null,
    weight:  number,
    source:  SourceT,
    points:  Coverpoint[],
  ): Cross;

  abstract createCoverInstance(
    name:    string,
    srcinfo: SourceInfo | null,
    weight:  number,
    source:  SourceT,
  ): Covergroup;
}
```

### Coverpoint.ts

```typescript
import { Scope }        from './Scope.js';
import type { SourceInfo }   from './SourceInfo.js';
import type { CoverIndex }   from './CoverIndex.js';
import type { CoverTypeT }   from './enums/CoverTypeT.js';

export abstract class Coverpoint extends Scope {
  abstract getAtLeast(): number;
  abstract setAtLeast(n: number): void;

  abstract createBin(
    name:    string,
    srcinfo: SourceInfo | null,
    atLeast: number,
    count:   number,
    rhs:     string,
    kind:    CoverTypeT,
  ): CoverIndex;
}
```

### Cross.ts

```typescript
import { Scope }       from './Scope.js';
import type { Coverpoint }  from './Coverpoint.js';

export abstract class Cross extends Scope {
  abstract getCrossedPoints(): Coverpoint[];
}
```

### HistoryNode.ts

```typescript
import { Obj }          from './Obj.js';
import type { TestData }     from './TestData.js';
import type { TestStatusT }  from './enums/TestStatusT.js';
import type { HistoryNodeKind } from './enums/HistoryNodeKind.js';

export abstract class HistoryNode extends Obj {
  abstract getKind(): HistoryNodeKind;
  abstract getParent(): HistoryNode | null;
  abstract getLogicalName():  string;
  abstract setLogicalName(n: string): void;
  abstract getPhysicalName(): string;
  abstract setPhysicalName(n: string): void;

  abstract getTestStatus(): TestStatusT;
  abstract setTestStatus(s: TestStatusT): void;
  abstract getSimTime():  number;
  abstract setSimTime(t:  number): void;
  abstract getTimeUnit(): string;
  abstract setTimeUnit(u: string): void;
  abstract getRunCwd():   string;
  abstract setRunCwd(cwd: string): void;
  abstract getCpuTime():  number;
  abstract setCpuTime(t:  number): void;
  abstract getSeed():     string;
  abstract setSeed(s:     string): void;
  abstract getCmd():      string;
  abstract setCmd(c:      string): void;
  abstract getArgs():     string[];
  abstract setArgs(a:     string[]): void;
  abstract getDate():     string;
  abstract setDate(d:     string): void;
  abstract getUserName(): string;
  abstract setUserName(u: string): void;
  abstract getCost():     number;
  abstract setCost(c:     number): void;
  abstract getToolCategory(): string;
  abstract setToolCategory(c: string): void;
  abstract getVendorId():          string;
  abstract setVendorId(id:         string): void;
  abstract getVendorTool():        string;
  abstract setVendorTool(t:        string): void;
  abstract getVendorToolVersion(): string;
  abstract setVendorToolVersion(v: string): void;
  abstract getComment(): string;
  abstract setComment(c: string): void;

  /** Bulk-set all test metadata from a TestData object. */
  setTestData(td: TestData): void {
    this.setTestStatus(td.testStatus);
    this.setToolCategory(td.toolCategory);
    this.setDate(td.date);
    this.setSimTime(td.simTime);
    this.setTimeUnit(td.timeUnit);
    this.setRunCwd(td.runCwd);
    this.setCpuTime(td.cpuTime);
    this.setSeed(td.seed);
    this.setCmd(td.cmd);
    this.setArgs(td.args);
    this.setUserName(td.user);
    this.setCost(td.cost);
  }
}
```

---

## In-Memory Implementation (`src/mem/`)

`MemUCIS` provides a fully in-memory UCIS backend.  It is the primary
concrete implementation and the target that `NcdbReader` populates.

### MemScope.ts (key fields)

```typescript
export class MemScope extends Scope {
  private _children: MemScope[] = [];
  private _coverItems: Array<{ name: string; data: CoverData; index: CoverIndex }> = [];

  constructor(
    private _name: string,
    private _type: bigint,
    private _parent: MemScope | null,
    private _source: SourceT,
    private _flags: number,
    public weight: number,
  ) { super(); }

  getScopeName() { return this._name; }
  getScopeType() { return this._type; }
  getParent()    { return this._parent; }

  *scopes(typeMask: bigint): Iterable<Scope> {
    for (const child of this._children)
      if (child._type & typeMask) yield child;
  }

  *coverItems() { yield* this._coverItems; }

  createScope(name, srcinfo, weight, source, type, flags): Scope {
    const s = new MemScope(name, type, this, source, flags, weight);
    this._children.push(s);
    return s;
  }

  // createInstance, createCovergroup, createToggle, createNextCover similarly
  // ...
}
```

### MemUCIS.ts

```typescript
export class MemUCIS extends MemScope implements UCIS {
  private _historyNodes: MemHistoryNode[] = [];
  private _fileHandles:  FileHandle[]     = [];
  private _writtenBy  = '';
  private _writtenTime = 0;

  constructor() {
    super('', ScopeTypeT.ALL, null, SourceT.NONE, 0, 1);
  }

  async read(path: string): Promise<void> {
    const reader = new NcdbReader();
    await reader.readInto(path, this);
  }

  async write(path: string): Promise<void> {
    const writer = new NcdbWriter();
    await writer.write(path, this);
  }

  close(): void { /* flush / GC */ }

  createHistoryNode(parent, logicalName, physicalName, kind): HistoryNode {
    const n = new MemHistoryNode(parent as MemHistoryNode | null, logicalName, physicalName, kind);
    this._historyNodes.push(n);
    return n;
  }

  *historyNodes(kindMask: HistoryNodeKind): Iterable<HistoryNode> {
    for (const n of this._historyNodes)
      if (kindMask === HistoryNodeKind.ALL || n.getKind() === kindMask)
        yield n;
  }

  createFileHandle(fileName: string, workDir: string): FileHandle {
    const fh = new FileHandle(fileName, workDir);
    this._fileHandles.push(fh);
    return fh;
  }

  isModified(): boolean         { return this._modified; }
  modifiedSinceSim(): boolean   { return this._modSinceSim; }
  getNumTests(): number         { return this._historyNodes.filter(n => n.getKind() === HistoryNodeKind.TEST).length; }
  getAPIVersion(): string       { return '1.0'; }
  getWrittenBy(): string        { return this._writtenBy; }
  setWrittenBy(by: string)      { this._writtenBy = by; }
  getWrittenTime(): number      { return this._writtenTime; }
  setWrittenTime(t: number)     { this._writtenTime = t; }

  private _modified    = false;
  private _modSinceSim = false;
}
```

---

## NCDB Infrastructure (`src/ncdb/`)

The NCDB `.cdb` file is a ZIP archive containing named binary/JSON members.
TypeScript uses `jszip` for ZIP I/O.

### constants.ts

```typescript
export const NCDB_FORMAT    = 'NCDB';
export const NCDB_VERSION   = '2.0';
export const NCDB_GENERATOR = 'covsight-ts';

// ZIP member names (must match Python constants.py exactly)
export const MEMBER_MANIFEST     = 'manifest.json';
export const MEMBER_STRINGS      = 'strings.bin';
export const MEMBER_SCOPE_TREE   = 'scope_tree.bin';
export const MEMBER_COUNTS       = 'counts.bin';
export const MEMBER_HISTORY      = 'history.json';
export const MEMBER_SOURCES      = 'sources.json';
export const MEMBER_ATTRS        = 'attrs.bin';
export const MEMBER_TOGGLE       = 'toggle.bin';
export const MEMBER_FSM          = 'fsm.bin';
export const MEMBER_CROSS        = 'cross.bin';
export const MEMBER_DESIGN_UNITS = 'design_units.json';
export const MEMBER_PROPERTIES   = 'properties.json';
export const MEMBER_FORMAL       = 'formal.bin';

// v2 history store members
export const MEMBER_TEST_REGISTRY = 'test_registry.bin';
export const MEMBER_TEST_STATS    = 'test_stats.bin';
export const MEMBER_BUCKET_INDEX  = 'history/bucket_index.bin';
export const HISTORY_BUCKET_DIR   = 'history/';
export const HISTORY_FORMAT_V1    = 'v1';
export const HISTORY_FORMAT_V2    = 'v2';
```

### Binary encoding helpers

```typescript
// varint.ts — LEB128 unsigned integer encode/decode
export function readVarUint(buf: Uint8Array, offset: number): [value: number, bytesRead: number] { ... }
export function writeVarUint(value: number): Uint8Array { ... }
```

### StringTable.ts

```typescript
/** Decode/encode strings.bin: [count:varint][len:varint][utf8 bytes] ... */
export class StringTable {
  private _strings: string[] = [''];  // index 0 is always ''

  static decode(buf: Uint8Array): StringTable { ... }
  encode(): Uint8Array { ... }
  get(index: number): string { return this._strings[index]; }
  intern(s: string): number { ... }  // returns existing index or appends
}
```

### ScopeTreeReader.ts  /  ScopeTreeWriter.ts

Decode/encode `scope_tree.bin` — a DFS pre-order encoding of the scope
hierarchy.  Each scope record contains:

| Field           | Encoding              | Notes                          |
|-----------------|-----------------------|-------------------------------|
| presence flags  | 1 byte                | bitmask of optional fields     |
| name index      | varint                | index into StringTable         |
| scope type      | varint (64-bit split) | two 32-bit varints, LE         |
| source type     | varint                |                                |
| flags           | varint                |                                |
| weight          | varint (if present)   |                                |
| DFS depth       | implicit (stack)      | END marker = pop stack         |

```typescript
export class ScopeTreeReader {
  read(buf: Uint8Array, strings: StringTable, db: MemUCIS): void { ... }
}

export class ScopeTreeWriter {
  write(db: MemUCIS, strings: StringTable): Uint8Array { ... }
}
```

### CountsReader.ts  /  CountsWriter.ts

Decode/encode `counts.bin`.  First byte is mode:
- `0` = fixed uint32 LE per cover item
- `1` = LEB128 varint per cover item

```typescript
export class CountsReader {
  read(buf: Uint8Array, db: MemUCIS): void { ... }
}
export class CountsWriter {
  write(db: MemUCIS): Uint8Array { ... }
}
```

### HistoryReader.ts  /  HistoryWriter.ts

Decode/encode `history.json`.  v2 additionally reads binary bucket files
under `history/`.  The reader populates `MemHistoryNode` objects on `MemUCIS`.

```typescript
export class HistoryReader {
  read(historyJson: string, db: MemUCIS): void { ... }
}
export class HistoryWriter {
  write(db: MemUCIS): string { ... }  // returns JSON string
}
```

### NcdbReader.ts

Top-level reader: opens the ZIP, dispatches each member to the appropriate
sub-reader, returns a populated `MemUCIS`.

```typescript
import JSZip from 'jszip';

export class NcdbReader {
  async read(path: string): Promise<MemUCIS> {
    const db = new MemUCIS();
    await this.readInto(path, db);
    return db;
  }

  async readInto(path: string, db: MemUCIS): Promise<void> {
    const buf  = await fs.promises.readFile(path);
    const zip  = await JSZip.loadAsync(buf);

    // 1. Manifest — validate format/version
    const manifest = JSON.parse(await zip.file(MEMBER_MANIFEST)!.async('string'));
    if (manifest.format !== NCDB_FORMAT) throw new Error(`Unsupported format: ${manifest.format}`);

    // 2. String table
    const strBuf  = await zip.file(MEMBER_STRINGS)!.async('uint8array');
    const strings = StringTable.decode(strBuf);

    // 3. Sources (FileHandle registry)
    const srcJson = await zip.file(MEMBER_SOURCES)?.async('string');
    if (srcJson) new SourcesReader().read(srcJson, db);

    // 4. Scope tree + counts
    const treeBuf   = await zip.file(MEMBER_SCOPE_TREE)!.async('uint8array');
    const countsBuf = await zip.file(MEMBER_COUNTS)!.async('uint8array');
    new ScopeTreeReader().read(treeBuf, strings, db);
    new CountsReader().read(countsBuf, db);

    // 5. History
    const histJson = await zip.file(MEMBER_HISTORY)?.async('string');
    if (histJson) new HistoryReader().read(histJson, db);

    // 6. Optional members (toggle, fsm, cross, attrs, …)
    const toggleBuf = await zip.file(MEMBER_TOGGLE)?.async('uint8array');
    if (toggleBuf) new ToggleReader().read(toggleBuf, strings, db);
    // … etc.
  }
}
```

### NcdbWriter.ts

```typescript
export class NcdbWriter {
  async write(path: string, db: MemUCIS): Promise<void> {
    const zip     = new JSZip();
    const strings = new StringTable();

    // Pre-pass: intern all names into strings table
    internAllNames(db, strings);

    zip.file(MEMBER_MANIFEST, JSON.stringify({
      format: NCDB_FORMAT, version: NCDB_VERSION, generator: NCDB_GENERATOR
    }));
    zip.file(MEMBER_STRINGS,    new ScopeTreeWriter().prepassStrings(db, strings));
    zip.file(MEMBER_SCOPE_TREE, new ScopeTreeWriter().write(db, strings));
    zip.file(MEMBER_COUNTS,     new CountsWriter().write(db));
    zip.file(MEMBER_SOURCES,    new SourcesWriter().write(db));
    zip.file(MEMBER_HISTORY,    new HistoryWriter().write(db));

    const buf = await zip.generateAsync({ type: 'nodebuffer', compression: 'DEFLATE' });
    await fs.promises.writeFile(path, buf);
  }
}
```

---

## Visitor / Traversal Pattern

A simple synchronous visitor mirrors `python/covsight/core/visitors.py`:

```typescript
// src/visitors.ts
export interface UCISVisitor {
  enterScope?(scope: Scope, depth: number): boolean;   // false = skip children
  exitScope?(scope: Scope,  depth: number): void;
  visitCoverItem?(scope: Scope, name: string, data: CoverData, index: CoverIndex): void;
}

export function traverse(root: Scope, visitor: UCISVisitor, typeMask = ScopeTypeT.ALL): void {
  function visit(scope: Scope, depth: number): void {
    if (visitor.enterScope) {
      if (!visitor.enterScope(scope, depth)) return;
    }
    for (const { name, data, index } of scope.coverItems()) {
      visitor.visitCoverItem?.(scope, name, data, index);
    }
    for (const child of scope.scopes(typeMask)) {
      visit(child, depth + 1);
    }
    visitor.exitScope?.(scope, depth);
  }
  visit(root, 0);
}
```

---

## Format Registry

Optional extension point matching the Python `FormatRegistry`:

```typescript
// src/ext.ts
export interface DbFormat {
  name: string;
  read(path: string):                    Promise<MemUCIS>;
  write(path: string, db: MemUCIS):      Promise<void>;
}

export class FormatRegistry {
  private _formats = new Map<string, DbFormat>();

  register(fmt: DbFormat): void { this._formats.set(fmt.name, fmt); }
  getDbFormat(name: string): DbFormat {
    const f = this._formats.get(name);
    if (!f) throw new Error(`Unknown format: ${name}`);
    return f;
  }
  dbFormats(): Map<string, DbFormat> { return new Map(this._formats); }
}

// Built-in NCDB format registration
import { NcdbReader, NcdbWriter } from './ncdb/index.js';
export const ncdbFormat: DbFormat = {
  name: 'ncdb',
  read:  path     => new NcdbReader().read(path),
  write: (path, db) => new NcdbWriter().write(path, db),
};
```

---

## Implementation Phases

### Phase 1 — Scaffolding & API
- `package.json`, `tsconfig.json`
- All enums in `src/api/enums/`
- Data classes: `SourceInfo`, `FileHandle`, `CoverData`, `CoverIndex`, `TestData`
- Abstract classes: `Obj`, `Scope`, `UCIS`, `Covergroup`, `Coverpoint`, `Cross`, `HistoryNode`

### Phase 2 — In-Memory Backend
- `MemScope`, `MemCovergroup`, `MemCoverpoint`, `MemCross`
- `MemHistoryNode`
- `MemUCIS` (without read/write)
- Unit tests covering scope creation, iteration, cover item creation

### Phase 3 — NCDB Reader (read-only)
- `constants.ts`, `varint.ts`, `StringTable.ts`
- `ScopeTreeReader`, `CountsReader`, `HistoryReader`, `SourcesReader`
- `NcdbReader` (top-level)
- Test: read an existing `.cdb` produced by Python and verify scope tree

### Phase 4 — NCDB Writer
- `ScopeTreeWriter`, `CountsWriter`, `HistoryWriter`, `SourcesWriter`
- `NcdbWriter` (top-level)
- Roundtrip test: write → read → compare; cross-language test with Python

### Phase 5 — Optional Members
- `ToggleReader/Writer`, `FsmReader/Writer`, `CrossReader/Writer`
- `AttrsReader/Writer`

### Phase 6 — Visitor & Registry
- `traverse`, `UCISVisitor`
- `FormatRegistry`, built-in NCDB registration

---

## Key Design Decisions

| Decision | Rationale |
|---|---|
| `abstract class` not `interface` for UCIS model | Allows shared method bodies (e.g. `setTestData`, `getIntProperty` dispatch) matching Python |
| `bigint` for scope/cover type masks | Values exceed 2^32; JS `number` is 64-bit float (only 53-bit integer precision) |
| `jszip` for ZIP I/O | Works in Node.js and browser; no native build required |
| Async `read`/`write` on UCIS | ZIP I/O is inherently async; synchronous wrappers can be layered on top |
| Separate `ScopeTreeReader`/`CountsReader` | Mirrors Python's modular per-member approach; enables testing in isolation |
| `StringTable.intern()` pre-pass before writing | Required by NCDB format: string table is written before scope tree |
| `MemUCIS` as `NcdbReader` output type | NcdbUCIS lazy-loading can be added as a subclass in a future phase |
