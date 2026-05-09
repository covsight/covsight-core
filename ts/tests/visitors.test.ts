import { CoverTypeT } from '../src/api/enums/CoverTypeT.js';
import { ScopeTypeT } from '../src/api/enums/ScopeTypeT.js';
import { MemUCIS } from '../src/mem/MemUCIS.js';
import { traverse } from '../src/visitors.js';

describe('traverse', () => {
  test('visits all scopes in DFS order', () => {
    const db = new MemUCIS();
    const top = db.createScope('top');
    const cg = top.createCovergroupDef('cg');
    const cp = cg.createCoverpoint('cp');
    cp.createBin('a', CoverTypeT.CVGBIN, 1n, 1n);
    cp.createBin('b', CoverTypeT.CVGBIN, 0n, 1n);

    const visited: string[] = [];
    traverse(top, { enterScope: (scope) => void visited.push(scope.logicalName) });

    expect(visited).toEqual(['top', 'cg', 'cp']);
  });

  test('enterScope returning false skips subtree', () => {
    const db = new MemUCIS();
    const top = db.createScope('top');
    const cg = top.createCovergroupDef('cg');
    cg.createCoverpoint('cp');

    const visited: string[] = [];
    traverse(top, {
      enterScope: (scope) => {
        visited.push(scope.logicalName);
        if (scope.logicalName === 'cg') {
          return false;
        }
      },
    });

    expect(visited).toEqual(['top', 'cg']);
  });

  test('visitCoverItem is called for each cover item', () => {
    const db = new MemUCIS();
    const top = db.createScope('top');
    const cg = top.createCovergroupDef('cg');
    const cp = cg.createCoverpoint('cp');
    cp.createBin('a', CoverTypeT.CVGBIN, 1n, 1n);
    cp.createBin('b', CoverTypeT.CVGBIN, 2n, 1n);

    const items: string[] = [];
    traverse(top, {
      visitCoverItem: (_scope, item) => {
        items.push(item.name);
      },
    });

    expect(items).toEqual(['a', 'b']);
  });

  test('typeMask filtering limits which scopes are entered', () => {
    const db = new MemUCIS();
    const top = db.createScope('top');
    const cg = top.createCovergroupDef('cg');
    cg.createCoverpoint('cp');

    const visited: string[] = [];
    traverse(cg, { enterScope: (scope) => void visited.push(scope.logicalName) }, ScopeTypeT.COVERGROUP);

    expect(visited).toEqual(['cg']);
  });

  test('empty db produces no calls', () => {
    const db = new MemUCIS();
    const visited: string[] = [];

    traverse(db, { enterScope: (scope) => void visited.push(scope.logicalName) });

    expect(visited).toEqual([]);
  });
});
