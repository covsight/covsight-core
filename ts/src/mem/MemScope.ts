import { CoverData } from '../api/CoverData.js';
import type { Covergroup } from '../api/Covergroup.js';
import type { CoverItem } from '../api/CoverItem.js';
import { CoverIndex } from '../api/CoverIndex.js';
import type { Coverpoint } from '../api/Coverpoint.js';
import type { Cross } from '../api/Cross.js';
import type { FileHandle } from '../api/FileHandle.js';
import { Scope } from '../api/Scope.js';
import type { SourceInfo } from '../api/SourceInfo.js';
import { CoverTypeT } from '../api/enums/CoverTypeT.js';
import { FlagsT } from '../api/enums/FlagsT.js';
import { IntProperty } from '../api/enums/IntProperty.js';
import { ScopeTypeT, type ScopeTypeTValue } from '../api/enums/ScopeTypeT.js';
import { MemCoverItem } from './MemCoverItem.js';
import { MemObj } from './MemObj.js';

export class MemScope extends Scope {
  static createCovergroupScope = (
    parent: MemScope,
    logicalName: string,
    scopeType: ScopeTypeTValue,
    fileHandle: FileHandle | null,
    sourceInfo: SourceInfo | null,
  ): MemScope => new MemScope(parent, scopeType, logicalName, fileHandle, sourceInfo);

  static createCoverpointScope = (
    parent: MemScope,
    logicalName: string,
    fileHandle: FileHandle | null,
    sourceInfo: SourceInfo | null,
  ): MemScope => new MemScope(parent, ScopeTypeT.COVERPOINT, logicalName, fileHandle, sourceInfo);

  static createCrossScope = (
    parent: MemScope,
    logicalName: string,
    fileHandle: FileHandle | null,
    sourceInfo: SourceInfo | null,
    _crossedPoints: readonly Coverpoint[],
  ): MemScope => new MemScope(parent, ScopeTypeT.CROSS, logicalName, fileHandle, sourceInfo);

  private static nextScopeIdx = 0;
  protected readonly propertyStore = new MemObj();
  protected readonly children: MemScope[] = [];
  protected readonly items: MemCoverItem[] = [];
  public readonly scopeIdx: number;

  constructor(
    parent: Scope | null,
    scopeType: ScopeTypeTValue,
    logicalName: string,
    fileHandle: FileHandle | null = null,
    sourceInfo: SourceInfo | null = null,
  ) {
    super(parent, scopeType, logicalName, fileHandle, sourceInfo);
    this.scopeIdx = MemScope.nextScopeIdx++;
  }

  addChildScope<T extends MemScope>(scope: T): T {
    this.children.push(scope);
    return scope;
  }

  addCoverItem(item: MemCoverItem): MemCoverItem {
    this.items.push(item);
    return item;
  }

  protected createChild(scopeType: ScopeTypeTValue, name: string, fileHandle: FileHandle | null = null, sourceInfo: SourceInfo | null = null): MemScope {
    return this.addChildScope(new MemScope(this, scopeType, name, fileHandle, sourceInfo));
  }

  createCovergroupDef(name: string, fileHandle: FileHandle | null = null, sourceInfo: SourceInfo | null = null): Covergroup {
    return this.addChildScope(MemScope.createCovergroupScope(this, name, ScopeTypeT.COVERGROUP, fileHandle, sourceInfo)) as unknown as Covergroup;
  }

  createCovergroupInstance(name: string, fileHandle: FileHandle | null = null, sourceInfo: SourceInfo | null = null): Covergroup {
    return this.addChildScope(MemScope.createCovergroupScope(this, name, ScopeTypeT.COVERINSTANCE, fileHandle, sourceInfo)) as unknown as Covergroup;
  }

  createCoverpoint(name: string, fileHandle: FileHandle | null = null, sourceInfo: SourceInfo | null = null): Coverpoint {
    return this.addChildScope(MemScope.createCoverpointScope(this, name, fileHandle, sourceInfo)) as unknown as Coverpoint;
  }

  createCross(name: string, fileHandle: FileHandle | null = null, sourceInfo: SourceInfo | null = null, crossedPoints: readonly Coverpoint[] = []): Cross {
    return this.addChildScope(MemScope.createCrossScope(this, name, fileHandle, sourceInfo, crossedPoints)) as unknown as Cross;
  }

  createBranch(name: string, fileHandle: FileHandle | null = null, sourceInfo: SourceInfo | null = null): Scope {
    return this.createChild(ScopeTypeT.BRANCH, name, fileHandle, sourceInfo);
  }

  createExpr(name: string, fileHandle: FileHandle | null = null, sourceInfo: SourceInfo | null = null): Scope {
    return this.createChild(ScopeTypeT.EXPR, name, fileHandle, sourceInfo);
  }

  createCond(name: string, fileHandle: FileHandle | null = null, sourceInfo: SourceInfo | null = null): Scope {
    return this.createChild(ScopeTypeT.COND, name, fileHandle, sourceInfo);
  }

  createToggle(name: string, fileHandle: FileHandle | null = null, sourceInfo: SourceInfo | null = null): Scope {
    return this.createChild(ScopeTypeT.TOGGLE, name, fileHandle, sourceInfo);
  }

  createToggleBin(name: string, count: bigint = 0n, atLeast: bigint = 0n): CoverItem {
    return this.addCoverItem(
      new MemCoverItem(CoverTypeT.TOGGLEBIN, name, new CoverData(count, atLeast), new CoverIndex(this.scopeIdx, this.items.length)),
    );
  }

  createBin(name: string, binType: number, count: bigint, atLeast: bigint): CoverItem {
    return this.addCoverItem(
      new MemCoverItem(binType, name, new CoverData(count, atLeast), new CoverIndex(this.scopeIdx, this.items.length)),
    );
  }

  numBins(): number {
    return this.numCoverItems();
  }

  bin(i: number): CoverItem {
    return this.coverItem(i);
  }

  createFsm(name: string, fileHandle: FileHandle | null = null, sourceInfo: SourceInfo | null = null): Scope {
    return this.createChild(ScopeTypeT.FSM, name, fileHandle, sourceInfo);
  }

  createFsmState(name: string, fileHandle: FileHandle | null = null, sourceInfo: SourceInfo | null = null): Scope {
    return this.createChild(ScopeTypeT.FSM_STATES, name, fileHandle, sourceInfo);
  }

  createFsmTrans(name: string, fileHandle: FileHandle | null = null, sourceInfo: SourceInfo | null = null): Scope {
    return this.createChild(ScopeTypeT.FSM_TRANS, name, fileHandle, sourceInfo);
  }

  numCoverChildren(): number {
    return this.children.length;
  }

  coverChild(i: number): Scope {
    const child = this.children[i];
    if (!child) {
      throw new RangeError(`No child scope at index ${i}`);
    }
    return child;
  }

  numCoverItems(): number {
    return this.items.length;
  }

  coverItem(i: number): CoverItem {
    const item = this.items[i];
    if (!item) {
      throw new RangeError(`No cover item at index ${i}`);
    }
    return item;
  }

  *scopes(mask: bigint = ScopeTypeT.ALL): Iterable<Scope> {
    for (const child of this.children) {
      if ((child.scopeType & mask) !== 0n) {
        yield child;
      }
    }
  }

  *coverItems(): Iterable<CoverItem> {
    yield* this.items;
  }

  getIntProperty(prop: IntProperty): number {
    switch (prop) {
      case IntProperty.SCOPE_GOAL:
        return this.goal;
      case IntProperty.SCOPE_WEIGHT:
        return this.weight;
      case IntProperty.SCOPE_SOURCE_TYPE:
        return this.sourceType;
      case IntProperty.SCOPE_EXCLUDE:
        return (this.flags & FlagsT.SCOPE_EXCLUDED) !== 0 ? 1 : 0;
      default:
        return this.propertyStore.getIntProperty(prop);
    }
  }

  setIntProperty(prop: IntProperty, val: number): void {
    switch (prop) {
      case IntProperty.SCOPE_GOAL:
        this.goal = val;
        break;
      case IntProperty.SCOPE_WEIGHT:
        this.weight = val;
        break;
      case IntProperty.SCOPE_SOURCE_TYPE:
        this.sourceType = val;
        break;
      case IntProperty.SCOPE_EXCLUDE:
        this.flags = val ? (this.flags | FlagsT.SCOPE_EXCLUDED) : (this.flags & ~FlagsT.SCOPE_EXCLUDED);
        break;
      default:
        this.propertyStore.setIntProperty(prop, val);
        break;
    }
  }
}
