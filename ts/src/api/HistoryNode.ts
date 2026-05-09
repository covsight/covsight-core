import type { TestData } from './TestData.js';
import { HistoryNodeKind } from './enums/HistoryNodeKind.js';

export abstract class HistoryNode {
  constructor(
    public readonly kind: HistoryNodeKind,
    public testName: string | null,
    public testData: TestData | null,
    public readonly parent: HistoryNode | null = null,
  ) {}
}
