import { HistoryNodeKind } from '../api/enums/HistoryNodeKind.js';
import type { MemUCIS } from '../mem/MemUCIS.js';

export class HistoryWriter {
  write(db: MemUCIS): string {
    const records = Array.from({ length: db.numHistoryNodes() }, (_, index) => {
      const node = db.historyNode(index);
      return {
        kind: kindToString(node.kind),
        logical_name: node.testName,
        test_data: node.testData
          ? {
              userName: node.testData.userName,
              testPlanName: node.testData.testPlanName,
              date: node.testData.date,
              simElapsed: node.testData.simElapsed,
              runCwd: node.testData.runCwd,
              comment: node.testData.comment,
              userName2: node.testData.userName2,
              toolCategory: node.testData.toolCategory,
              compulsory: node.testData.compulsory,
              date2: node.testData.date2,
              simCmd: node.testData.simCmd,
              elaborCmd: node.testData.elaborCmd,
              seed: node.testData.seed,
              goldenLog: node.testData.goldenLog,
              randstate: node.testData.randstate,
              attributes: Object.fromEntries(node.testData.attributes),
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
