import { ScopeTypeT } from '../api/enums/ScopeTypeT.js';
import { MemCross } from '../mem/MemCross.js';
import type { MemUCIS } from '../mem/MemUCIS.js';
import { dfsScopes } from './dfsScopes.js';

const CROSS_VERSION = 1;

export class CrossWriter {
  write(db: MemUCIS): string {
    const entries = dfsScopes(db)
      .map((scope, idx) => ({ scope, idx }))
      .filter(({ scope }) => scope.scopeType === ScopeTypeT.CROSS)
      .map(({ scope, idx }) => ({
        idx,
        crossed: scope instanceof MemCross ? scope.getCrossedPoints().map((coverpoint) => coverpoint.logicalName) : [],
      }))
      .filter((entry) => entry.crossed.length > 0);

    return entries.length === 0 ? '' : JSON.stringify({ version: CROSS_VERSION, entries });
  }
}
