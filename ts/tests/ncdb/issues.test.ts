import {
  CoverageIssueLinkInfo,
  IssueSet,
  IssueSpec,
  LINK_BLOCKED_BY,
  LINK_RELATED,
  RES_FIXED,
  SEV_CRITICAL,
  SEV_HIGH,
  SEV_LOW,
  SEV_MEDIUM,
  STATE_CLOSED,
  STATE_IN_PROGRESS,
  STATE_OPEN,
  STATE_RESOLVED,
  TestpointIssueLinkInfo,
  WaiverIssueLinkInfo,
} from '../../src/ncdb/index.js';

function makeIssueSet(): IssueSet {
  const issues = new IssueSet();
  issues.addIssue(new IssueSpec({ id: 'I-001', severity: SEV_HIGH, state: STATE_OPEN }));
  issues.addIssue(new IssueSpec({ id: 'I-002', severity: SEV_LOW, state: STATE_CLOSED, resolution: RES_FIXED }));
  issues.addIssue(new IssueSpec({ id: 'I-003', severity: SEV_MEDIUM, state: STATE_IN_PROGRESS }));
  return issues;
}

describe('IssueSet', () => {
  test('add/query roundtrip', () => {
    const issues = new IssueSet();
    issues.addIssue(new IssueSpec({
      id: 'I-001',
      severity: SEV_HIGH,
      state: STATE_OPEN,
      createdAt: 1000,
      updatedAt: 2000,
      syncedAt: 3000,
    }));

    const handle = issues.get('I-001');
    expect(handle).not.toBeNull();
    expect(handle?.id).toBe('I-001');
    expect(handle?.severity).toBe(SEV_HIGH);
    expect(handle?.state).toBe(STATE_OPEN);
    expect(handle?.createdAt).toBe(1000);
    expect(handle?.updatedAt).toBe(2000);
    expect(handle?.syncedAt).toBe(3000);
  });

  test('addIssue uses defaults', () => {
    const issues = new IssueSet();
    const handle = issues.addIssue(new IssueSpec({ id: 'I-001' }));
    expect(handle.ext).toBe('');
    expect(handle.severity).toBe(SEV_MEDIUM);
    expect(handle.state).toBe(STATE_OPEN);
    expect(handle.updatedAt).toBe(0);
  });

  test('get finds by id', () => {
    const issues = makeIssueSet();
    expect(issues.get('I-002')?.id).toBe('I-002');
    expect(issues.get('missing')).toBeNull();
  });

  test('openIssues filters correctly', () => {
    const issues = makeIssueSet();
    expect(Array.from(issues.openIssues()).map((handle) => handle.id)).toEqual(['I-001', 'I-003']);
  });

  test('issuesBySeverity filters correctly', () => {
    const issues = makeIssueSet();
    expect(Array.from(issues.issuesBySeverity(SEV_HIGH)).map((handle) => handle.id)).toEqual(['I-001']);
    expect(Array.from(issues.issuesBySeverity(SEV_CRITICAL))).toEqual([]);
  });

  test('waiver/testpoint/coverage links work', () => {
    const issues = makeIssueSet();
    const first = issues.get('I-001');
    expect(first).not.toBeNull();

    issues.addWaiverLink('W-001', 'I-001');
    issues.addWaiverLink('W-001', 'I-002');
    issues.addTestpointLink('tp_uart_tx', 'I-001', LINK_BLOCKED_BY);
    issues.addTestpointLink('tp_uart_tx', 'I-003', LINK_RELATED);
    issues.addCoverageLink('top.uart', '*', 'I-001', LINK_BLOCKED_BY);

    expect(Array.from(issues.issuesForWaiver('W-001')).map((handle) => handle.id)).toEqual(['I-001', 'I-002']);
    expect(Array.from(issues.issuesForTestpoint('tp_uart_tx')).map((handle) => handle.id)).toEqual(['I-001', 'I-003']);
    expect(Array.from(issues.waiversForIssue(first!))[0]).toEqual(new WaiverIssueLinkInfo('W-001', 'I-001'));
    expect(Array.from(issues.testpointsForIssue(first!))[0]).toEqual(new TestpointIssueLinkInfo('tp_uart_tx', 'I-001', LINK_BLOCKED_BY));
    expect(Array.from(issues.coverageLinks())[0]).toEqual(new CoverageIssueLinkInfo('top.uart', '*', 'I-001', LINK_BLOCKED_BY));
  });

  test('serialize/fromBytes roundtrip preserves all data', () => {
    const issues = new IssueSet();
    issues.addIssue(new IssueSpec({
      id: 'A-1',
      ext: 'EXT-1',
      severity: SEV_HIGH,
      state: STATE_OPEN,
      createdAt: 100,
      updatedAt: 200,
      syncedAt: 300,
    }));
    issues.addIssue(new IssueSpec({
      id: 'A-2',
      severity: SEV_LOW,
      state: STATE_RESOLVED,
      resolution: RES_FIXED,
      updatedAt: 201,
    }));
    issues.addWaiverLink('W-1', 'A-1');
    issues.addTestpointLink('tp_foo', 'A-1', LINK_BLOCKED_BY);
    issues.addCoverageLink('top.mod', '*', 'A-2', LINK_RELATED);

    const roundTrip = IssueSet.fromBytes(issues.serialize());
    expect(roundTrip.length).toBe(2);
    expect(roundTrip.get('A-1')?.ext).toBe('EXT-1');
    expect(roundTrip.get('A-2')?.state).toBe(STATE_RESOLVED);
    expect(Array.from(roundTrip.issuesForWaiver('W-1')).map((handle) => handle.id)).toEqual(['A-1']);
    expect(Array.from(roundTrip.issuesForTestpoint('tp_foo')).map((handle) => handle.id)).toEqual(['A-1']);
    expect(Array.from(roundTrip.coverageLinks())[0]).toEqual(new CoverageIssueLinkInfo('top.mod', '*', 'A-2', LINK_RELATED));
  });
});
