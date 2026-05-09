import type { Coverpoint } from '../api/Coverpoint.js';
import { ScopeTypeT } from '../api/enums/ScopeTypeT.js';
import type { Scope } from '../api/Scope.js';
import { MemCross } from '../mem/MemCross.js';
import type { MemUCIS } from '../mem/MemUCIS.js';
import { dfsScopes } from './dfsScopes.js';

const CROSS_VERSION = 1;

interface CrossEntry {
  idx: number;
  crossed?: string[];
}

export class CrossReader {
  read(data: string, db: MemUCIS): void {
    if (data.length === 0) {
      return;
    }
    const payload = JSON.parse(data) as { version?: number; entries?: CrossEntry[] };
    if (payload.version !== CROSS_VERSION) {
      throw new Error(`Unsupported cross.bin version: ${payload.version}`);
    }
    const scopes = dfsScopes(db);
    for (const entry of payload.entries ?? []) {
      const crossScope = scopes[entry.idx];
      if (!(crossScope instanceof MemCross) || crossScope.scopeType !== ScopeTypeT.CROSS) {
        continue;
      }
      const parent = crossScope.parent;
      if (!parent) {
        continue;
      }
      const siblingMap = new Map<string, Scope>();
      for (const sibling of parent.scopes(ScopeTypeT.ALL)) {
        siblingMap.set(sibling.logicalName, sibling);
      }
      const crossedPoints = (entry.crossed ?? [])
        .map((name) => siblingMap.get(name))
        .filter((scope): scope is Coverpoint => scope !== undefined && scope.scopeType === ScopeTypeT.COVERPOINT);
      crossScope.setCrossedPoints(crossedPoints);
    }
  }
}
