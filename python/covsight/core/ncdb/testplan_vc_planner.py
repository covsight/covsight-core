"""Synopsys Verdi VC Planner reader.

Supports both CSV and XML variants of the VC Planner testplan format.

**CSV format** — one row per item, with at least a ``name`` and ``type``
column. ``type`` is one of ``group``, ``test``, or ``coverpoint``.  Hierarchy
is implicit: a ``test`` row belongs to the last ``group`` row seen; a
``coverpoint`` row belongs to the last ``test`` row seen.

**XML format** — a ``<vcplanner>`` root containing nested ``<group>``,
``<test>``, and ``<coverpoint>`` elements.  The same fields as CSV are
available as element attributes.
"""
from __future__ import annotations

import csv
import os
import xml.etree.ElementTree as ET
from typing import List, Optional

from .testplan import (
    CoverageBinding,
    CovergroupEntry,
    CoverpointEntry,
    Goal,
    Testplan,
    Testpoint,
)

_DEFAULT_STAGE = "V1"

_STATUS_MAP = {
    "active":      "in_progress",
    "complete":    "complete",
    "done":        "complete",
    "waived":      "waived",
    "planned":     "planned",
    "not_started": "planned",
}

_PRIORITY_MAP = {
    "high": "high",
    "medium": "medium",
    "med": "medium",
    "low": "low",
}


def import_vc_planner(path: str) -> Testplan:
    """Parse a Synopsys VC Planner CSV or XML file and return a :class:`Testplan`.

    The file type is determined by the extension (``.csv`` → CSV parser,
    anything else → XML parser).

    Args:
        path: Path to the VC Planner file.

    Returns:
        A :class:`Testplan` populated from the VC Planner file.

    Raises:
        FileNotFoundError: If *path* does not exist.
        ValueError: If the CSV file is missing the required ``name`` column.
    """
    _, ext = os.path.splitext(path)
    if ext.lower() == ".csv":
        return _import_csv(path)
    return _import_xml(path)


# ---------------------------------------------------------------------------
# CSV parser
# ---------------------------------------------------------------------------

def _import_csv(path: str) -> Testplan:
    plan = Testplan(source_file=path)
    current_goal: Optional[Goal] = None
    current_tp: Optional[Testpoint] = None

    with open(path, newline="", encoding="utf-8") as fh:
        reader = csv.DictReader(fh)
        if reader.fieldnames is None or "name" not in reader.fieldnames:
            raise ValueError(f"VC Planner CSV {path!r} missing required 'name' column")

        for row in reader:
            kind = row.get("type", "").lower().strip()
            name = row.get("name", "").strip()
            if not name:
                continue

            if kind == "group":
                current_goal = _row_to_goal(row)
                plan.goals.append(current_goal)
                current_tp = None
            elif kind == "test":
                tp = _row_to_testpoint(row)
                if current_goal is not None:
                    current_goal.testpoints.append(tp)
                else:
                    plan.testpoints.append(tp)
                current_tp = tp
            elif kind == "coverpoint":
                cp_name = name
                cp_path = row.get("path", name)
                binding = CoverageBinding(
                    type="coverpoint", path=cp_path,
                    desc=row.get("description", row.get("desc", "")),
                )
                if current_tp is not None:
                    current_tp.coverage.append(binding)
                cp = CoverpointEntry(name=cp_name, path=cp_path,
                                     desc=binding.desc)
                _ensure_covergroup(plan.covergroups, cp_name, cp)

    return plan


# ---------------------------------------------------------------------------
# XML parser
# ---------------------------------------------------------------------------

def _import_xml(path: str) -> Testplan:
    tree = ET.parse(path)
    root = tree.getroot()
    plan = Testplan(source_file=path)
    plan.name = root.get("name", "")
    plan.description = root.get("description", root.get("desc", ""))

    for g_elem in root.findall("group"):
        plan.goals.append(_xml_group(g_elem, plan.covergroups))
    for t_elem in root.findall("test"):
        plan.testpoints.append(_xml_test(t_elem, plan.covergroups))

    return plan


def _xml_group(elem: ET.Element, covergroups: List[CovergroupEntry]) -> Goal:
    raw_status = elem.get("status", "")
    goal = Goal(
        id=elem.get("id", elem.get("name", "")),
        title=elem.get("name", elem.get("title", "")),
        desc=elem.get("description", elem.get("desc", "")),
        owner=elem.get("owner", ""),
        priority=_PRIORITY_MAP.get(elem.get("priority", "").lower(), ""),
        status=_STATUS_MAP.get(raw_status.lower(), raw_status),
        tags=[t.text.strip() for t in elem.findall("tag") if t.text],
    )
    for sub in elem.findall("group"):
        goal.goals.append(_xml_group(sub, covergroups))
    for t_elem in elem.findall("test"):
        goal.testpoints.append(_xml_test(t_elem, covergroups))
    return goal


def _xml_test(elem: ET.Element, covergroups: List[CovergroupEntry]) -> Testpoint:
    tests_str = elem.get("tests", "")
    tp = Testpoint(
        name=elem.get("name", ""),
        stage=elem.get("stage", _DEFAULT_STAGE),
        desc=elem.get("description", elem.get("desc", "")),
        owner=elem.get("owner", ""),
        priority=_PRIORITY_MAP.get(elem.get("priority", "").lower(), ""),
        tests=[t.strip() for t in tests_str.split(",") if t.strip()],
    )
    weight_str = elem.get("weight", "")
    if weight_str.isdigit():
        tp.weight = int(weight_str)
    for cp_elem in elem.findall("coverpoint"):
        cp_name = cp_elem.get("name", "")
        cp_path = cp_elem.get("path", cp_name)
        tp.coverage.append(CoverageBinding(type="coverpoint", path=cp_path))
        _ensure_covergroup(
            covergroups, cp_name,
            CoverpointEntry(name=cp_name, path=cp_path,
                             desc=cp_elem.get("desc", "")),
        )
    return tp


# ---------------------------------------------------------------------------
# Shared helpers
# ---------------------------------------------------------------------------

def _row_to_goal(row: dict) -> Goal:
    raw_status = row.get("status", "")
    return Goal(
        id=row.get("id", row.get("name", "")),
        title=row.get("name", row.get("title", "")),
        desc=row.get("description", row.get("desc", "")),
        owner=row.get("owner", ""),
        priority=_PRIORITY_MAP.get(row.get("priority", "").lower(), ""),
        status=_STATUS_MAP.get(raw_status.lower(), raw_status),
    )


def _row_to_testpoint(row: dict) -> Testpoint:
    tests_str = row.get("tests", "")
    tp = Testpoint(
        name=row.get("name", ""),
        stage=row.get("stage", _DEFAULT_STAGE),
        desc=row.get("description", row.get("desc", "")),
        owner=row.get("owner", ""),
        priority=_PRIORITY_MAP.get(row.get("priority", "").lower(), ""),
        tests=[t.strip() for t in tests_str.split(",") if t.strip()],
    )
    weight_str = row.get("weight", "")
    if weight_str.isdigit():
        tp.weight = int(weight_str)
    return tp


def _ensure_covergroup(covergroups: List[CovergroupEntry],
                        cp_name: str,
                        cp: CoverpointEntry) -> None:
    """Add *cp* to the first covergroup or create a default one."""
    cg_name = cp_name.split(".")[0] if "." in cp_name else "default"
    existing = next((cg for cg in covergroups if cg.name == cg_name), None)
    if existing is None:
        existing = CovergroupEntry(name=cg_name)
        covergroups.append(existing)
    if not any(c.name == cp_name for c in existing.coverpoints):
        existing.coverpoints.append(cp)
