"""NCDB round-trip tests for Testplan with all new schema fields.

These tests write a full Testplan (goals, coverage bindings, requirements,
custom fields, imports) to an NCDB .cdb file, read it back, and assert that
all fields are preserved.
"""
from __future__ import annotations

import os
import tempfile

import pytest

from covsight.core.ncdb.testplan import (
    CoverageBinding,
    CoverpointEntry,
    CovergroupEntry,
    Goal,
    ImportEntry,
    RequirementLink,
    Testplan,
    Testpoint,
)
from covsight.core.ncdb.ncdb_writer import NcdbWriter
from covsight.core.ncdb.ncdb_ucis import NcdbUCIS


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_full_testplan() -> Testplan:
    plan = Testplan(source_file="test.yaml")
    plan.format_version = 2
    plan.name = "uart_full"
    plan.description = "Full UART testplan"
    plan.owner = "alice"
    plan.tags = ["uart", "regression"]
    plan.substitutions = {"baud": "9600"}
    plan.custom = {"dept": "hw-verify", "project_id": 42}

    # Top-level testpoints
    tp1 = Testpoint(name="tp_smoke", stage="V1", tests=["uart_smoke"],
                    desc="Smoke test", owner="bob", priority="high", weight=3,
                    tags=["smoke"])
    tp1.requirements = [
        RequirementLink(system="JIRA", project="UART", item_id="REQ-1",
                        url="http://jira/1"),
    ]
    tp1.coverage = [
        CoverageBinding(type="covergroup", path="top.dut.cg*", desc="CG"),
        CoverageBinding(type="assertion", path="top.chk.a1"),
    ]
    tp1.custom = {"sim_time": "100ns"}
    plan.testpoints.append(tp1)

    # Covergroups
    plan.covergroups.append(CovergroupEntry(
        name="cg_baud", desc="Baud rate",
        coverpoints=[
            CoverpointEntry(name="cp_baud", desc="Baud value",
                            path="top.cg.cp_baud"),
        ],
        custom={"tool": "vcs"},
    ))

    # Goals with nested testpoints
    inner_tp = Testpoint(name="tp_full_duplex", stage="V2",
                         tests=["uart_full_duplex"], owner="carol")
    inner_tp.requirements = [RequirementLink(system="ALM", item_id="REQ-2")]
    inner_tp.coverage = [CoverageBinding(type="coverpoint", path="top.cp1")]
    inner_tp.custom = {"priority_note": "critical"}

    goal = Goal(
        id="g_data", title="Data Path",
        desc="Data path coverage",
        owner="dave",
        priority="high",
        status="in_progress",
        tags=["data"],
        testpoints=[inner_tp],
        goals=[
            Goal(id="g_data.rx", title="RX Path",
                 testpoints=[
                     Testpoint(name="tp_rx", stage="V2", tests=["uart_rx"]),
                 ]),
        ],
        custom={"milestone": "m2"},
    )
    plan.goals.append(goal)

    # Import entries (metadata only — paths not resolved during round-trip)
    plan.imports = [ImportEntry(path="base.yaml", substitutions={"mod": "uart"})]

    return plan


def _roundtrip(plan: Testplan) -> Testplan:
    """Write *plan* to a temp .cdb and read it back."""
    with tempfile.NamedTemporaryFile(suffix=".cdb", delete=False) as tf:
        out_path = tf.name
    try:
        # Create a minimal NcdbUCIS, attach the testplan, and write it
        from covsight.core.mem.mem_ucis import MemUCIS
        db = MemUCIS()
        db._testplan = plan
        NcdbWriter().write(db, out_path)
        rt_db = NcdbUCIS(out_path)
        return rt_db.getTestplan()
    finally:
        os.unlink(out_path)


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class TestNcdbRoundTripMetadata:
    @pytest.fixture(scope="class")
    def rt(self):
        return _roundtrip(_make_full_testplan())

    def test_format_version_preserved(self, rt):
        assert rt.format_version == 2

    def test_name_preserved(self, rt):
        assert rt.name == "uart_full"

    def test_description_preserved(self, rt):
        assert rt.description == "Full UART testplan"

    def test_owner_preserved(self, rt):
        assert rt.owner == "alice"

    def test_tags_preserved(self, rt):
        assert rt.tags == ["uart", "regression"]

    def test_substitutions_preserved(self, rt):
        assert rt.substitutions == {"baud": "9600"}

    def test_custom_dict_preserved(self, rt):
        assert rt.custom == {"dept": "hw-verify", "project_id": 42}

    def test_imports_preserved(self, rt):
        assert len(rt.imports) == 1
        assert rt.imports[0].path == "base.yaml"
        assert rt.imports[0].substitutions == {"mod": "uart"}


class TestNcdbRoundTripTestpoints:
    @pytest.fixture(scope="class")
    def rt(self):
        return _roundtrip(_make_full_testplan())

    @pytest.fixture(scope="class")
    def tp1(self, rt):
        return next(t for t in rt.testpoints if t.name == "tp_smoke")

    def test_testpoint_name_preserved(self, tp1):
        assert tp1.name == "tp_smoke"

    def test_testpoint_stage_preserved(self, tp1):
        assert tp1.stage == "V1"

    def test_testpoint_tests_preserved(self, tp1):
        assert tp1.tests == ["uart_smoke"]

    def test_testpoint_desc_preserved(self, tp1):
        assert tp1.desc == "Smoke test"

    def test_testpoint_owner_preserved(self, tp1):
        assert tp1.owner == "bob"

    def test_testpoint_priority_preserved(self, tp1):
        assert tp1.priority == "high"

    def test_testpoint_weight_preserved(self, tp1):
        assert tp1.weight == 3

    def test_testpoint_tags_preserved(self, tp1):
        assert tp1.tags == ["smoke"]

    def test_testpoint_requirements_preserved(self, tp1):
        assert len(tp1.requirements) == 1
        req = tp1.requirements[0]
        assert req.system == "JIRA"
        assert req.item_id == "REQ-1"
        assert req.url == "http://jira/1"

    def test_testpoint_coverage_bindings_preserved(self, tp1):
        assert len(tp1.coverage) == 2
        types = {b.type for b in tp1.coverage}
        assert types == {"covergroup", "assertion"}

    def test_testpoint_custom_preserved(self, tp1):
        assert tp1.custom == {"sim_time": "100ns"}


class TestNcdbRoundTripCovergroups:
    @pytest.fixture(scope="class")
    def rt(self):
        return _roundtrip(_make_full_testplan())

    def test_covergroup_name(self, rt):
        assert rt.covergroups[0].name == "cg_baud"

    def test_coverpoint_preserved(self, rt):
        cg = rt.covergroups[0]
        assert len(cg.coverpoints) == 1
        assert cg.coverpoints[0].name == "cp_baud"
        assert cg.coverpoints[0].path == "top.cg.cp_baud"

    def test_covergroup_custom(self, rt):
        assert rt.covergroups[0].custom == {"tool": "vcs"}


class TestNcdbRoundTripGoals:
    @pytest.fixture(scope="class")
    def rt(self):
        return _roundtrip(_make_full_testplan())

    @pytest.fixture(scope="class")
    def goal(self, rt):
        return rt.goals[0]

    def test_goal_id_preserved(self, goal):
        assert goal.id == "g_data"

    def test_goal_title_preserved(self, goal):
        assert goal.title == "Data Path"

    def test_goal_owner_preserved(self, goal):
        assert goal.owner == "dave"

    def test_goal_priority_preserved(self, goal):
        assert goal.priority == "high"

    def test_goal_status_preserved(self, goal):
        assert goal.status == "in_progress"

    def test_goal_tags_preserved(self, goal):
        assert goal.tags == ["data"]

    def test_goal_custom_preserved(self, goal):
        assert goal.custom == {"milestone": "m2"}

    def test_goal_testpoints_count(self, goal):
        assert len(goal.testpoints) == 1

    def test_goal_testpoint_name(self, goal):
        assert goal.testpoints[0].name == "tp_full_duplex"

    def test_goal_testpoint_requirements(self, goal):
        tp = goal.testpoints[0]
        assert len(tp.requirements) == 1
        assert tp.requirements[0].item_id == "REQ-2"

    def test_goal_testpoint_coverage(self, goal):
        tp = goal.testpoints[0]
        assert len(tp.coverage) == 1
        assert tp.coverage[0].type == "coverpoint"

    def test_goal_testpoint_custom(self, goal):
        tp = goal.testpoints[0]
        assert tp.custom == {"priority_note": "critical"}

    def test_nested_goal_preserved(self, goal):
        assert len(goal.goals) == 1
        assert goal.goals[0].id == "g_data.rx"

    def test_nested_goal_testpoint(self, goal):
        assert goal.goals[0].testpoints[0].name == "tp_rx"
