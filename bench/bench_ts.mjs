/**
 * Benchmark: TypeScript UCIS write + read roundtrip.
 * Usage: node --experimental-vm-modules bench_ts.mjs [N_GROUPS] [N_POINTS] [N_BINS] [N_TESTS]
 */
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { rm } from 'node:fs/promises';
import { MemUCIS } from '../ts/dist/mem/MemUCIS.js';
import { NcdbWriter } from '../ts/dist/ncdb/ncdbWriter.js';
import { NcdbReader } from '../ts/dist/ncdb/ncdbReader.js';
import { ScopeTypeT } from '../ts/dist/api/enums/ScopeTypeT.js';
import { CoverTypeT } from '../ts/dist/api/enums/CoverTypeT.js';
import { HistoryNodeKind } from '../ts/dist/api/enums/HistoryNodeKind.js';

const N_GROUPS = parseInt(process.argv[2] ?? '50');
const N_POINTS = parseInt(process.argv[3] ?? '10');
const N_BINS   = parseInt(process.argv[4] ?? '20');
const N_TESTS  = parseInt(process.argv[5] ?? '5');

function buildDb() {
  const db = new MemUCIS();
  db.getFileHandle('bench.sv');
  const du  = db.createScope('bench_du');                          // DU_MODULE
  const top = db.createScope('top', du);                          // INSTANCE → du
  for (let g = 0; g < N_GROUPS; g++) {
    const cg = top.createCovergroupDef(`cg_${g}`);
    for (let p = 0; p < N_POINTS; p++) {
      const cp = cg.createCoverpoint(`cp_${p}`);
      for (let b = 0; b < N_BINS; b++) {
        cp.createBin(`bin_${b}`, CoverTypeT.CVGBIN, BigInt(b % 10), 1n);
      }
    }
  }
  for (let t = 0; t < N_TESTS; t++) {
    db.createHistoryNode(HistoryNodeKind.TEST, `test_${t}`);
  }
  return db;
}

async function main() {
  const path = join(tmpdir(), `bench_ts_${process.pid}.cdb`);

  // Build
  const t0 = performance.now();
  const db = buildDb();
  const tBuild = performance.now() - t0;

  // Write
  const t1 = performance.now();
  await new NcdbWriter().write(path, db);
  const tWrite = performance.now() - t1;

  const { size: fsize } = await import('node:fs').then(m =>
    Promise.resolve(m.statSync(path))
  );

  // Read
  const t2 = performance.now();
  await new NcdbReader().read(path);
  const tRead = performance.now() - t2;

  await rm(path);

  const totalBins = N_GROUPS * N_POINTS * N_BINS;
  console.log(
    `TypeScript  build=${tBuild.toFixed(1)}ms  write=${tWrite.toFixed(1)}ms  read=${tRead.toFixed(1)}ms  ` +
    `total=${(tBuild + tWrite + tRead).toFixed(1)}ms  ` +
    `file=${Math.floor(fsize / 1024)}KB  bins=${totalBins}`
  );
}

main().catch(e => { console.error(e); process.exit(1); });
