"""Cadence vManager VPF XML reader.

Imports a Cadence vManager testplan XML (VPF format) and returns a
:class:`~covsight.core.ncdb.testplan.Testplan`.

Typical VPF structure::

    <testplan>
      <tpGoal name="Feature A" id="feat_a" owner="alice" status="active">
        <tpTest name="test_basic" stage="V1">
          <tpCoverPoint name="cg_basic" type="covergroup" path="top.cg"/>
        </tpTest>
        <tpGoal name="Sub-goal" ...>
          ...
        </tpGoal>
        <attributes>
          <attr name="jira_id" value="UART-10"/>
        </attributes>
      </tpGoal>
    </testplan>
"""
from __future__ import annotations

import re
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

# VPF status → CovSight status mapping
_STATUS_MAP = {
    "active":      "in_progress",
    "complete":    "complete",
    "done":        "complete",
    "waived":      "waived",
    "planned":     "planned",
    "not_started": "planned",
}

# VPF priority keywords (case-insensitive)
_PRIORITY_MAP = {
    "high": "high",
    "medium": "medium",
    "med": "medium",
    "low": "low",
}

# Default stage when not specified in VPF (VPF has no native stage concept)
_DEFAULT_STAGE = "V1"


def import_vpf(xml_path: str) -> Testplan:
    """Parse a Cadence vManager VPF XML file and return a :class:`Testplan`.

    Args:
        xml_path: Path to the VPF ``.xml`` file.

    Returns:
        A :class:`Testplan` populated with goals, testpoints, and covergroups
        extracted from the VPF file.

    Raises:
        FileNotFoundError: If *xml_path* does not exist.
        xml.etree.ElementTree.ParseError: If the file is not valid XML.
    """
    tree = ET.parse(xml_path)
    root = tree.getroot()

    plan = Testplan(source_file=xml_path)

    # Root element may be <testplan> or <tpPlan>; handle both
    if root.tag in ("testplan", "tpPlan"):
        plan.name = root.get("name", "")
        plan.description = root.get("desc", root.get("description", ""))
        plan.owner = root.get("owner", "")
        goal_elems = root.findall("tpGoal") + root.findall("tpGroup")
        for g_elem in goal_elems:
            plan.goals.append(_parse_goal(g_elem, plan.covergroups))
        for tp_elem in root.findall("tpTest"):
            plan.testpoints.append(_parse_test(tp_elem, plan.covergroups))
    else:
        # Bare goals at root level
        for g_elem in root.findall("tpGoal") + root.findall("tpGroup"):
            plan.goals.append(_parse_goal(g_elem, plan.covergroups))

    return plan


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

def _parse_goal(elem: ET.Element, covergroups: List[CovergroupEntry]) -> Goal:
    raw_status = elem.get("status", "")
    raw_priority = elem.get("priority", "")
    goal = Goal(
        id=elem.get("id", elem.get("name", "")),
        title=elem.get("name", elem.get("title", "")),
        desc=elem.get("desc", elem.get("description", "")),
        owner=elem.get("owner", ""),
        priority=_map_priority(raw_priority),
        status=_STATUS_MAP.get(raw_status.lower(), raw_status),
        tags=[t.text.strip() for t in elem.findall("tag") if t.text],
        custom=_parse_attributes(elem),
    )

    # Nested tpGoal / tpGroup
    for sub in elem.findall("tpGoal") + elem.findall("tpGroup"):
        goal.goals.append(_parse_goal(sub, covergroups))

    # tpTest children
    for tp_elem in elem.findall("tpTest"):
        goal.testpoints.append(_parse_test(tp_elem, covergroups))

    return goal


def _parse_test(elem: ET.Element, covergroups: List[CovergroupEntry]) -> Testpoint:
    tp = Testpoint(
        name=elem.get("name", ""),
        stage=elem.get("stage", _DEFAULT_STAGE),
        desc=elem.get("desc", elem.get("description", "")),
        owner=elem.get("owner", ""),
        tests=_split_tests(elem.get("tests", "")),
        custom=_parse_attributes(elem),
    )
    if elem.get("na", "").lower() in ("true", "1", "yes"):
        tp.na = True

    for cp_elem in elem.findall("tpCoverPoint"):
        cp_name = cp_elem.get("name", "")
        cp_type = cp_elem.get("type", "covergroup")
        cp_path = cp_elem.get("path", cp_name)

        # Add coverage binding to testpoint
        tp.coverage.append(CoverageBinding(type=cp_type, path=cp_path,
                                            desc=cp_elem.get("desc", "")))

        # Also register the coverpoint declaration if it has a path
        _register_covergroup(covergroups, cp_name, cp_type, cp_path,
                              cp_elem.get("desc", ""))

    return tp


def _register_covergroup(covergroups: List[CovergroupEntry], name: str,
                          kind: str, path: str, desc: str) -> None:
    """Add a covergroup/coverpoint entry if not already present."""
    if kind in ("coverpoint", "cross"):
        # Find parent covergroup by path prefix, or create one
        cg_name = name.split(".")[0] if "." in name else name
        existing = next((cg for cg in covergroups if cg.name == cg_name), None)
        if existing is None:
            existing = CovergroupEntry(name=cg_name)
            covergroups.append(existing)
        if not any(cp.name == name for cp in existing.coverpoints):
            existing.coverpoints.append(CoverpointEntry(name=name, desc=desc,
                                                         path=path))
    else:
        # covergroup
        if not any(cg.name == name for cg in covergroups):
            covergroups.append(CovergroupEntry(name=name, desc=desc))


def _parse_attributes(elem: ET.Element) -> dict:
    """Parse ``<attributes><attr name=… value=…/></attributes>`` into a dict."""
    attrs_elem = elem.find("attributes")
    if attrs_elem is None:
        return {}
    return {
        a.get("name", ""): a.get("value", a.text or "")
        for a in attrs_elem.findall("attr")
        if a.get("name")
    }


def _split_tests(value: str) -> List[str]:
    """Split a comma- or space-separated test list into individual names."""
    if not value:
        return []
    return [t.strip() for t in re.split(r"[,\s]+", value) if t.strip()]


def _map_priority(value: str) -> str:
    return _PRIORITY_MAP.get(value.lower(), "")
