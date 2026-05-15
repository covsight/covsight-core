import { FileHandle } from '../api/FileHandle.js';
import type { HistoryNode } from '../api/HistoryNode.js';
import type { Scope } from '../api/Scope.js';
import { UCIS } from '../api/UCIS.js';
import { HistoryNodeKind } from '../api/enums/HistoryNodeKind.js';
import { IntProperty } from '../api/enums/IntProperty.js';
import { ScopeTypeT } from '../api/enums/ScopeTypeT.js';
import { MemHistoryNode } from './MemHistoryNode.js';
import { MemScope } from './MemScope.js';
import './MemCovergroup.js';
import './MemCoverpoint.js';
import './MemCross.js';
import { IssueSet } from '../ncdb/IssueSet.js';
import { IssuesMeta } from '../ncdb/IssuesMeta.js';
import { IssuesHistoryReader } from '../ncdb/issuesHistoryReader.js';
import { IssuesHistoryWriter } from '../ncdb/issuesHistoryWriter.js';

export class MemUCIS extends MemScope implements UCIS {
  private readonly historyNodesList: MemHistoryNode[] = [];
  private readonly fileHandleByPath = new Map<string, FileHandle>();
  private readonly fileHandleList: FileHandle[] = [];
  public readonly duIndex = new Map<string, Scope>();
  public writtenBy = '';
  public writtenTime = 0;
  public modified = false;
  public modifiedSinceSim = false;
  public issues: IssueSet | null = null;
  public issuesMeta: IssuesMeta | null = null;
  public issueHistory: IssuesHistoryReader | null = null;
  public issueHistoryWriter: IssuesHistoryWriter | null = null;
  public _issuesMetaRaw: string | null = null;

  constructor() {
    super(null, ScopeTypeT.RESERVEDSCOPE, '', null, null);
  }

  reset(): void {
    this.children.length = 0;
    this.items.length = 0;
    this.historyNodesList.length = 0;
    this.fileHandleByPath.clear();
    this.fileHandleList.length = 0;
    this.duIndex.clear();
    this.writtenBy = '';
    this.writtenTime = 0;
    this.modified = false;
    this.modifiedSinceSim = false;
    this.issues = null;
    this.issuesMeta = null;
    this.issueHistory = null;
    this.issueHistoryWriter = null;
    this._issuesMetaRaw = null;
  }

  createHistoryNode(kind: HistoryNodeKind, name: string, parent: HistoryNode | null = null): HistoryNode {
    const node = new MemHistoryNode(kind, name, null, parent);
    this.historyNodesList.push(node);
    this.modified = true;
    return node;
  }

  numHistoryNodes(): number {
    return this.historyNodesList.length;
  }

  historyNode(i: number): HistoryNode {
    const node = this.historyNodesList[i];
    if (!node) {
      throw new RangeError(`No history node at index ${i}`);
    }
    return node;
  }

  createScope(name: string, du?: Scope): Scope {
    const scope = new MemScope(this, du ? ScopeTypeT.INSTANCE : ScopeTypeT.DU_MODULE, name, null, null);
    scope.designUnit = du ?? null;
    this.addChildScope(scope);
    this.modified = true;
    return scope;
  }

  numScopes(): number {
    return this.numCoverChildren();
  }

  scope(i: number): Scope {
    return this.coverChild(i);
  }

  getFileHandle(path: string): FileHandle {
    const existing = this.fileHandleByPath.get(path);
    if (existing) {
      return existing;
    }
    const handle = new FileHandle(this.fileHandleList.length, path);
    this.fileHandleByPath.set(path, handle);
    this.fileHandleList.push(handle);
    return handle;
  }

  getFileHandles(): readonly FileHandle[] {
    return this.fileHandleList;
  }

  getIssuesMeta(): IssuesMeta | null {
    if (this.issuesMeta === null && this._issuesMetaRaw !== null) {
      this.issuesMeta = IssuesMeta.fromJson(this._issuesMetaRaw);
      this._issuesMetaRaw = null;
    }
    return this.issuesMeta;
  }

  override getIntProperty(prop: IntProperty): number {
    switch (prop) {
      case IntProperty.IS_MODIFIED:
        return this.modified ? 1 : 0;
      case IntProperty.MODIFIED_SINCE_SIM:
        return this.modifiedSinceSim ? 1 : 0;
      case IntProperty.NUM_TESTS:
        return this.historyNodesList.filter((node) => node.kind === HistoryNodeKind.TEST).length;
      default:
        return super.getIntProperty(prop);
    }
  }

  override setIntProperty(prop: IntProperty, val: number): void {
    switch (prop) {
      case IntProperty.IS_MODIFIED:
        this.modified = val !== 0;
        break;
      case IntProperty.MODIFIED_SINCE_SIM:
        this.modifiedSinceSim = val !== 0;
        break;
      default:
        super.setIntProperty(prop, val);
        break;
    }
  }

  async write(path: string): Promise<void> {
    const { NcdbWriter } = await import('../ncdb/ncdbWriter.js');
    await new NcdbWriter().write(path, this);
  }

  async read(path: string): Promise<void> {
    const { NcdbReader } = await import('../ncdb/ncdbReader.js');
    await new NcdbReader().readInto(path, this);
  }
}
