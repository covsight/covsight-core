import { inflateSync } from 'node:zlib';
import { readVarint } from './varint.js';
import type { IssueStateTransition } from './issuesHistoryWriter.js';

const MAGIC = 0x49535348;
const VERSION = 1;
const NO_COMMENT = 0xFFFF;
const XZ_MAGIC = [0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00] as const;
export class IssuesHistoryReader {
  private readonly idTable: string[];
  private readonly commentTable: string[];
  private readonly issueIndex: Array<[number, number, number]>;
  private readonly idToIssueIdx = new Map<string, number>();
  private readonly tsBase: number;
  private readonly tsDeltas: number[];
  private readonly stateBytes: Uint8Array;
  private readonly commentIdxs: number[];

  constructor(compressed: Uint8Array) {
    if (hasPrefix(compressed, XZ_MAGIC)) {
      throw new Error('LZMA-compressed issues_history.bin is not supported in TypeScript; use deflate compression');
    }

    const data = inflateSync(Buffer.from(compressed));
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    let offset = 0;

    const magic = view.getUint32(offset, true); offset += 4;
    const version = view.getUint8(offset); offset += 1;
    const numIdStrings = view.getUint16(offset, true); offset += 2;
    const numComments = view.getUint16(offset, true); offset += 2;
    const numIssues = view.getUint16(offset, true); offset += 2;
    const numRecords = view.getUint32(offset, true); offset += 4;

    if (magic !== MAGIC) {
      throw new Error(`Invalid issues_history.bin magic: 0x${magic.toString(16).padStart(8, '0').toUpperCase()}`);
    }
    if (version !== VERSION) {
      throw new Error(`Unsupported issues_history.bin version: ${version}`);
    }

    this.idTable = [];
    for (let i = 0; i < numIdStrings; i += 1) {
      const length = data[offset]!;
      offset += 1;
      const end = offset + length;
      this.idTable.push(new TextDecoder().decode(data.subarray(offset, end)));
      offset = end;
    }

    this.commentTable = [];
    for (let i = 0; i < numComments; i += 1) {
      const length = data[offset]!;
      offset += 1;
      const end = offset + length;
      this.commentTable.push(new TextDecoder().decode(data.subarray(offset, end)));
      offset = end;
    }

    this.issueIndex = [];
    for (let i = 0; i < numIssues; i += 1) {
      const idStrIdx = view.getUint16(offset, true); offset += 2;
      const startRow = view.getUint32(offset, true); offset += 4;
      const count = view.getUint16(offset, true); offset += 2;
      this.issueIndex.push([idStrIdx, startRow, count]);
      this.idToIssueIdx.set(this.idTable[idStrIdx]!, i);
    }

    this.tsBase = view.getUint32(offset, true);
    offset += 4;

    this.tsDeltas = [];
    for (let i = 0; i < numRecords; i += 1) {
      const result = readVarint(data, offset);
      this.tsDeltas.push(Number(result.value));
      offset += result.bytesRead;
    }

    this.stateBytes = data.subarray(offset, offset + numRecords);
    offset += numRecords;

    this.commentIdxs = [];
    for (let i = 0; i < numRecords; i += 1) {
      this.commentIdxs.push(view.getUint16(offset, true));
      offset += 2;
    }
  }

  *historyForIssue(issueId: string): Generator<IssueStateTransition> {
    const entryIdx = this.idToIssueIdx.get(issueId);
    if (entryIdx === undefined) {
      return;
    }
    const [, startRow, count] = this.issueIndex[entryIdx]!;
    yield* this.iterRows(issueId, startRow, count);
  }

  stateAt(issueId: string, ts: number): number | null {
    const entryIdx = this.idToIssueIdx.get(issueId);
    if (entryIdx === undefined) {
      return null;
    }
    const [, startRow, count] = this.issueIndex[entryIdx]!;
    let currentTs = this.tsBase;
    let lastState: number | null = null;
    for (let i = startRow; i < startRow + count; i += 1) {
      currentTs += this.tsDeltas[i]!;
      if (currentTs > ts) {
        break;
      }
      lastState = this.stateBytes[i]!;
    }
    return lastState;
  }

  *allTransitions(): Generator<IssueStateTransition> {
    for (const [idStrIdx, startRow, count] of this.issueIndex) {
      yield* this.iterRows(this.idTable[idStrIdx]!, startRow, count);
    }
  }

  private *iterRows(issueId: string, startRow: number, count: number): Generator<IssueStateTransition> {
    let currentTs = this.tsBase;
    for (let i = startRow; i < startRow + count; i += 1) {
      currentTs += this.tsDeltas[i]!;
      const commentIdx = this.commentIdxs[i]!;
      yield {
        issueId,
        ts: currentTs,
        newState: this.stateBytes[i]!,
        comment: commentIdx === NO_COMMENT ? '' : this.commentTable[commentIdx]!,
      };
    }
  }
}

function hasPrefix(data: Uint8Array, prefix: readonly number[]): boolean {
  if (data.length < prefix.length) {
    return false;
  }
  for (let i = 0; i < prefix.length; i += 1) {
    if (data[i] !== prefix[i]) {
      return false;
    }
  }
  return true;
}
