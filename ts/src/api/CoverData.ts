export class CoverData {
  constructor(
    public count: bigint = 0n,
    public atLeast: bigint = 1n,
  ) {}

  isCovered(): boolean {
    return this.count >= this.atLeast;
  }
}
