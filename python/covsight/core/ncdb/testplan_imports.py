"""
covsight/core/ncdb/testplan_imports.py — Import resolver for CovSight testplans.

Handles recursive ``imports[]`` resolution:

* Paths are relative to the importing file (or absolute).
* Substitutions in an ``ImportEntry`` override plan-level substitutions for
  testpoints sourced from that import.
* Each file is merged **once** — duplicate references (same resolved absolute
  path appearing via multiple import chains) are silently skipped.
* Circular imports (A → B → A) raise :class:`ParseError`.
* Testpoint name collisions across merged plans raise :class:`ParseError`.
"""
from __future__ import annotations

import json
import os
from typing import Dict, Optional, Set

from .testplan import (
    CovergroupEntry,
    CoverpointEntry,
    Goal,
    ImportEntry,
    Testplan,
    Testpoint,
    _cg_from_dict,
    _goal_from_dict,
    _tp_from_dict,
)


class ParseError(ValueError):
    """Raised when a testplan file cannot be parsed or is structurally invalid.

    Attributes:
        path:    Absolute path to the file where the error was detected.
        message: Human-readable description of the problem.
    """
    def __init__(self, message: str, path: str = "") -> None:
        super().__init__(f"{path}: {message}" if path else message)
        self.path = path
        self.message = message


# ── public API ────────────────────────────────────────────────────────────────

def resolve_imports(plan_dict: dict,
                    base_dir: str,
                    resolved_paths: Optional[Set[str]] = None,
                    parent_path: str = "",
                    _visiting: Optional[Set[str]] = None) -> dict:
    """Recursively resolve and merge ``imports[]`` from *plan_dict*.

    The function mutates *plan_dict* in-place **and** returns it for
    convenience.  After the call, ``plan_dict["testpoints"]``,
    ``plan_dict["goals"]``, and ``plan_dict["covergroups"]`` contain all
    items from imported files (merged after the file's own items).

    Args:
        plan_dict:      Raw parsed dict for the current file.
        base_dir:       Directory containing the current file; used to
                        resolve relative import paths.
        resolved_paths: Set of already **fully merged** absolute paths
                        (deduplication: each file is merged only once).
                        Pass ``None`` to start a fresh set.
        parent_path:    Absolute path of the importing file (for error
                        messages).
        _visiting:      Internal — set of files currently on the recursion
                        stack (used for cycle detection).  Do not pass.

    Returns:
        The mutated *plan_dict*.

    Raises:
        ParseError: On circular import, missing file, or testpoint name
                    collision across imported plans.
    """
    if resolved_paths is None:
        resolved_paths = set()
    if _visiting is None:
        _visiting = set()
    if parent_path:
        _visiting.add(parent_path)

    # Normalize OpenTitan's "import_testplans" key to standard "imports"
    if "import_testplans" in plan_dict and "imports" not in plan_dict:
        plan_dict["imports"] = [{"path": p} for p in plan_dict["import_testplans"]]

    plan_level_subs: dict = plan_dict.get("substitutions", {})

    for entry in plan_dict.get("imports", []):
        rel_path = entry.get("path", "") if isinstance(entry, dict) else entry.path
        import_subs: dict = (entry.get("substitutions", {})
                             if isinstance(entry, dict) else entry.substitutions)
        if not rel_path:
            continue

        abs_path = os.path.normpath(
            rel_path if os.path.isabs(rel_path)
            else os.path.join(base_dir, rel_path)
        )

        # Cycle: the file is currently being processed on the call stack
        if abs_path in _visiting:
            raise ParseError(
                f"circular import: '{abs_path}' is an ancestor of '{parent_path}'",
                path=parent_path,
            )

        # Dedup: already fully merged on a different branch — skip silently
        if abs_path in resolved_paths:
            continue

        if not os.path.exists(abs_path):
            raise ParseError(f"imported file not found: {abs_path}",
                             path=parent_path)

        imported_raw = _parse_file(abs_path)

        # Effective substitutions: plan-level ← import-level (import wins)
        effective_subs = {**plan_level_subs, **import_subs}
        if effective_subs:
            imported_raw.setdefault("substitutions", {})
            imported_raw["substitutions"] = {
                **imported_raw["substitutions"],
                **effective_subs,
            }

        # Recurse into the imported file's own imports
        resolve_imports(imported_raw,
                        base_dir=os.path.dirname(abs_path),
                        resolved_paths=resolved_paths,
                        parent_path=abs_path,
                        _visiting=_visiting)

        # Mark as fully merged
        resolved_paths.add(abs_path)

        # Merge testpoints, goals, covergroups into the parent dict
        _merge_list(plan_dict, imported_raw, "testpoints")
        _merge_list(plan_dict, imported_raw, "goals")
        _merge_list(plan_dict, imported_raw, "covergroups")

    if parent_path:
        _visiting.discard(parent_path)

    # Check for testpoint name collisions across all merged testpoints
    _check_name_collisions(plan_dict, parent_path)

    return plan_dict


# ── internal helpers ──────────────────────────────────────────────────────────

def _parse_file(path: str) -> dict:
    """Parse a YAML, JSON, or Hjson file and return the raw dict."""
    ext = os.path.splitext(path)[1].lower()
    with open(path, "r", encoding="utf-8") as fh:
        raw = fh.read()

    if ext in (".yaml", ".yml"):
        import yaml  # type: ignore[import-untyped]
        return yaml.safe_load(raw) or {}

    if ext == ".hjson":
        try:
            import hjson  # type: ignore[import-untyped]
            return hjson.loads(raw) or {}
        except ImportError:
            pass

    return json.loads(raw) or {}


def _merge_list(target: dict, source: dict, key: str) -> None:
    """Append ``source[key]`` items to ``target[key]``."""
    items = source.get(key)
    if items:
        target.setdefault(key, [])
        target[key].extend(items)


def _check_name_collisions(plan_dict: dict, file_path: str) -> None:
    """Raise ParseError if any two testpoints share the same ``name``."""
    seen: Dict[str, int] = {}
    for tp in plan_dict.get("testpoints", []):
        name = tp.get("name", "") if isinstance(tp, dict) else tp.name
        if not name:
            continue
        seen[name] = seen.get(name, 0) + 1
        if seen[name] > 1:
            raise ParseError(
                f"duplicate testpoint name: '{name}'",
                path=file_path,
            )
    # Also check testpoints nested in goals
    for goal in plan_dict.get("goals", []):
        goal_dict = goal if isinstance(goal, dict) else {}
        for tp in goal_dict.get("testpoints", []):
            name = tp.get("name", "") if isinstance(tp, dict) else tp.name
            if not name:
                continue
            seen[name] = seen.get(name, 0) + 1
            if seen[name] > 1:
                raise ParseError(
                    f"duplicate testpoint name: '{name}'",
                    path=file_path,
                )


def _is_ancestor(candidate: str, resolved: Set[str], current: str) -> bool:
    """Unused — kept for API stability. Cycle detection now uses _visiting."""
    return candidate == current
