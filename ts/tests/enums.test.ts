import { CoverTypeT, HistoryNodeKind, ScopeTypeT, SourceT, TestStatusT } from '../src/api/index.js';

describe('enum values', () => {
  test('scope type constants match expected values', () => {
    expect(ScopeTypeT.COVERGROUP).toBe(0x0000000000001000n);
    expect(ScopeTypeT.COVERPOINT).toBe(0x0000000000004000n);
    expect(ScopeTypeT.DU_MODULE).toBe(0x0000000001000000n);
  });

  test('cover type constants match expected values', () => {
    expect(CoverTypeT.CVGBIN).toBe(0x00000001);
    expect(CoverTypeT.TOGGLEBIN).toBe(0x00000200);
    expect(CoverTypeT.BLOCKBIN).toBe(0x01000000);
  });

  test('numeric enums match expected values', () => {
    expect(SourceT.NONE).toBe(10);
    expect(HistoryNodeKind.MERGE).toBe(2);
    expect(TestStatusT.FATAL).toBe(3);
  });
});
