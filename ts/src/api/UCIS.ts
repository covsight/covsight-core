import type { Covergroup } from './Covergroup.js';
import type { CoverItem } from './CoverItem.js';
import type { Coverpoint } from './Coverpoint.js';
import type { Cross } from './Cross.js';
import type { FileHandle } from './FileHandle.js';
import type { HistoryNode } from './HistoryNode.js';
import { Scope } from './Scope.js';
import type { SourceInfo } from './SourceInfo.js';
import { ScopeTypeT } from './enums/ScopeTypeT.js';
import type { IntProperty } from './enums/IntProperty.js';
import { HistoryNodeKind } from './enums/HistoryNodeKind.js';

export abstract class UCIS extends Scope {
  protected constructor() {
    super(null, ScopeTypeT.RESERVEDSCOPE, '', null, null);
  }

  abstract createHistoryNode(kind: HistoryNodeKind, name: string, parent?: HistoryNode | null): HistoryNode;
  abstract numHistoryNodes(): number;
  abstract historyNode(i: number): HistoryNode;
  abstract createScope(name: string, du?: Scope): Scope;
  abstract numScopes(): number;
  abstract scope(i: number): Scope;
  abstract getFileHandle(path: string): FileHandle;
  abstract write(path: string): Promise<void>;
  abstract read(path: string): Promise<void>;
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
  abstract getIntProperty(prop: IntProperty): number;
}
