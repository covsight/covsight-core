import { deflateSync } from 'node:zlib';
import { writeVarint } from './varint.js';

const MAGIC = 0x49535348;
const VERSION = 1;
const NO_COMMENT = 0xFFFF;
const HEADER_SIZE = 15;
const ISSUE_INDEX_SIZE = 8;

export interface IssueStateTransition {
  issueId: string;
  ts: number;
  newState: number;
  comment: string;
}

export class IssuesHistoryWriter {
  private transitions = new Map<string, Array<[number, number, string]>>();

  add(issueId: string, ts: number, newState: number, comment = ''): void {
    const rows = this.transitions.get(issueId);
    if (rows) {
      rows.push([ts, newState, comment]);
    } else {
      this.transitions.set(issueId, [[ts, newState, comment]]);
    }
  }

  seal(): Uint8Array {
    return deflateSync(this.buildRaw());
  }

  sealFast(): Uint8Array {
    return deflateSync(this.buildRaw(), { level: 1 });
  }

  private buildRaw(): Uint8Array {
    const sortedIds = Array.from(this.transitions.keys()).sort();
    const rowsByIssue = new Map<string, Array<[number, number, string]>>();
    const commentIndex = new Map<string, number>();
    const commentTable: string[] = [];

    for (const issueId of sortedIds) {
      const rows = [...(this.transitions.get(issueId) ?? [])].sort((lhs, rhs) => lhs[0] - rhs[0]);
      rowsByIssue.set(issueId, rows);
      for (const [, , comment] of rows) {
        if (comment && !commentIndex.has(comment)) {
          commentIndex.set(comment, commentTable.length);
          commentTable.push(comment);
        }
      }
    }

    const allRows: Array<[number, number, number, number]> = [];
    const issueIndex: Array<[number, number, number]> = [];
    for (let idStrIdx = 0; idStrIdx < sortedIds.length; idStrIdx += 1) {
      const issueId = sortedIds[idStrIdx]!;
      const rows = rowsByIssue.get(issueId) ?? [];
      const startRow = allRows.length;
      for (const [ts, state, comment] of rows) {
        allRows.push([idStrIdx, ts, state, comment ? (commentIndex.get(comment) ?? NO_COMMENT) : NO_COMMENT]);
      }
      issueIndex.push([idStrIdx, startRow, rows.length]);
    }

    const tsBase = allRows.length === 0 ? 0 : Math.min(...allRows.map((row) => row[1]));
    const deltaParts: Uint8Array[] = [];
    const stateBytes = new Uint8Array(allRows.length);
    const commentBytes = new Uint8Array(allRows.length * 2);
    const commentView = new DataView(commentBytes.buffer, commentBytes.byteOffset, commentBytes.byteLength);
    let rowIdx = 0;
    for (const [, startRow, count] of issueIndex) {
      let prev = tsBase;
      for (let localIdx = 0; localIdx < count; localIdx += 1) {
        const row = allRows[startRow + localIdx]!;
        const delta = row[1] - prev;
        deltaParts.push(writeVarint(BigInt(delta)));
        stateBytes[rowIdx] = row[2];
        commentView.setUint16(rowIdx * 2, row[3], true);
        prev = row[1];
        rowIdx += 1;
      }
    }

    const idTableBytes = encodeU8Strings(sortedIds);
    const commentTableBytes = encodeU8Strings(commentTable);
    const issueIndexBytes = new Uint8Array(issueIndex.length * ISSUE_INDEX_SIZE);
    const issueIndexView = new DataView(issueIndexBytes.buffer, issueIndexBytes.byteOffset, issueIndexBytes.byteLength);
    for (let i = 0; i < issueIndex.length; i += 1) {
      const [idStrIdx, startRow, count] = issueIndex[i]!;
      const offset = i * ISSUE_INDEX_SIZE;
      issueIndexView.setUint16(offset, idStrIdx, true);
      issueIndexView.setUint32(offset + 2, startRow, true);
      issueIndexView.setUint16(offset + 6, count, true);
    }

    const tsBaseBytes = new Uint8Array(4);
    new DataView(tsBaseBytes.buffer).setUint32(0, tsBase, true);

    const header = new Uint8Array(HEADER_SIZE);
    const headerView = new DataView(header.buffer, header.byteOffset, header.byteLength);
    headerView.setUint32(0, MAGIC, true);
    headerView.setUint8(4, VERSION);
    headerView.setUint16(5, sortedIds.length, true);
    headerView.setUint16(7, commentTable.length, true);
    headerView.setUint16(9, issueIndex.length, true);
    headerView.setUint32(11, allRows.length, true);

    return concat([
      header,
      idTableBytes,
      commentTableBytes,
      issueIndexBytes,
      tsBaseBytes,
      ...deltaParts,
      stateBytes,
      commentBytes,
    ]);
  }
}

function encodeU8Strings(values: readonly string[]): Uint8Array {
  const encoder = new TextEncoder();
  const parts: Uint8Array[] = [];
  for (const value of values) {
    const encoded = encoder.encode(value);
    if (encoded.length > 0xFF) {
      throw new Error('issues_history.bin supports strings up to 255 bytes');
    }
    const chunk = new Uint8Array(1 + encoded.length);
    chunk[0] = encoded.length;
    chunk.set(encoded, 1);
    parts.push(chunk);
  }
  return concat(parts);
}

function concat(parts: readonly Uint8Array[]): Uint8Array {
  const total = parts.reduce((acc, part) => acc + part.length, 0);
  const result = new Uint8Array(total);
  let offset = 0;
  for (const part of parts) {
    result.set(part, offset);
    offset += part.length;
  }
  return result;
}
