"""Unit tests for src/ucis/ncdb/testplan_hjson.py."""
from __future__ import annotations

import json
import os
import pytest

from covsight.core.ncdb.testplan_hjson import (
    _expand_template,
    _expand_tests,
    import_hjson,
)
from covsight.core.ncdb.testplan import Testplan


# ── _expand_template ──────────────────────────────────────────────────────────

class TestExpandTemplate:
    def test_no_placeholders(self):
        assert _expand_template("uart_smoke", {}) == ["uart_smoke"]

    def test_scalar_substitution(self):
        assert _expand_template("test_{baud}", {"baud": "9600"}) == ["test_9600"]

    def test_list_substitution_cartesian(self):
        result = _expand_template("test_{baud}", {"baud": ["9600", "115200"]})
        assert result == ["test_9600", "test_115200"]

    def test_multiple_keys_cartesian_product(self):
        result = _expand_template("{mod}_{type}_test",
                                  {"mod": ["a", "b"], "type": ["x", "y"]})
        assert len(result) == 4
        assert "a_x_test" in result
        assert "b_y_test" in result

    def test_unknown_key_left_verbatim(self):
        result = _expand_template("test_{unknown}", {})
        assert result == ["test_{unknown}"]

    def test_mixed_known_unknown(self):
        result = _expand_template("{a}_{b}", {"a": "hello"})
        assert result == ["hello_{b}"]

    def test_duplicate_key_in_template(self):
        # {a} appears twice — should expand both consistently
        result = _expand_template("{a}_{a}", {"a": ["x", "y"]})
        assert set(result) == {"x_x", "y_y"}

    def test_no_subs_empty_dict(self):
        result = _expand_template("{x}", {})
        assert result == ["{x}"]


# ── _expand_tests ─────────────────────────────────────────────────────────────

class TestExpandTests:
    def test_flat_list_no_expansion(self):
        result = _expand_tests(["a", "b", "c"], {})
        assert result == ["a", "b", "c"]

    def test_with_expansion(self):
        result = _expand_tests(["{m}_test"], {"m": ["u", "v"]})
        assert result == ["u_test", "v_test"]

    def test_mixed_plain_and_template(self):
        result = _expand_tests(["plain", "{x}_test"], {"x": ["a", "b"]})
        assert result == ["plain", "a_test", "b_test"]


# ── import_hjson ──────────────────────────────────────────────────────────────

def _write_hjson(tmp_path, data: dict, name: str = "plan.json") -> str:
    path = str(tmp_path / name)
    with open(path, "w") as f:
        json.dump(data, f)
    return path


class TestImportHjson:
    def test_basic_import(self, tmp_path):
        path = _write_hjson(tmp_path, {
            "testpoints": [
                {"name": "uart_reset", "stage": "V1",
                 "tests": ["uart_smoke", "uart_init"]},
            ],
        })
        plan = import_hjson(path)
        assert isinstance(plan, Testplan)
        assert len(plan.testpoints) == 1
        tp = plan.testpoints[0]
        assert tp.name == "uart_reset"
        assert tp.stage == "V1"
        assert tp.tests == ["uart_smoke", "uart_init"]
        assert tp.na is False

    def test_na_testpoint(self, tmp_path):
        path = _write_hjson(tmp_path, {
            "testpoints": [
                {"name": "not_impl", "stage": "V2", "tests": ["N/A"]},
            ],
        })
        plan = import_hjson(path)
        tp = plan.testpoints[0]
        assert tp.na is True
        assert tp.tests == []

    def test_wildcard_expansion(self, tmp_path):
        path = _write_hjson(tmp_path, {
            "testpoints": [
                {"name": "tp", "stage": "V1",
                 "tests": ["{baud}_test"]},
            ],
        })
        plan = import_hjson(path, substitutions={"baud": ["9600", "115200"]})
        assert plan.testpoints[0].tests == ["9600_test", "115200_test"]

    def test_cartesian_expansion(self, tmp_path):
        path = _write_hjson(tmp_path, {
            "testpoints": [
                {"name": "tp", "stage": "V1",
                 "tests": ["{mod}_{intf}_test"]},
            ],
        })
        plan = import_hjson(path, substitutions={
            "mod": ["uart", "spi"],
            "intf": ["a", "b"],
        })
        assert len(plan.testpoints[0].tests) == 4

    def test_source_file_set(self, tmp_path):
        path = _write_hjson(tmp_path, {"testpoints": []})
        plan = import_hjson(path)
        assert os.path.isabs(plan.source_file)
        assert plan.source_file.endswith(".json")

    def test_covergroups_imported(self, tmp_path):
        path = _write_hjson(tmp_path, {
            "testpoints": [],
            "covergroups": [
                {"name": "cg_reset", "desc": "Reset coverage"},
            ],
        })
        plan = import_hjson(path)
        assert len(plan.covergroups) == 1
        assert plan.covergroups[0].name == "cg_reset"

    def test_optional_fields_defaults(self, tmp_path):
        path = _write_hjson(tmp_path, {
            "testpoints": [{"name": "tp", "stage": "V1", "tests": ["t"]}],
        })
        plan = import_hjson(path)
        tp = plan.testpoints[0]
        assert tp.desc == ""
        assert tp.tags == []

    def test_tags_preserved(self, tmp_path):
        path = _write_hjson(tmp_path, {
            "testpoints": [
                {"name": "tp", "stage": "V1", "tests": ["t"],
                 "tags": ["smoke", "regression"]},
            ],
        })
        plan = import_hjson(path, {})
        assert plan.testpoints[0].tags == ["smoke", "regression"]

    def test_source_template_recorded(self, tmp_path):
        path = _write_hjson(tmp_path, {
            "testpoints": [
                {"name": "tp", "stage": "V1", "tests": ["{x}_test"]},
            ],
        })
        plan = import_hjson(path, {"x": ["a", "b"]})
        # source_template captures the original template
        assert "{x}_test" in plan.testpoints[0].source_template

    def test_empty_testplan(self, tmp_path):
        path = _write_hjson(tmp_path, {})
        plan = import_hjson(path)
        assert plan.testpoints == []
        assert plan.covergroups == []


# ── import_testplans (OpenTitan import key) ───────────────────────────────────

class TestImportTestplans:
    def test_import_testplans_merged(self, tmp_path):
        child_path = _write_hjson(tmp_path, {
            "testpoints": [{"name": "child_tp", "stage": "V1",
                             "tests": ["child_test"]}],
        }, name="child.json")
        parent_path = _write_hjson(tmp_path, {
            "testpoints": [{"name": "parent_tp", "stage": "V1",
                             "tests": ["parent_test"]}],
            "import_testplans": [child_path],
        })
        plan = import_hjson(parent_path)
        names = {tp.name for tp in plan.testpoints}
        assert "parent_tp" in names
        assert "child_tp" in names

    def test_import_testplans_transitive(self, tmp_path):
        grand_path = _write_hjson(tmp_path, {
            "testpoints": [{"name": "grand_tp", "stage": "V1", "tests": ["g"]}],
        }, name="grand.json")
        mid_path = _write_hjson(tmp_path, {
            "testpoints": [{"name": "mid_tp", "stage": "V1", "tests": ["m"]}],
            "import_testplans": [grand_path],
        }, name="mid.json")
        parent_path = _write_hjson(tmp_path, {
            "testpoints": [{"name": "top_tp", "stage": "V1", "tests": ["t"]}],
            "import_testplans": [mid_path],
        })
        plan = import_hjson(parent_path)
        names = {tp.name for tp in plan.testpoints}
        assert names == {"top_tp", "mid_tp", "grand_tp"}


# ── plan-level substitutions ──────────────────────────────────────────────────

class TestPlanLevelSubstitutions:
    def test_name_key_used_as_substitution(self, tmp_path):
        path = _write_hjson(tmp_path, {
            "name": "uart",
            "testpoints": [{"name": "tp", "stage": "V1",
                             "tests": ["{name}_smoke"]}],
        })
        plan = import_hjson(path)
        assert plan.testpoints[0].tests == ["uart_smoke"]

    def test_file_substitutions_dict_used(self, tmp_path):
        path = _write_hjson(tmp_path, {
            "substitutions": {"baud": ["9600", "115200"]},
            "testpoints": [{"name": "tp", "stage": "V1",
                             "tests": ["{baud}_test"]}],
        })
        plan = import_hjson(path)
        assert set(plan.testpoints[0].tests) == {"9600_test", "115200_test"}

    def test_file_subs_override_caller_subs(self, tmp_path):
        # File says "name": "spi"; caller passes name="uart" → file wins
        path = _write_hjson(tmp_path, {
            "name": "spi",
            "testpoints": [{"name": "tp", "stage": "V1",
                             "tests": ["{name}_smoke"]}],
        })
        plan = import_hjson(path, substitutions={"name": "uart"})
        assert plan.testpoints[0].tests == ["spi_smoke"]

    def test_caller_subs_fill_unknown_keys(self, tmp_path):
        # File doesn't define "baud"; caller does
        path = _write_hjson(tmp_path, {
            "testpoints": [{"name": "tp", "stage": "V1",
                             "tests": ["{baud}_test"]}],
        })
        plan = import_hjson(path, substitutions={"baud": "9600"})
        assert plan.testpoints[0].tests == ["9600_test"]

    def test_plan_name_stored(self, tmp_path):
        path = _write_hjson(tmp_path, {"name": "uart", "testpoints": []})
        plan = import_hjson(path)
        assert plan.name == "uart"


# ── requirements parsing ──────────────────────────────────────────────────────

class TestRequirementsPreserved:
    def test_requirements_parsed(self, tmp_path):
        path = _write_hjson(tmp_path, {
            "testpoints": [
                {"name": "tp", "stage": "V1", "tests": ["t"],
                 "requirements": [
                     {"system": "JIRA", "project": "UART",
                      "item_id": "REQ-42", "url": "http://jira/42"},
                 ]},
            ],
        })
        plan = import_hjson(path)
        tp = plan.testpoints[0]
        assert len(tp.requirements) == 1
        req = tp.requirements[0]
        assert req.system == "JIRA"
        assert req.item_id == "REQ-42"
        assert req.url == "http://jira/42"

    def test_multiple_requirements(self, tmp_path):
        path = _write_hjson(tmp_path, {
            "testpoints": [
                {"name": "tp", "stage": "V1", "tests": ["t"],
                 "requirements": [
                     {"system": "JIRA", "item_id": "REQ-1"},
                     {"system": "ALM",  "item_id": "REQ-2"},
                 ]},
            ],
        })
        plan = import_hjson(path)
        assert len(plan.testpoints[0].requirements) == 2

    def test_empty_requirements_allowed(self, tmp_path):
        path = _write_hjson(tmp_path, {
            "testpoints": [{"name": "tp", "stage": "V1", "tests": ["t"]}],
        })
        plan = import_hjson(path)
        assert plan.testpoints[0].requirements == []

    def test_na_testpoint_with_requirements(self, tmp_path):
        path = _write_hjson(tmp_path, {
            "testpoints": [
                {"name": "tp", "stage": "V1", "tests": ["N/A"],
                 "requirements": [{"system": "JIRA", "item_id": "REQ-99"}]},
            ],
        })
        plan = import_hjson(path)
        tp = plan.testpoints[0]
        assert tp.na is True
        assert len(tp.requirements) == 1
