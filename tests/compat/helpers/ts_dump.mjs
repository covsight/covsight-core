/**
 * ts_dump.mjs — TypeScript NCDB dump helper for cross-compat tests.
 *
 * Usage:
 *   node ts_dump.mjs read <file>              → canonical JSON to stdout
 *   node ts_dump.mjs write <scenario> <file>  → write scenario .cdb
 *
 * Import paths are relative to tests/compat/helpers/ → ts/dist/
 */

import { MemUCIS } from '../../../ts/dist/mem/index.js';
import { CoverTypeT } from '../../../ts/dist/api/enums/CoverTypeT.js';
import { ScopeTypeT } from '../../../ts/dist/api/enums/ScopeTypeT.js';
import { HistoryNodeKind } from '../../../ts/dist/api/enums/HistoryNodeKind.js';
import { SourceInfo } from '../../../ts/dist/api/SourceInfo.js';

// ---------------------------------------------------------------------------
// READ: load a .cdb and emit canonical JSON
// ---------------------------------------------------------------------------

function scopeToJson(scope) {
  const children = [];
  for (const child of scope.scopes(ScopeTypeT.ALL)) {
    children.push(scopeToJson(child));
  }
  const items = [];
  for (const item of scope.coverItems()) {
    items.push({
      atLeast: Number(item.data.atLeast),
      count: Number(item.data.count),
      coverType: Number(item.coverType),
      name: item.name,
    });
  }
  return {
    children,
    items,
    name: scope.logicalName,
    type: Number(scope.scopeType),
  };
}

async function cmdRead(filePath) {
  const db = new MemUCIS();
  await db.read(filePath);

  const scopes = [];
  for (const top of db.scopes(ScopeTypeT.ALL)) {
    scopes.push(scopeToJson(top));
  }

  const kindNames = { [HistoryNodeKind.TEST]: 'TEST', [HistoryNodeKind.MERGE]: 'MERGE' };
  const history = [];
  for (let i = 0; i < db.numHistoryNodes(); i++) {
    const node = db.historyNode(i);
    history.push({
      kind: kindNames[node.kind] ?? String(node.kind),
      name: node.testName,
    });
  }

  const doc = { format: 'ncdb-dump-v1', history, scopes };
  process.stdout.write(JSON.stringify(doc, null, 2) + '\n');
}

// ---------------------------------------------------------------------------
// WRITE: build a named scenario and serialize to .cdb
// ---------------------------------------------------------------------------

const scenarios = {

  empty: async (db) => {
    // No scopes, no history.
  },

  minimal: async (db) => {
    const cg = db.createCovergroupDef('cg_minimal');
    const cp = cg.createCoverpoint('cp0');
    cp.createBin('bin0', CoverTypeT.CVGBIN, 7n, 1n);
  },

  basic: async (db) => {
    const fh = db.getFileHandle('rtl/top.sv');
    const du = db.createScope('top');
    const inst = db.createScope('top', du);
    for (let g = 0; g < 2; g++) {
      const cg = inst.createCovergroupDef(`cg_${g}`, fh);
      for (let p = 0; p < 3; p++) {
        const cp = cg.createCoverpoint(`cp_${p}`);
        for (let b = 0; b < 5; b++) {
          cp.createBin(`bin_${b}`, CoverTypeT.CVGBIN, BigInt(g * 15 + p * 5 + b), 1n);
        }
      }
    }
  },

  at_least: async (db) => {
    const cg = db.createCovergroupDef('cg_al');
    const cp = cg.createCoverpoint('cp0');
    cp.createBin('lo', CoverTypeT.CVGBIN, 0n, 2n);
    cp.createBin('hi', CoverTypeT.CVGBIN, 5n, 2n);
  },

  toggle: async (db) => {
    const fh = db.getFileHandle('rtl/top.sv');
    const du = db.createScope('top');
    const inst = db.createScope('top', du);
    const br = inst.createBranch('sig_valid', fh);
    br.createToggleBin('0 -> 1', 3n, 0n);
    br.createToggleBin('1 -> 0', 2n, 0n);
  },

  source_info: async (db) => {
    const fh = db.getFileHandle('rtl/foo.sv');
    const du = db.createScope('foo');
    const inst = db.createScope('foo', du);
    const cg = inst.createCovergroupDef('cg_src', fh, new SourceInfo(fh.fileId, 10, 0));
    const cp = cg.createCoverpoint('cp0', fh, new SourceInfo(fh.fileId, 11, 5));
    cp.createBin('b0', CoverTypeT.CVGBIN, 1n, 1n);
  },

  history: async (db) => {
    const cg = db.createCovergroupDef('cg_h');
    const cp = cg.createCoverpoint('cp0');
    cp.createBin('b0', CoverTypeT.CVGBIN, 1n, 1n);
    db.createHistoryNode(HistoryNodeKind.TEST, 'smoke');
    db.createHistoryNode(HistoryNodeKind.TEST, 'regression');
  },

  cross: async (db) => {
    const cg = db.createCovergroupDef('cg_cross');
    const cpA = cg.createCoverpoint('cp_a');
    cpA.createBin('a0', CoverTypeT.CVGBIN, 1n, 1n);
    cpA.createBin('a1', CoverTypeT.CVGBIN, 2n, 1n);
    const cpB = cg.createCoverpoint('cp_b');
    cpB.createBin('b0', CoverTypeT.CVGBIN, 3n, 1n);
    cpB.createBin('b1', CoverTypeT.CVGBIN, 4n, 1n);
    const x = cg.createCross('x_ab', null, null, [cpA, cpB]);
    x.createBin('a0_x_b0', CoverTypeT.DEFAULTBIN, 5n, 1n);
  },

  deep: async (db) => {
    // 5-level nesting; cover items only at leaf
    let parent = db.createCovergroupDef('level_0');
    for (let d = 1; d <= 4; d++) {
      parent = parent.createCoverpoint(`level_${d}`);
    }
    parent.createBin('leaf_bin', CoverTypeT.CVGBIN, 42n, 1n);
  },

  full: async (db) => {
    // Combine all non-mutually-exclusive scenarios
    await scenarios.basic(db);
    await scenarios.at_least(db);
    await scenarios.toggle(db);
    await scenarios.history(db);
    await scenarios.cross(db);
    await scenarios.deep(db);
  },
};

async function cmdWrite(scenarioName, filePath) {
  const fn = scenarios[scenarioName];
  if (!fn) {
    console.error(`Unknown scenario: ${scenarioName}`);
    console.error(`Available: ${Object.keys(scenarios).join(', ')}`);
    process.exit(1);
  }
  const db = new MemUCIS();
  await fn(db);
  await db.write(filePath);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

const [, , cmd, ...rest] = process.argv;

if (cmd === 'read') {
  const [filePath] = rest;
  if (!filePath) { console.error('Usage: ts_dump.mjs read <file>'); process.exit(1); }
  await cmdRead(filePath);
} else if (cmd === 'write') {
  const [scenarioName, filePath] = rest;
  if (!scenarioName || !filePath) {
    console.error('Usage: ts_dump.mjs write <scenario> <file>');
    process.exit(1);
  }
  await cmdWrite(scenarioName, filePath);
} else {
  console.error('Usage: ts_dump.mjs <read|write> ...');
  process.exit(1);
}
