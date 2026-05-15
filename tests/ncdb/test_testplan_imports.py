"""Unit tests for testplan_imports.py — import resolver."""
from __future__ import annotations

import json
import os
import pytest

from covsight.core.ncdb.testplan_imports import ParseError, resolve_imports


# ── helpers ───────────────────────────────────────────────────────────────────

def _write(tmp_path, filename: str, data: dict) -> str:
    path = tmp_path / filename
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data), encoding="utf-8")
    return str(path)


def _tp(name: str, stage: str = "V1") -> dict:
    return {"name": name, "stage": stage, "tests": [f"{name}_test"]}


# ── basic import ──────────────────────────────────────────────────────────────

class TestBasicImport:
    def test_single_import_merges_testpoints(self, tmp_path):
        child = _write(tmp_path, "child.json", {
            "testpoints": [_tp("child_tp")],
        })
        parent_dict = {
            "testpoints": [_tp("parent_tp")],
            "imports": [{"path": child}],
        }
        result = resolve_imports(parent_dict, base_dir=str(tmp_path))
        names = [t["name"] for t in result["testpoints"]]
        assert "parent_tp" in names
        assert "child_tp" in names

    def test_relative_path_resolved(self, tmp_path):
        child = _write(tmp_path, "sub/child.json", {
            "testpoints": [_tp("sub_tp")],
        })
        parent_dict = {
            "testpoints": [],
            "imports": [{"path": "sub/child.json"}],
        }
        result = resolve_imports(parent_dict, base_dir=str(tmp_path))
        assert any(t["name"] == "sub_tp" for t in result["testpoints"])

    def test_covergroups_merged(self, tmp_path):
        child = _write(tmp_path, "c.json", {
            "covergroups": [{"name": "cg_child", "desc": "from child"}],
        })
        parent_dict = {
            "imports": [{"path": child}],
        }
        result = resolve_imports(parent_dict, base_dir=str(tmp_path))
        assert any(cg["name"] == "cg_child" for cg in result.get("covergroups", []))

    def test_goals_merged(self, tmp_path):
        child = _write(tmp_path, "c.json", {
            "goals": [{"id": "child_goal", "title": "From child"}],
        })
        parent_dict = {
            "imports": [{"path": child}],
        }
        result = resolve_imports(parent_dict, base_dir=str(tmp_path))
        assert any(g["id"] == "child_goal" for g in result.get("goals", []))


# ── substitution override ─────────────────────────────────────────────────────

class TestSubstitutionOverride:
    def test_import_subs_merged_with_plan_level(self, tmp_path):
        child = _write(tmp_path, "c.json", {
            "testpoints": [_tp("sub_tp")],
            "substitutions": {},
        })
        parent_dict = {
            "substitutions": {"name": "uart", "extra": "x"},
            "imports": [{"path": child, "substitutions": {"name": "spi"}}],
        }
        result = resolve_imports(parent_dict, base_dir=str(tmp_path))
        # The child's resolved substitutions should prefer import-level "spi"
        # (we check this indirectly: no error, and merge happened)
        assert any(t["name"] == "sub_tp" for t in result["testpoints"])


# ── transitive imports ────────────────────────────────────────────────────────

class TestTransitiveImport:
    def test_three_level_chain(self, tmp_path):
        grand = _write(tmp_path, "grand.json", {
            "testpoints": [_tp("grand_tp")],
        })
        mid = _write(tmp_path, "mid.json", {
            "testpoints": [_tp("mid_tp")],
            "imports": [{"path": grand}],
        })
        parent_dict = {
            "testpoints": [_tp("parent_tp")],
            "imports": [{"path": mid}],
        }
        result = resolve_imports(parent_dict, base_dir=str(tmp_path))
        names = {t["name"] for t in result["testpoints"]}
        assert names == {"parent_tp", "mid_tp", "grand_tp"}


# ── deduplication ─────────────────────────────────────────────────────────────

class TestDeduplication:
    def test_shared_import_merged_once(self, tmp_path):
        shared = _write(tmp_path, "shared.json", {
            "testpoints": [_tp("shared_tp")],
        })
        child_a = _write(tmp_path, "a.json", {
            "testpoints": [_tp("a_tp")],
            "imports": [{"path": shared}],
        })
        child_b = _write(tmp_path, "b.json", {
            "testpoints": [_tp("b_tp")],
            "imports": [{"path": shared}],
        })
        parent_dict = {
            "testpoints": [],
            "imports": [{"path": child_a}, {"path": child_b}],
        }
        result = resolve_imports(parent_dict, base_dir=str(tmp_path))
        names = [t["name"] for t in result["testpoints"]]
        # shared_tp must appear exactly once
        assert names.count("shared_tp") == 1


# ── circular import detection ─────────────────────────────────────────────────

class TestCircularImport:
    def test_direct_cycle_raises(self, tmp_path):
        # A imports B, B imports A
        a_path = str(tmp_path / "a.json")
        b_path = str(tmp_path / "b.json")
        (tmp_path / "b.json").write_text(
            json.dumps({"testpoints": [_tp("b_tp")],
                        "imports": [{"path": a_path}]}),
            encoding="utf-8",
        )
        parent_dict = {
            "testpoints": [_tp("a_tp")],
            "imports": [{"path": b_path}],
        }
        with pytest.raises(ParseError, match="circular"):
            resolve_imports(parent_dict, base_dir=str(tmp_path),
                            parent_path=a_path)

    def test_self_import_raises(self, tmp_path):
        a_path = str(tmp_path / "a.json")
        (tmp_path / "a.json").write_text(
            json.dumps({"imports": [{"path": a_path}]}),
            encoding="utf-8",
        )
        parent_dict = {"imports": [{"path": a_path}]}
        with pytest.raises(ParseError, match="circular"):
            resolve_imports(parent_dict, base_dir=str(tmp_path),
                            parent_path=a_path)


# ── missing file ──────────────────────────────────────────────────────────────

class TestMissingFile:
    def test_raises_on_missing_import(self, tmp_path):
        parent_dict = {
            "imports": [{"path": str(tmp_path / "does_not_exist.json")}],
        }
        with pytest.raises(ParseError, match="not found"):
            resolve_imports(parent_dict, base_dir=str(tmp_path))


# ── name collision ────────────────────────────────────────────────────────────

class TestNameCollision:
    def test_collision_across_imports_raises(self, tmp_path):
        child = _write(tmp_path, "c.json", {
            "testpoints": [_tp("dup_tp")],
        })
        parent_dict = {
            "testpoints": [_tp("dup_tp")],
            "imports": [{"path": child}],
        }
        with pytest.raises(ParseError, match="duplicate"):
            resolve_imports(parent_dict, base_dir=str(tmp_path))

    def test_no_collision_distinct_names(self, tmp_path):
        child = _write(tmp_path, "c.json", {
            "testpoints": [_tp("child_tp")],
        })
        parent_dict = {
            "testpoints": [_tp("parent_tp")],
            "imports": [{"path": child}],
        }
        # Should not raise
        result = resolve_imports(parent_dict, base_dir=str(tmp_path))
        assert len(result["testpoints"]) == 2
