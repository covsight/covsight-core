import { isDUScope } from '../api/enums/ScopeTypeT.js';
import type { MemUCIS } from '../mem/MemUCIS.js';
import { dfsScopes } from './dfsScopes.js';

const DESIGN_UNITS_VERSION = 1;

export class DesignUnitsWriter {
  write(db: MemUCIS): string {
    const units = dfsScopes(db)
      .map((scope, idx) => ({ scope, idx }))
      .filter(({ scope }) => isDUScope(scope.scopeType))
      .map(({ scope, idx }) => ({ name: scope.logicalName, idx, type: Number(scope.scopeType) }));

    return units.length === 0 ? '' : JSON.stringify({ version: DESIGN_UNITS_VERSION, units });
  }
}
