import { IssueHandle } from './IssueSet.js';

interface SerializedEntry {
  ti?: string;
  ur?: string;
}

export class IssuesMeta {
  private entries: Array<[string, string] | null> = [];

  getTitle(handle: IssueHandle): string | null {
    const idx = IssueHandle._getIdx(handle);
    const entry = idx < this.entries.length ? this.entries[idx] : null;
    if (!entry) {
      return null;
    }
    return entry[0] || null;
  }

  getUrl(handle: IssueHandle): string | null {
    const idx = IssueHandle._getIdx(handle);
    const entry = idx < this.entries.length ? this.entries[idx] : null;
    if (!entry) {
      return null;
    }
    return entry[1] || null;
  }

  setTitle(handle: IssueHandle, title: string): void {
    const idx = IssueHandle._getIdx(handle);
    this.growTo(idx);
    const entry = this.entries[idx];
    this.entries[idx] = entry ? [title, entry[1]] : [title, ''];
  }

  setUrl(handle: IssueHandle, url: string): void {
    const idx = IssueHandle._getIdx(handle);
    this.growTo(idx);
    const entry = this.entries[idx];
    this.entries[idx] = entry ? [entry[0], url] : ['', url];
  }

  serialize(): string {
    const m = this.entries.map((entry) => {
      if (!entry) {
        return null;
      }
      const [title, url] = entry;
      const obj: SerializedEntry = {};
      if (title) {
        obj.ti = title;
      }
      if (url) {
        obj.ur = url;
      }
      return Object.keys(obj).length === 0 ? null : obj;
    });
    return JSON.stringify({ v: 1, m });
  }

  static fromJson(json: string): IssuesMeta {
    const parsed = JSON.parse(json) as { m?: Array<SerializedEntry | null> };
    const meta = new IssuesMeta();
    for (const entry of parsed.m ?? []) {
      if (!entry) {
        meta.entries.push(null);
      } else {
        meta.entries.push([entry.ti ?? '', entry.ur ?? '']);
      }
    }
    return meta;
  }

  private growTo(idx: number): void {
    while (this.entries.length <= idx) {
      this.entries.push(null);
    }
  }
}
