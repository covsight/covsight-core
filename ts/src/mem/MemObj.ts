import { Obj } from '../api/Obj.js';
import { IntProperty } from '../api/enums/IntProperty.js';

export class MemObj extends Obj {
  private readonly props = new Map<IntProperty, number>();

  getIntProperty(prop: IntProperty): number {
    return this.props.get(prop) ?? 0;
  }

  setIntProperty(prop: IntProperty, val: number): void {
    this.props.set(prop, val);
  }
}
