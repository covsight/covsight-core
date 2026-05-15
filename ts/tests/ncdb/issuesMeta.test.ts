import { IssueSet, IssueSpec, IssuesMeta } from '../../src/ncdb/index.js';

describe('IssuesMeta', () => {
  test('setTitle/getTitle and setUrl/getUrl', () => {
    const issues = new IssueSet();
    const handle = issues.addIssue(new IssueSpec({ id: 'I-001' }));
    const meta = new IssuesMeta();

    meta.setTitle(handle, 'UART parity error');
    meta.setUrl(handle, 'https://jira.example.com/browse/PROJ-1');

    expect(meta.getTitle(handle)).toBe('UART parity error');
    expect(meta.getUrl(handle)).toBe('https://jira.example.com/browse/PROJ-1');
  });

  test('serialize/fromJson roundtrip', () => {
    const issues = new IssueSet();
    const h0 = issues.addIssue(new IssueSpec({ id: 'I-000' }));
    const h1 = issues.addIssue(new IssueSpec({ id: 'I-001' }));
    const h2 = issues.addIssue(new IssueSpec({ id: 'I-002' }));
    const meta = new IssuesMeta();

    meta.setTitle(h0, 'First issue');
    meta.setUrl(h0, 'https://example.com/1');
    meta.setTitle(h2, 'Third issue');

    const roundTrip = IssuesMeta.fromJson(meta.serialize());
    expect(roundTrip.getTitle(h0)).toBe('First issue');
    expect(roundTrip.getUrl(h0)).toBe('https://example.com/1');
    expect(roundTrip.getTitle(h1)).toBeNull();
    expect(roundTrip.getUrl(h1)).toBeNull();
    expect(roundTrip.getTitle(h2)).toBe('Third issue');
  });

  test('getTitle returns null for missing entries', () => {
    const issues = new IssueSet();
    const handle = issues.addIssue(new IssueSpec({ id: 'I-001' }));
    const meta = new IssuesMeta();
    expect(meta.getTitle(handle)).toBeNull();
    expect(meta.getUrl(handle)).toBeNull();
  });

  test('serialize omits empty strings', () => {
    const issues = new IssueSet();
    const handle = issues.addIssue(new IssueSpec({ id: 'I-001' }));
    const meta = new IssuesMeta();

    meta.setTitle(handle, '');
    meta.setUrl(handle, 'https://example.com/1');

    expect(meta.serialize()).toBe('{"v":1,"m":[{"ur":"https://example.com/1"}]}');
  });
});
