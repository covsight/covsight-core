import type { MemUCIS } from '../mem/MemUCIS.js';

export class SourcesWriter {
  write(db: MemUCIS): string {
    return JSON.stringify([...db.getFileHandles()].sort((lhs, rhs) => lhs.fileId - rhs.fileId).map((fh) => fh.filePath), null, 2);
  }
}
