import { TestData } from '../api/TestData.js';
import { HistoryNodeKind } from '../api/enums/HistoryNodeKind.js';
import type { MemUCIS } from '../mem/MemUCIS.js';

interface HistoryRecord {
  kind?: string | number;
  test_name?: string | null;
  logical_name?: string | null;
  /* flat fields (Python/C compatible) */
  user_name?: string | null;
  seed?: string | null;
  tool_category?: string | null;
  comment?: string | null;
  /* nested test_data (TS internal format) */
  test_data?: Record<string, unknown> | null;
}

export class HistoryReader {
  read(json: string, db: MemUCIS): void {
    const records = JSON.parse(json) as HistoryRecord[];
    for (const record of records) {
      const node = db.createHistoryNode(parseKind(record.kind), record.logical_name ?? record.test_name ?? '');
      const raw = record.test_data;
      /* Read testdata fields: prefer flat (Python/C) format, fall back to nested test_data */
      const userName     = toString(record.user_name)     || (raw ? toString(raw.userName)     : '');
      const seed         = toString(record.seed)          || (raw ? toString(raw.seed)         : '');
      const toolCategory = toString(record.tool_category) || (raw ? toString(raw.toolCategory) : '');
      const comment      = toString(record.comment)       || (raw ? toString(raw.comment)      : '');
      if (raw || record.user_name != null || record.seed != null || record.tool_category != null || record.comment != null) {
        node.testData = new TestData({
          userName,
          testPlanName: raw ? toString(raw.testPlanName) : '',
          date:         raw ? toString(raw.date)         : '',
          simElapsed:   raw ? toString(raw.simElapsed)   : '',
          runCwd:       raw ? toString(raw.runCwd)       : '',
          comment,
          userName2:    raw ? toString(raw.userName2)    : '',
          toolCategory,
          compulsory:   raw ? Boolean(raw.compulsory)    : false,
          date2:        raw ? toString(raw.date2)        : '',
          simCmd:       raw ? toString(raw.simCmd)       : '',
          elaborCmd:    raw ? toString(raw.elaborCmd)    : '',
          seed,
          goldenLog:    raw ? toString(raw.goldenLog)    : '',
          randstate:    raw ? toString(raw.randstate)    : '',
          attributes: new Map<string, string>(Object.entries((raw?.attributes ?? {}) as Record<string, string>)),
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
