import { readVarint, writeVarint } from './varint.js';

const encoder = new TextEncoder();
const decoder = new TextDecoder();

export class StringTable {
  private readonly strings: string[] = [];
  private readonly index = new Map<string, number>();

  constructor() {
    this.intern('');
  }

  intern(value: string): number {
    const normalized = value ?? '';
    const existing = this.index.get(normalized);
    if (existing !== undefined) {
      return existing;
    }
    const idx = this.strings.length;
    this.strings.push(normalized);
    this.index.set(normalized, idx);
    return idx;
  }

  get(idx: number): string {
    const value = this.strings[idx];
    if (value === undefined) {
      throw new RangeError(`No string at index ${idx}`);
    }
    return value;
  }

  encode(): Uint8Array {
    const chunks: number[] = [];
    append(chunks, writeVarint(BigInt(this.strings.length)));
    for (const value of this.strings) {
      const data = encoder.encode(value);
      append(chunks, writeVarint(BigInt(data.length)));
      append(chunks, data);
    }
    return Uint8Array.from(chunks);
  }

  static read(buf: Uint8Array): StringTable {
    const table = new StringTable();
    table.strings.length = 0;
    table.index.clear();
    let offset = 0;
    const countResult = readVarint(buf, offset);
    const count = Number(countResult.value);
    offset += countResult.bytesRead;
    for (let i = 0; i < count; i += 1) {
      const lenResult = readVarint(buf, offset);
      const length = Number(lenResult.value);
      offset += lenResult.bytesRead;
      const end = offset + length;
      table.intern(decoder.decode(buf.slice(offset, end)));
      offset = end;
    }
    return table;
  }
}

function append(target: number[], data: Uint8Array): void {
  target.push(...data);
}
