export function readVarint(buf: Uint8Array, offset: number): { value: bigint; bytesRead: number } {
  let value = 0n;
  let shift = 0n;
  let index = offset;
  while (index < buf.length) {
    const byte = BigInt(buf[index]!);
    value |= (byte & 0x7Fn) << shift;
    index += 1;
    if ((byte & 0x80n) === 0n) {
      return { value, bytesRead: index - offset };
    }
    shift += 7n;
  }
  throw new Error('Buffer too short for varint');
}

export function writeVarint(value: bigint): Uint8Array {
  if (value < 0n) {
    throw new Error(`Varint requires non-negative integer, got ${value}`);
  }
  const bytes: number[] = [];
  let current = value;
  do {
    let byte = Number(current & 0x7Fn);
    current >>= 7n;
    if (current !== 0n) {
      byte |= 0x80;
    }
    bytes.push(byte);
  } while (current !== 0n);
  return Uint8Array.from(bytes);
}
