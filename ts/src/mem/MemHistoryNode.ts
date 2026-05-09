import { HistoryNode } from '../api/HistoryNode.js';
import { TestData } from '../api/TestData.js';
import { HistoryNodeKind } from '../api/enums/HistoryNodeKind.js';

export class MemHistoryNode extends HistoryNode {
  constructor(kind: HistoryNodeKind, testName: string | null, testData: TestData | null = null, parent: HistoryNode | null = null) {
    super(kind, testName, testData, parent);
  }
}
