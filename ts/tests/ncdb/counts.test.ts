import { COUNTS_MODE_UINT32, COUNTS_MODE_VARINT } from '../../src/ncdb/constants.js';
import { CountsReader } from '../../src/ncdb/countsReader.js';
import { CountsWriter } from '../../src/ncdb/countsWriter.js';

describe('counts', () => {
  test('encodes empty array', () => {
    expect(Array.from(new CountsWriter().encode([]))).toEqual([COUNTS_MODE_UINT32, 0x00]);
  });

  test('uses smaller encoding for low values', () => {
    const encoded = new CountsWriter().encode([0n, 0n, 0n]);
    expect(encoded[0]).toBe(COUNTS_MODE_VARINT);
    expect(Array.from(new CountsReader().decode(encoded))).toEqual([0n, 0n, 0n]);
  });

  test('uses uint32 mode when it is smaller', () => {
    const values = [0xFFFF_FFFFn, 0xFFFF_FFFFn];
    const encoded = new CountsWriter().encode(values);
    expect(encoded[0]).toBe(COUNTS_MODE_UINT32);
    expect(Array.from(new CountsReader().decode(encoded))).toEqual(values);
  });

  test('forces varint mode for large values', () => {
    const values = [0n, 0x1_0000_0000n];
    const encoded = new CountsWriter().encode(values);
    expect(encoded[0]).toBe(COUNTS_MODE_VARINT);
    expect(Array.from(new CountsReader().decode(encoded))).toEqual(values);
  });

  test('round-trips mixed counts', () => {
    const values = [0n, 0n, 0n, 1n, 0n, 0n, 5n];
    expect(Array.from(new CountsReader().decode(new CountsWriter().encode(values)))).toEqual(values);
  });
});
