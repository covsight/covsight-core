import { Cross } from '../api/Cross.js';
import type { Coverpoint } from '../api/Coverpoint.js';
import type { FileHandle } from '../api/FileHandle.js';
import type { SourceInfo } from '../api/SourceInfo.js';
import { ScopeTypeT } from '../api/enums/ScopeTypeT.js';
import { MemCoverpoint } from './MemCoverpoint.js';
import { MemScope } from './MemScope.js';

export class MemCross extends MemCoverpoint implements Cross {
  private crossedPoints: Coverpoint[];

  constructor(
    parent: MemScope,
    logicalName: string,
    fileHandle: FileHandle | null = null,
    sourceInfo: SourceInfo | null = null,
    crossedPoints: readonly Coverpoint[] = [],
  ) {
    super(parent, logicalName, fileHandle, sourceInfo, ScopeTypeT.CROSS);
    this.crossedPoints = [...crossedPoints];
  }

  getCrossedPoints(): readonly Coverpoint[] {
    return this.crossedPoints;
  }

  setCrossedPoints(points: readonly Coverpoint[]): void {
    this.crossedPoints = [...points];
  }
}

MemScope.createCrossScope = (
  parent: MemScope,
  logicalName: string,
  fileHandle: FileHandle | null,
  sourceInfo: SourceInfo | null,
  crossedPoints: readonly Coverpoint[],
): MemScope => new MemCross(parent, logicalName, fileHandle, sourceInfo, crossedPoints);
