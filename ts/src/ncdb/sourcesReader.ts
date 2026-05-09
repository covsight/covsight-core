import type { FileHandle } from '../api/FileHandle.js';
import type { MemUCIS } from '../mem/MemUCIS.js';

export class SourcesReader {
  readToArray(json: string, db: MemUCIS): FileHandle[] {
    const paths = JSON.parse(json) as string[];
    return paths.map((filePath) => db.getFileHandle(filePath));
  }
}
