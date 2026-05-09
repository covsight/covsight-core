export class TestData {
  userName = '';
  testPlanName = '';
  date = '';
  simElapsed = '';
  runCwd = '';
  comment = '';
  userName2 = '';
  toolCategory = '';
  compulsory = false;
  date2 = '';
  simCmd = '';
  elaborCmd = '';
  seed = '';
  goldenLog = '';
  randstate = '';
  attributes = new Map<string, string>();

  constructor(init?: Partial<TestData>) {
    if (init) {
      Object.assign(this, init);
      if (init.attributes) {
        this.attributes = new Map<string, string>(init.attributes);
      }
    }
  }
}
