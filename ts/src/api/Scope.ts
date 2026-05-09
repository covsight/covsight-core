import type { Covergroup } from './Covergroup.js';
import type { CoverItem } from './CoverItem.js';
import type { Coverpoint } from './Coverpoint.js';
import type { Cross } from './Cross.js';
import type { FileHandle } from './FileHandle.js';
import { Obj } from './Obj.js';
import type { SourceInfo } from './SourceInfo.js';
import { SourceT } from './enums/SourceT.js';

export abstract class Scope extends Obj {
  public sourceInfo: SourceInfo | null;
  public flags = 0;
  public weight = 1;
  public goal = -1;
  public sourceType = SourceT.NONE;
  public designUnit: Scope | null = null;

  protected constructor(
    public readonly parent: Scope | null,
    public readonly scopeType: bigint,
    public readonly logicalName: string,
    public readonly fileHandle: FileHandle | null = null,
    sourceInfo: SourceInfo | null = null,
  ) {
    super();
    this.sourceInfo = sourceInfo;
  }

  abstract createCovergroupDef(name: string, fileHandle?: FileHandle | null, sourceInfo?: SourceInfo | null): Covergroup;
  abstract createCovergroupInstance(name: string, fileHandle?: FileHandle | null, sourceInfo?: SourceInfo | null): Covergroup;
  abstract createCoverpoint(name: string, fileHandle?: FileHandle | null, sourceInfo?: SourceInfo | null): Coverpoint;
  abstract createCross(name: string, fileHandle?: FileHandle | null, sourceInfo?: SourceInfo | null, crossedPoints?: readonly Coverpoint[]): Cross;
  abstract createBranch(name: string, fileHandle?: FileHandle | null, sourceInfo?: SourceInfo | null): Scope;
  abstract createExpr(name: string, fileHandle?: FileHandle | null, sourceInfo?: SourceInfo | null): Scope;
  abstract createCond(name: string, fileHandle?: FileHandle | null, sourceInfo?: SourceInfo | null): Scope;
  abstract createToggle(name: string, fileHandle?: FileHandle | null, sourceInfo?: SourceInfo | null): Scope;
  abstract createToggleBin(name: string, count?: bigint, atLeast?: bigint): CoverItem;
  abstract createFsm(name: string, fileHandle?: FileHandle | null, sourceInfo?: SourceInfo | null): Scope;
  abstract createFsmState(name: string, fileHandle?: FileHandle | null, sourceInfo?: SourceInfo | null): Scope;
  abstract createFsmTrans(name: string, fileHandle?: FileHandle | null, sourceInfo?: SourceInfo | null): Scope;
  abstract numCoverChildren(): number;
  abstract coverChild(i: number): Scope;
  abstract numCoverItems(): number;
  abstract coverItem(i: number): CoverItem;
  abstract scopes(mask?: bigint): Iterable<Scope>;
  abstract coverItems(): Iterable<CoverItem>;
}
