import { mkdir, rm } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { CoverTypeT, SourceInfo, TestData } from '../src/api/index.js';
import { HistoryNodeKind } from '../src/api/enums/HistoryNodeKind.js';
import { MemUCIS } from '../src/mem/index.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

describe('NCDB roundtrip', () => {
  test('writes and reads database with DU links, crosses, toggle bins, and history', async () => {
    const outDir = path.join(__dirname, '.artifacts');
    const outFile = path.join(outDir, 'roundtrip.cdb');
    await mkdir(outDir, { recursive: true });

    const db = new MemUCIS();
    const fh = db.getFileHandle('rtl/top.sv');
    const du = db.createScope('counter');
    const top = db.createScope('counter', du);
    const cg = top.createCovergroupDef('cg_addr', fh, new SourceInfo(fh.fileId, 42, 0));
    const cpAddr = cg.createCoverpoint('cp_addr', fh, new SourceInfo(fh.fileId, 43, 0));
    const cpData = cg.createCoverpoint('cp_data');
    cpAddr.createBin('low', CoverTypeT.CVGBIN, 5n, 2n);
    cpAddr.createBin('mid', CoverTypeT.CVGBIN, 0n, 2n);
    cpAddr.createBin('high', CoverTypeT.CVGBIN, 1n, 2n);
    cpData.createBin('zero', CoverTypeT.CVGBIN, 2n, 1n);
    cpData.createBin('nonzero', CoverTypeT.CVGBIN, 4n, 1n);
    const cross = cg.createCross('x_addr_data', null, null, [cpAddr, cpData]);
    cross.createBin('addr_x_data', CoverTypeT.DEFAULTBIN, 3n, 1n);
    const toggle = top.createBranch('sig_valid');
    toggle.createToggleBin('0 -> 1', 3n, 0n);
    toggle.createToggleBin('1 -> 0', 2n, 0n);
    const history = db.createHistoryNode(HistoryNodeKind.TEST, 'smoke');
    history.testData = new TestData({ userName: 'tester', seed: '1234', toolCategory: 'sim' });

    await db.write(outFile);

    const loaded = new MemUCIS();
    await loaded.read(outFile);

    expect(loaded.numScopes()).toBe(2);
    const loadedDu = loaded.scope(0);
    const loadedTop = loaded.scope(1);
    expect(loadedDu.logicalName).toBe('counter');
    expect(loadedTop.logicalName).toBe('counter');
    expect(loadedTop.designUnit).toBe(loadedDu);
    expect(loaded.duIndex.get('counter')).toBe(loadedDu);
    expect(loaded.getFileHandles().map((handle) => handle.filePath)).toEqual(['rtl/top.sv']);

    const loadedCg = loadedTop.coverChild(0);
    expect(loadedCg.logicalName).toBe('cg_addr');
    expect(loadedCg.sourceInfo?.line).toBe(42);
    const loadedCpAddr = loadedCg.coverChild(0);
    const loadedCpData = loadedCg.coverChild(1);
    const loadedCross = loadedCg.coverChild(2) as typeof cross;
    expect(loadedCpAddr.logicalName).toBe('cp_addr');
    expect(loadedCpAddr.sourceInfo?.line).toBe(43);
    expect(loadedCpAddr.numCoverItems()).toBe(3);
    expect(loadedCpAddr.coverItem(0).name).toBe('low');
    expect(loadedCpAddr.coverItem(0).data.count).toBe(5n);
    expect(loadedCpAddr.coverItem(2).data.atLeast).toBe(2n);
    expect(loadedCross.getCrossedPoints().map((scope) => scope.logicalName)).toEqual(['cp_addr', 'cp_data']);
    expect(loadedCross.coverItem(0).data.count).toBe(3n);
    expect(loadedCpData.coverItem(1).data.count).toBe(4n);

    const loadedToggle = loadedTop.coverChild(1);
    expect(loadedToggle.coverItem(0).data.count).toBe(3n);
    expect(loadedToggle.coverItem(1).data.count).toBe(2n);
    expect(loaded.numHistoryNodes()).toBe(1);
    expect(loaded.historyNode(0).testName).toBe('smoke');
    expect(loaded.historyNode(0).testData?.seed).toBe('1234');

    await rm(outFile, { force: true });
  });
});
