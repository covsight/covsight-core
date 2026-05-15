"""Unit tests for src/ucis/ncdb/testplan_closure.py."""
from __future__ import annotations

import pytest

from covsight.core.ncdb.testplan import Testplan, Testpoint
from covsight.core.ncdb.testplan_closure import (
    TPStatus,
    TestpointResult,
    compute_closure,
    stage_gate_status,
)


# ── stub DB ───────────────────────────────────────────────────────────────────

class _FakeStats:
    def __init__(self, pass_count, fail_count):
        self.pass_count = pass_count
        self.fail_count = fail_count


class _FakeRegistry:
    def __init__(self, names):
        self._names = names


class _FakeDB:
    """Minimal NcdbUCIS-like db using the v2 history path."""

    def __init__(self, runs: dict):
        """runs: {name: (pass_count, fail_count)}"""
        names = list(runs.keys())
        self._test_registry = _FakeRegistry(names)
        self._test_stats = _FakeStatsTable(runs)

    def historyNodes(self, _kind):
        return []


class _FakeStatsTable:
    def __init__(self, runs):
        self._runs = runs
        self._names = list(runs.keys())

    def get(self, nid):
        name = self._names[nid]
        p, f = self._runs[name]
        return _FakeStats(p, f)


def _db_with(**kwargs):
    """Helper: _db_with(uart_smoke=(3,1)) → fake db."""
    return _FakeDB(kwargs)


# ── plan helpers ──────────────────────────────────────────────────────────────

def _make_plan(*testpoints) -> Testplan:
    plan = Testplan()
    for tp in testpoints:
        plan.add_testpoint(tp)
    return plan


# ── compute_closure ───────────────────────────────────────────────────────────

class TestComputeClosure:
    def test_closed_when_all_pass(self):
        plan = _make_plan(Testpoint(name="tp", stage="V1", tests=["uart_smoke"]))
        db = _db_with(uart_smoke=(5, 0))
        results = compute_closure(plan, db)
        assert results[0].status == TPStatus.CLOSED

    def test_failing_when_all_fail(self):
        plan = _make_plan(Testpoint(name="tp", stage="V1", tests=["t"]))
        db = _db_with(t=(0, 3))
        results = compute_closure(plan, db)
        assert results[0].status == TPStatus.FAILING

    def test_partial_when_mixed(self):
        plan = _make_plan(Testpoint(name="tp", stage="V1", tests=["t"]))
        db = _db_with(t=(2, 1))
        results = compute_closure(plan, db)
        assert results[0].status == TPStatus.PARTIAL

    def test_not_run_when_absent(self):
        plan = _make_plan(Testpoint(name="tp", stage="V1", tests=["t"]))
        db = _db_with()
        results = compute_closure(plan, db)
        assert results[0].status == TPStatus.NOT_RUN

    def test_na_testpoint(self):
        plan = _make_plan(Testpoint(name="tp", stage="V1", na=True))
        db = _db_with()
        results = compute_closure(plan, db)
        assert results[0].status == TPStatus.NA

    def test_unimplemented_empty_tests(self):
        plan = _make_plan(Testpoint(name="tp", stage="V1", tests=[]))
        db = _db_with()
        results = compute_closure(plan, db)
        assert results[0].status == TPStatus.UNIMPLEMENTED

    def test_wildcard_pattern_matches(self):
        plan = _make_plan(Testpoint(name="tp", stage="V1", tests=["uart_*"]))
        db = _db_with(uart_loopback=(3, 0), uart_reset=(2, 0))
        results = compute_closure(plan, db)
        assert results[0].status == TPStatus.CLOSED
        assert len(results[0].matched_tests) == 2

    def test_seed_strip_matches(self):
        plan = _make_plan(Testpoint(name="tp", stage="V1", tests=["uart_smoke_42"]))
        db = _db_with(uart_smoke=(4, 0))   # DB has stripped name
        results = compute_closure(plan, db)
        assert results[0].status == TPStatus.CLOSED

    def test_pass_fail_counts_accurate(self):
        plan = _make_plan(Testpoint(name="tp", stage="V1",
                                    tests=["a", "b"]))
        db = _db_with(a=(3, 1), b=(2, 2))
        results = compute_closure(plan, db)
        r = results[0]
        assert r.pass_count == 5
        assert r.fail_count == 3

    def test_multiple_testpoints_independent(self):
        plan = _make_plan(
            Testpoint(name="tp1", stage="V1", tests=["a"]),
            Testpoint(name="tp2", stage="V2", tests=["b"]),
        )
        db = _db_with(a=(5, 0), b=(0, 2))
        results = compute_closure(plan, db)
        assert results[0].status == TPStatus.CLOSED
        assert results[1].status == TPStatus.FAILING

    def test_result_order_matches_testplan(self):
        plan = _make_plan(
            Testpoint(name="first",  stage="V1", tests=["x"]),
            Testpoint(name="second", stage="V1", tests=["y"]),
        )
        db = _db_with(x=(1, 0), y=(1, 0))
        results = compute_closure(plan, db)
        assert results[0].testpoint.name == "first"
        assert results[1].testpoint.name == "second"


# ── stage_gate_status ─────────────────────────────────────────────────────────

class TestStageGateStatus:
    def _plan_and_results(self, statuses: dict) -> tuple:
        plan = Testplan()
        for name, (stage, st) in statuses.items():
            plan.add_testpoint(Testpoint(name=name, stage=stage,
                                         tests=["t"] if st != TPStatus.UNIMPLEMENTED else []))
        results = []
        for tp in plan.testpoints:
            st = statuses[tp.name][1]
            results.append(TestpointResult(tp, st, [], 1 if st == TPStatus.CLOSED else 0, 0))
        return plan, results

    def test_gate_passes_all_closed(self):
        plan, results = self._plan_and_results({
            "v1_tp": ("V1", TPStatus.CLOSED),
            "v2_tp": ("V2", TPStatus.CLOSED),
        })
        gate = stage_gate_status(results, "V2", plan)
        assert gate["passed"] is True
        assert gate["blocking"] == []

    def test_gate_fails_if_lower_stage_not_closed(self):
        plan, results = self._plan_and_results({
            "v1_tp": ("V1", TPStatus.FAILING),
            "v2_tp": ("V2", TPStatus.CLOSED),
        })
        gate = stage_gate_status(results, "V2", plan)
        assert gate["passed"] is False
        assert any(r.testpoint.name == "v1_tp" for r in gate["blocking"])

    def test_gate_passes_na_testpoints_ignored(self):
        plan, results = self._plan_and_results({
            "v1_tp": ("V1", TPStatus.CLOSED),
            "v1_na": ("V1", TPStatus.NA),
        })
        gate = stage_gate_status(results, "V1", plan)
        assert gate["passed"] is True

    def test_gate_ignores_higher_stage(self):
        plan, results = self._plan_and_results({
            "v1_tp": ("V1", TPStatus.CLOSED),
            "v3_tp": ("V3", TPStatus.FAILING),   # V3 not evaluated for V2 gate
        })
        gate = stage_gate_status(results, "V2", plan)
        assert gate["passed"] is True

    def test_message_includes_stage(self):
        plan, results = self._plan_and_results({
            "tp": ("V1", TPStatus.CLOSED),
        })
        gate = stage_gate_status(results, "V1", plan)
        assert "V1" in gate["message"]

    def test_gate_returns_stage_key(self):
        plan, results = self._plan_and_results({"tp": ("V1", TPStatus.CLOSED)})
        gate = stage_gate_status(results, "V1", plan)
        assert gate["stage"] == "V1"


# ── iter_testpoints integration ───────────────────────────────────────────────

class TestGoalTestpoints:
    """compute_closure must include testpoints nested inside goals."""

    def test_goal_testpoints_included(self):
        from covsight.core.ncdb.testplan import Goal
        plan = Testplan()
        plan.goals = [Goal(
            id="g1", title="Goal 1",
            testpoints=[Testpoint("goal_tp", stage="V1", tests=["goal_test"])],
        )]
        db = _db_with(goal_test=(3, 0))
        results = compute_closure(plan, db)
        names = {r.testpoint.name for r in results}
        assert "goal_tp" in names

    def test_goal_testpoints_status_computed(self):
        from covsight.core.ncdb.testplan import Goal
        plan = Testplan()
        plan.goals = [Goal(
            id="g1", title="Goal 1",
            testpoints=[Testpoint("goal_tp", stage="V1", tests=["goal_test"])],
        )]
        db = _db_with(goal_test=(2, 1))
        results = compute_closure(plan, db)
        r = next(r for r in results if r.testpoint.name == "goal_tp")
        assert r.status == TPStatus.PARTIAL

    def test_top_level_and_goal_testpoints_combined(self):
        from covsight.core.ncdb.testplan import Goal
        plan = Testplan()
        plan.testpoints = [Testpoint("top_tp", stage="V1", tests=["top_test"])]
        plan.goals = [Goal(
            id="g1", title="G",
            testpoints=[Testpoint("nested_tp", stage="V1", tests=["nested_test"])],
        )]
        db = _db_with(top_test=(1, 0), nested_test=(1, 0))
        results = compute_closure(plan, db)
        names = {r.testpoint.name for r in results}
        assert names == {"top_tp", "nested_tp"}


# ── compute_coverage_binding ──────────────────────────────────────────────────

class TestComputeCoverageBinding:
    from covsight.core.ncdb.testplan_closure import compute_coverage_binding

    def _tp_with_binding(self, path, btype="covergroup"):
        from covsight.core.ncdb.testplan import CoverageBinding
        tp = Testpoint("tp", stage="V1", tests=["t"])
        tp.coverage = [CoverageBinding(type=btype, path=path)]
        return tp

    class _CovDB:
        """Fake DB that exposes coverageItems and getCoveragePercent."""
        def __init__(self, paths, pct_map=None):
            self._paths = paths
            self._pct_map = pct_map or {}
            self._test_registry = None

        def coverageItems(self):
            return self._paths

        def getCoveragePercent(self, path):
            return self._pct_map.get(path)

        def historyNodes(self, _):
            return []

    def test_exact_path_resolved(self):
        from covsight.core.ncdb.testplan_closure import compute_coverage_binding
        tp = self._tp_with_binding("top.dut.cg")
        db = self._CovDB(["top.dut.cg", "top.dut.other"])
        results = compute_coverage_binding(tp, db)
        assert len(results) == 1
        assert results[0].matched_paths == ["top.dut.cg"]

    def test_glob_path_expanded(self):
        from covsight.core.ncdb.testplan_closure import compute_coverage_binding
        tp = self._tp_with_binding("top.dut.*")
        db = self._CovDB(["top.dut.cg1", "top.dut.cg2", "top.other.cg"])
        results = compute_coverage_binding(tp, db)
        assert set(results[0].matched_paths) == {"top.dut.cg1", "top.dut.cg2"}

    def test_coverage_pct_populated(self):
        from covsight.core.ncdb.testplan_closure import compute_coverage_binding
        tp = self._tp_with_binding("top.dut.cg")
        db = self._CovDB(["top.dut.cg"], {"top.dut.cg": 87.5})
        results = compute_coverage_binding(tp, db)
        assert results[0].coverage_pct == pytest.approx(87.5)

    def test_no_coverage_bindings_returns_empty(self):
        from covsight.core.ncdb.testplan_closure import compute_coverage_binding
        tp = Testpoint("tp", stage="V1", tests=["t"])
        db = self._CovDB([])
        assert compute_coverage_binding(tp, db) == []

    def test_no_coverageItems_on_db(self):
        """DB without coverageItems: exact path still resolves."""
        from covsight.core.ncdb.testplan_closure import compute_coverage_binding
        tp = self._tp_with_binding("top.dut.cg")
        db = _db_with()  # no coverageItems method
        results = compute_coverage_binding(tp, db)
        assert results[0].matched_paths == ["top.dut.cg"]

    def test_binding_type_preserved(self):
        from covsight.core.ncdb.testplan_closure import compute_coverage_binding
        tp = self._tp_with_binding("top.*", btype="assertion")
        db = self._CovDB(["top.a", "top.b"])
        results = compute_coverage_binding(tp, db)
        assert results[0].binding_type == "assertion"

    def test_coverage_results_attached_to_result(self):
        """compute_closure attaches coverage_results to TestpointResult."""
        from covsight.core.ncdb.testplan import CoverageBinding
        tp = Testpoint("tp", stage="V1", tests=["t"])
        tp.coverage = [CoverageBinding(type="covergroup", path="top.cg")]
        plan = Testplan()
        plan.testpoints = [tp]
        db = _db_with(t=(1, 0))
        results = compute_closure(plan, db)
        assert len(results[0].coverage_results) == 1
        assert results[0].coverage_results[0].path_pattern == "top.cg"


# ── require_goals_closed ──────────────────────────────────────────────────────

class TestRequireGoalsClosed:
    def test_require_goals_closed_passes_all_closed(self):
        from covsight.core.ncdb.testplan import Goal
        plan = Testplan()
        plan.goals = [Goal(
            id="g1", title="G",
            testpoints=[Testpoint("g_tp", stage="V1", tests=["g_test"])],
        )]
        db = _db_with(g_test=(1, 0))
        results = compute_closure(plan, db)
        gate = stage_gate_status(results, "V1", plan, require_goals_closed=True)
        assert gate["passed"] is True

    def test_require_goals_closed_fails_nested_failing(self):
        from covsight.core.ncdb.testplan import Goal
        plan = Testplan()
        plan.goals = [Goal(
            id="g1", title="G",
            testpoints=[Testpoint("g_tp", stage="V3", tests=["g_test"])],
        )]
        db = _db_with(g_test=(0, 1))
        results = compute_closure(plan, db)
        gate = stage_gate_status(results, "V1", plan, require_goals_closed=True)
        assert gate["passed"] is False

    def test_default_ignores_nested_higher_stage(self):
        from covsight.core.ncdb.testplan import Goal
        plan = Testplan()
        plan.testpoints = [Testpoint("tp", stage="V1", tests=["t"])]
        plan.goals = [Goal(
            id="g1", title="G",
            testpoints=[Testpoint("g_tp", stage="V3", tests=["g_test"])],
        )]
        db = _db_with(t=(1, 0), g_test=(0, 1))
        results = compute_closure(plan, db)
        gate = stage_gate_status(results, "V1", plan)
        assert gate["passed"] is True
