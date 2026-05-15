"""
src/ucis/ncdb/testplan_hjson.py — Import OpenTitan-style Hjson testplans.

The OpenTitan testplan format is a Hjson (human JSON) file with a ``testpoints``
list.  Each testpoint can have a ``tests`` list that uses ``{key}`` wildcards
expanded by cartesian product with a ``substitutions`` dict.  ``tests: ["N/A"]``
marks a testpoint as intentionally unmapped.

OpenTitan-specific keys handled:

* ``import_testplans`` — list of file paths to merge (resolved via
  :func:`~covsight.core.ncdb.testplan_imports.resolve_imports`).
* ``name`` at plan level — treated as ``substitutions.name`` (OpenTitan
  convention where the plan file itself declares the DUT name).
* ``substitutions`` dict — merged with the *substitutions* argument
  (file wins on collision, same as :func:`load_testplan`).
* ``requirements`` list on testpoints — mapped to
  :class:`~covsight.core.ncdb.testplan.RequirementLink` objects.

Falls back to standard ``json`` if the ``hjson`` package is not installed
(works for files that happen to be valid JSON or JSON-subset Hjson).
"""
from __future__ import annotations

import itertools
import os
import re
from typing import Dict, List, Optional

from .testplan import CovergroupEntry, RequirementLink, Testplan, Testpoint
from .testplan_imports import resolve_imports, _parse_file

try:
    import hjson as _hjson
    _HJSON_AVAILABLE = True
except ImportError:
    import json as _hjson  # type: ignore[no-redef]
    _HJSON_AVAILABLE = False


# ── public API ────────────────────────────────────────────────────────────────

def import_hjson(hjson_path: str,
                 substitutions: Optional[Dict[str, object]] = None) -> Testplan:
    """Parse an OpenTitan-style Hjson testplan and return a :class:`~covsight.core.ncdb.testplan.Testplan`.

    Handles OpenTitan-specific keys in addition to the base schema:

    * ``import_testplans`` — list of paths to merge transitively.
    * Plan-level ``name`` key — treated as a substitution (``{name}``).
    * ``substitutions`` dict — merged with the *substitutions* arg.
    * ``requirements`` list on testpoints.

    Args:
        hjson_path:    Path to the ``.hjson`` (or ``.json``) file.
        substitutions: Optional dict of ``{key: value_or_list}`` pairs used
                       for wildcard expansion in test names.  The file's own
                       ``substitutions`` dict takes precedence on collision.

    Returns:
        A fully expanded :class:`~covsight.core.ncdb.testplan.Testplan` with
        all ``{key}`` templates replaced and imports merged.
    """
    abs_path = os.path.abspath(hjson_path)
    data = _parse_file(abs_path)

    # OpenTitan convention: plan-level "name" key acts as substitutions.name
    plan_name = data.get("name", "")
    file_subs: dict = dict(data.get("substitutions", {}))
    if plan_name and "name" not in file_subs:
        file_subs["name"] = plan_name

    # Merge: caller-supplied ← file-level (file wins)
    effective_subs = {**(substitutions or {}), **file_subs}

    # Normalise OpenTitan's "import_testplans" key to the standard "imports"
    if "import_testplans" in data and "imports" not in data:
        data["imports"] = [{"path": p} for p in data["import_testplans"]]

    # Resolve imports recursively
    resolve_imports(data,
                    base_dir=os.path.dirname(abs_path),
                    resolved_paths=set(),
                    parent_path=abs_path,
                    _visiting={abs_path})

    plan = Testplan(
        name=plan_name,
        substitutions=effective_subs,
        source_file=abs_path,
    )

    for rec in data.get("testpoints", []):
        plan.add_testpoint(_parse_testpoint(rec, effective_subs))

    for rec in data.get("covergroups", []):
        plan.covergroups.append(CovergroupEntry(
            name=rec.get("name", ""),
            desc=rec.get("desc", ""),
        ))

    return plan


# ── internal helpers ──────────────────────────────────────────────────────────

def _parse_testpoint(rec: dict, subs: dict) -> Testpoint:
    """Parse one testpoint record from an OpenTitan Hjson file."""
    raw_tests = rec.get("tests", [])
    if raw_tests == ["N/A"]:
        return Testpoint(
            name=rec.get("name", ""),
            stage=rec.get("stage", ""),
            desc=rec.get("desc", ""),
            tags=rec.get("tags", []),
            na=True,
            tests=[],
            source_template="",
            requirements=_parse_requirements(rec.get("requirements", [])),
        )

    expanded: List[str] = []
    templates: List[str] = []
    for tmpl in raw_tests:
        results = _expand_template(tmpl, subs)
        expanded.extend(results)
        if len(results) > 1 or tmpl != results[0]:
            templates.append(tmpl)

    return Testpoint(
        name=rec.get("name", ""),
        stage=rec.get("stage", ""),
        desc=rec.get("desc", ""),
        tags=rec.get("tags", []),
        na=False,
        tests=expanded,
        source_template=", ".join(templates),
        requirements=_parse_requirements(rec.get("requirements", [])),
    )


def _parse_requirements(raw: list) -> List[RequirementLink]:
    return [
        RequirementLink(
            system=r.get("system", ""),
            project=r.get("project", ""),
            item_id=r.get("item_id", ""),
            url=r.get("url", ""),
        )
        for r in raw
        if isinstance(r, dict)
    ]


# ── internal helpers ──────────────────────────────────────────────────────────

def _expand_template(template: str,
                     subs: Dict[str, object]) -> List[str]:
    """Expand ``{key}`` placeholders in *template* using *subs*.

    Each ``{key}`` whose value in *subs* is a list produces multiple
    output strings (cartesian product).  Scalar values are substituted
    directly.  Keys absent from *subs* are left as-is.

    Examples::

        _expand_template("uart_{baud}_test", {"baud": ["9600", "115200"]})
        # → ["uart_9600_test", "uart_115200_test"]

        _expand_template("{mod}_{type}", {"mod": ["a", "b"], "type": "x"})
        # → ["a_x", "b_x"]
    """
    keys_found = re.findall(r'\{(\w+)\}', template)
    if not keys_found:
        return [template]

    # Build lists for each placeholder
    lists: List[List[str]] = []
    ordered_keys: List[str] = []
    for key in dict.fromkeys(keys_found):   # preserve order, deduplicate
        val = subs.get(key)
        if val is None:
            lists.append([f"{{{key}}}"])    # unknown key left verbatim
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


def _expand_tests(test_list: List[str],
                  subs: Dict[str, object]) -> List[str]:
    """Expand an entire ``tests`` list, returning the flat list of names."""
    result: List[str] = []
    for tmpl in test_list:
        result.extend(_expand_template(tmpl, subs))
    return result
