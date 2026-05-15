import {
  IssuesHistoryReader,
  IssuesHistoryWriter,
  STATE_CLOSED,
  STATE_IN_PROGRESS,
  STATE_OPEN,
  STATE_RESOLVED,
} from '../../src/ncdb/index.js';

describe('IssuesHistory', () => {
  test('writer add + seal + reader roundtrip', () => {
    const writer = new IssuesHistoryWriter();
    writer.add('I-001', 1700000000, STATE_OPEN);
    writer.add('I-001', 1700001000, STATE_RESOLVED);

    const reader = new IssuesHistoryReader(writer.seal());
    expect(Array.from(reader.historyForIssue('I-001'))).toEqual([
      { issueId: 'I-001', ts: 1700000000, newState: STATE_OPEN, comment: '' },
      { issueId: 'I-001', ts: 1700001000, newState: STATE_RESOLVED, comment: '' },
    ]);
  });

  test('historyForIssue returns correct transitions in order', () => {
    const writer = new IssuesHistoryWriter();
    writer.add('I-001', 30, STATE_RESOLVED);
    writer.add('I-001', 10, STATE_OPEN);
    writer.add('I-001', 20, STATE_IN_PROGRESS);

    const reader = new IssuesHistoryReader(writer.seal());
    expect(Array.from(reader.historyForIssue('I-001')).map((transition) => transition.ts)).toEqual([10, 20, 30]);
  });

  test('stateAt returns correct state', () => {
    const writer = new IssuesHistoryWriter();
    writer.add('I-001', 100, STATE_OPEN);
    writer.add('I-001', 200, STATE_IN_PROGRESS);
    writer.add('I-001', 300, STATE_CLOSED);

    const reader = new IssuesHistoryReader(writer.seal());
    expect(reader.stateAt('I-001', 99)).toBeNull();
    expect(reader.stateAt('I-001', 100)).toBe(STATE_OPEN);
    expect(reader.stateAt('I-001', 250)).toBe(STATE_IN_PROGRESS);
    expect(reader.stateAt('I-001', 999)).toBe(STATE_CLOSED);
  });

  test('allTransitions yields all transitions', () => {
    const writer = new IssuesHistoryWriter();
    writer.add('A', 100, STATE_OPEN);
    writer.add('B', 200, STATE_RESOLVED);

    const reader = new IssuesHistoryReader(writer.seal());
    expect(Array.from(reader.allTransitions())).toEqual([
      { issueId: 'A', ts: 100, newState: STATE_OPEN, comment: '' },
      { issueId: 'B', ts: 200, newState: STATE_RESOLVED, comment: '' },
    ]);
  });

  test('delta encoding uses global ts_base and resets per issue', () => {
    const writer = new IssuesHistoryWriter();
    writer.add('B', 200, STATE_OPEN);
    writer.add('B', 260, STATE_RESOLVED);
    writer.add('A', 100, STATE_OPEN);
    writer.add('A', 180, STATE_IN_PROGRESS);

    const reader = new IssuesHistoryReader(writer.seal());
    expect(Array.from(reader.allTransitions())).toEqual([
      { issueId: 'A', ts: 100, newState: STATE_OPEN, comment: '' },
      { issueId: 'A', ts: 180, newState: STATE_IN_PROGRESS, comment: '' },
      { issueId: 'B', ts: 200, newState: STATE_OPEN, comment: '' },
      { issueId: 'B', ts: 260, newState: STATE_RESOLVED, comment: '' },
    ]);
  });

  test('comments roundtrip', () => {
    const writer = new IssuesHistoryWriter();
    writer.add('I-001', 100, STATE_OPEN, 'created');
    writer.add('I-001', 200, STATE_RESOLVED, 'fixed');

    const reader = new IssuesHistoryReader(writer.sealFast());
    expect(Array.from(reader.historyForIssue('I-001')).map((transition) => transition.comment)).toEqual(['created', 'fixed']);
  });
});
