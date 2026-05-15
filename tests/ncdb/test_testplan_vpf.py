"""Tests for the Cadence vManager VPF XML reader."""
from __future__ import annotations

import textwrap
import pytest

from covsight.core.ncdb.testplan_vpf import import_vpf


def _write_xml(tmp_path, content: str, name="plan.xml") -> str:
    path = str(tmp_path / name)
    with open(path, "w") as f:
        f.write(textwrap.dedent(content).strip())
    return path


# ── basic structure ───────────────────────────────────────────────────────────

class TestVpfBasic:
    def test_empty_testplan(self, tmp_path):
        path = _write_xml(tmp_path, "<testplan/>")
        plan = import_vpf(path)
        assert plan.goals == []
        assert plan.testpoints == []

    def test_plan_name_from_attribute(self, tmp_path):
        path = _write_xml(tmp_path, '<testplan name="uart" owner="alice"/>')
        plan = import_vpf(path)
        assert plan.name == "uart"

    def test_source_file_set(self, tmp_path):
        path = _write_xml(tmp_path, "<testplan/>")
        plan = import_vpf(path)
        assert plan.source_file == path

    def test_invalid_xml_raises(self, tmp_path):
        path = _write_xml(tmp_path, "not xml at all")
        with pytest.raises(Exception):
            import_vpf(path)

    def test_file_not_found_raises(self, tmp_path):
        with pytest.raises(FileNotFoundError):
            import_vpf(str(tmp_path / "nonexistent.xml"))


# ── goal parsing ──────────────────────────────────────────────────────────────

class TestVpfGoals:
    def test_single_goal(self, tmp_path):
        path = _write_xml(tmp_path, """
            <testplan>
              <tpGoal name="Feature A" id="feat_a"/>
            </testplan>
        """)
        plan = import_vpf(path)
        assert len(plan.goals) == 1
        assert plan.goals[0].title == "Feature A"
        assert plan.goals[0].id == "feat_a"

    def test_goal_owner(self, tmp_path):
        path = _write_xml(tmp_path, """
            <testplan>
              <tpGoal name="G" id="g1" owner="alice"/>
            </testplan>
        """)
        plan = import_vpf(path)
        assert plan.goals[0].owner == "alice"

    def test_goal_status_mapped(self, tmp_path):
        path = _write_xml(tmp_path, """
            <testplan>
              <tpGoal name="G" id="g1" status="active"/>
            </testplan>
        """)
        plan = import_vpf(path)
        assert plan.goals[0].status == "in_progress"

    def test_goal_status_complete(self, tmp_path):
        path = _write_xml(tmp_path, """
            <testplan>
              <tpGoal name="G" id="g1" status="complete"/>
            </testplan>
        """)
        plan = import_vpf(path)
        assert plan.goals[0].status == "complete"

    def test_goal_priority_mapped(self, tmp_path):
        path = _write_xml(tmp_path, """
            <testplan>
              <tpGoal name="G" id="g1" priority="high"/>
            </testplan>
        """)
        plan = import_vpf(path)
        assert plan.goals[0].priority == "high"

    def test_nested_goals(self, tmp_path):
        path = _write_xml(tmp_path, """
            <testplan>
              <tpGoal name="Parent" id="p">
                <tpGoal name="Child" id="c"/>
              </tpGoal>
            </testplan>
        """)
        plan = import_vpf(path)
        assert len(plan.goals) == 1
        assert len(plan.goals[0].goals) == 1
        assert plan.goals[0].goals[0].id == "c"

    def test_tpgroup_as_goal(self, tmp_path):
        path = _write_xml(tmp_path, """
            <testplan>
              <tpGroup name="Grp" id="grp1"/>
            </testplan>
        """)
        plan = import_vpf(path)
        assert len(plan.goals) == 1
        assert plan.goals[0].title == "Grp"


# ── testpoint parsing ─────────────────────────────────────────────────────────

class TestVpfTestpoints:
    def test_testpoint_in_goal(self, tmp_path):
        path = _write_xml(tmp_path, """
            <testplan>
              <tpGoal name="G" id="g">
                <tpTest name="tp_smoke" stage="V1" tests="uart_smoke"/>
              </tpGoal>
            </testplan>
        """)
        plan = import_vpf(path)
        tps = plan.goals[0].testpoints
        assert len(tps) == 1
        assert tps[0].name == "tp_smoke"
        assert tps[0].stage == "V1"
        assert tps[0].tests == ["uart_smoke"]

    def test_top_level_testpoint(self, tmp_path):
        path = _write_xml(tmp_path, """
            <testplan>
              <tpTest name="tp1" stage="V1"/>
            </testplan>
        """)
        plan = import_vpf(path)
        assert len(plan.testpoints) == 1

    def test_testpoint_na_flag(self, tmp_path):
        path = _write_xml(tmp_path, """
            <testplan>
              <tpTest name="tp1" stage="V1" na="true"/>
            </testplan>
        """)
        plan = import_vpf(path)
        assert plan.testpoints[0].na is True

    def test_testpoint_desc(self, tmp_path):
        path = _write_xml(tmp_path, """
            <testplan>
              <tpTest name="tp1" stage="V1" desc="A description"/>
            </testplan>
        """)
        plan = import_vpf(path)
        assert plan.testpoints[0].desc == "A description"

    def test_multiple_tests_comma_separated(self, tmp_path):
        path = _write_xml(tmp_path, """
            <testplan>
              <tpTest name="tp1" stage="V1" tests="test_a, test_b, test_c"/>
            </testplan>
        """)
        plan = import_vpf(path)
        assert plan.testpoints[0].tests == ["test_a", "test_b", "test_c"]


# ── coverpoint parsing ────────────────────────────────────────────────────────

class TestVpfCoverpoints:
    def test_coverpoint_creates_coverage_binding(self, tmp_path):
        path = _write_xml(tmp_path, """
            <testplan>
              <tpTest name="tp1" stage="V1">
                <tpCoverPoint name="cg1" type="covergroup" path="top.cg1"/>
              </tpTest>
            </testplan>
        """)
        plan = import_vpf(path)
        tp = plan.testpoints[0]
        assert len(tp.coverage) == 1
        assert tp.coverage[0].type == "covergroup"
        assert tp.coverage[0].path == "top.cg1"

    def test_coverpoint_registered_in_covergroups(self, tmp_path):
        path = _write_xml(tmp_path, """
            <testplan>
              <tpTest name="tp1" stage="V1">
                <tpCoverPoint name="cg1" type="covergroup" path="top.cg1"/>
              </tpTest>
            </testplan>
        """)
        plan = import_vpf(path)
        assert any(cg.name == "cg1" for cg in plan.covergroups)

    def test_multiple_coverpoints(self, tmp_path):
        path = _write_xml(tmp_path, """
            <testplan>
              <tpTest name="tp1" stage="V1">
                <tpCoverPoint name="cg1" type="covergroup" path="top.cg1"/>
                <tpCoverPoint name="a1"  type="assertion"  path="top.a1"/>
              </tpTest>
            </testplan>
        """)
        plan = import_vpf(path)
        types = {b.type for b in plan.testpoints[0].coverage}
        assert types == {"covergroup", "assertion"}


# ── attributes parsing ────────────────────────────────────────────────────────

class TestVpfAttributes:
    def test_attributes_parsed_into_custom(self, tmp_path):
        path = _write_xml(tmp_path, """
            <testplan>
              <tpGoal name="G" id="g">
                <attributes>
                  <attr name="jira_id" value="UART-10"/>
                  <attr name="team" value="hw_verify"/>
                </attributes>
              </tpGoal>
            </testplan>
        """)
        plan = import_vpf(path)
        assert plan.goals[0].custom["jira_id"] == "UART-10"
        assert plan.goals[0].custom["team"] == "hw_verify"

    def test_no_attributes_gives_empty_custom(self, tmp_path):
        path = _write_xml(tmp_path, "<testplan><tpGoal name='G' id='g'/></testplan>")
        plan = import_vpf(path)
        assert plan.goals[0].custom == {}


# ── tpPlan root element ───────────────────────────────────────────────────────

class TestVpfRootElement:
    def test_tpplan_root_supported(self, tmp_path):
        path = _write_xml(tmp_path, """
            <tpPlan name="uart">
              <tpGoal name="G" id="g1"/>
            </tpPlan>
        """)
        plan = import_vpf(path)
        assert plan.name == "uart"
        assert len(plan.goals) == 1
