"""Unit tests for testplan_yaml.py — canonical YAML/JSON reader."""
from __future__ import annotations

import json
import os
import warnings

import pytest

try:
    import yaml
    _YAML_AVAILABLE = True
except ImportError:
    _YAML_AVAILABLE = False

from covsight.core.ncdb.testplan import CoverageBinding, Testplan
from covsight.core.ncdb.testplan_imports import ParseError
from covsight.core.ncdb.testplan_yaml import load_testplan


# ── helpers ───────────────────────────────────────────────────────────────────

def _write_json(tmp_path, data: dict, name: str = "plan.json") -> str:
    path = tmp_path / name
    path.write_text(json.dumps(data), encoding="utf-8")
    return str(path)


def _write_yaml(tmp_path, content: str, name: str = "plan.yaml") -> str:
    path = tmp_path / name
    path.write_text(content, encoding="utf-8")
    return str(path)


_FULL_JSON = {
    "$schema": "https://schema.covsight.io/testplan/v1",
    "format_version": 1,
    "name": "uart",
    "description": "UART block verification",
    "owner": "dv-team",
    "tags": ["regression", "block"],
    "substitutions": {"name": "uart"},
    "goals": [
        {
            "id": "functional",
            "title": "Functional verification",
            "owner": "alice",
            "priority": "high",
            "status": "in_progress",
            "tags": ["nightly"],
            "custom": {"jira_epic": "UART-10"},
            "testpoints": [
                {
                    "name": "uart_reset",
                    "stage": "V1",
                    "desc": "Verify reset.",
                    "tests": ["uart_smoke"],
                    "coverage": [
                        {"type": "covergroup",
                         "path": "uart_env.uart_reset_cg"}
                    ],
                    "requirements": [
                        {"system": "JIRA", "project": "UART", "item_id": "REQ-1"}
                    ],
                    "owner": "bob",
                    "priority": "high",
                    "weight": 2,
                    "custom": {"acme": {"sim_time": 60}},
                }
            ],
            "goals": [{"id": "sub", "title": "Sub goal"}],
        }
    ],
    "testpoints": [
        {
            "name": "uart_baud",
            "stage": "V2",
            "tests": ["uart_baud_{name}_test"],
            "coverage": [
                {"type": "coverpoint",
                 "path": "uart_env.uart_cg.baud_rate_cp"}
            ],
        }
    ],
    "covergroups": [
        {
            "name": "uart_cg",
            "desc": "UART functional coverage",
            "coverpoints": [
                {"name": "baud_rate_cp",
                 "path": "uart_env.uart_cg.baud_rate_cp"},
            ],
        }
    ],
    "custom": {"acme": {"dv_doc": "http://x"}},
}


# ── load from JSON ────────────────────────────────────────────────────────────

class TestLoadJson:
    def test_basic_load(self, tmp_path):
        path = _write_json(tmp_path, {"testpoints": [{"name": "tp", "stage": "V1",
                                                       "tests": ["t"]}]})
        plan = load_testplan(path)
        assert isinstance(plan, Testplan)
        assert len(plan.testpoints) == 1
        assert plan.testpoints[0].name == "tp"

    def test_full_fields(self, tmp_path):
        path = _write_json(tmp_path, _FULL_JSON)
        plan = load_testplan(path)
        assert plan.name == "uart"
        assert plan.description == "UART block verification"
        assert plan.owner == "dv-team"
        assert plan.tags == ["regression", "block"]
        assert plan.schema == "https://schema.covsight.io/testplan/v1"
        assert plan.custom == {"acme": {"dv_doc": "http://x"}}

    def test_goal_tree_loaded(self, tmp_path):
        path = _write_json(tmp_path, _FULL_JSON)
        plan = load_testplan(path)
        assert len(plan.goals) == 1
        g = plan.goals[0]
        assert g.id == "functional"
        assert g.owner == "alice"
        assert g.priority == "high"
        assert g.status == "in_progress"
        assert g.custom == {"jira_epic": "UART-10"}
        assert len(g.testpoints) == 1
        assert len(g.goals) == 1
        assert g.goals[0].id == "sub"

    def test_testpoint_in_goal_full_fields(self, tmp_path):
        path = _write_json(tmp_path, _FULL_JSON)
        plan = load_testplan(path)
        tp = plan.goals[0].testpoints[0]
        assert tp.name == "uart_reset"
        assert tp.owner == "bob"
        assert tp.priority == "high"
        assert tp.weight == 2
        assert tp.custom == {"acme": {"sim_time": 60}}
        assert len(tp.coverage) == 1
        assert tp.coverage[0].type == "covergroup"
        assert tp.coverage[0].path == "uart_env.uart_reset_cg"
        assert len(tp.requirements) == 1
        assert tp.requirements[0].item_id == "REQ-1"

    def test_covergroup_with_coverpoints(self, tmp_path):
        path = _write_json(tmp_path, _FULL_JSON)
        plan = load_testplan(path)
        assert len(plan.covergroups) == 1
        cg = plan.covergroups[0]
        assert cg.name == "uart_cg"
        assert len(cg.coverpoints) == 1
        assert cg.coverpoints[0].name == "baud_rate_cp"

    def test_source_file_is_absolute(self, tmp_path):
        path = _write_json(tmp_path, {})
        plan = load_testplan(path)
        assert os.path.isabs(plan.source_file)

    def test_file_not_found(self, tmp_path):
        with pytest.raises(FileNotFoundError):
            load_testplan(str(tmp_path / "missing.json"))


# ── load from YAML ────────────────────────────────────────────────────────────

@pytest.mark.skipif(not _YAML_AVAILABLE, reason="pyyaml not installed")
class TestLoadYaml:
    def test_basic_yaml(self, tmp_path):
        yaml_text = """
name: uart
testpoints:
  - name: uart_reset
    stage: V1
    tests: [uart_smoke]
"""
        path = _write_yaml(tmp_path, yaml_text)
        plan = load_testplan(path)
        assert plan.name == "uart"
        assert plan.testpoints[0].name == "uart_reset"

    def test_yaml_goals_and_coverage(self, tmp_path):
        yaml_text = """
goals:
  - id: functional
    title: Functional
    testpoints:
      - name: uart_reset
        stage: V1
        tests: [uart_smoke]
        coverage:
          - type: covergroup
            path: env.reset_cg
"""
        path = _write_yaml(tmp_path, yaml_text)
        plan = load_testplan(path)
        assert len(plan.goals) == 1
        tp = plan.goals[0].testpoints[0]
        assert tp.coverage[0].type == "covergroup"

    def test_yaml_and_json_produce_same_result(self, tmp_path):
        json_path = _write_json(tmp_path, _FULL_JSON)
        plan_json = load_testplan(json_path)

        import yaml
        yaml_path = _write_yaml(tmp_path, yaml.dump(_FULL_JSON))
        plan_yaml = load_testplan(yaml_path)

        assert plan_json.name == plan_yaml.name
        assert plan_json.owner == plan_yaml.owner
        assert len(plan_json.goals) == len(plan_yaml.goals)
        assert len(plan_json.testpoints) == len(plan_yaml.testpoints)
        assert len(plan_json.covergroups) == len(plan_yaml.covergroups)


# ── substitution expansion ────────────────────────────────────────────────────

class TestSubstitutionExpansion:
    def test_file_level_substitutions(self, tmp_path):
        data = {
            "substitutions": {"baud": ["9600", "115200"]},
            "testpoints": [{"name": "tp", "stage": "V1",
                             "tests": ["{baud}_test"]}],
        }
        path = _write_json(tmp_path, data)
        plan = load_testplan(path)
        assert set(plan.testpoints[0].tests) == {"9600_test", "115200_test"}

    def test_caller_supplied_substitutions(self, tmp_path):
        data = {"testpoints": [{"name": "tp", "stage": "V1",
                                 "tests": ["{name}_test"]}]}
        path = _write_json(tmp_path, data)
        plan = load_testplan(path, substitutions={"name": "uart"})
        assert plan.testpoints[0].tests == ["uart_test"]

    def test_file_level_overrides_caller(self, tmp_path):
        # File has "name": "spi"; caller passes "name": "uart" → file wins
        data = {
            "substitutions": {"name": "spi"},
            "testpoints": [{"name": "tp", "stage": "V1",
                             "tests": ["{name}_test"]}],
        }
        path = _write_json(tmp_path, data)
        plan = load_testplan(path, substitutions={"name": "uart"})
        assert plan.testpoints[0].tests == ["spi_test"]

    def test_cartesian_product(self, tmp_path):
        data = {
            "substitutions": {"mod": ["uart", "spi"], "intf": ["a", "b"]},
            "testpoints": [{"name": "tp", "stage": "V1",
                             "tests": ["{mod}_{intf}_test"]}],
        }
        path = _write_json(tmp_path, data)
        plan = load_testplan(path)
        assert len(plan.testpoints[0].tests) == 4

    def test_na_testpoint(self, tmp_path):
        data = {"testpoints": [{"name": "tp", "stage": "V1", "tests": ["N/A"]}]}
        path = _write_json(tmp_path, data)
        plan = load_testplan(path)
        assert plan.testpoints[0].na is True
        assert plan.testpoints[0].tests == []

    def test_substitution_in_goal_testpoint(self, tmp_path):
        data = {
            "substitutions": {"x": ["a", "b"]},
            "goals": [{"id": "g", "title": "G",
                        "testpoints": [{"name": "tp", "stage": "V1",
                                        "tests": ["{x}_test"]}]}],
        }
        path = _write_json(tmp_path, data)
        plan = load_testplan(path)
        assert set(plan.goals[0].testpoints[0].tests) == {"a_test", "b_test"}


# ── coverage binding types ────────────────────────────────────────────────────

class TestCoverageBindingLoad:
    def test_all_nine_types(self, tmp_path):
        bindings = [{"type": t, "path": f"a.b.{t}"}
                    for t in CoverageBinding.TYPES]
        data = {"testpoints": [{"name": "tp", "stage": "V1",
                                 "tests": ["t"],
                                 "coverage": bindings}]}
        path = _write_json(tmp_path, data)
        plan = load_testplan(path)
        types = {b.type for b in plan.testpoints[0].coverage}
        assert types == CoverageBinding.TYPES

    def test_unknown_type_warns_not_raises(self, tmp_path):
        data = {"testpoints": [{"name": "tp", "stage": "V1", "tests": ["t"],
                                 "coverage": [{"type": "unknown_type",
                                               "path": "a.b"}]}]}
        path = _write_json(tmp_path, data)
        with warnings.catch_warnings(record=True) as w:
            warnings.simplefilter("always")
            plan = load_testplan(path)
        assert any("unknown_type" in str(x.message) for x in w)
        # The binding is still stored
        assert plan.testpoints[0].coverage[0].type == "unknown_type"

    def test_glob_path_preserved(self, tmp_path):
        data = {"testpoints": [{"name": "tp", "stage": "V1", "tests": ["t"],
                                 "coverage": [{"type": "covergroup",
                                               "path": "env.*.cg"}]}]}
        path = _write_json(tmp_path, data)
        plan = load_testplan(path)
        assert plan.testpoints[0].coverage[0].path == "env.*.cg"


# ── import resolution through YAML reader ────────────────────────────────────

class TestImportResolutionViaReader:
    def test_imports_merged(self, tmp_path):
        child = str(tmp_path / "child.json")
        (tmp_path / "child.json").write_text(
            json.dumps({"testpoints": [{"name": "child_tp", "stage": "V1",
                                        "tests": ["child_test"]}]}),
            encoding="utf-8",
        )
        parent_data = {
            "testpoints": [{"name": "parent_tp", "stage": "V1",
                             "tests": ["parent_test"]}],
            "imports": [{"path": child}],
        }
        path = _write_json(tmp_path, parent_data)
        plan = load_testplan(path)
        names = {tp.name for tp in plan.testpoints}
        assert "parent_tp" in names
        assert "child_tp" in names

    def test_circular_import_raises(self, tmp_path):
        a_path = str(tmp_path / "a.json")
        b_path = str(tmp_path / "b.json")
        (tmp_path / "b.json").write_text(
            json.dumps({"imports": [{"path": a_path}]}), encoding="utf-8"
        )
        (tmp_path / "a.json").write_text(
            json.dumps({"imports": [{"path": b_path}]}), encoding="utf-8"
        )
        with pytest.raises(ParseError, match="circular"):
            load_testplan(a_path)
