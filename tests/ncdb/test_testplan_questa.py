"""Tests for the Siemens Questa Visualizer reader (XML and CSV)."""
from __future__ import annotations

import textwrap
import pytest

from covsight.core.ncdb.testplan_questa import import_questa


def _write(tmp_path, content: str, name: str) -> str:
    path = str(tmp_path / name)
    with open(path, "w") as f:
        f.write(textwrap.dedent(content).strip())
    return path


# ── XML parser ────────────────────────────────────────────────────────────────

class TestQuestaXml:
    def test_empty_testplan(self, tmp_path):
        path = _write(tmp_path, "<questa_testplan/>", "plan.xml")
        plan = import_questa(path)
        assert plan.goals == []
        assert plan.testpoints == []

    def test_plan_name_from_root(self, tmp_path):
        path = _write(tmp_path, '<questa_testplan name="uart" owner="alice"/>',
                      "plan.xml")
        plan = import_questa(path)
        assert plan.name == "uart"
        assert plan.owner == "alice"

    def test_plan_description_from_element(self, tmp_path):
        path = _write(tmp_path, """
            <questa_testplan>
              <description>UART testplan</description>
            </questa_testplan>
        """, "plan.xml")
        plan = import_questa(path)
        assert plan.description == "UART testplan"

    def test_plan_tags_from_elements(self, tmp_path):
        path = _write(tmp_path, """
            <questa_testplan>
              <tag>regression</tag>
              <tag>nightly</tag>
            </questa_testplan>
        """, "plan.xml")
        plan = import_questa(path)
        assert set(plan.tags) == {"regression", "nightly"}

    def test_goal_parsed(self, tmp_path):
        path = _write(tmp_path, """
            <questa_testplan>
              <goal id="g1" title="Data Path" owner="bob"/>
            </questa_testplan>
        """, "plan.xml")
        plan = import_questa(path)
        assert len(plan.goals) == 1
        assert plan.goals[0].id == "g1"
        assert plan.goals[0].title == "Data Path"
        assert plan.goals[0].owner == "bob"

    def test_goal_status_mapped(self, tmp_path):
        path = _write(tmp_path, """
            <questa_testplan>
              <goal id="g1" title="G" status="active"/>
            </questa_testplan>
        """, "plan.xml")
        plan = import_questa(path)
        assert plan.goals[0].status == "in_progress"

    def test_goal_tags(self, tmp_path):
        path = _write(tmp_path, """
            <questa_testplan>
              <goal id="g1" title="G">
                <tag>smoke</tag>
              </goal>
            </questa_testplan>
        """, "plan.xml")
        plan = import_questa(path)
        assert "smoke" in plan.goals[0].tags

    def test_nested_goals(self, tmp_path):
        path = _write(tmp_path, """
            <questa_testplan>
              <goal id="p" title="Parent">
                <goal id="c" title="Child"/>
              </goal>
            </questa_testplan>
        """, "plan.xml")
        plan = import_questa(path)
        assert len(plan.goals[0].goals) == 1
        assert plan.goals[0].goals[0].id == "c"

    def test_testpoint_in_goal(self, tmp_path):
        path = _write(tmp_path, """
            <questa_testplan>
              <goal id="g1" title="G">
                <testpoint name="tp_smoke" stage="V1">
                  <test>uart_smoke</test>
                </testpoint>
              </goal>
            </questa_testplan>
        """, "plan.xml")
        plan = import_questa(path)
        tp = plan.goals[0].testpoints[0]
        assert tp.name == "tp_smoke"
        assert tp.tests == ["uart_smoke"]

    def test_metric_creates_coverage_binding(self, tmp_path):
        path = _write(tmp_path, """
            <questa_testplan>
              <testpoint name="tp1" stage="V1">
                <metric type="covergroup" coverage="top.dut.cg_baud"/>
                <metric type="assertion" coverage="top.chk.a1"/>
              </testpoint>
            </questa_testplan>
        """, "plan.xml")
        plan = import_questa(path)
        tp = plan.testpoints[0]
        assert len(tp.coverage) == 2
        types = {b.type for b in tp.coverage}
        assert types == {"covergroup", "assertion"}

    def test_metric_coverage_path(self, tmp_path):
        path = _write(tmp_path, """
            <questa_testplan>
              <testpoint name="tp1" stage="V1">
                <metric type="covergroup" coverage="top.dut.cg"/>
              </testpoint>
            </questa_testplan>
        """, "plan.xml")
        plan = import_questa(path)
        assert plan.testpoints[0].coverage[0].path == "top.dut.cg"

    def test_testpoint_na_flag(self, tmp_path):
        path = _write(tmp_path, """
            <questa_testplan>
              <testpoint name="tp1" stage="V1" na="true"/>
            </questa_testplan>
        """, "plan.xml")
        plan = import_questa(path)
        assert plan.testpoints[0].na is True

    def test_testpoint_tags(self, tmp_path):
        path = _write(tmp_path, """
            <questa_testplan>
              <testpoint name="tp1" stage="V1">
                <tag>smoke</tag>
              </testpoint>
            </questa_testplan>
        """, "plan.xml")
        plan = import_questa(path)
        assert "smoke" in plan.testpoints[0].tags

    def test_source_file_set(self, tmp_path):
        path = _write(tmp_path, "<questa_testplan/>", "plan.xml")
        plan = import_questa(path)
        assert plan.source_file == path

    def test_invalid_xml_raises(self, tmp_path):
        path = _write(tmp_path, "not xml", "plan.xml")
        with pytest.raises(Exception):
            import_questa(path)

    def test_testplan_root_also_supported(self, tmp_path):
        """Generic <testplan> root should also parse correctly."""
        path = _write(tmp_path, """
            <testplan name="spi">
              <goal id="g1" title="SPI Goals"/>
            </testplan>
        """, "plan.xml")
        plan = import_questa(path)
        assert plan.name == "spi"
        assert len(plan.goals) == 1


# ── CSV parser ────────────────────────────────────────────────────────────────

class TestQuestaCsv:
    def test_empty_csv(self, tmp_path):
        path = _write(tmp_path, "id,type,title\n", "plan.csv")
        plan = import_questa(path)
        assert plan.goals == []
        assert plan.testpoints == []

    def test_csv_missing_id_column(self, tmp_path):
        path = _write(tmp_path, "title,type\nG,goal\n", "plan.csv")
        with pytest.raises(ValueError, match="id"):
            import_questa(path)

    def test_goal_row(self, tmp_path):
        path = _write(tmp_path, """
            id,type,title,owner,status
            g1,goal,Data Path,alice,active
        """, "plan.csv")
        plan = import_questa(path)
        assert len(plan.goals) == 1
        assert plan.goals[0].id == "g1"
        assert plan.goals[0].owner == "alice"
        assert plan.goals[0].status == "in_progress"

    def test_testpoint_row(self, tmp_path):
        path = _write(tmp_path, """
            id,type,title,name,stage,tests
            tp1,testpoint,Smoke,tp_smoke,V1,uart_smoke
        """, "plan.csv")
        plan = import_questa(path)
        assert len(plan.testpoints) == 1
        assert plan.testpoints[0].name == "tp_smoke"
        assert plan.testpoints[0].tests == ["uart_smoke"]

    def test_testpoint_under_goal(self, tmp_path):
        path = _write(tmp_path, """
            id,type,title,name,parent_id,stage
            g1,goal,G,,
            tp1,testpoint,T,tp_smoke,g1,V1
        """, "plan.csv")
        plan = import_questa(path)
        assert len(plan.goals[0].testpoints) == 1

    def test_testpoint_coverage_from_csv(self, tmp_path):
        path = _write(tmp_path, """
            id,type,name,stage,metric_type,coverage_path
            tp1,testpoint,tp1,V1,covergroup,top.cg
        """, "plan.csv")
        plan = import_questa(path)
        tp = plan.testpoints[0]
        assert len(tp.coverage) == 1
        assert tp.coverage[0].type == "covergroup"
        assert tp.coverage[0].path == "top.cg"

    def test_nested_goals_via_parent_id(self, tmp_path):
        path = _write(tmp_path, """
            id,type,title,parent_id
            g1,goal,Parent,
            g2,goal,Child,g1
        """, "plan.csv")
        plan = import_questa(path)
        assert len(plan.goals) == 1
        assert len(plan.goals[0].goals) == 1
        assert plan.goals[0].goals[0].id == "g2"

    def test_goal_priority(self, tmp_path):
        path = _write(tmp_path, """
            id,type,title,priority
            g1,goal,G,high
        """, "plan.csv")
        plan = import_questa(path)
        assert plan.goals[0].priority == "high"
