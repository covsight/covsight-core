import { CoverData } from '../api/CoverData.js';
import { CoverItem } from '../api/CoverItem.js';
import { CoverIndex } from '../api/CoverIndex.js';

export class MemCoverItem extends CoverItem {
  constructor(
    coverType: number,
    name: string,
    data: CoverData,
    public readonly index: CoverIndex,
  ) {
    super(coverType, name, data);
  }
}
