import type { CoverItem } from './CoverItem.js';
import { Scope } from './Scope.js';

export abstract class Coverpoint extends Scope {
  abstract createBin(name: string, binType: number, count: bigint, atLeast: bigint): CoverItem;
  abstract numBins(): number;
  abstract bin(i: number): CoverItem;
}
