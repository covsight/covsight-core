"""
covsight/core/ncdb/testplan_yaml.py — Canonical YAML/JSON reader for CovSight
testplans.

This is the primary reader for the schema described in ``testplan-schema.md``.
It supports:

* YAML (primary authoring format) — ``.yaml`` / ``.yml``
* JSON — ``.json``
* Hjson (OpenTitan compatibility) — ``.hjson`` (requires ``hjson`` package)
* ``.testplan`` as a generic alias (YAML assumed)

Usage::

    from covsight.core.ncdb.testplan_yaml import load_testplan

    plan = load_testplan("uart.yaml")
    plan = load_testplan("uart.yaml", substitutions={"name": "uart"})
"""
from __future__ import annotations

import itertools
import json
import os
import re
from typing import Any, Dict, List, Optional

from .testplan import (
    CoverageBinding,
    CovergroupEntry,
    CoverpointEntry,
    Goal,
    ImportEntry,
    RequirementLink,
    Testplan,
    Testpoint,
)
from .testplan_imports import ParseError, _parse_file, resolve_imports


# ── public API ────────────────────────────────────────────────────────────────

def load_testplan(path: str,
                  substitutions: Optional[Dict[str, Any]] = None) -> Testplan:
    """Load a CovSight testplan from a YAML, JSON, or Hjson file.

    Recursively resolves ``imports[]``, expands ``{key}`` wildcards in
    test-name templates via cartesian product, and returns a fully-populated
    :class:`~covsight.core.ncdb.testplan.Testplan`.

    Args:
        path:          Path to the testplan file.  Extension determines the
                       format: ``.yaml``/``.yml`` → YAML, ``.json`` → JSON,
                       ``.hjson`` → Hjson, ``.testplan`` → YAML.
        substitutions: Caller-supplied ``{key: value_or_list}`` pairs.  These
                       are **merged with** (but do not override) the
                       ``substitutions`` dict inside the file; use
                       ``file["substitutions"]`` to override them there.

    Returns:
        A :class:`~covsight.core.ncdb.testplan.Testplan` with every field
        populated.

    Raises:
        ParseError:   On structural errors, circular imports, or name
                      collisions.
        FileNotFoundError: If *path* does not exist.
    """
    abs_path = os.path.abspath(path)
    if not os.path.exists(abs_path):
        raise FileNotFoundError(f"testplan file not found: {abs_path}")

    raw = _parse_file(abs_path)

    # Merge caller-supplied substitutions (file-level win on collision)
    if substitutions:
        merged_subs = {**substitutions, **raw.get("substitutions", {})}
        raw["substitutions"] = merged_subs

    # Resolve imports recursively
    resolve_imports(raw,
                    base_dir=os.path.dirname(abs_path),
                    resolved_paths=set(),
                    parent_path=abs_path,
                    _visiting={abs_path})

    # Build the Testplan object
    subs = raw.get("substitutions", {})
    return _build_plan(raw, subs, abs_path)


def validate_testplan(plan_dict: dict) -> List[str]:
    """Validate *plan_dict* against the CovSight testplan JSON Schema.

    Requires the ``jsonschema`` package (``pip install covsight-core[validate]``).

    Args:
        plan_dict: Raw dict, e.g. from :meth:`~Testplan.to_dict`.

    Returns:
        List of validation error messages (empty list means valid).
    """
    try:
        import jsonschema  # type: ignore[import-untyped]
    except ImportError:
        raise ImportError(
            "jsonschema is required for validation: "
            "pip install 'covsight-core[validate]'"
        ) from None

    schema_path = os.path.join(
        os.path.dirname(__file__), "..", "schema", "testplan.schema.json"
    )
    with open(schema_path, "r", encoding="utf-8") as fh:
        schema = json.load(fh)

    errors: List[str] = []
    validator = jsonschema.Draft7Validator(schema)
    for err in validator.iter_errors(plan_dict):
        errors.append(f"{'/'.join(str(p) for p in err.absolute_path)}: "
                       f"{err.message}" if err.absolute_path else err.message)
    return errors


# ── builder ───────────────────────────────────────────────────────────────────

def _build_plan(raw: dict, subs: dict, source_file: str) -> Testplan:
    """Construct a :class:`Testplan` from a raw parsed dict."""
    plan = Testplan(
        format_version=raw.get("format_version", 1),
        schema=raw.get("$schema", raw.get("schema", "")),
        name=raw.get("name", ""),
        description=raw.get("description", ""),
        owner=raw.get("owner", ""),
        tags=raw.get("tags", []),
        substitutions=raw.get("substitutions", {}),
        imports=[
            ImportEntry(
                path=ie.get("path", "") if isinstance(ie, dict) else ie,
                substitutions=ie.get("substitutions", {})
                if isinstance(ie, dict) else {},
            )
            for ie in raw.get("imports", [])
        ],
        source_file=source_file,
        custom=raw.get("custom", {}),
    )

    for tp_raw in raw.get("testpoints", []):
        plan.testpoints.append(_build_testpoint(tp_raw, subs))

    for g_raw in raw.get("goals", []):
        plan.goals.append(_build_goal(g_raw, subs))

    for cg_raw in raw.get("covergroups", []):
        plan.covergroups.append(_build_covergroup(cg_raw))

    return plan


def _build_goal(raw: dict, subs: dict) -> Goal:
    goal = Goal(
        id=raw.get("id", ""),
        title=raw.get("title", ""),
        desc=raw.get("desc", ""),
        owner=raw.get("owner", ""),
        priority=raw.get("priority", ""),
        status=raw.get("status", ""),
        tags=raw.get("tags", []),
        custom=raw.get("custom", {}),
    )
    for tp_raw in raw.get("testpoints", []):
        goal.testpoints.append(_build_testpoint(tp_raw, subs))
    for sub_raw in raw.get("goals", []):
        goal.goals.append(_build_goal(sub_raw, subs))
    return goal


def _build_testpoint(raw: dict, subs: dict) -> Testpoint:
    raw_tests = raw.get("tests", [])
    na = False
    tests: List[str] = []
    source_templates: List[str] = []

    if raw_tests == ["N/A"]:
        na = True
    else:
        for tmpl in raw_tests:
            expanded = _expand_template(tmpl, subs)
            tests.extend(expanded)
            if len(expanded) > 1 or (expanded and expanded[0] != tmpl):
                source_templates.append(tmpl)

    return Testpoint(
        name=raw.get("name", ""),
        stage=raw.get("stage", ""),
        desc=raw.get("desc", ""),
        tests=tests,
        tags=raw.get("tags", []),
        na=na,
        source_template=", ".join(source_templates),
        requirements=[_build_req(r) for r in raw.get("requirements", [])],
        coverage=[_build_binding(b) for b in raw.get("coverage", [])],
        owner=raw.get("owner", ""),
        priority=raw.get("priority", ""),
        weight=raw.get("weight", 1),
        custom=raw.get("custom", {}),
    )


def _build_req(raw: dict) -> RequirementLink:
    return RequirementLink(
        system=raw.get("system", ""),
        project=raw.get("project", ""),
        item_id=raw.get("item_id", ""),
        url=raw.get("url", ""),
    )


def _build_binding(raw: dict) -> CoverageBinding:
    type_ = raw.get("type", "functional")
    if type_ not in CoverageBinding.TYPES:
        # Unknown type: warn but don't fail — store as-is
        import warnings
        warnings.warn(
            f"Unknown coverage binding type '{type_}'; "
            f"expected one of {sorted(CoverageBinding.TYPES)}",
            stacklevel=4,
        )
    return CoverageBinding(
        type=type_,
        path=raw.get("path", ""),
        desc=raw.get("desc", ""),
    )


def _build_covergroup(raw: dict) -> CovergroupEntry:
    return CovergroupEntry(
        name=raw.get("name", ""),
        desc=raw.get("desc", ""),
        coverpoints=[
            CoverpointEntry(
                name=cp.get("name", ""),
                desc=cp.get("desc", ""),
                path=cp.get("path", ""),
                custom=cp.get("custom", {}),
            )
            for cp in raw.get("coverpoints", [])
        ],
        custom=raw.get("custom", {}),
    )


# ── substitution expansion ────────────────────────────────────────────────────

def _expand_template(template: str, subs: Dict[str, Any]) -> List[str]:
    """Expand ``{key}`` placeholders in *template* via cartesian product.

    Mirrors the logic in :mod:`testplan_hjson` but is shared here so both
    readers use identical expansion semantics.
    """
    keys_found = re.findall(r'\{(\w+)\}', template)
    if not keys_found:
        return [template]

    lists: List[List[str]] = []
    ordered_keys: List[str] = []
    for key in dict.fromkeys(keys_found):
        val = subs.get(key)
        if val is None:
            lists.append([f"{{{key}}}"])
        elif isinstance(val, list):
            lists.append([str(v) for v in val])
        else:
            lists.append([str(val)])
        ordered_keys.append(key)

    results: List[str] = []
    for combo in itertools.product(*lists):
        s = template
        for key, replacement in zip(ordered_keys, combo):
            s = s.replace(f"{{{key}}}", replacement)
        results.append(s)
    return results
