import { FormatRegistry, ncdbFormat } from '../src/ext.js';

describe('FormatRegistry', () => {
  test('register and getDbFormat round-trip', () => {
    const registry = new FormatRegistry();
    registry.register(ncdbFormat);

    expect(registry.getDbFormat('ncdb')).toBe(ncdbFormat);
  });

  test('getDbFormat throws for unknown format', () => {
    const registry = new FormatRegistry();

    expect(() => registry.getDbFormat('unknown')).toThrow("Unknown format: 'unknown'. Registered: ");
  });

  test('dbFormats returns read-only map', () => {
    const registry = new FormatRegistry();
    registry.register(ncdbFormat);

    expect(() => (registry.dbFormats() as Map<string, typeof ncdbFormat>).set('other', ncdbFormat)).toThrow('Format registry is read-only');
  });

  test("ncdbFormat.name is 'ncdb'", () => {
    expect(ncdbFormat.name).toBe('ncdb');
  });
});
