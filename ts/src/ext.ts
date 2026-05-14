import type { MemUCIS } from './mem/MemUCIS.js';
import { NcdbReader } from './ncdb/ncdbReader.js';
import { NcdbWriter } from './ncdb/ncdbWriter.js';

export interface DbFormat {
  /** Unique format name, e.g. 'ncdb'. */
  name: string;
  read(path: string): Promise<MemUCIS>;
  write(path: string, db: MemUCIS): Promise<void>;
}

export class FormatRegistry {
  private readonly _formats = new Map<string, DbFormat>();
  private readonly _readonlyFormats = new Proxy(this._formats, {
    get: (target, prop) => {
      if (prop === 'set' || prop === 'delete' || prop === 'clear') {
        return (): never => {
          throw new TypeError('Format registry is read-only');
        };
      }
      // eslint-disable-next-line @typescript-eslint/no-unsafe-assignment
      const value = Reflect.get(target, prop, target);
      // eslint-disable-next-line @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access
      return typeof value === 'function' ? value.bind(target) : value;
    },
  }) as ReadonlyMap<string, DbFormat>;

  register(fmt: DbFormat): void {
    this._formats.set(fmt.name, fmt);
  }

  getDbFormat(name: string): DbFormat {
    const fmt = this._formats.get(name);
    if (!fmt) throw new Error(`Unknown format: '${name}'. Registered: ${[...this._formats.keys()].join(', ')}`);
    return fmt;
  }

  dbFormats(): ReadonlyMap<string, DbFormat> {
    return this._readonlyFormats;
  }
}

export const ncdbFormat: DbFormat = {
  name: 'ncdb',
  read: (path) => new NcdbReader().read(path),
  write: (path, db) => new NcdbWriter().write(path, db),
};

/** Global default registry with NCDB pre-registered. */
export const defaultRegistry = new FormatRegistry();
defaultRegistry.register(ncdbFormat);
