import {
  KIND_DESIGN_BUG,
  LINK_BLOCKED_BY,
  RES_NONE,
  SEV_MEDIUM,
  STATE_CLOSED,
  STATE_IN_PROGRESS,
  STATE_OPEN,
  STATE_RESOLVED,
  STATE_WONTFIX,
} from './constants.js';
import { StringTable } from './stringTable.js';

const MAGIC = 0x49535342;
const VERSION = 1;
const HEADER_SIZE = 25;
const ISSUE_RECORD_SIZE = 18;
const WAIVER_LINK_SIZE = 4;
const TESTPOINT_LINK_SIZE = 5;
const COVERAGE_LINK_SIZE = 7;

function packEnums(severity: number, kind: number, state: number, resolution: number): number {
  return ((severity & 0x7) << 13)
    | ((kind & 0x3) << 11)
    | ((state & 0x7) << 8)
    | ((resolution & 0x7) << 5);
}

function unpackEnums(value: number): readonly [number, number, number, number] {
  return [
    (value >> 13) & 0x7,
    (value >> 11) & 0x3,
    (value >> 8) & 0x7,
    (value >> 5) & 0x7,
  ];
}

export interface IssueSpecInit {
  id: string;
  ext?: string;
  severity?: number;
  kind?: number;
  state?: number;
  resolution?: number;
  createdAt?: number;
  updatedAt?: number;
  syncedAt?: number;
}

export class WaiverIssueLinkInfo {
  constructor(
    public readonly waiverId: string,
    public readonly issueId: string,
  ) {}
}

export class TestpointIssueLinkInfo {
  constructor(
    public readonly testpointName: string,
    public readonly issueId: string,
    public readonly linkType: number,
  ) {}
}

export class CoverageIssueLinkInfo {
  constructor(
    public readonly scopePath: string,
    public readonly binName: string,
    public readonly issueId: string,
    public readonly linkType: number,
  ) {}
}

export class IssueSpec {
  public readonly id: string;
  public readonly ext: string;
  public readonly severity: number;
  public readonly kind: number;
  public readonly state: number;
  public readonly resolution: number;
  public readonly createdAt: number;
  public readonly updatedAt: number;
  public readonly syncedAt: number;

  constructor(init: IssueSpecInit) {
    this.id = init.id;
    this.ext = init.ext ?? '';
    this.severity = init.severity ?? SEV_MEDIUM;
    this.kind = init.kind ?? KIND_DESIGN_BUG;
    this.state = init.state ?? STATE_OPEN;
    this.resolution = init.resolution ?? RES_NONE;
    this.createdAt = init.createdAt ?? 0;
    this.updatedAt = init.updatedAt ?? 0;
    this.syncedAt = init.syncedAt ?? 0;
  }
}

export class IssueHandle {
  private constructor(
    private readonly _owner: IssueSet,
    private readonly _idx: number,
  ) {}

  static _make(owner: IssueSet, idx: number): IssueHandle {
    return new IssueHandle(owner, idx);
  }

  static _getIdx(handle: IssueHandle): number {
    return handle._idx;
  }

  get id(): string {
    return this._owner._strings.get(this.required(this._owner._idIdxs, 'id'));
  }

  get ext(): string {
    return this._owner._strings.get(this.required(this._owner._extIdxs, 'ext'));
  }

  get severity(): number {
    return unpackEnums(this.required(this._owner._enums, 'enums'))[0];
  }

  get kind(): number {
    return unpackEnums(this.required(this._owner._enums, 'enums'))[1];
  }

  get state(): number {
    return unpackEnums(this.required(this._owner._enums, 'enums'))[2];
  }

  get resolution(): number {
    return unpackEnums(this.required(this._owner._enums, 'enums'))[3];
  }

  get createdAt(): number {
    return this.required(this._owner._created, 'createdAt');
  }

  get updatedAt(): number {
    return this.required(this._owner._updated, 'updatedAt');
  }

  get syncedAt(): number {
    return this.required(this._owner._synced, 'syncedAt');
  }

  isOpen(): boolean {
    return this.state === STATE_OPEN || this.state === STATE_IN_PROGRESS;
  }

  isClosed(): boolean {
    return this.state === STATE_RESOLVED || this.state === STATE_CLOSED || this.state === STATE_WONTFIX;
  }

  private required(values: readonly number[], name: string): number {
    const value = values[this._idx];
    if (value === undefined) {
      throw new RangeError(`No ${name} value at issue index ${this._idx}`);
    }
    return value;
  }
}

export class IssueSet {
  _idIdxs: number[] = [];
  _extIdxs: number[] = [];
  _enums: number[] = [];
  _created: number[] = [];
  _updated: number[] = [];
  _synced: number[] = [];
  _strings = new StringTable();
  _wl: Array<[number, number]> = [];
  _tl: Array<[number, number, number]> = [];
  _cl: Array<[number, number, number, number]> = [];

  private byId: Map<string, number> | null = null;
  private bySeverity: Map<number, number[]> | null = null;
  private openIdxs: number[] | null = null;
  private waiverToIdxs: Map<string, number[]> | null = null;
  private issueToWaiverLinks: Map<string, WaiverIssueLinkInfo[]> | null = null;
  private tpToIdxs: Map<string, number[]> | null = null;
  private issueToTpLinks: Map<string, TestpointIssueLinkInfo[]> | null = null;
  private coverageLinksCache: CoverageIssueLinkInfo[] | null = null;

  addIssue(spec: IssueSpec): IssueHandle {
    this.ensureById();
    if (this.byId!.has(spec.id)) {
      throw new Error(`Issue id ${JSON.stringify(spec.id)} already exists`);
    }

    const idx = this._idIdxs.length;
    this._idIdxs.push(this._strings.intern(spec.id));
    this._extIdxs.push(this._strings.intern(spec.ext));
    this._enums.push(packEnums(spec.severity, spec.kind, spec.state, spec.resolution));
    this._created.push(spec.createdAt);
    this._updated.push(spec.updatedAt);
    this._synced.push(spec.syncedAt);
    this.byId!.set(spec.id, idx);
    this.bySeverity = null;
    this.openIdxs = null;
    return IssueHandle._make(this, idx);
  }

  updateIssue(handle: IssueHandle, spec: IssueSpec): void {
    const idx = IssueHandle._getIdx(handle);
    this._extIdxs[idx] = this._strings.intern(spec.ext);
    this._enums[idx] = packEnums(spec.severity, spec.kind, spec.state, spec.resolution);
    this._created[idx] = spec.createdAt;
    this._updated[idx] = spec.updatedAt;
    this._synced[idx] = spec.syncedAt;
    this.invalidateIndices();
  }

  addWaiverLink(waiverId: string, issueId: string): void {
    this._wl.push([this._strings.intern(waiverId), this._strings.intern(issueId)]);
    this.waiverToIdxs = null;
    this.issueToWaiverLinks = null;
  }

  addTestpointLink(tpName: string, issueId: string, linkType = LINK_BLOCKED_BY): void {
    this._tl.push([this._strings.intern(tpName), this._strings.intern(issueId), linkType]);
    this.tpToIdxs = null;
    this.issueToTpLinks = null;
  }

  addCoverageLink(scopePath: string, binName: string, issueId: string, linkType = LINK_BLOCKED_BY): void {
    this._cl.push([
      this._strings.intern(scopePath),
      this._strings.intern(binName),
      this._strings.intern(issueId),
      linkType,
    ]);
    this.coverageLinksCache = null;
  }

  *issues(): Generator<IssueHandle> {
    for (let i = 0; i < this._idIdxs.length; i += 1) {
      yield IssueHandle._make(this, i);
    }
  }

  get(issueId: string): IssueHandle | null {
    this.ensureById();
    const idx = this.byId!.get(issueId);
    return idx === undefined ? null : IssueHandle._make(this, idx);
  }

  *openIssues(): Generator<IssueHandle> {
    this.ensureOpenIndex();
    for (const idx of this.openIdxs ?? []) {
      yield IssueHandle._make(this, idx);
    }
  }

  *issuesBySeverity(severity: number): Generator<IssueHandle> {
    this.ensureSeverityIndex();
    for (const idx of this.bySeverity?.get(severity) ?? []) {
      yield IssueHandle._make(this, idx);
    }
  }

  *issuesForWaiver(waiverId: string): Generator<IssueHandle> {
    this.ensureWaiverIndices();
    for (const idx of this.waiverToIdxs?.get(waiverId) ?? []) {
      yield IssueHandle._make(this, idx);
    }
  }

  *issuesForTestpoint(tpName: string): Generator<IssueHandle> {
    this.ensureTpIndices();
    for (const idx of this.tpToIdxs?.get(tpName) ?? []) {
      yield IssueHandle._make(this, idx);
    }
  }

  *waiversForIssue(handle: IssueHandle): Generator<WaiverIssueLinkInfo> {
    this.ensureWaiverIndices();
    yield* this.issueToWaiverLinks?.get(handle.id) ?? [];
  }

  *testpointsForIssue(handle: IssueHandle): Generator<TestpointIssueLinkInfo> {
    this.ensureTpIndices();
    yield* this.issueToTpLinks?.get(handle.id) ?? [];
  }

  *coverageLinks(): Generator<CoverageIssueLinkInfo> {
    this.ensureCoverageLinks();
    yield* this.coverageLinksCache ?? [];
  }

  serialize(): Uint8Array {
    const stringData = this._strings.encode();
    const totalSize = HEADER_SIZE
      + stringData.length
      + (this._idIdxs.length * ISSUE_RECORD_SIZE)
      + (this._wl.length * WAIVER_LINK_SIZE)
      + (this._tl.length * TESTPOINT_LINK_SIZE)
      + (this._cl.length * COVERAGE_LINK_SIZE);
    const data = new Uint8Array(totalSize);
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    let offset = 0;

    view.setUint32(offset, MAGIC, true); offset += 4;
    view.setUint8(offset, VERSION); offset += 1;
    view.setUint32(offset, 0, true); offset += 4;
    view.setUint32(offset, this._idIdxs.length, true); offset += 4;
    view.setUint32(offset, this._wl.length, true); offset += 4;
    view.setUint32(offset, this._tl.length, true); offset += 4;
    view.setUint32(offset, this._cl.length, true); offset += 4;

    data.set(stringData, offset);
    offset += stringData.length;

    for (let i = 0; i < this._idIdxs.length; i += 1) {
      view.setUint16(offset, this._idIdxs[i]!, true); offset += 2;
      view.setUint16(offset, this._extIdxs[i]!, true); offset += 2;
      view.setUint16(offset, this._enums[i]!, true); offset += 2;
      view.setUint32(offset, this._created[i]!, true); offset += 4;
      view.setUint32(offset, this._updated[i]!, true); offset += 4;
      view.setUint32(offset, this._synced[i]!, true); offset += 4;
    }

    for (const [waiverIdIdx, issueIdIdx] of this._wl) {
      view.setUint16(offset, waiverIdIdx, true); offset += 2;
      view.setUint16(offset, issueIdIdx, true); offset += 2;
    }

    for (const [tpNameIdx, issueIdIdx, linkType] of this._tl) {
      view.setUint16(offset, tpNameIdx, true); offset += 2;
      view.setUint16(offset, issueIdIdx, true); offset += 2;
      view.setUint8(offset, linkType); offset += 1;
    }

    for (const [scopeIdx, binIdx, issueIdIdx, linkType] of this._cl) {
      view.setUint16(offset, scopeIdx, true); offset += 2;
      view.setUint16(offset, binIdx, true); offset += 2;
      view.setUint16(offset, issueIdIdx, true); offset += 2;
      view.setUint8(offset, linkType); offset += 1;
    }

    return data;
  }

  static fromBytes(data: Uint8Array): IssueSet {
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    let offset = 0;

    const magic = view.getUint32(offset, true); offset += 4;
    const version = view.getUint8(offset); offset += 1;
    offset += 4; // header synced_at
    const numIssues = view.getUint32(offset, true); offset += 4;
    const numWaiverLinks = view.getUint32(offset, true); offset += 4;
    const numTestpointLinks = view.getUint32(offset, true); offset += 4;
    const numCoverageLinks = view.getUint32(offset, true); offset += 4;

    if (magic !== MAGIC) {
      throw new Error(`Invalid issues.bin magic: 0x${magic.toString(16).padStart(8, '0').toUpperCase()}`);
    }
    if (version !== VERSION) {
      throw new Error(`Unsupported issues.bin version: ${version}`);
    }

    const { table, bytesRead } = StringTable.readFrom(data, offset);
    offset += bytesRead;

    const issues = new IssueSet();
    issues._strings = table;
    issues._idIdxs = [];
    issues._extIdxs = [];
    issues._enums = [];
    issues._created = [];
    issues._updated = [];
    issues._synced = [];
    issues._wl = [];
    issues._tl = [];
    issues._cl = [];
    issues.invalidateIndices();

    for (let i = 0; i < numIssues; i += 1) {
      issues._idIdxs.push(view.getUint16(offset, true)); offset += 2;
      issues._extIdxs.push(view.getUint16(offset, true)); offset += 2;
      issues._enums.push(view.getUint16(offset, true)); offset += 2;
      issues._created.push(view.getUint32(offset, true)); offset += 4;
      issues._updated.push(view.getUint32(offset, true)); offset += 4;
      issues._synced.push(view.getUint32(offset, true)); offset += 4;
    }

    for (let i = 0; i < numWaiverLinks; i += 1) {
      const waiverIdIdx = view.getUint16(offset, true); offset += 2;
      const issueIdIdx = view.getUint16(offset, true); offset += 2;
      issues._wl.push([waiverIdIdx, issueIdIdx]);
    }

    for (let i = 0; i < numTestpointLinks; i += 1) {
      const tpNameIdx = view.getUint16(offset, true); offset += 2;
      const issueIdIdx = view.getUint16(offset, true); offset += 2;
      const linkType = view.getUint8(offset); offset += 1;
      issues._tl.push([tpNameIdx, issueIdIdx, linkType]);
    }

    for (let i = 0; i < numCoverageLinks; i += 1) {
      const scopeIdx = view.getUint16(offset, true); offset += 2;
      const binIdx = view.getUint16(offset, true); offset += 2;
      const issueIdIdx = view.getUint16(offset, true); offset += 2;
      const linkType = view.getUint8(offset); offset += 1;
      issues._cl.push([scopeIdx, binIdx, issueIdIdx, linkType]);
    }

    return issues;
  }

  get length(): number {
    return this._idIdxs.length;
  }

  private invalidateIndices(): void {
    this.byId = null;
    this.bySeverity = null;
    this.openIdxs = null;
    this.waiverToIdxs = null;
    this.issueToWaiverLinks = null;
    this.tpToIdxs = null;
    this.issueToTpLinks = null;
    this.coverageLinksCache = null;
  }

  private ensureById(): void {
    if (this.byId) {
      return;
    }
    this.byId = new Map<string, number>();
    for (let i = 0; i < this._idIdxs.length; i += 1) {
      this.byId.set(this._strings.get(this._idIdxs[i]!), i);
    }
  }

  private ensureSeverityIndex(): void {
    if (this.bySeverity) {
      return;
    }
    this.bySeverity = new Map<number, number[]>();
    for (let i = 0; i < this._enums.length; i += 1) {
      const severity = unpackEnums(this._enums[i]!)[0];
      const values = this.bySeverity.get(severity);
      if (values) {
        values.push(i);
      } else {
        this.bySeverity.set(severity, [i]);
      }
    }
  }

  private ensureOpenIndex(): void {
    if (this.openIdxs) {
      return;
    }
    this.openIdxs = [];
    for (let i = 0; i < this._enums.length; i += 1) {
      const state = unpackEnums(this._enums[i]!)[2];
      if (state === STATE_OPEN || state === STATE_IN_PROGRESS) {
        this.openIdxs.push(i);
      }
    }
  }

  private ensureWaiverIndices(): void {
    if (this.waiverToIdxs && this.issueToWaiverLinks) {
      return;
    }
    this.ensureById();
    this.waiverToIdxs = new Map<string, number[]>();
    this.issueToWaiverLinks = new Map<string, WaiverIssueLinkInfo[]>();
    for (const [waiverIdIdx, issueIdIdx] of this._wl) {
      const waiverId = this._strings.get(waiverIdIdx);
      const issueId = this._strings.get(issueIdIdx);
      const issueIdx = this.byId!.get(issueId);
      if (issueIdx !== undefined) {
        const values = this.waiverToIdxs.get(waiverId);
        if (values) {
          values.push(issueIdx);
        } else {
          this.waiverToIdxs.set(waiverId, [issueIdx]);
        }
      }
      const links = this.issueToWaiverLinks.get(issueId);
      const info = new WaiverIssueLinkInfo(waiverId, issueId);
      if (links) {
        links.push(info);
      } else {
        this.issueToWaiverLinks.set(issueId, [info]);
      }
    }
  }

  private ensureTpIndices(): void {
    if (this.tpToIdxs && this.issueToTpLinks) {
      return;
    }
    this.ensureById();
    this.tpToIdxs = new Map<string, number[]>();
    this.issueToTpLinks = new Map<string, TestpointIssueLinkInfo[]>();
    for (const [tpNameIdx, issueIdIdx, linkType] of this._tl) {
      const tpName = this._strings.get(tpNameIdx);
      const issueId = this._strings.get(issueIdIdx);
      const issueIdx = this.byId!.get(issueId);
      if (issueIdx !== undefined) {
        const values = this.tpToIdxs.get(tpName);
        if (values) {
          values.push(issueIdx);
        } else {
          this.tpToIdxs.set(tpName, [issueIdx]);
        }
      }
      const links = this.issueToTpLinks.get(issueId);
      const info = new TestpointIssueLinkInfo(tpName, issueId, linkType);
      if (links) {
        links.push(info);
      } else {
        this.issueToTpLinks.set(issueId, [info]);
      }
    }
  }

  private ensureCoverageLinks(): void {
    if (this.coverageLinksCache) {
      return;
    }
    this.coverageLinksCache = this._cl.map(([scopeIdx, binIdx, issueIdIdx, linkType]) => new CoverageIssueLinkInfo(
      this._strings.get(scopeIdx),
      this._strings.get(binIdx),
      this._strings.get(issueIdIdx),
      linkType,
    ));
  }
}
