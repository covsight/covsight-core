import { CoverTypeT } from '../src/api/index.js';
import { MemUCIS } from '../src/mem/index.js';

describe('MemUCIS', () => {
  test('creates nested scopes and bins', () => {
    const db = new MemUCIS();
    const top = db.createScope('top');
    const cg = top.createCovergroupDef('cg1');
    const cp = cg.createCoverpoint('cp1');
    cp.createBin('bin_a', CoverTypeT.CVGBIN, 3n, 1n);
    cp.createBin('bin_b', CoverTypeT.CVGBIN, 0n, 1n);

    expect(db.numScopes()).toBe(1);
    expect(top.numCoverChildren()).toBe(1);
    expect(cg.numCoverChildren()).toBe(1);
    expect(cp.numBins()).toBe(2);
    expect(cp.bin(0).data.isCovered()).toBe(true);
    expect(cp.bin(1).data.isCovered()).toBe(false);
    expect(Array.from(cp.coverItems()).map((item) => item.name)).toEqual(['bin_a', 'bin_b']);
  });
});
