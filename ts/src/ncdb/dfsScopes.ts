import type { Scope } from '../api/Scope.js';
import { ScopeTypeT } from '../api/enums/ScopeTypeT.js';
import { TOGGLE_BIN_0_TO_1, TOGGLE_BIN_1_TO_0 } from './constants.js';

function isTogglePair(scope: Scope): boolean {
  if (scope.scopeType !== ScopeTypeT.BRANCH) {
    return false;
  }
  const coverItems = Array.from(scope.coverItems());
  if (coverItems.length !== 2 || Array.from(scope.scopes(ScopeTypeT.ALL)).length !== 0) {
    return false;
  }
  const names = new Set(coverItems.map((item) => item.name));
  return names.has(TOGGLE_BIN_0_TO_1) && names.has(TOGGLE_BIN_1_TO_0);
}

export function dfsScopes(root: Scope): Scope[] {
  const result: Scope[] = [];
  // Iterative DFS using an explicit stack to avoid call stack overflow.
  const stack: Scope[] = [];

  if ((root.scopeType & ScopeTypeT.ALL) !== 0n) {
    stack.push(root);
  } else {
    // Root is the DB container — push top-level children in reverse order.
    const top = Array.from(root.scopes(ScopeTypeT.ALL));
    for (let i = top.length - 1; i >= 0; i--) stack.push(top[i]!);
  }

  while (stack.length > 0) {
    const scope = stack.pop()!;
    result.push(scope);
    if (!isTogglePair(scope)) {
      const children = Array.from(scope.scopes(ScopeTypeT.ALL));
      for (let i = children.length - 1; i >= 0; i--) stack.push(children[i]!);
    }
  }

  return result;
}
