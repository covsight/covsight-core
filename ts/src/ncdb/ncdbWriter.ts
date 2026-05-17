import JSZip from 'jszip';
import type { MemUCIS } from '../mem/MemUCIS.js';
import {
  HISTORY_FORMAT_V1,
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
  NCDB_GENERATOR,
  NCDB_VERSION,
} from './constants.js';
import { CountsWriter } from './countsWriter.js';
import { CrossWriter } from './crossWriter.js';
import { DesignUnitsWriter } from './designUnitsWriter.js';
import { HistoryWriter } from './historyWriter.js';
import { ScopeTreeWriter } from './scopeTreeWriter.js';
import { SourcesWriter } from './sourcesWriter.js';
import { StringTable } from './stringTable.js';

export class NcdbWriter {
  /** Browser-safe: serialize a MemUCIS to NCDB ZIP bytes. */
  async toBytes(db: MemUCIS): Promise<Uint8Array> {
    const strings = new StringTable();
    const { scopeTree, counts } = new ScopeTreeWriter(strings).write(db);
    const zip = new JSZip();
    zip.file(MEMBER_MANIFEST, JSON.stringify({
      format: NCDB_FORMAT,
      version: NCDB_VERSION,
      generator: NCDB_GENERATOR,
      history_format: HISTORY_FORMAT_V1,
      path_separator: '/',
    }, null, 2));
    zip.file(MEMBER_STRINGS, strings.encode());
    zip.file(MEMBER_SCOPE_TREE, scopeTree);
    zip.file(MEMBER_COUNTS, new CountsWriter().encode(counts));
    zip.file(MEMBER_HISTORY, new HistoryWriter().write(db));
    zip.file(MEMBER_SOURCES, new SourcesWriter().write(db));
    const cross = new CrossWriter().write(db);
    if (cross.length > 0) {
      zip.file(MEMBER_CROSS, cross);
    }
    const designUnits = new DesignUnitsWriter().write(db);
    if (designUnits.length > 0) {
      zip.file(MEMBER_DESIGN_UNITS, designUnits);
    }
    if (db.issues) {
      zip.file(MEMBER_ISSUES, db.issues.serialize());
    }
    if (db.issuesMeta) {
      zip.file(MEMBER_ISSUES_META, db.issuesMeta.serialize());
    }
    if (db.issueHistoryWriter) {
      zip.file(MEMBER_ISSUES_HISTORY, db.issueHistoryWriter.sealFast(), { compression: 'STORE' });
    }
    return zip.generateAsync({ type: 'uint8array', compression: 'DEFLATE', compressionOptions: { level: 6 } });
  }

  /** Node.js convenience: write a MemUCIS to a file path. Delegates to toBytes. */
  async write(path: string, db: MemUCIS): Promise<void> {
    const { writeFile } = await import('node:fs/promises');
    await writeFile(path, await this.toBytes(db));
  }
}
