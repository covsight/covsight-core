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
import { IntProperty } from '../../../ts/dist/api/enums/IntProperty.js';
import { SourceInfo } from '../../../ts/dist/api/SourceInfo.js';
import { TestData } from '../../../ts/dist/api/TestData.js';
import { IssueSet, IssueSpec } from '../../../ts/dist/ncdb/IssueSet.js';
import {
  SEV_HIGH, SEV_LOW, SEV_MEDIUM, SEV_CRITICAL,
  KIND_DESIGN_BUG, KIND_TEST_BUG,
  STATE_OPEN, STATE_IN_PROGRESS, STATE_RESOLVED, STATE_CLOSED,
  RES_NONE, RES_FIXED, RES_WONT_FIX,
  LINK_BLOCKED_BY, LINK_CAUSED_BY, LINK_RELATED,
} from '../../../ts/dist/ncdb/constants.js';
import { IssuesMeta } from '../../../ts/dist/ncdb/IssuesMeta.js';

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
  const srcFile  = scope.fileHandle?.filePath ?? null;
  const srcLine  = scope.sourceInfo?.line  ?? 0;
  const srcToken = scope.sourceInfo?.token ?? 0;
  let crossedPoints = null;
  if (scope.scopeType === ScopeTypeT.CROSS && typeof scope.getCrossedPoints === 'function') {
    const pts = scope.getCrossedPoints();
    if (pts && pts.length > 0) {
      crossedPoints = pts.map(cp => cp.logicalName);
    }
  }
  return {
    children,
    goal: scope.getIntProperty(IntProperty.SCOPE_GOAL),
    items,
    name: scope.logicalName,
    source_file:  srcFile,
    source_line:  srcLine,
    source_token: srcToken,
    type: Number(scope.scopeType),
    weight: scope.getIntProperty(IntProperty.SCOPE_WEIGHT),
    crossed_points: crossedPoints,
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
      kind:          kindNames[node.kind] ?? String(node.kind),
      name:          node.testName,
      user_name:     node.testData?.userName     ?? null,
      seed:          node.testData?.seed         ?? null,
      tool_category: node.testData?.toolCategory ?? null,
      comment:       node.testData?.comment      ?? null,
    });
  }

  const doc = { format: 'ncdb-dump-v1', history, scopes };

  const issuesList = [];
  const waiverLinks = [];
  const tpLinks = [];
  const covLinks = [];
  if (db.issues) {
    for (const handle of db.issues.issues()) {
      issuesList.push({
        id:         handle.id,
        ext:        handle.ext,
        severity:   handle.severity,
        kind:       handle.kind,
        state:      handle.state,
        resolution: handle.resolution,
      });
      for (const lk of db.issues.waiversForIssue(handle)) {
        waiverLinks.push({ waiver_id: lk.waiverId, issue_id: lk.issueId });
      }
      for (const lk of db.issues.testpointsForIssue(handle)) {
        tpLinks.push({ tp_name: lk.testpointName, issue_id: lk.issueId, link_type: lk.linkType });
      }
    }
    for (const lk of db.issues.coverageLinks()) {
      covLinks.push({ scope_path: lk.scopePath, bin_name: lk.binName,
                      issue_id: lk.issueId, link_type: lk.linkType });
    }
  }
  doc.issues = issuesList;
  doc.waiver_links = waiverLinks;
  doc.testpoint_links = tpLinks;
  doc.coverage_links = covLinks;

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
    const h1 = db.createHistoryNode(HistoryNodeKind.TEST, 'smoke');
    h1.testData = new TestData({ userName: 'alice', seed: '42', toolCategory: 'sim', comment: 'smoke run' });
    const h2 = db.createHistoryNode(HistoryNodeKind.TEST, 'regression');
    h2.testData = new TestData({ userName: 'bob', seed: '99', toolCategory: 'sim', comment: 'full regression' });
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

  unicode_names: async (db) => {
    const cg = db.createCovergroupDef('cg_αβγ');
    const cp = cg.createCoverpoint('cp_café');
    cp.createBin('bin_日本語', CoverTypeT.CVGBIN, 3n, 1n);
  },

  large_count: async (db) => {
    const cg = db.createCovergroupDef('cg_large');
    const cp = cg.createCoverpoint('cp0');
    cp.createBin('big_bin', CoverTypeT.CVGBIN, 4294967296n, 1n);
  },
  weight_goal: async (db) => {
    const cg = db.createCovergroupDef('cg_wg');
    cg.setIntProperty(IntProperty.SCOPE_WEIGHT, 2);
    cg.setIntProperty(IntProperty.SCOPE_GOAL, 80);
    const cp = cg.createCoverpoint('cp0');
    cp.createBin('b0', CoverTypeT.CVGBIN, 1n, 1n);
  },

  issues_minimal: async (db) => {
    const issues = new IssueSet();
    issues.addIssue(new IssueSpec({
      id: 'I-001', ext: 'EXT-1',
      severity: SEV_HIGH, kind: KIND_DESIGN_BUG,
      state: STATE_OPEN, resolution: RES_NONE,
      createdAt: 1000, updatedAt: 1000, syncedAt: 0,
    }));
    issues.addIssue(new IssueSpec({
      id: 'I-002', ext: '',
      severity: SEV_LOW, kind: KIND_TEST_BUG,
      state: STATE_CLOSED, resolution: RES_FIXED,
      createdAt: 2000, updatedAt: 2001, syncedAt: 0,
    }));
    issues.addWaiverLink('W-001', 'I-001');
    db.issues = issues;
  },

  issues_full: async (db) => {
    // Coverage data coexisting with issues
    const cg = db.createCovergroupDef('cg_iss');
    const cp = cg.createCoverpoint('cp0');
    cp.createBin('b0', CoverTypeT.CVGBIN, 5n, 1n);

    const issues = new IssueSet();
    const h1 = issues.addIssue(new IssueSpec({
      id: 'I-001', ext: 'BUG-42',
      severity: SEV_CRITICAL, kind: KIND_DESIGN_BUG,
      state: STATE_IN_PROGRESS, resolution: RES_NONE,
      createdAt: 1000, updatedAt: 1100, syncedAt: 0,
    }));
    const h2 = issues.addIssue(new IssueSpec({
      id: 'I-002', ext: '',
      severity: SEV_MEDIUM, kind: KIND_TEST_BUG,
      state: STATE_RESOLVED, resolution: RES_FIXED,
      createdAt: 2000, updatedAt: 2500, syncedAt: 0,
    }));
    issues.addIssue(new IssueSpec({
      id: 'I-003', ext: 'SPEC-7',
      severity: SEV_LOW, kind: KIND_TEST_BUG,
      state: STATE_OPEN, resolution: RES_NONE,
      createdAt: 3000, updatedAt: 3000, syncedAt: 0,
    }));
    const h4 = issues.addIssue(new IssueSpec({
      id: 'I-004', ext: '',
      severity: SEV_HIGH, kind: KIND_DESIGN_BUG,
      state: STATE_CLOSED, resolution: RES_WONT_FIX,
      createdAt: 4000, updatedAt: 4100, syncedAt: 0,
    }));
    issues.addWaiverLink('W-001', 'I-001');
    issues.addWaiverLink('W-002', 'I-003');
    issues.addTestpointLink('tp_smoke', 'I-002', LINK_BLOCKED_BY);
    issues.addTestpointLink('tp_reg',   'I-004', LINK_CAUSED_BY);
    issues.addCoverageLink('cg_iss', 'b0', 'I-001', LINK_RELATED);
    db.issues = issues;

    const meta = new IssuesMeta();
    meta.setTitle(h1, 'Critical design bug in arbiter');
    meta.setUrl(h1,   'https://bugs.example.com/42');
    meta.setTitle(h2, 'Test flakiness in smoke suite');
    // h3 intentionally no meta
    meta.setTitle(h4, "Won't fix \u2014 by design");
    db.issuesMeta = meta;
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
