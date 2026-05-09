export class SourceInfo {
  constructor(
    public readonly fileId: number,
    public readonly line: number,
    public readonly token: number = 0,
  ) {}
}
