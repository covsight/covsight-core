"""
src/ucis/ncdb/testplan.py — Testplan data model for NCDB.

A ``Testplan`` describes the structured set of verification tasks (testpoints)
and functional-coverage groups expected for a design.  It may be embedded
inside a ``.cdb`` file as ``testplan.json`` (Mode A) or kept as a standalone
file (Mode B).  Either way the same ``Testplan`` object is used.

Schema version 1 added:
  - ``Goal`` hierarchy (nested goals with testpoints at any level).
  - ``CoverageBinding`` — explicit links from testpoints to coverage-DB paths.
  - ``CoverpointEntry`` — individual coverpoints inside a covergroup declaration.
  - Plan-level metadata (``name``, ``owner``, ``tags``, ``substitutions``).
  - ``ImportEntry`` — composition via imported sub-plans.
  - ``custom`` dicts on every object for user-defined extensions.
"""
from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Dict, Iterator, List, Optional


# ── leaf types ────────────────────────────────────────────────────────────────

@dataclass
class RequirementLink:
    """Reference to an external requirement item (e.g. ALM/JIRA)."""
    system:  str = ""   # e.g. "ALM", "JIRA"
    project: str = ""   # e.g. "PROJ-X"
    item_id: str = ""   # e.g. "REQ-42"
    url:     str = ""   # optional direct URL


@dataclass
class CoverageBinding:
    """Explicit link from a testpoint to a coverage object in the database.

    Args:
        type: One of ``covergroup``, ``coverpoint``, ``cross``,
              ``assertion``, ``expression``, ``toggle``, ``line``,
              ``branch``, ``functional``.
        path: Dotted hierarchical path in the coverage DB (UCIS scope
              hierarchy).  Glob patterns ``*`` and ``**`` are accepted
              for bulk binding.
        desc: Optional human-readable description of this binding.
    """
    type: str        # see COVERAGE_BINDING_TYPES
    path: str        # hierarchical path; globs allowed
    desc: str = ""

    #: All recognised coverage binding type strings.
    TYPES = frozenset({
        "covergroup", "coverpoint", "cross", "assertion",
        "expression", "toggle", "line", "branch", "functional",
    })


@dataclass
class CoverpointEntry:
    """An individual coverpoint within a :class:`CovergroupEntry`.

    Args:
        name: Coverpoint name (as it appears in the SV source / DB).
        desc: Optional human-readable description.
        path: Optional explicit DB path (e.g. ``uart_env.uart_cg.baud_rate_cp``).
        custom: Opaque user-defined extensions.
    """
    name:   str
    desc:   str = ""
    path:   str = ""
    custom: Dict = field(default_factory=dict)


@dataclass
class CovergroupEntry:
    """One functional-coverage group expected to be exercised by the design.

    Args:
        name: Covergroup type name.
        desc: Optional human-readable description.
        coverpoints: Optional list of :class:`CoverpointEntry` objects
                     documenting the individual coverpoints.
        custom: Opaque user-defined extensions.
    """
    name:        str
    desc:        str = ""
    coverpoints: List[CoverpointEntry] = field(default_factory=list)
    custom:      Dict = field(default_factory=dict)


@dataclass
class Testpoint:
    """One verification task (maps to one or more test names).

    Args:
        name:            Unique lower_snake_case identifier within the plan.
        stage:           Verification stage (``"V1"``, ``"V2"``, ``"V2S"``,
                         ``"V3"``, or any custom string).
        desc:            Markdown description of the verification intent.
        tests:           Expanded list of test names that exercise this
                         testpoint.  ``{key}`` wildcards are expanded by the
                         reader before storage here.
        tags:            Free-form labels (e.g. ``["gls", "fpga"]``).
        na:              ``True`` when ``tests: ["N/A"]`` — intentionally
                         unmapped.
        source_template: Original ``{key}`` template string(s) before
                         expansion (informational).
        requirements:    Links to external requirement items.
        coverage:        Explicit links to coverage objects in the DB.
        owner:           Person or team responsible for this testpoint.
        priority:        ``"high"``, ``"medium"``, or ``"low"``.
        weight:          Relative importance (positive integer, default 1).
        custom:          Opaque user-defined extensions.
    """
    name:            str
    stage:           str = ""
    desc:            str = ""
    tests:           List[str] = field(default_factory=list)
    tags:            List[str] = field(default_factory=list)
    na:              bool = False
    source_template: str = ""
    requirements:    List[RequirementLink] = field(default_factory=list)
    coverage:        List[CoverageBinding] = field(default_factory=list)
    owner:           str = ""
    priority:        str = ""
    weight:          int = 1
    custom:          Dict = field(default_factory=dict)


@dataclass
class ImportEntry:
    """A reference to another testplan file to merge into this one.

    Args:
        path:          File path, relative to the importing file or absolute.
        substitutions: Substitution overrides for testpoints sourced from
                       this import (take precedence over plan-level subs).
    """
    path:          str
    substitutions: Dict = field(default_factory=dict)


# ── goal (forward reference needed for recursive nesting) ─────────────────────

@dataclass
class Goal:
    """A hierarchical verification goal node.

    Goals form a tree: chip → subsystem → block → feature.  Each goal may
    contain sub-goals and/or testpoints directly.

    Args:
        id:         Unique identifier within the plan (lower_snake_case).
        title:      Human-readable short title.
        desc:       Markdown description.
        owner:      Person or team responsible.
        priority:   ``"high"``, ``"medium"``, or ``"low"``.
        status:     ``"planned"``, ``"in_progress"``, ``"complete"``, or
                    ``"waived"``.
        tags:       Free-form labels.
        goals:      Nested sub-goals (arbitrary depth).
        testpoints: Testpoints that belong directly to this goal.
        custom:     Opaque user-defined extensions.
    """
    id:         str = ""
    title:      str = ""
    desc:       str = ""
    owner:      str = ""
    priority:   str = ""
    status:     str = ""
    tags:       List[str] = field(default_factory=list)
    goals:      List["Goal"] = field(default_factory=list)
    testpoints: List[Testpoint] = field(default_factory=list)
    custom:     Dict = field(default_factory=dict)


# ── serialisation helpers ─────────────────────────────────────────────────────

def _req_to_dict(r: RequirementLink) -> dict:
    return {"system": r.system, "project": r.project,
            "item_id": r.item_id, "url": r.url}


def _req_from_dict(d: dict) -> RequirementLink:
    return RequirementLink(
        system=d.get("system", ""), project=d.get("project", ""),
        item_id=d.get("item_id", ""), url=d.get("url", ""),
    )


def _binding_to_dict(b: CoverageBinding) -> dict:
    return {"type": b.type, "path": b.path, "desc": b.desc}


def _binding_from_dict(d: dict) -> CoverageBinding:
    return CoverageBinding(
        type=d.get("type", "functional"),
        path=d.get("path", ""),
        desc=d.get("desc", ""),
    )


def _cp_entry_to_dict(cp: CoverpointEntry) -> dict:
    return {"name": cp.name, "desc": cp.desc,
            "path": cp.path, "custom": cp.custom}


def _cp_entry_from_dict(d: dict) -> CoverpointEntry:
    return CoverpointEntry(
        name=d.get("name", ""), desc=d.get("desc", ""),
        path=d.get("path", ""), custom=d.get("custom", {}),
    )


def _cg_to_dict(cg: CovergroupEntry) -> dict:
    return {
        "name": cg.name,
        "desc": cg.desc,
        "coverpoints": [_cp_entry_to_dict(cp) for cp in cg.coverpoints],
        "custom": cg.custom,
    }


def _cg_from_dict(d: dict) -> CovergroupEntry:
    return CovergroupEntry(
        name=d.get("name", ""),
        desc=d.get("desc", ""),
        coverpoints=[_cp_entry_from_dict(c) for c in d.get("coverpoints", [])],
        custom=d.get("custom", {}),
    )


def _tp_to_dict(tp: Testpoint) -> dict:
    return {
        "name":            tp.name,
        "stage":           tp.stage,
        "desc":            tp.desc,
        "tests":           tp.tests,
        "tags":            tp.tags,
        "na":              tp.na,
        "source_template": tp.source_template,
        "requirements":    [_req_to_dict(r) for r in tp.requirements],
        "coverage":        [_binding_to_dict(b) for b in tp.coverage],
        "owner":           tp.owner,
        "priority":        tp.priority,
        "weight":          tp.weight,
        "custom":          tp.custom,
    }


def _tp_from_dict(d: dict) -> Testpoint:
    return Testpoint(
        name=d["name"],
        stage=d.get("stage", ""),
        desc=d.get("desc", ""),
        tests=d.get("tests", []),
        tags=d.get("tags", []),
        na=d.get("na", False),
        source_template=d.get("source_template", ""),
        requirements=[_req_from_dict(r) for r in d.get("requirements", [])],
        coverage=[_binding_from_dict(b) for b in d.get("coverage", [])],
        owner=d.get("owner", ""),
        priority=d.get("priority", ""),
        weight=d.get("weight", 1),
        custom=d.get("custom", {}),
    )


def _goal_to_dict(g: Goal) -> dict:
    return {
        "id":         g.id,
        "title":      g.title,
        "desc":       g.desc,
        "owner":      g.owner,
        "priority":   g.priority,
        "status":     g.status,
        "tags":       g.tags,
        "goals":      [_goal_to_dict(sg) for sg in g.goals],
        "testpoints": [_tp_to_dict(tp) for tp in g.testpoints],
        "custom":     g.custom,
    }


def _goal_from_dict(d: dict) -> Goal:
    return Goal(
        id=d.get("id", ""),
        title=d.get("title", ""),
        desc=d.get("desc", ""),
        owner=d.get("owner", ""),
        priority=d.get("priority", ""),
        status=d.get("status", ""),
        tags=d.get("tags", []),
        goals=[_goal_from_dict(sg) for sg in d.get("goals", [])],
        testpoints=[_tp_from_dict(tp) for tp in d.get("testpoints", [])],
        custom=d.get("custom", {}),
    )


# ── iter_testpoints ───────────────────────────────────────────────────────────

def iter_testpoints(plan: "Testplan") -> Iterator[Testpoint]:
    """Yield every :class:`Testpoint` in *plan* regardless of location.

    Testpoints at the top level (``plan.testpoints``) are yielded first,
    followed by testpoints nested inside the ``goals`` tree (depth-first,
    pre-order).

    This provides a flat view for consumers such as
    :func:`~covsight.core.ncdb.testplan_closure.compute_closure` that do
    not need to understand the goal hierarchy.
    """
    yield from plan.testpoints
    yield from _iter_goal_testpoints(plan.goals)


def _iter_goal_testpoints(goals: List[Goal]) -> Iterator[Testpoint]:
    for goal in goals:
        yield from goal.testpoints
        yield from _iter_goal_testpoints(goal.goals)


# ── main class ────────────────────────────────────────────────────────────────

@dataclass
class Testplan:
    """Structured verification testplan.

    Attributes:
        format_version:   Schema version (currently 1).
        schema:           ``$schema`` URI (e.g.
                          ``"https://schema.covsight.io/testplan/v1"``).
        name:             Short identifier for the plan (e.g. ``"uart"``).
        description:      Human-readable description.
        owner:            Person or team responsible for the plan.
        tags:             Free-form labels applied to the whole plan.
        substitutions:    Plan-level ``{key}`` substitution dict used by the
                          reader for wildcard expansion in test name templates.
        imports:          List of :class:`ImportEntry` objects to merge.
        source_file:      Path to the source file (informational only).
        import_timestamp: ISO-8601 UTC timestamp set when embedded in a .cdb.
        testpoints:       Top-level flat testpoints (OpenTitan-compatible
                          shorthand; equivalent to a single unnamed root goal).
        goals:            Hierarchical goal tree.
        covergroups:      Coverage groups expected in the design.
        custom:           Opaque user-defined plan-level extensions.
    """
    format_version:   int = 1
    schema:           str = ""
    name:             str = ""
    description:      str = ""
    owner:            str = ""
    tags:             List[str] = field(default_factory=list)
    substitutions:    Dict = field(default_factory=dict)
    imports:          List[ImportEntry] = field(default_factory=list)
    source_file:      str = ""
    import_timestamp: str = ""

    testpoints:  List[Testpoint]       = field(default_factory=list)
    goals:       List[Goal]            = field(default_factory=list)
    covergroups: List[CovergroupEntry] = field(default_factory=list)
    custom:      Dict                  = field(default_factory=dict)

    # ── in-memory indices (built lazily) ──────────────────────────────────
    _tp_by_name: dict = field(default_factory=dict, repr=False, compare=False)
    _tp_by_test: dict = field(default_factory=dict, repr=False, compare=False)
    _indexed:    bool = field(default=False,        repr=False, compare=False)

    # ── index building ────────────────────────────────────────────────────

    def _build_indices(self) -> None:
        self._tp_by_name.clear()
        self._tp_by_test.clear()
        for tp in iter_testpoints(self):
            self._tp_by_name[tp.name] = tp
            for t in tp.tests:
                self._tp_by_test[t] = tp
        self._indexed = True

    def _ensure_indexed(self) -> None:
        if not self._indexed:
            self._build_indices()

    def _invalidate_index(self) -> None:
        self._indexed = False

    # ── public query API ──────────────────────────────────────────────────

    def getTestpoint(self, name: str) -> Optional[Testpoint]:
        """Return the testpoint with *name*, or ``None``."""
        self._ensure_indexed()
        return self._tp_by_name.get(name)

    def testpointForTest(self, test_name: str) -> Optional[Testpoint]:
        """Return the testpoint that owns *test_name*.

        Match order:

        1. **Exact** — ``test_name`` appears literally in ``testpoint.tests``.
        2. **Seed-suffix strip** — strip a trailing ``_\\d+`` (e.g.
           ``uart_smoke_42`` → ``uart_smoke``) and retry exact match.
        3. **Wildcard** — any ``testpoint.tests`` entry ending in ``_*``
           whose prefix matches ``test_name``.

        Returns ``None`` if no testpoint matches.
        """
        self._ensure_indexed()
        tp = self._tp_by_test.get(test_name)
        if tp is not None:
            return tp
        stripped = re.sub(r'_\d+$', '', test_name)
        if stripped != test_name:
            tp = self._tp_by_test.get(stripped)
            if tp is not None:
                return tp
        for pattern, candidate in self._tp_by_test.items():
            if pattern.endswith('_*') and test_name.startswith(pattern[:-1]):
                return candidate
        return None

    def testpointsForStage(self, stage: str) -> List[Testpoint]:
        """Return all testpoints targeting *stage* (e.g. ``"V2"``).

        Searches both top-level and goal-nested testpoints.
        """
        return [tp for tp in iter_testpoints(self) if tp.stage == stage]

    def stages(self) -> List[str]:
        """Return the ordered unique stages present in the testplan."""
        _ORDER = {"V1": 0, "V2": 1, "V2S": 2, "V3": 3}
        seen = dict.fromkeys(tp.stage for tp in iter_testpoints(self))
        return sorted(seen, key=lambda s: _ORDER.get(s, 99))

    def add_testpoint(self, tp: Testpoint) -> None:
        """Append *tp* to the top-level testpoints and invalidate indices."""
        self.testpoints.append(tp)
        self._invalidate_index()

    # ── serialization ─────────────────────────────────────────────────────

    def to_dict(self) -> dict:
        """Return a JSON-serialisable dict representation."""
        return {
            "format_version":   self.format_version,
            "schema":           self.schema,
            "name":             self.name,
            "description":      self.description,
            "owner":            self.owner,
            "tags":             self.tags,
            "substitutions":    self.substitutions,
            "imports":          [
                {"path": ie.path, "substitutions": ie.substitutions}
                for ie in self.imports
            ],
            "source_file":      self.source_file,
            "import_timestamp": self.import_timestamp,
            "testpoints":       [_tp_to_dict(tp) for tp in self.testpoints],
            "goals":            [_goal_to_dict(g) for g in self.goals],
            "covergroups":      [_cg_to_dict(cg) for cg in self.covergroups],
            "custom":           self.custom,
        }

    def serialize(self) -> bytes:
        """Serialise to compact JSON bytes (for ZIP embedding)."""
        return json.dumps(self.to_dict(), separators=(',', ':')).encode()

    @classmethod
    def from_dict(cls, d: dict) -> "Testplan":
        """Reconstruct a :class:`Testplan` from a plain dict.

        Fully backward-compatible: dicts written by older versions that only
        contain ``testpoints``/``covergroups``/``format_version``/
        ``source_file``/``import_timestamp`` are accepted and all new fields
        default to empty.
        """
        obj = cls(
            format_version=d.get("format_version", 1),
            schema=d.get("schema", ""),
            name=d.get("name", ""),
            description=d.get("description", ""),
            owner=d.get("owner", ""),
            tags=d.get("tags", []),
            substitutions=d.get("substitutions", {}),
            imports=[
                ImportEntry(path=ie.get("path", ""),
                            substitutions=ie.get("substitutions", {}))
                for ie in d.get("imports", [])
            ],
            source_file=d.get("source_file", ""),
            import_timestamp=d.get("import_timestamp", ""),
            custom=d.get("custom", {}),
        )
        for rec in d.get("testpoints", []):
            obj.testpoints.append(_tp_from_dict(rec))
        for rec in d.get("goals", []):
            obj.goals.append(_goal_from_dict(rec))
        for rec in d.get("covergroups", []):
            obj.covergroups.append(_cg_from_dict(rec))
        return obj

    @classmethod
    def from_bytes(cls, data: bytes) -> "Testplan":
        """Reconstruct from JSON bytes (inverse of :meth:`serialize`)."""
        return cls.from_dict(json.loads(data.decode()))

    @classmethod
    def load(cls, path: str) -> "Testplan":
        """Load a testplan from a standalone JSON/hjson file (Mode B)."""
        with open(path, "rb") as f:
            return cls.from_bytes(f.read())

    def save(self, path: str) -> None:
        """Write this testplan to a standalone JSON file (Mode B)."""
        with open(path, "wb") as f:
            f.write(self.serialize())

    def stamp_import_time(self) -> None:
        """Set :attr:`import_timestamp` to the current UTC time."""
        self.import_timestamp = datetime.now(timezone.utc).isoformat()


# ── module-level helpers ──────────────────────────────────────────────────────

def get_testplan(db) -> Optional[Testplan]:
    """Retrieve testplan from any UCIS db object (NcdbUCIS or MemUCIS).

    Works with any object that has a ``getTestplan()`` method
    (e.g. :class:`~ucis.ncdb.ncdb_ucis.NcdbUCIS`) or a ``_testplan``
    attribute (e.g. a :class:`~ucis.mem.mem_ucis.MemUCIS` returned by
    :class:`~ucis.ncdb.ncdb_reader.NcdbReader`).
    """
    if hasattr(db, "getTestplan"):
        return db.getTestplan()
    return getattr(db, "_testplan", None)


def set_testplan(db, tp: Testplan) -> None:
    """Embed *tp* into *db*.

    Works with any object that has a ``setTestplan()`` method.
    """
    if hasattr(db, "setTestplan"):
        db.setTestplan(tp)
    else:
        raise TypeError(f"{type(db).__name__} does not support setTestplan()")
