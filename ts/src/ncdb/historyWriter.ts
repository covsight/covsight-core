import { HistoryNodeKind } from '../api/enums/HistoryNodeKind.js';
import type { MemUCIS } from '../mem/MemUCIS.js';

export class HistoryWriter {
  write(db: MemUCIS): string {
    const records = Array.from({ length: db.numHistoryNodes() }, (_, index) => {
      const node = db.historyNode(index);
      const td = node.testData;
      return {
        kind: kindToString(node.kind),
        logical_name: node.testName,
        /* flat fields (Python/C compatible) */
        user_name:     td?.userName     ?? null,
        seed:          td?.seed         ?? null,
        tool_category: td?.toolCategory ?? null,
        comment:       td?.comment      ?? null,
        /* nested test_data (TS internal format, kept for backward compat) */
        test_data: td
          ? {
              userName: td.userName,
              testPlanName: td.testPlanName,
              date: td.date,
              simElapsed: td.simElapsed,
              runCwd: td.runCwd,
              comment: td.comment,
              userName2: td.userName2,
              toolCategory: td.toolCategory,
              compulsory: td.compulsory,
              date2: td.date2,
              simCmd: td.simCmd,
              elaborCmd: td.elaborCmd,
              seed: td.seed,
              goldenLog: td.goldenLog,
              randstate: td.randstate,
              attributes: Object.fromEntries(td.attributes),
            }
          : null,
      };
    });
    return JSON.stringify(records, null, 2);
  }
}

function kindToString(kind: HistoryNodeKind): string {
  switch (kind) {
    case HistoryNodeKind.MERGE:
      return 'MERGE';
    case HistoryNodeKind.NONE:
      return 'NONE';
    case HistoryNodeKind.ALL:
      return 'ALL';
    case HistoryNodeKind.TEST:
    default:
      return 'TEST';
  }
}
