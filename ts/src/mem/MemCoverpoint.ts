import { CoverData } from '../api/CoverData.js';
import { CoverIndex } from '../api/CoverIndex.js';
import type { CoverItem } from '../api/CoverItem.js';
import { Coverpoint } from '../api/Coverpoint.js';
import type { FileHandle } from '../api/FileHandle.js';
import type { SourceInfo } from '../api/SourceInfo.js';
import { ScopeTypeT, type ScopeTypeTValue } from '../api/enums/ScopeTypeT.js';
import { MemCoverItem } from './MemCoverItem.js';
import { MemScope } from './MemScope.js';

export class MemCoverpoint extends MemScope implements Coverpoint {
  constructor(
    parent: MemScope,
    logicalName: string,
    fileHandle: FileHandle | null = null,
    sourceInfo: SourceInfo | null = null,
    scopeType: ScopeTypeTValue = ScopeTypeT.COVERPOINT,
  ) {
    super(parent, scopeType, logicalName, fileHandle, sourceInfo);
  }

  createBin(name: string, binType: number, count: bigint, atLeast: bigint): CoverItem {
    return this.addCoverItem(new MemCoverItem(binType, name, new CoverData(count, atLeast), new CoverIndex(this.scopeIdx, this.numCoverItems())));
  }

  numBins(): number {
    return this.numCoverItems();
  }

  bin(i: number): CoverItem {
    return this.coverItem(i);
  }
}

MemScope.createCoverpointScope = (
  parent: MemScope,
  logicalName: string,
  fileHandle: FileHandle | null,
  sourceInfo: SourceInfo | null,
): MemScope => new MemCoverpoint(parent, logicalName, fileHandle, sourceInfo);
