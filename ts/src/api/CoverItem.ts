import { CoverData } from './CoverData.js';

export abstract class CoverItem {
  constructor(
    public readonly coverType: number,
    public readonly name: string,
    public data: CoverData,
  ) {}
}
