"""Tests for validate_testplan() and the testplan JSON Schema."""
import pytest
from covsight.core.ncdb.testplan_yaml import validate_testplan


def _minimal_tp():
    return {"name": "tp1", "stage": "V1"}


def _minimal_plan(**extra):
    d = {"testpoints": [_minimal_tp()]}
    d.update(extra)
    return d


# ── happy-path tests ──────────────────────────────────────────────────────────

class TestValidateValid:
    def test_minimal_plan_is_valid(self):
        assert validate_testplan({}) == []

    def test_full_testpoint_fields(self):
        plan = _minimal_plan()
        plan["testpoints"] = [{
            "name": "tp", "stage": "V1",
            "desc": "A testpoint",
            "tests": ["test_a", "test_b"],
            "tags": ["smoke"],
            "na": False,
            "owner": "alice",
            "priority": "high",
            "weight": 3,
            "requirements": [
                {"system": "JIRA", "item_id": "REQ-1", "url": "http://x"}
            ],
            "coverage": [
                {"type": "covergroup", "path": "top.dut.cg*", "desc": "cg"}
            ],
            "custom": {"foo": "bar"},
        }]
        assert validate_testplan(plan) == []

    def test_goals_nested(self):
        plan = {
            "goals": [{
                "id": "g1", "title": "Goal 1",
                "goals": [{"id": "g1.1", "title": "Sub-goal"}],
                "testpoints": [{"name": "tp", "stage": "V1"}],
            }],
        }
        assert validate_testplan(plan) == []

    def test_all_priority_values(self):
        for prio in ("high", "medium", "low", ""):
            plan = {"testpoints": [{"name": "tp", "stage": "V1", "priority": prio}]}
            assert validate_testplan(plan) == [], f"priority={prio!r} should be valid"

    def test_all_coverage_types(self):
        types = [
            "covergroup", "coverpoint", "cross",
            "assertion", "expression", "toggle",
            "line", "branch", "functional",
        ]
        for ct in types:
            plan = {"testpoints": [{
                "name": "tp", "stage": "V1",
                "coverage": [{"type": ct, "path": "top.*"}],
            }]}
            assert validate_testplan(plan) == [], f"coverage type {ct!r} should be valid"

    def test_all_goal_status_values(self):
        for status in ("planned", "in_progress", "complete", "waived", ""):
            plan = {"goals": [{"id": "g1", "title": "T", "status": status}]}
            assert validate_testplan(plan) == [], f"status={status!r} should be valid"

    def test_imports_entry(self):
        plan = {"imports": [{"path": "other.yaml"},
                             {"path": "x.yaml", "substitutions": {"k": "v"}}]}
        assert validate_testplan(plan) == []

    def test_top_level_metadata(self):
        plan = {
            "format_version": 2,
            "name": "uart",
            "description": "UART testplan",
            "owner": "bob",
            "tags": ["module", "uart"],
            "substitutions": {"baud": "9600"},
            "custom": {"dept": "hw-verify"},
        }
        assert validate_testplan(plan) == []

    def test_covergroup_with_coverpoints(self):
        plan = {"covergroups": [{
            "name": "cg_baud",
            "desc": "Baud rate covergroup",
            "coverpoints": [
                {"name": "cp_baud", "desc": "Baud value", "path": "top.cg.cp_baud"},
            ],
            "custom": {"tool": "vcs"},
        }]}
        assert validate_testplan(plan) == []


# ── error-path tests ──────────────────────────────────────────────────────────

class TestValidateInvalid:
    def test_testpoint_missing_name(self):
        errors = validate_testplan({"testpoints": [{"stage": "V1"}]})
        assert errors, "expected error for missing 'name'"

    def test_testpoint_missing_stage(self):
        errors = validate_testplan({"testpoints": [{"name": "tp"}]})
        assert errors, "expected error for missing 'stage'"

    def test_goal_missing_id(self):
        errors = validate_testplan({"goals": [{"title": "G"}]})
        assert errors, "expected error for missing 'id'"

    def test_goal_missing_title(self):
        errors = validate_testplan({"goals": [{"id": "g1"}]})
        assert errors, "expected error for missing 'title'"

    def test_bad_priority_value(self):
        plan = {"testpoints": [{"name": "tp", "stage": "V1", "priority": "critical"}]}
        errors = validate_testplan(plan)
        assert errors, "expected error for invalid priority 'critical'"

    def test_bad_coverage_type(self):
        plan = {"testpoints": [{
            "name": "tp", "stage": "V1",
            "coverage": [{"type": "fsm", "path": "top.*"}],
        }]}
        errors = validate_testplan(plan)
        assert errors, "expected error for invalid coverage type 'fsm'"

    def test_bad_goal_status(self):
        plan = {"goals": [{"id": "g1", "title": "T", "status": "blocked"}]}
        errors = validate_testplan(plan)
        assert errors, "expected error for invalid status 'blocked'"

    def test_coverage_binding_missing_path(self):
        plan = {"testpoints": [{
            "name": "tp", "stage": "V1",
            "coverage": [{"type": "covergroup"}],
        }]}
        errors = validate_testplan(plan)
        assert errors, "expected error for missing coverage 'path'"

    def test_import_entry_missing_path(self):
        errors = validate_testplan({"imports": [{"substitutions": {"k": "v"}}]})
        assert errors, "expected error for import entry missing 'path'"

    def test_unknown_top_level_property(self):
        errors = validate_testplan({"not_a_known_key": 42})
        assert errors, "expected error for unknown top-level property"

    def test_weight_must_be_integer(self):
        plan = {"testpoints": [{"name": "tp", "stage": "V1", "weight": "high"}]}
        errors = validate_testplan(plan)
        assert errors, "expected error for non-integer weight"

    def test_tags_must_be_array(self):
        plan = {"testpoints": [{"name": "tp", "stage": "V1", "tags": "smoke"}]}
        errors = validate_testplan(plan)
        assert errors, "expected error for non-array tags"


# ── import error tests ────────────────────────────────────────────────────────

class TestValidateImportError:
    def test_missing_jsonschema_raises(self, monkeypatch):
        import builtins, importlib
        real_import = builtins.__import__
        def mock_import(name, *args, **kwargs):
            if name == "jsonschema":
                raise ImportError("no module named jsonschema")
            return real_import(name, *args, **kwargs)
        monkeypatch.setattr(builtins, "__import__", mock_import)
        with pytest.raises(ImportError, match="jsonschema"):
            validate_testplan({})
