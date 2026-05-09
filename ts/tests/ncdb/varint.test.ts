import { readVarint, writeVarint } from '../../src/ncdb/varint.js';

describe('varint', () => {
  test('encodes known values', () => {
    expect(Array.from(writeVarint(0n))).toEqual([0x00]);
    expect(Array.from(writeVarint(127n))).toEqual([0x7F]);
    expect(Array.from(writeVarint(128n))).toEqual([0x80, 0x01]);
    expect(Array.from(writeVarint(300n))).toEqual([0xAC, 0x02]);
  });

  test('decodes round-trip values', () => {
    for (const value of [0n, 1n, 127n, 128n, 255n, 300n, 16_384n, 9_876_543n]) {
      const encoded = writeVarint(value);
      expect(readVarint(encoded, 0)).toEqual({ value, bytesRead: encoded.length });
    }
  });

  test('truncated buffer throws', () => {
    expect(() => readVarint(Uint8Array.from([0x80]), 0)).toThrow('Buffer too short for varint');
  });
});
