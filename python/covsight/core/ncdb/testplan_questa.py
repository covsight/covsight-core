"""Siemens Questa Visualizer testplan reader.

Supports both XML and CSV variants of the Questa Visualizer testplan format.

**XML format** — a ``<questa_testplan>`` or ``<testplan>`` root containing
nested ``<goal>`` and ``<testpoint>`` elements::

    <questa_testplan name="uart" owner="alice">
      <goal id="g_data" title="Data Path">
        <description>Data path goals</description>
        <tag>regression</tag>
        <testpoint name="tp_smoke" stage="V1">
          <metric type="covergroup" coverage="top.dut.cg_baud"/>
          <metric type="assertion" coverage="top.chk.a1"/>
          <test>uart_smoke</test>
        </testpoint>
      </goal>
    </questa_testplan>

**CSV format** — one row per item with ``id``, ``title``, ``type``
(``goal`` or ``testpoint``), ``metric_type``, ``coverage_path``, and
``stage`` columns.  ``parent_id`` links testpoints to goals.
"""
from __future__ import annotations

import csv
import os
import xml.etree.ElementTree as ET
from typing import Dict, List, Optional

from .testplan import (
    CoverageBinding,
    Goal,
    Testplan,
    Testpoint,
)

_DEFAULT_STAGE = "V1"

_STATUS_MAP = {
    "active":      "in_progress",
    "in_progress": "in_progress",
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


def import_questa(path: str) -> Testplan:
    """Parse a Siemens Questa Visualizer XML or CSV file and return a
    :class:`Testplan`.

    The file type is determined by the extension (``.csv`` → CSV parser,
    anything else → XML parser).

    Args:
        path: Path to the Questa Visualizer file.

    Returns:
        A :class:`Testplan` populated from the Questa file.

    Raises:
        FileNotFoundError: If *path* does not exist.
        ValueError: If the CSV file is missing the required ``id`` column.
    """
    _, ext = os.path.splitext(path)
    if ext.lower() == ".csv":
        return _import_csv(path)
    return _import_xml(path)


# ---------------------------------------------------------------------------
# XML parser
# ---------------------------------------------------------------------------

def _import_xml(path: str) -> Testplan:
    tree = ET.parse(path)
    root = tree.getroot()

    plan = Testplan(source_file=path)
    plan.name = root.get("name", "")
    plan.description = _text(root.find("description"))
    plan.owner = root.get("owner", "")
    plan.tags = [t.text.strip() for t in root.findall("tag") if t.text]

    for g_elem in root.findall("goal"):
        plan.goals.append(_xml_goal(g_elem))

    for tp_elem in root.findall("testpoint"):
        plan.testpoints.append(_xml_testpoint(tp_elem))

    return plan


def _xml_goal(elem: ET.Element) -> Goal:
    raw_status = elem.get("status", "")
    goal = Goal(
        id=elem.get("id", ""),
        title=elem.get("title", elem.get("name", "")),
        desc=_text(elem.find("description")),
        owner=elem.get("owner", ""),
        priority=_PRIORITY_MAP.get(elem.get("priority", "").lower(), ""),
        status=_STATUS_MAP.get(raw_status.lower(), raw_status),
        tags=[t.text.strip() for t in elem.findall("tag") if t.text],
    )
    for sub in elem.findall("goal"):
        goal.goals.append(_xml_goal(sub))
    for tp_elem in elem.findall("testpoint"):
        goal.testpoints.append(_xml_testpoint(tp_elem))
    return goal


def _xml_testpoint(elem: ET.Element) -> Testpoint:
    tp = Testpoint(
        name=elem.get("name", elem.get("id", "")),
        stage=elem.get("stage", _DEFAULT_STAGE),
        desc=_text(elem.find("description")),
        owner=elem.get("owner", ""),
        tests=[t.text.strip() for t in elem.findall("test") if t.text],
        tags=[t.text.strip() for t in elem.findall("tag") if t.text],
    )
    if elem.get("na", "").lower() in ("true", "1", "yes"):
        tp.na = True

    for m_elem in elem.findall("metric"):
        m_type = m_elem.get("type", "covergroup")
        m_path = m_elem.get("coverage", m_elem.get("path", ""))
        m_desc = m_elem.get("desc", _text(m_elem) or "")
        tp.coverage.append(CoverageBinding(type=m_type, path=m_path, desc=m_desc))

    return tp


# ---------------------------------------------------------------------------
# CSV parser
# ---------------------------------------------------------------------------

def _import_csv(path: str) -> Testplan:
    plan = Testplan(source_file=path)
    goal_map: Dict[str, Goal] = {}

    with open(path, newline="", encoding="utf-8") as fh:
        reader = csv.DictReader(fh)
        if reader.fieldnames is None or "id" not in reader.fieldnames:
            raise ValueError(f"Questa CSV {path!r} missing required 'id' column")

        for row in reader:
            kind = row.get("type", "").lower().strip()
            item_id = row.get("id", "").strip()
            if not item_id:
                continue

            if kind == "goal":
                goal = _csv_goal(row)
                goal_map[item_id] = goal
                parent_id = row.get("parent_id", "").strip()
                if parent_id and parent_id in goal_map:
                    goal_map[parent_id].goals.append(goal)
                else:
                    plan.goals.append(goal)

            elif kind == "testpoint":
                tp = _csv_testpoint(row)
                parent_id = row.get("parent_id", "").strip()
                if parent_id and parent_id in goal_map:
                    goal_map[parent_id].testpoints.append(tp)
                else:
                    plan.testpoints.append(tp)

    return plan


def _csv_goal(row: dict) -> Goal:
    raw_status = row.get("status", "")
    return Goal(
        id=row.get("id", ""),
        title=row.get("title", row.get("name", "")),
        desc=row.get("description", row.get("desc", "")),
        owner=row.get("owner", ""),
        priority=_PRIORITY_MAP.get(row.get("priority", "").lower(), ""),
        status=_STATUS_MAP.get(raw_status.lower(), raw_status),
    )


def _csv_testpoint(row: dict) -> Testpoint:
    tests_str = row.get("tests", "")
    tp = Testpoint(
        name=row.get("name", row.get("id", "")),
        stage=row.get("stage", _DEFAULT_STAGE),
        desc=row.get("description", row.get("desc", "")),
        owner=row.get("owner", ""),
        tests=[t.strip() for t in tests_str.split(",") if t.strip()],
    )
    m_type = row.get("metric_type", "").strip()
    m_path = row.get("coverage_path", "").strip()
    if m_type and m_path:
        tp.coverage.append(CoverageBinding(type=m_type, path=m_path))
    return tp


# ---------------------------------------------------------------------------
# Utility
# ---------------------------------------------------------------------------

def _text(elem: Optional[ET.Element]) -> str:
    if elem is None or elem.text is None:
        return ""
    return elem.text.strip()
