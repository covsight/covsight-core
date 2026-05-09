import { StringTable } from '../../src/ncdb/stringTable.js';

describe('StringTable', () => {
  test("intern('') is always 0", () => {
    const table = new StringTable();
    expect(table.intern('')).toBe(0);
  });

  test("intern('foo') deduplicates", () => {
    const table = new StringTable();
    expect(table.intern('foo')).toBe(1);
    expect(table.intern('foo')).toBe(1);
    expect(table.get(1)).toBe('foo');
  });

  test('encode/decode round-trip preserves strings', () => {
    const table = new StringTable();
    const values = ['', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i'];
    values.forEach((value) => table.intern(value));

    const decoded = StringTable.read(table.encode());
    values.forEach((value, index) => {
      expect(decoded.get(index)).toBe(value);
    });
  });

  test('preserves UTF-8 strings', () => {
    const table = new StringTable();
    table.intern('café');
    table.intern('東京');

    const decoded = StringTable.read(table.encode());
    expect(decoded.get(1)).toBe('café');
    expect(decoded.get(2)).toBe('東京');
  });
});
