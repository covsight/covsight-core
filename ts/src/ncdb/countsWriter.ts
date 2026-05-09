import { COUNTS_MODE_UINT32, COUNTS_MODE_VARINT } from './constants.js';
import { writeVarint } from './varint.js';

export class CountsWriter {
  encode(counts: readonly bigint[]): Uint8Array {
    // First pass: encode each count as varint and check uint32 fit.
    const varintChunks: Uint8Array[] = [];
    let allFitUint32 = true;
    let varintTotalBytes = 0;
    for (const count of counts) {
      if (count > 0xFFFF_FFFFn) allFitUint32 = false;
      const chunk = writeVarint(count);
      varintChunks.push(chunk);
      varintTotalBytes += chunk.length;
    }

    const fixedSize = counts.length * 4;
    const useVarint = !allFitUint32 || varintTotalBytes < fixedSize;

    const countHeader = writeVarint(BigInt(counts.length));
    // 1 byte mode + header varint + payload
    const payloadSize = useVarint ? varintTotalBytes : fixedSize;
    const result = new Uint8Array(1 + countHeader.length + payloadSize);
    let pos = 0;

    result[pos++] = useVarint ? COUNTS_MODE_VARINT : COUNTS_MODE_UINT32;
    result.set(countHeader, pos); pos += countHeader.length;

    if (useVarint) {
      for (const chunk of varintChunks) {
        result.set(chunk, pos); pos += chunk.length;
      }
    } else {
      const view = new DataView(result.buffer, pos);
      counts.forEach((count, i) => view.setUint32(i * 4, Number(count), true));
    }
    return result;
  }
}
