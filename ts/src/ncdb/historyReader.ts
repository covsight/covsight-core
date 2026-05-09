import { TestData } from '../api/TestData.js';
import { HistoryNodeKind } from '../api/enums/HistoryNodeKind.js';
import type { MemUCIS } from '../mem/MemUCIS.js';

interface HistoryRecord {
  kind?: string | number;
  test_name?: string | null;
  logical_name?: string | null;
  test_data?: Record<string, unknown> | null;
}

export class HistoryReader {
  read(json: string, db: MemUCIS): void {
    const records = JSON.parse(json) as HistoryRecord[];
    for (const record of records) {
      const node = db.createHistoryNode(parseKind(record.kind), record.logical_name ?? record.test_name ?? '');
      if (record.test_data) {
        const raw = record.test_data;
        node.testData = new TestData({
          userName: toString(raw.userName),
          testPlanName: toString(raw.testPlanName),
          date: toString(raw.date),
          simElapsed: toString(raw.simElapsed),
          runCwd: toString(raw.runCwd),
          comment: toString(raw.comment),
          userName2: toString(raw.userName2),
          toolCategory: toString(raw.toolCategory),
          compulsory: Boolean(raw.compulsory),
          date2: toString(raw.date2),
          simCmd: toString(raw.simCmd),
          elaborCmd: toString(raw.elaborCmd),
          seed: toString(raw.seed),
          goldenLog: toString(raw.goldenLog),
          randstate: toString(raw.randstate),
          attributes: new Map<string, string>(Object.entries((raw.attributes ?? {}) as Record<string, string>)),
        });
      }
    }
  }
}

function parseKind(value: string | number | undefined): HistoryNodeKind {
  if (value === 'MERGE' || value === HistoryNodeKind.MERGE) {
    return HistoryNodeKind.MERGE;
  }
  if (value === 'NONE' || value === HistoryNodeKind.NONE) {
    return HistoryNodeKind.NONE;
  }
  if (value === 'ALL' || value === HistoryNodeKind.ALL) {
    return HistoryNodeKind.ALL;
  }
  return HistoryNodeKind.TEST;
}

function toString(value: unknown): string {
  return typeof value === 'string' ? value : '';
}
