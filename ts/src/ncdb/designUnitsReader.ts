import type { Scope } from '../api/Scope.js';
import { isDUScope } from '../api/enums/ScopeTypeT.js';
import type { MemUCIS } from '../mem/MemUCIS.js';
import { dfsScopes } from './dfsScopes.js';

const DESIGN_UNITS_VERSION = 1;

interface DesignUnitEntry {
  name: string;
  idx: number;
}

export class DesignUnitsReader {
  buildIndex(data: string, db: MemUCIS): Map<string, Scope> {
    const scopes = dfsScopes(db);
    if (data.length !== 0) {
      const payload = JSON.parse(data) as { version?: number; units?: DesignUnitEntry[] };
      if (payload.version !== DESIGN_UNITS_VERSION) {
        throw new Error(`Unsupported design_units.json version: ${payload.version}`);
      }
      const index = new Map<string, Scope>();
      for (const unit of payload.units ?? []) {
        const scope = scopes[unit.idx];
        if (scope) {
          index.set(unit.name, scope);
        }
      }
      return index;
    }

    const index = new Map<string, Scope>();
    for (const scope of scopes) {
      if (isDUScope(scope.scopeType)) {
        index.set(scope.logicalName, scope);
      }
    }
    return index;
  }
}
