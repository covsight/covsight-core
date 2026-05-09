import type { Coverpoint } from './Coverpoint.js';
import { Coverpoint as CoverpointScope } from './Coverpoint.js';

export abstract class Cross extends CoverpointScope {
  abstract getCrossedPoints(): readonly Coverpoint[];
  abstract setCrossedPoints(points: readonly Coverpoint[]): void;
}
