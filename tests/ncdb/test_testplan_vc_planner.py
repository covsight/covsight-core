"""Tests for the Synopsys Verdi VC Planner reader (CSV and XML)."""
from __future__ import annotations

import textwrap
import pytest

from covsight.core.ncdb.testplan_vc_planner import import_vc_planner


def _write(tmp_path, content: str, name: str) -> str:
    path = str(tmp_path / name)
    with open(path, "w") as f:
        f.write(textwrap.dedent(content).strip())
    return path


# ── CSV parser ────────────────────────────────────────────────────────────────

class TestVcPlannerCsv:
    def test_empty_csv(self, tmp_path):
        path = _write(tmp_path, "name,type\n", "plan.csv")
        plan = import_vc_planner(path)
        assert plan.goals == []
        assert plan.testpoints == []

    def test_csv_missing_name_column(self, tmp_path):
        path = _write(tmp_path, "title,type\nG,group\n", "plan.csv")
        with pytest.raises(ValueError, match="name"):
            import_vc_planner(path)

    def test_group_becomes_goal(self, tmp_path):
        path = _write(tmp_path, """
            name,type,id
            Feature A,group,feat_a
        """, "plan.csv")
        plan = import_vc_planner(path)
        assert len(plan.goals) == 1
        assert plan.goals[0].title == "Feature A"

    def test_test_under_group(self, tmp_path):
        path = _write(tmp_path, """
            name,type,id,stage,tests
            GroupA,group,g1,,
            tp_smoke,test,,V1,uart_smoke
        """, "plan.csv")
        plan = import_vc_planner(path)
        assert len(plan.goals[0].testpoints) == 1
        assert plan.goals[0].testpoints[0].name == "tp_smoke"

    def test_test_without_group_goes_toplevel(self, tmp_path):
        path = _write(tmp_path, """
            name,type,id,stage
            tp_smoke,test,,V1
        """, "plan.csv")
        plan = import_vc_planner(path)
        assert len(plan.testpoints) == 1

    def test_coverpoint_adds_binding(self, tmp_path):
        path = _write(tmp_path, """
            name,type,id,path
            tp1,test,,
            cp1,coverpoint,,top.cp1
        """, "plan.csv")
        plan = import_vc_planner(path)
        tp = plan.testpoints[0]
        assert len(tp.coverage) == 1
        assert tp.coverage[0].path == "top.cp1"

    def test_group_owner_and_status(self, tmp_path):
        path = _write(tmp_path, """
            name,type,id,owner,status
            G,group,g1,alice,active
        """, "plan.csv")
        plan = import_vc_planner(path)
        g = plan.goals[0]
        assert g.owner == "alice"
        assert g.status == "in_progress"

    def test_testpoint_weight(self, tmp_path):
        path = _write(tmp_path, """
            name,type,id,stage,weight
            tp1,test,,V1,3
        """, "plan.csv")
        plan = import_vc_planner(path)
        assert plan.testpoints[0].weight == 3

    def test_testpoint_priority(self, tmp_path):
        path = _write(tmp_path, """
            name,type,id,stage,priority
            tp1,test,,V1,high
        """, "plan.csv")
        plan = import_vc_planner(path)
        assert plan.testpoints[0].priority == "high"

    def test_source_file_set(self, tmp_path):
        path = _write(tmp_path, "name,type\n", "plan.csv")
        plan = import_vc_planner(path)
        assert plan.source_file == path


# ── XML parser ────────────────────────────────────────────────────────────────

class TestVcPlannerXml:
    def test_empty_xml(self, tmp_path):
        path = _write(tmp_path, "<vcplanner/>", "plan.xml")
        plan = import_vc_planner(path)
        assert plan.goals == []
        assert plan.testpoints == []

    def test_plan_name_from_root(self, tmp_path):
        path = _write(tmp_path, '<vcplanner name="uart"/>', "plan.xml")
        plan = import_vc_planner(path)
        assert plan.name == "uart"

    def test_group_element_becomes_goal(self, tmp_path):
        path = _write(tmp_path, """
            <vcplanner>
              <group name="Feature A" id="feat_a"/>
            </vcplanner>
        """, "plan.xml")
        plan = import_vc_planner(path)
        assert len(plan.goals) == 1
        assert plan.goals[0].title == "Feature A"

    def test_nested_groups(self, tmp_path):
        path = _write(tmp_path, """
            <vcplanner>
              <group name="Parent" id="p">
                <group name="Child" id="c"/>
              </group>
            </vcplanner>
        """, "plan.xml")
        plan = import_vc_planner(path)
        assert len(plan.goals[0].goals) == 1

    def test_test_under_group(self, tmp_path):
        path = _write(tmp_path, """
            <vcplanner>
              <group name="G" id="g">
                <test name="tp1" stage="V1" tests="t1"/>
              </group>
            </vcplanner>
        """, "plan.xml")
        plan = import_vc_planner(path)
        assert plan.goals[0].testpoints[0].name == "tp1"

    def test_test_weight_parsed(self, tmp_path):
        path = _write(tmp_path, """
            <vcplanner>
              <test name="tp1" stage="V1" weight="5"/>
            </vcplanner>
        """, "plan.xml")
        plan = import_vc_planner(path)
        assert plan.testpoints[0].weight == 5

    def test_coverpoint_element(self, tmp_path):
        path = _write(tmp_path, """
            <vcplanner>
              <test name="tp1" stage="V1">
                <coverpoint name="cp1" path="top.cp1"/>
              </test>
            </vcplanner>
        """, "plan.xml")
        plan = import_vc_planner(path)
        assert len(plan.testpoints[0].coverage) == 1
        assert plan.testpoints[0].coverage[0].path == "top.cp1"

    def test_coverpoint_registers_in_covergroups(self, tmp_path):
        path = _write(tmp_path, """
            <vcplanner>
              <test name="tp1" stage="V1">
                <coverpoint name="cp1" path="top.cp1"/>
              </test>
            </vcplanner>
        """, "plan.xml")
        plan = import_vc_planner(path)
        assert len(plan.covergroups) >= 1

    def test_group_status_mapped(self, tmp_path):
        path = _write(tmp_path, """
            <vcplanner>
              <group name="G" id="g" status="complete"/>
            </vcplanner>
        """, "plan.xml")
        plan = import_vc_planner(path)
        assert plan.goals[0].status == "complete"

    def test_description_from_attribute(self, tmp_path):
        path = _write(tmp_path, """
            <vcplanner>
              <group name="G" id="g" description="Some desc"/>
            </vcplanner>
        """, "plan.xml")
        plan = import_vc_planner(path)
        assert plan.goals[0].desc == "Some desc"

    def test_invalid_xml_raises(self, tmp_path):
        path = _write(tmp_path, "not xml", "plan.xml")
        with pytest.raises(Exception):
            import_vc_planner(path)
