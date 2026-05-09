import { IntProperty } from './enums/IntProperty.js';

export abstract class Obj {
  abstract getIntProperty(prop: IntProperty): number;
  abstract setIntProperty(prop: IntProperty, val: number): void;
}
