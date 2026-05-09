import { ScopeTypeT } from './api/enums/ScopeTypeT.js';
import type { Scope } from './api/Scope.js';
import type { CoverItem } from './api/CoverItem.js';

export interface UCISVisitor {
  /** Return false to skip children of this scope. */
  enterScope?(scope: Scope, depth: number): boolean | void;
  exitScope?(scope: Scope, depth: number): void;
  visitCoverItem?(scope: Scope, item: CoverItem): void;
}

/**
 * Traverse a scope tree depth-first, calling visitor hooks.
 * @param root - root scope to start traversal
 * @param visitor - visitor object with optional hooks
 * @param typeMask - only enter scopes whose type bits are in this mask (default: ALL)
 */
export function traverse(
  root: Scope,
  visitor: UCISVisitor,
  typeMask: bigint = ScopeTypeT.ALL,
): void {
  function visit(scope: Scope, depth: number): void {
    if ((scope.scopeType & typeMask) === 0n) return;
    const descend = visitor.enterScope?.(scope, depth);
    if (descend === false) return;
    for (const item of scope.coverItems()) {
      visitor.visitCoverItem?.(scope, item);
    }
    for (const child of scope.scopes(typeMask)) {
      visit(child, depth + 1);
    }
    visitor.exitScope?.(scope, depth);
  }
  visit(root, 0);
}
