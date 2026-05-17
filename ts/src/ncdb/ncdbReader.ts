import JSZip from 'jszip';
import type { MemUCIS } from '../mem/MemUCIS.js';
import {
  MEMBER_COUNTS,
  MEMBER_CROSS,
  MEMBER_DESIGN_UNITS,
  MEMBER_HISTORY,
  MEMBER_ISSUES,
  MEMBER_ISSUES_HISTORY,
  MEMBER_ISSUES_META,
  MEMBER_MANIFEST,
  MEMBER_SCOPE_TREE,
  MEMBER_SOURCES,
  MEMBER_STRINGS,
  NCDB_FORMAT,
} from './constants.js';
import { CountsReader } from './countsReader.js';
import { CrossReader } from './crossReader.js';
import { DesignUnitsReader } from './designUnitsReader.js';
import { HistoryReader } from './historyReader.js';
import { IssueSet } from './IssueSet.js';
import { IssuesHistoryReader } from './issuesHistoryReader.js';
import { ScopeTreeReader } from './scopeTreeReader.js';
import { SourcesReader } from './sourcesReader.js';
import { StringTable } from './stringTable.js';

export class NcdbReader {
  /** Browser-safe: create a new MemUCIS from raw NCDB ZIP bytes. */
  async readFromBytes(data: Uint8Array): Promise<MemUCIS> {
    const { MemUCIS } = await import('../mem/MemUCIS.js');
    const db = new MemUCIS();
    await this.readIntoFromBytes(data, db);
    return db;
  }

  /** Browser-safe: populate an existing MemUCIS from raw NCDB ZIP bytes. */
  async readIntoFromBytes(data: Uint8Array, db: MemUCIS): Promise<void> {
    db.reset();
    const zip = await JSZip.loadAsync(data);
    const manifestEntry = zip.file(MEMBER_MANIFEST);
    if (!manifestEntry) {
      throw new Error('Missing manifest.json in NCDB file');
    }
    const manifest = JSON.parse(await manifestEntry.async('string')) as { format?: string };
    if (manifest.format !== NCDB_FORMAT) {
      throw new Error(`Expected format ${NCDB_FORMAT}, got ${manifest.format}`);
    }
    const stringsEntry = zip.file(MEMBER_STRINGS);
    const treeEntry = zip.file(MEMBER_SCOPE_TREE);
    const countsEntry = zip.file(MEMBER_COUNTS);
    if (!stringsEntry || !treeEntry || !countsEntry) {
      throw new Error('NCDB archive missing required members');
    }
    const strings = StringTable.read(await stringsEntry.async('uint8array'));
    const fileHandles = zip.file(MEMBER_SOURCES)
      ? new SourcesReader().readToArray(await zip.file(MEMBER_SOURCES)!.async('string'), db)
      : [];
    new ScopeTreeReader(strings, fileHandles).read(
      await treeEntry.async('uint8array'),
      db,
      new CountsReader().decode(await countsEntry.async('uint8array')),
    );
    const crossEntry = zip.file(MEMBER_CROSS);
    if (crossEntry) {
      new CrossReader().read(await crossEntry.async('string'), db);
    }
    const issuesEntry = zip.file(MEMBER_ISSUES);
    if (issuesEntry) {
      db.issues = IssueSet.fromBytes(await issuesEntry.async('uint8array'));
    }
    const issuesMetaEntry = zip.file(MEMBER_ISSUES_META);
    if (issuesMetaEntry) {
      db._issuesMetaRaw = await issuesMetaEntry.async('string');
    }
    const issuesHistEntry = zip.file(MEMBER_ISSUES_HISTORY);
    if (issuesHistEntry) {
      db.issueHistory = new IssuesHistoryReader(await issuesHistEntry.async('uint8array'));
    }
    db.duIndex.clear();
    const duIndex = new DesignUnitsReader().buildIndex(
      zip.file(MEMBER_DESIGN_UNITS) ? await zip.file(MEMBER_DESIGN_UNITS)!.async('string') : '',
      db,
    );
    for (const [name, scope] of duIndex) {
      db.duIndex.set(name, scope);
    }
    const historyEntry = zip.file(MEMBER_HISTORY);
    if (historyEntry) {
      new HistoryReader().read(await historyEntry.async('string'), db);
    }
  }

  /** Node.js convenience: read from a file path. Delegates to readFromBytes. */
  async read(path: string): Promise<MemUCIS> {
    const { readFile } = await import('node:fs/promises');
    return this.readFromBytes(await readFile(path));
  }

  /** Node.js convenience: populate an existing MemUCIS from a file path. Delegates to readIntoFromBytes. */
  async readInto(path: string, db: MemUCIS): Promise<void> {
    const { readFile } = await import('node:fs/promises');
    return this.readIntoFromBytes(await readFile(path), db);
  }
}
