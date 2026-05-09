import { CoverData } from '../api/CoverData.js';
import { CoverIndex } from '../api/CoverIndex.js';
import { SourceInfo } from '../api/SourceInfo.js';
import type { FileHandle } from '../api/FileHandle.js';
import { ScopeTypeT, isDUScope, type ScopeTypeTValue } from '../api/enums/ScopeTypeT.js';
import { MemCovergroup } from '../mem/MemCovergroup.js';
import { MemCoverItem } from '../mem/MemCoverItem.js';
import { MemCoverpoint } from '../mem/MemCoverpoint.js';
import { MemCross } from '../mem/MemCross.js';
import { MemScope } from '../mem/MemScope.js';
import {
  COVER_TYPE_DEFAULTS,
  PRESENCE_AT_LEAST,
  PRESENCE_FLAGS,
  PRESENCE_GOAL,
  PRESENCE_SOURCE,
  PRESENCE_SOURCE_TYPE,
  PRESENCE_WEIGHT,
  SCOPE_MARKER_REGULAR,
  SCOPE_MARKER_TOGGLE_PAIR,
  TOGGLE_BIN_0_TO_1,
  TOGGLE_BIN_1_TO_0,
} from './constants.js';
import { StringTable } from './stringTable.js';
import { readVarint } from './varint.js';

export class ScopeTreeReader {
  constructor(
    private readonly strings: StringTable,
    private readonly fileHandles: FileHandle[],
  ) {}

  read(buf: Uint8Array, parent: MemScope, countsIter: Iterator<bigint>): void {
    let offset = 0;
    while (offset < buf.length) {
      offset = this.readScope(buf, offset, parent, countsIter);
    }
    this.fixupDuLinks(parent);
  }

  private readScope(buf: Uint8Array, offset: number, parent: MemScope, countsIter: Iterator<bigint>): number {
    const marker = buf[offset];
    if (marker === undefined) {
      throw new Error('Unexpected end of scope tree');
    }
    offset += 1;
    if (marker === SCOPE_MARKER_TOGGLE_PAIR) {
      const nameResult = readVarint(buf, offset);
      offset += nameResult.bytesRead;
      const scope = parent.addChildScope(new MemScope(parent, ScopeTypeT.BRANCH, this.strings.get(Number(nameResult.value))));
      scope.createToggleBin(TOGGLE_BIN_0_TO_1, readCount(countsIter), 0n);
      scope.createToggleBin(TOGGLE_BIN_1_TO_0, readCount(countsIter), 0n);
      return offset;
    }
    if (marker !== SCOPE_MARKER_REGULAR) {
      throw new Error(`Unknown scope marker ${marker}`);
    }
    // Python-compatible format: scope_type BEFORE name_ref, type as single varint
    const typeResult = readVarint(buf, offset);
    offset += typeResult.bytesRead;
    const scopeType = typeResult.value as ScopeTypeTValue;

    const nameResult = readVarint(buf, offset);
    offset += nameResult.bytesRead;

    const presenceResult = readVarint(buf, offset);
    const presence = Number(presenceResult.value);
    offset += presenceResult.bytesRead;

    let flags = 0;
    let sourceInfo: SourceInfo | null = null;
    let fileHandle: FileHandle | null = null;
    let weight = 1;
    let atLeastOverride: bigint | null = null;
    let goal = -1;
    let sourceType = 10;
    if ((presence & PRESENCE_FLAGS) !== 0) {
      const result = readVarint(buf, offset);
      flags = Number(result.value);
      offset += result.bytesRead;
    }
    if ((presence & PRESENCE_SOURCE) !== 0) {
      const fileIdResult = readVarint(buf, offset);
      offset += fileIdResult.bytesRead;
      const lineResult = readVarint(buf, offset);
      offset += lineResult.bytesRead;
      const tokenResult = readVarint(buf, offset);
      offset += tokenResult.bytesRead;
      const fileId = Number(fileIdResult.value);
      fileHandle = this.fileHandles[fileId] ?? null;
      sourceInfo = new SourceInfo(fileId, Number(lineResult.value), Number(tokenResult.value));
    }
    if ((presence & PRESENCE_WEIGHT) !== 0) {
      const result = readVarint(buf, offset);
      weight = Number(result.value);
      offset += result.bytesRead;
    }
    if ((presence & PRESENCE_AT_LEAST) !== 0) {
      const result = readVarint(buf, offset);
      atLeastOverride = result.value;
      offset += result.bytesRead;
    }
    if ((presence & PRESENCE_GOAL) !== 0) {
      const result = readVarint(buf, offset);
      goal = Number(BigInt.asIntN(32, result.value));
      offset += result.bytesRead;
    }
    if ((presence & PRESENCE_SOURCE_TYPE) !== 0) {
      const result = readVarint(buf, offset);
      sourceType = Number(result.value);
      offset += result.bytesRead;
    }

    // Python format: children_count, then items_count, then [shared cover_type], then item names
    const childCountResult = readVarint(buf, offset);
    const childCount = Number(childCountResult.value);
    offset += childCountResult.bytesRead;

    const itemCountResult = readVarint(buf, offset);
    const itemCount = Number(itemCountResult.value);
    offset += itemCountResult.bytesRead;

    const scope = createScopeForType(parent, scopeType, this.strings.get(Number(nameResult.value)), fileHandle, sourceInfo);
    scope.flags = flags;
    scope.weight = weight;
    scope.goal = goal;
    scope.sourceType = sourceType;
    if (scopeType === ScopeTypeT.INSTANCE) {
      scope.designUnit = new MemScope(null, ScopeTypeT.DU_MODULE, scope.logicalName);
    }

    if (itemCount > 0) {
      const coverTypeResult = readVarint(buf, offset);
      const coverType = Number(coverTypeResult.value);
      offset += coverTypeResult.bytesRead;

      const defaults = COVER_TYPE_DEFAULTS.get(coverType);
      const defaultAtLeast = defaults ? defaults[1] : 0n;
      const atLeast = atLeastOverride !== null ? atLeastOverride : defaultAtLeast;

      for (let i = 0; i < itemCount; i += 1) {
        const itemNameResult = readVarint(buf, offset);
        offset += itemNameResult.bytesRead;
        scope.addCoverItem(
          new MemCoverItem(
            coverType,
            this.strings.get(Number(itemNameResult.value)),
            new CoverData(readCount(countsIter), atLeast),
            new CoverIndex(scope.scopeIdx, scope.numCoverItems()),
          ),
        );
      }
    }

    for (let i = 0; i < childCount; i += 1) {
      offset = this.readScope(buf, offset, scope, countsIter);
    }
    return offset;
  }

  private fixupDuLinks(root: MemScope): void {
    const visit = (scope: MemScope): void => {
      const children = Array.from(scope.scopes()) as MemScope[];
      const duByName = new Map<string, MemScope>();
      for (const child of children) {
        if (isDUScope(child.scopeType)) {
          duByName.set(child.logicalName, child);
        }
      }
      for (const child of children) {
        if (child.scopeType === ScopeTypeT.INSTANCE) {
          const du = duByName.get(child.logicalName);
          if (du) {
            child.designUnit = du;
          }
        }
        visit(child);
      }
    };
    visit(root);
  }
}

function createScopeForType(parent: MemScope, scopeType: ScopeTypeTValue, logicalName: string, fileHandle: FileHandle | null, sourceInfo: SourceInfo | null): MemScope {
  switch (scopeType) {
    case ScopeTypeT.COVERGROUP:
    case ScopeTypeT.COVERINSTANCE:
      return parent.addChildScope(new MemCovergroup(parent, logicalName, scopeType, fileHandle, sourceInfo));
    case ScopeTypeT.COVERPOINT:
      return parent.addChildScope(new MemCoverpoint(parent, logicalName, fileHandle, sourceInfo));
    case ScopeTypeT.CROSS:
      return parent.addChildScope(new MemCross(parent, logicalName, fileHandle, sourceInfo));
    default:
      return parent.addChildScope(new MemScope(parent, scopeType, logicalName, fileHandle, sourceInfo));
  }
}

function readCount(countsIter: Iterator<bigint>): bigint {
  const next = countsIter.next();
  return next.done === true ? 0n : next.value;
}
