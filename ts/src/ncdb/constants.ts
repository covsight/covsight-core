import { CoverTypeT } from '../api/enums/CoverTypeT.js';
import { ScopeTypeT } from '../api/enums/ScopeTypeT.js';
import { SourceT } from '../api/enums/SourceT.js';

export const NCDB_FORMAT = 'NCDB';
export const NCDB_VERSION = '2.0';
export const NCDB_GENERATOR = 'covsight-ts';
export const HISTORY_FORMAT_V1 = 'v1';
export const HISTORY_FORMAT_V2 = 'v2';
export const MEMBER_MANIFEST = 'manifest.json';
export const MEMBER_STRINGS = 'strings.bin';
export const MEMBER_SCOPE_TREE = 'scope_tree.bin';
export const MEMBER_COUNTS = 'counts.bin';
export const MEMBER_HISTORY = 'history.json';
export const MEMBER_SOURCES = 'sources.json';
export const MEMBER_ATTRS = 'attrs.bin';
export const MEMBER_TAGS = 'tags.json';
export const MEMBER_TOGGLE = 'toggle.bin';
export const MEMBER_FSM = 'fsm.bin';
export const MEMBER_CROSS = 'cross.bin';
export const MEMBER_FORMAL = 'formal.bin';
export const MEMBER_DESIGN_UNITS = 'design_units.json';
export const MEMBER_PROPERTIES = 'properties.json';
export const MEMBER_CONTRIB_DIR = 'contrib/';
export const MEMBER_TEST_REGISTRY = 'test_registry.bin';
export const MEMBER_TEST_STATS = 'test_stats.bin';
export const MEMBER_BUCKET_INDEX = 'history/bucket_index.bin';
export const MEMBER_CONTRIB_INDEX = 'contrib_index.bin';
export const MEMBER_SQUASH_LOG = 'squash_log.bin';
export const MEMBER_TESTPLAN = 'testplan.json';
export const MEMBER_WAIVERS = 'waivers.json';
export const HISTORY_BUCKET_DIR = 'history/';
export const HISTORY_BUCKET_MAX_RECORDS = 10_000;
export const HIST_STATUS_OK = 0;
export const HIST_STATUS_FAIL = 1;
export const HIST_STATUS_ERROR = 2;
export const HIST_STATUS_FATAL = 3;
export const HIST_STATUS_COMPILE = 4;
export const HIST_FLAG_SEED_IS_HASH = 0x01;
export const HIST_FLAG_IS_RERUN = 0x02;
export const HIST_FLAG_HAS_COVERAGE = 0x04;
export const HIST_FLAG_WAS_SQUASHED = 0x08;
export const SCOPE_MARKER_REGULAR = 0x00;
export const SCOPE_MARKER_TOGGLE_PAIR = 0x01;
export const PRESENCE_FLAGS = 0x01;
export const PRESENCE_SOURCE = 0x02;
export const PRESENCE_WEIGHT = 0x04;
export const PRESENCE_AT_LEAST = 0x08;
export const PRESENCE_CVG_OPTS = 0x10;
export const PRESENCE_GOAL = 0x20;
export const PRESENCE_SOURCE_TYPE = 0x40;
export const COVER_PRESENCE_AT_LEAST = 0x01;
export const COUNTS_MODE_UINT32 = 0;
export const COUNTS_MODE_VARINT = 1;
export const TOGGLE_BIN_0_TO_1 = '0 -> 1';
export const TOGGLE_BIN_1_TO_0 = '1 -> 0';
export const COVER_TYPE_DEFAULTS = new Map<number, readonly [number, bigint, number]>([
  [CoverTypeT.TOGGLEBIN, [0x01, 0n, 1]],
  [CoverTypeT.STMTBIN, [0x01, 0n, 1]],
  [CoverTypeT.BRANCHBIN, [0x01, 0n, 1]],
  [CoverTypeT.CONDBIN, [0x01, 0n, 1]],
  [CoverTypeT.EXPRBIN, [0x01, 0n, 1]],
  [CoverTypeT.FSMBIN, [0x01, 0n, 1]],
  [CoverTypeT.CVGBIN, [0x19, 1n, 1]],
  [CoverTypeT.DEFAULTBIN, [0x01, 0n, 1]],
  [CoverTypeT.IGNOREBIN, [0x01, 0n, 1]],
  [CoverTypeT.ILLEGALBIN, [0x01, 0n, 1]],
  [CoverTypeT.BLOCKBIN, [0x01, 0n, 1]],
  [CoverTypeT.COVERBIN, [0x01, 0n, 1]],
  [CoverTypeT.ASSERTBIN, [0x01, 0n, 1]],
  [CoverTypeT.PASSBIN, [0x01, 0n, 1]],
  [CoverTypeT.FAILBIN, [0x01, 0n, 1]],
]);
export const SCOPE_TO_COVER_TYPE = new Map<bigint, number>([
  [ScopeTypeT.TOGGLE, CoverTypeT.TOGGLEBIN],
  [ScopeTypeT.BRANCH, CoverTypeT.TOGGLEBIN],
  [ScopeTypeT.EXPR, CoverTypeT.BRANCHBIN],
  [ScopeTypeT.COND, CoverTypeT.CONDBIN],
  [ScopeTypeT.BLOCK, CoverTypeT.STMTBIN],
  [ScopeTypeT.COVBLOCK, CoverTypeT.BLOCKBIN],
  [ScopeTypeT.FSM, CoverTypeT.FSMBIN],
  [ScopeTypeT.FSM_STATES, CoverTypeT.FSMBIN],
  [ScopeTypeT.FSM_TRANS, CoverTypeT.FSMBIN],
  [ScopeTypeT.COVERPOINT, CoverTypeT.CVGBIN],
  [ScopeTypeT.CROSS, CoverTypeT.DEFAULTBIN],
  [ScopeTypeT.CVGBINSCOPE, CoverTypeT.CVGBIN],
  [ScopeTypeT.ILLEGALBINSCOPE, CoverTypeT.ILLEGALBIN],
  [ScopeTypeT.IGNOREBINSCOPE, CoverTypeT.IGNOREBIN],
  [ScopeTypeT.COVER, CoverTypeT.COVERBIN],
  [ScopeTypeT.ASSERT, CoverTypeT.ASSERTBIN],
]);
export const DEFAULT_SCOPE_FLAGS = 0;
export const DEFAULT_SCOPE_WEIGHT = 1;
export const DEFAULT_SCOPE_GOAL = -1;
export const DEFAULT_SOURCE_TYPE = SourceT.NONE;

export const MEMBER_ISSUES = 'issues.bin';
export const MEMBER_ISSUES_META = 'issues_meta.json';
export const MEMBER_ISSUES_HISTORY = 'issues_history.bin';

export const SEV_INFO = 0;
export const SEV_LOW = 1;
export const SEV_MEDIUM = 2;
export const SEV_HIGH = 3;
export const SEV_CRITICAL = 4;

export const KIND_DESIGN_BUG = 0;
export const KIND_TEST_BUG = 1;
export const KIND_INFRA = 2;
export const KIND_SPEC_GAP = 3;

export const STATE_OPEN = 0;
export const STATE_IN_PROGRESS = 1;
export const STATE_RESOLVED = 2;
export const STATE_CLOSED = 3;
export const STATE_WONTFIX = 4;

export const RES_NONE = 0;
export const RES_FIXED = 1;
export const RES_WONT_FIX = 2;
export const RES_DUPLICATE = 3;
export const RES_NOT_A_BUG = 4;

export const LINK_BLOCKED_BY = 0;
export const LINK_CAUSED_BY = 1;
export const LINK_RELATED = 2;
