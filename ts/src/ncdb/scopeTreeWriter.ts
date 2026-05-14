import type { Scope } from '../api/Scope.js';
import { ScopeTypeT } from '../api/enums/ScopeTypeT.js';
import type { MemUCIS } from '../mem/MemUCIS.js';
import {
  COVER_TYPE_DEFAULTS,
  DEFAULT_SCOPE_FLAGS,
  DEFAULT_SCOPE_GOAL,
  DEFAULT_SCOPE_WEIGHT,
  DEFAULT_SOURCE_TYPE,
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
import { writeVarint } from './varint.js';

export class ScopeTreeWriter {
  constructor(private readonly strings: StringTable) {}

  write(db: MemUCIS): { scopeTree: Uint8Array; counts: bigint[] } {
    const bytes: number[] = [];
    const counts: bigint[] = [];
    for (const scope of db.scopes()) {
      this.writeScope(scope, bytes, counts);
    }
    return { scopeTree: Uint8Array.from(bytes), counts };
  }

  private writeScope(scope: Scope, bytes: number[], counts: bigint[]): void {
    if (this.isTogglePair(scope)) {
      bytes.push(SCOPE_MARKER_TOGGLE_PAIR);
      bytes.push(...writeVarint(BigInt(this.strings.intern(scope.logicalName))));
      const items = Array.from(scope.coverItems());
      counts.push(items.find((item) => item.name === TOGGLE_BIN_0_TO_1)?.data.count ?? 0n);
      counts.push(items.find((item) => item.name === TOGGLE_BIN_1_TO_0)?.data.count ?? 0n);
      return;
    }
    bytes.push(SCOPE_MARKER_REGULAR);
    // Python-compatible format: scope_type BEFORE name_ref, type as single varint
    bytes.push(...writeVarint(scope.scopeType));
    bytes.push(...writeVarint(BigInt(this.strings.intern(scope.logicalName))));

    const items = Array.from(scope.coverItems());
    const firstItem = items[0];

    // Determine at_least override: compare first item's atLeast to type default
    let atLeastOverride: bigint | null = null;
    if (firstItem) {
      const defaults = COVER_TYPE_DEFAULTS.get(firstItem.coverType);
      const defaultAtLeast = defaults ? defaults[1] : 0n;
      if (firstItem.data.atLeast !== defaultAtLeast) {
        atLeastOverride = firstItem.data.atLeast;
      }
    }

    let presence = 0;
    if (scope.flags !== DEFAULT_SCOPE_FLAGS) presence |= PRESENCE_FLAGS;
    if (scope.fileHandle && scope.sourceInfo) presence |= PRESENCE_SOURCE;
    if (scope.weight !== DEFAULT_SCOPE_WEIGHT) presence |= PRESENCE_WEIGHT;
    if (atLeastOverride !== null) presence |= PRESENCE_AT_LEAST;
    if (scope.goal !== DEFAULT_SCOPE_GOAL) presence |= PRESENCE_GOAL;
    if (scope.sourceType !== DEFAULT_SOURCE_TYPE) presence |= PRESENCE_SOURCE_TYPE;
    bytes.push(...writeVarint(BigInt(presence)));

    if ((presence & PRESENCE_FLAGS) !== 0) {
      bytes.push(...writeVarint(BigInt(scope.flags)));
    }
    if ((presence & PRESENCE_SOURCE) !== 0 && scope.fileHandle && scope.sourceInfo) {
      bytes.push(...writeVarint(BigInt(scope.fileHandle.fileId)));
      bytes.push(...writeVarint(BigInt(scope.sourceInfo.line)));
      bytes.push(...writeVarint(BigInt(scope.sourceInfo.token)));
    }
    if ((presence & PRESENCE_WEIGHT) !== 0) {
      bytes.push(...writeVarint(BigInt(scope.weight)));
    }
    if ((presence & PRESENCE_AT_LEAST) !== 0 && atLeastOverride !== null) {
      bytes.push(...writeVarint(atLeastOverride));
    }
    if ((presence & PRESENCE_GOAL) !== 0) {
      bytes.push(...writeVarint(BigInt.asUintN(32, BigInt(scope.goal))));
    }
    if ((presence & PRESENCE_SOURCE_TYPE) !== 0) {
      bytes.push(...writeVarint(BigInt(scope.sourceType)));
    }

    const children = Array.from(scope.scopes());
    // Python format: children_count, then items_count, then [shared cover_type], then item names
    bytes.push(...writeVarint(BigInt(children.length)));
    bytes.push(...writeVarint(BigInt(items.length)));
    if (items.length > 0) {
      bytes.push(...writeVarint(BigInt(firstItem!.coverType)));
      for (const item of items) {
        bytes.push(...writeVarint(BigInt(this.strings.intern(item.name))));
        counts.push(item.data.count);
      }
    }
    for (const child of children) {
      this.writeScope(child, bytes, counts);
    }
  }

  private isTogglePair(scope: Scope): boolean {
    if (scope.scopeType !== ScopeTypeT.BRANCH || scope.numCoverChildren() !== 0 || scope.numCoverItems() !== 2) {
      return false;
    }
    const names = new Set(Array.from(scope.coverItems(), (item) => item.name));
    return names.has(TOGGLE_BIN_0_TO_1) && names.has(TOGGLE_BIN_1_TO_0);
  }
}
