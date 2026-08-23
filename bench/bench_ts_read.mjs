/**
 * Read-only benchmark: repeated reads of a pre-built .cdb file.
 * Usage: node bench_ts_read.mjs [path] [reps]
 */
import { NcdbReader } from '../ts/dist/ncdb/ncdbReader.js';
import { ScopeTypeT } from '../ts/dist/api/enums/ScopeTypeT.js';

const path = process.argv[2] ?? '/tmp/bench_large.cdb';
const reps = parseInt(process.argv[3] ?? '5');

function walk(db) {
  function recurse(scope) {
    for (const item of scope.coverItems()) { void item.data; }
    for (const child of scope.scopes(ScopeTypeT.ALL)) { recurse(child); }
  }
  for (const top of db.scopes(ScopeTypeT.ALL)) { recurse(top); }
}

// warm-up
await new NcdbReader().read(path);

let total = 0;
for (let i = 0; i < reps; i++) {
  const t0 = performance.now();
  const db = await new NcdbReader().read(path);
  walk(db);
  total += performance.now() - t0;
}
console.log(`TypeScript read  avg=${(total/reps).toFixed(2)}ms  (×${reps})  file=${path}`);
