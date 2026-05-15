import { mkdir, rm } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { MemUCIS } from '../../src/mem/index.js';
import {
  IssueSet,
  IssueSpec,
  IssuesHistoryWriter,
  IssuesMeta,
  NcdbReader,
  NcdbWriter,
  RES_FIXED,
  SEV_HIGH,
  SEV_LOW,
  STATE_OPEN,
  STATE_RESOLVED,
} from '../../src/ncdb/index.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

describe('NCDB issues integration', () => {
  test('writes and reads issues through NcdbWriter/NcdbReader', async () => {
    const outDir = path.join(__dirname, '..', '.artifacts');
    const outFile = path.join(outDir, 'issues-roundtrip.cdb');
    await mkdir(outDir, { recursive: true });

    const db = new MemUCIS();
    const issues = new IssueSet();
    const handle = issues.addIssue(new IssueSpec({ id: 'I-001', severity: SEV_HIGH, state: STATE_OPEN }));
    issues.addIssue(new IssueSpec({ id: 'I-002', severity: SEV_LOW, state: STATE_RESOLVED, resolution: RES_FIXED }));
    db.issues = issues;

    const meta = new IssuesMeta();
    meta.setTitle(handle, 'First issue');
    db.issuesMeta = meta;

    const history = new IssuesHistoryWriter();
    history.add('I-001', 100, STATE_OPEN);
    history.add('I-001', 200, STATE_RESOLVED, 'fixed');
    db.issueHistoryWriter = history;

    await new NcdbWriter().write(outFile, db);
    const loaded = await new NcdbReader().read(outFile);

    expect(loaded.issues?.length).toBe(2);
    expect(Array.from(loaded.issues?.issues() ?? []).map((item) => item.id)).toEqual(['I-001', 'I-002']);
    expect(loaded.issues?.get('I-001')?.state).toBe(STATE_OPEN);
    expect(loaded.getIssuesMeta()?.getTitle(loaded.issues!.get('I-001')!)).toBe('First issue');
    expect(Array.from(loaded.issueHistory?.historyForIssue('I-001') ?? []).map((item) => item.ts)).toEqual([100, 200]);

    await rm(outFile, { force: true });
  });

  test('cdb without issues loads cleanly', async () => {
    const outDir = path.join(__dirname, '..', '.artifacts');
    const outFile = path.join(outDir, 'issues-none.cdb');
    await mkdir(outDir, { recursive: true });

    const db = new MemUCIS();
    await new NcdbWriter().write(outFile, db);

    const loaded = await new NcdbReader().read(outFile);
    expect(loaded.issues).toBeNull();
    expect(loaded.getIssuesMeta()).toBeNull();
    expect(loaded.issueHistory).toBeNull();

    await rm(outFile, { force: true });
  });
});
