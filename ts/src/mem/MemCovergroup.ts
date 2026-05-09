import { Covergroup } from '../api/Covergroup.js';
import type { FileHandle } from '../api/FileHandle.js';
import type { SourceInfo } from '../api/SourceInfo.js';
import { IntProperty } from '../api/enums/IntProperty.js';
import { ScopeTypeT, type ScopeTypeTValue } from '../api/enums/ScopeTypeT.js';
import { MemScope } from './MemScope.js';

export class MemCovergroup extends MemScope implements Covergroup {
  public perInstance = false;
  public mergeInstances = true;
  public getInstCoverage = false;
  public atLeast = 1;
  public autoBinMax = 0;

  constructor(
    parent: MemScope,
    logicalName: string,
    scopeType: ScopeTypeTValue = ScopeTypeT.COVERGROUP,
    fileHandle: FileHandle | null = null,
    sourceInfo: SourceInfo | null = null,
  ) {
    super(parent, scopeType, logicalName, fileHandle, sourceInfo);
  }

  override getIntProperty(prop: IntProperty): number {
    switch (prop) {
      case IntProperty.CVG_PERINSTANCE:
        return this.perInstance ? 1 : 0;
      case IntProperty.CVG_MERGEINSTANCES:
        return this.mergeInstances ? 1 : 0;
      case IntProperty.CVG_GETINSTCOVERAGE:
        return this.getInstCoverage ? 1 : 0;
      case IntProperty.CVG_ATLEAST:
        return this.atLeast;
      case IntProperty.CVG_AUTOBINMAX:
        return this.autoBinMax;
      default:
        return super.getIntProperty(prop);
    }
  }

  override setIntProperty(prop: IntProperty, val: number): void {
    switch (prop) {
      case IntProperty.CVG_PERINSTANCE:
        this.perInstance = val !== 0;
        break;
      case IntProperty.CVG_MERGEINSTANCES:
        this.mergeInstances = val !== 0;
        break;
      case IntProperty.CVG_GETINSTCOVERAGE:
        this.getInstCoverage = val !== 0;
        break;
      case IntProperty.CVG_ATLEAST:
        this.atLeast = val;
        break;
      case IntProperty.CVG_AUTOBINMAX:
        this.autoBinMax = val;
        break;
      default:
        super.setIntProperty(prop, val);
        break;
    }
  }
}

MemScope.createCovergroupScope = (
  parent: MemScope,
  logicalName: string,
  scopeType: ScopeTypeTValue,
  fileHandle: FileHandle | null,
  sourceInfo: SourceInfo | null,
): MemScope => new MemCovergroup(parent, logicalName, scopeType, fileHandle, sourceInfo);
