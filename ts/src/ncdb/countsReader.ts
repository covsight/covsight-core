import { COUNTS_MODE_UINT32, COUNTS_MODE_VARINT } from './constants.js';
import { readVarint } from './varint.js';

export class CountsReader {
  *decode(buf: Uint8Array): IterableIterator<bigint> {
    if (buf.length === 0) {
      return;
    }
    const mode = buf[0];
    const countResult = readVarint(buf, 1);
    const count = Number(countResult.value);
    let offset = 1 + countResult.bytesRead;
    if (mode === COUNTS_MODE_VARINT) {
      for (let i = 0; i < count; i += 1) {
        const result = readVarint(buf, offset);
        offset += result.bytesRead;
        yield result.value;
      }
      return;
    }
    if (mode !== COUNTS_MODE_UINT32) {
      throw new Error(`Unknown counts mode ${mode}`);
    }
    const view = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
    for (let i = 0; i < count; i += 1) {
      yield BigInt(view.getUint32(offset, true));
      offset += 4;
    }
  }
}
