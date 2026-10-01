"""Map Verilator ``coverage.dat`` items onto the UCIS data model.

Layout produced (``<inst>`` is an INSTANCE scope built from ``h``):

* One ``DU_MODULE`` per module named in ``page`` (``v_line/<module>``); each
  instance points at its module's DU.
* line   -> ``<inst>/block`` (BLOCK), one STMTBIN per point.  A point is a
  basic block; when it spans more than its first line, the full line set
  (Verilator's ``S``, e.g. ``117,119,126-131``) is kept in the item's
  ``lines`` attribute.
* branch -> ``<inst>/<file>:<line>`` (BRANCH), one BRANCHBIN per arm
  (if/else/...); an arm's body lines go in its ``lines`` attribute.
* expr   -> ``<inst>/<file>:<line>:<col>`` (EXPR), one EXPRBIN per term row
* toggle -> ``<inst>/<signal>`` (TOGGLE), bins ``0->1``/``1->0`` (scalar) or
  ``<bit>:0->1`` (vector)
* fsm    -> ``<inst>/<var>`` (FSM) with FSMBIN states and ``A->B`` arcs.  The
  instance comes from ``Fv`` (Verilator's ``h`` for FSM points is malformed).
* user   -> ``<inst>/<label>`` (COVER), one COVERBIN
* covergroup -> ``<inst>/<cg type>`` (COVERGROUP) with COVERPOINT and CROSS
  scopes.  Verilator records type-level coverage only, with no instance path,
  so the covergroup is attached to the declaring module's instance when that
  module (found from file/line) has exactly one; otherwise to a top-level
  instance named after the module (or ``$unit`` when no module is known).

Instance paths may contain ``*`` (``top.u_*``) when Verilator aggregated
instances; build with ``--coverage-per-instance`` to keep them separate.

Points in Verilator's own ``std`` package (``h=std`` / ``std::...``) are
dropped unless ``include_std=True``.
"""
import logging
import os
import re
from collections import OrderedDict
from typing import Dict, List, Optional, Tuple

from covsight.core.api import CoverData, SourceInfo
from covsight.core.api.enums import (
    CoverTypeT, FlagsT, HistoryNodeKind, ScopeTypeT, SourceT, TestStatusT,
)
from covsight.core.mem.mem_ucis import MemUCIS

from .vlt_parser import VltItem

_log = logging.getLogger(__name__)

CODE_KINDS = ("line", "branch", "expr", "toggle")
FSM_KINDS = ("fsm_state", "fsm_arc")
ALL_KINDS = CODE_KINDS + FSM_KINDS + ("user", "covergroup")

_DU_FLAGS = (FlagsT.ENABLED_STMT | FlagsT.ENABLED_BRANCH | FlagsT.ENABLED_EXPR
             | FlagsT.ENABLED_FSM | FlagsT.ENABLED_TOGGLE
             | FlagsT.INST_ONCE | FlagsT.SCOPE_UNDER_DU)

_BIN_KIND = {
    "default": CoverTypeT.DEFAULTBIN,
    "ignore": CoverTypeT.IGNOREBIN,
    "illegal": CoverTypeT.ILLEGALBIN,
}

_LASTIDX_RE = re.compile(r"^(.*)\[(\d+)\]$")

UNIT_INSTANCE = "$unit"

# Cover-item attribute holding a line point's full line set ("12,14-16")
LINES_ATTR = "lines"


def is_std_path(path: str) -> bool:
    return path == "std" or path.startswith("std::")


class VltToUcis:
    """Builds a :class:`MemUCIS` from parsed :class:`VltItem` objects.

    ``warnings`` collects one message per class of dropped/unknown data.
    """

    def __init__(self, db=None, include_std: bool = False):
        self.db = db if db is not None else MemUCIS()
        self.include_std = include_std
        self.warnings: List[str] = []
        self._files: Dict[str, object] = {}
        self._dus: Dict[str, object] = {}
        self._insts: Dict[str, object] = {}
        self._inst_module: Dict[str, str] = {}
        self._module_first_line: Dict[Tuple[str, str], int] = {}
        self._file_modules = None

    # ------------------------------------------------------------------
    def map(self, items: List[VltItem], test_name: Optional[str] = None,
            physical_name: Optional[str] = None):
        """Populate the database; returns it."""
        placed: List[Tuple[str, VltItem]] = []
        unknown: Dict[str, int] = {}
        skipped_std = 0

        for it in items:
            kind = it.kind
            if kind not in ALL_KINDS:
                unknown[kind] = unknown.get(kind, 0) + 1
                continue
            if kind == "covergroup":
                placed.append(("", it))      # resolved below
                continue
            path = self._item_path(it)
            if not path:
                path = "top"
            if is_std_path(path) and not self.include_std:
                skipped_std += 1
                continue
            placed.append((path, it))
            module = it.page_scope
            if module:
                self._inst_module.setdefault(path, module)
                if it.filename and it.line:
                    key = (it.filename, module)
                    first = self._module_first_line.get(key)
                    if first is None or it.line < first:
                        self._module_first_line[key] = it.line

        for kind, n in sorted(unknown.items()):
            self._warn("ignored %d point(s) of unsupported kind '%s'" % (n, kind))
        if skipped_std:
            _log.debug("dropped %d point(s) in the std package", skipped_std)

        placed = [(p or self._place_covergroup(it), it) for p, it in placed]

        self._create_test(test_name, physical_name)

        by_kind: Dict[str, List[Tuple[str, VltItem]]] = OrderedDict(
            (k, []) for k in ALL_KINDS)
        for path, it in placed:
            by_kind[it.kind].append((path, it))

        self._map_line(by_kind["line"])
        self._map_branch(by_kind["branch"])
        self._map_expr(by_kind["expr"])
        self._map_toggle(by_kind["toggle"])
        self._map_fsm(by_kind["fsm_state"] + by_kind["fsm_arc"])
        self._map_user(by_kind["user"])
        self._map_covergroups(by_kind["covergroup"])
        return self.db

    # ------------------------------------------------------------------
    # Hierarchy
    # ------------------------------------------------------------------
    @staticmethod
    def _item_path(it: VltItem) -> str:
        if it.kind in FSM_KINDS:
            fv = it.get("Fv")
            if fv:
                return fv.rpartition(".")[0]
        return it.hier

    def _place_covergroup(self, it: VltItem) -> str:
        """Choose the instance for a covergroup point (see module docstring)."""
        module = self._module_at(it.filename, it.line)
        if module is None:
            return UNIT_INSTANCE
        paths = [p for p, m in self._inst_module.items() if m == module]
        if len(paths) > 1:
            # Aggregated code coverage (top.u_*) alongside per-instance FSM
            # paths: the aggregate is the module's single representative.
            paths = [p for p in paths if "*" in p] or paths
        if len(paths) == 1:
            return paths[0]
        self._inst_module.setdefault(module, module)
        return module

    def _module_at(self, filename: str, line: int) -> Optional[str]:
        """Module whose first coverage point in ``filename`` is the last one
        at or before ``line`` -- i.e. the module that encloses the line."""
        if self._file_modules is None:
            self._file_modules = {}
            for (fname, module), first in self._module_first_line.items():
                self._file_modules.setdefault(fname, _Bisect()).add(first, module)
        idx = self._file_modules.get(filename)
        return idx.lookup(line) if idx is not None else None

    def _inst(self, path: str):
        inst = self._insts.get(path)
        if inst is not None:
            return inst
        parent_path, _, leaf = path.rpartition(".")
        if path.startswith("std::"):
            parent_path, leaf = "", path       # '::' paths are flat
        module = self._inst_module.get(path, leaf)
        du = self._du(module)
        if parent_path:
            parent = self._inst(parent_path)
            inst = parent.createInstance(leaf, None, 1, SourceT.SV,
                                         ScopeTypeT.INSTANCE, du,
                                         FlagsT.INST_ONCE)
        else:
            inst = self.db.createInstance(leaf, None, 1, SourceT.SV,
                                          ScopeTypeT.INSTANCE, du,
                                          FlagsT.INST_ONCE)
        self._insts[path] = inst
        return inst

    def _du(self, module: str):
        du = self._dus.get(module)
        if du is None:
            du = self.db.createScope(module, None, 1, SourceT.SV,
                                     ScopeTypeT.DU_MODULE, _DU_FLAGS)
            self._dus[module] = du
        return du

    def _fh(self, filename: str):
        if not filename:
            return None
        fh = self._files.get(filename)
        if fh is None:
            fh = self.db.createFileHandle(filename, os.path.dirname(filename) or ".")
            self._files[filename] = fh
        return fh

    def _src(self, it: VltItem) -> Optional[SourceInfo]:
        fh = self._fh(it.filename)
        return SourceInfo(fh, it.line, it.col) if fh is not None else None

    # ------------------------------------------------------------------
    # Test history
    # ------------------------------------------------------------------
    def _create_test(self, test_name, physical_name):
        node = self.db.createHistoryNode(
            None, test_name or "verilator", physical_name, HistoryNodeKind.TEST)
        node.setTestStatus(TestStatusT.OK)
        node.setToolCategory("simulator")
        node.setVendorId("verilator")
        node.setVendorTool("verilator")
        self.history_node = node

    # ------------------------------------------------------------------
    # Code coverage
    # ------------------------------------------------------------------
    @staticmethod
    def _point_name(it: VltItem, with_col: bool = True) -> str:
        base = os.path.basename(it.filename) if it.filename else "?"
        if with_col:
            return "%s:%d:%d" % (base, it.line, it.col)
        return "%s:%d" % (base, it.line)

    def _cover(self, scope, name, cover_t, it: VltItem, used: set, src=True):
        name = _unique(name, used)
        cd = CoverData(cover_t, 0)
        cd.data = it.count
        cd.goal = 1
        cd.at_least = 1
        return scope.createNextCover(name, cd, self._src(it) if src else None)

    def _map_line(self, points):
        blocks = {}
        for path, it in points:
            ent = blocks.get(path)
            if ent is None:
                scope = self._inst(path).createScope(
                    "block", None, 1, SourceT.SV, ScopeTypeT.BLOCK,
                    FlagsT.ENABLED_STMT)
                ent = blocks[path] = (scope, set())
            ci = self._cover(ent[0], self._point_name(it), CoverTypeT.STMTBIN,
                             it, ent[1])
            self._set_lines(ci, it)

    @staticmethod
    def _set_lines(ci, it: VltItem):
        """Keep Verilator's line set (``S``) unless it is just the point's line."""
        lines = it.get("S")
        if lines and lines != str(it.line) and hasattr(ci, "setAttribute"):
            ci.setAttribute(LINES_ATTR, lines)

    def _map_branch(self, points):
        scopes = {}
        for path, it in points:
            key = (path, it.filename, it.line)
            ent = scopes.get(key)
            if ent is None:
                scope = self._inst(path).createScope(
                    self._point_name(it, with_col=False), self._src(it), 1,
                    SourceT.SV, ScopeTypeT.BRANCH, FlagsT.ENABLED_BRANCH)
                ent = scopes[key] = (scope, set())
            ci = self._cover(ent[0], it.comment or "arm", CoverTypeT.BRANCHBIN,
                             it, ent[1])
            self._set_lines(ci, it)

    def _map_expr(self, points):
        scopes = {}
        for path, it in points:
            key = (path, it.filename, it.line, it.col)
            ent = scopes.get(key)
            if ent is None:
                scope = self._inst(path).createScope(
                    self._point_name(it), self._src(it), 1, SourceT.SV,
                    ScopeTypeT.EXPR, FlagsT.ENABLED_EXPR)
                ent = scopes[key] = (scope, set())
            self._cover(ent[0], it.comment or "term", CoverTypeT.EXPRBIN,
                        it, ent[1])

    def _map_toggle(self, points):
        scopes = {}
        for path, it in points:
            net, _, direction = it.comment.rpartition(":")
            if not net:
                net, direction = it.comment, ""
            m = _LASTIDX_RE.match(net)
            if m:
                signal, bin_name = m.group(1), "%s:%s" % (m.group(2), direction)
            else:
                signal, bin_name = net, direction
            key = (path, signal)
            ent = scopes.get(key)
            if ent is None:
                scope = self._inst(path).createScope(
                    signal, self._src(it), 1, SourceT.SV, ScopeTypeT.TOGGLE,
                    FlagsT.ENABLED_TOGGLE)
                ent = scopes[key] = (scope, set())
            self._cover(ent[0], bin_name or "toggle", CoverTypeT.TOGGLEBIN,
                        it, ent[1], src=False)

    def _map_fsm(self, points):
        # With --coverage-per-instance Verilator emits each FSM point once per
        # instance of the module; only the owning instance counts, so sum.
        fsms: Dict[Tuple[str, str], Dict] = OrderedDict()
        for path, it in points:
            var = it.get("Fv").rpartition(".")[2] or "fsm"
            ent = fsms.setdefault((path, var), {
                "src": it, "states": OrderedDict(), "arcs": OrderedDict()})
            if it.kind == "fsm_state":
                bins, name = ent["states"], it.get("Ft")
            else:
                bins, name = ent["arcs"], "%s->%s" % (it.get("Ff"), it.get("Ft"))
            bins[name] = bins.get(name, 0) + it.count

        for (path, var), ent in fsms.items():
            scope = self._inst(path).createScope(
                var, self._src(ent["src"]), 1, SourceT.SV, ScopeTypeT.FSM,
                FlagsT.ENABLED_FSM)
            for name, count in list(ent["states"].items()) + list(ent["arcs"].items()):
                cd = CoverData(CoverTypeT.FSMBIN, 0)
                cd.data = count
                cd.goal = 1
                cd.at_least = 1
                scope.createNextCover(name, cd, None)

    def _map_user(self, points):
        used: Dict[str, set] = {}
        for path, it in points:
            label = it.comment if it.comment not in ("", "cover") else \
                "cover@" + self._point_name(it, with_col=False)
            name = _unique(label, used.setdefault(path, set()))
            scope = self._inst(path).createScope(
                name, self._src(it), 1, SourceT.SV, ScopeTypeT.COVER, 0)
            self._cover(scope, "coverBin", CoverTypeT.COVERBIN, it, set())

    # ------------------------------------------------------------------
    # Functional coverage
    # ------------------------------------------------------------------
    def _map_covergroups(self, points):
        groups: Dict[Tuple[str, str], List[VltItem]] = OrderedDict()
        for path, it in points:
            cg_name = it.page_scope or it.hier.partition(".")[0] or "covergroup"
            groups.setdefault((path, cg_name), []).append(it)

        for (path, cg_name), cg_items in groups.items():
            cg = self._inst(path).createCovergroup(
                cg_name, self._src(cg_items[0]), 1, SourceT.SV)

            # h = <cg>.<coverpoint|cross>.<bin>
            members: Dict[str, List[VltItem]] = OrderedDict()
            for it in cg_items:
                parts = it.hier.split(".")
                member = parts[1] if len(parts) >= 3 else "default"
                members.setdefault(member, []).append(it)

            cps = OrderedDict()
            crosses = OrderedDict()
            for name, bins in members.items():
                if any(b.get("cross") == "1" for b in bins):
                    crosses[name] = bins
                    continue
                cp = cg.createCoverpoint(name, self._src(bins[0]), 1, SourceT.SV)
                cps[name] = (cp, [self._bin_name(b) for b in bins])
                for b in bins:
                    cp.createBin(self._bin_name(b), self._src(b), 1, b.count,
                                 self._bin_name(b),
                                 _BIN_KIND.get(b.get("bin_type"), CoverTypeT.CVGBIN))

            for name, bins in crosses.items():
                cp_names = self._cross_members(name, bins, cps)
                cr = cg.createCross(name, self._src(bins[0]), 1, SourceT.SV,
                                    [cps[n][0] for n in cp_names])
                for b in bins:
                    cr.createBin(self._bin_name(b), self._src(b), 1, b.count,
                                 b.get("Cb"),
                                 _BIN_KIND.get(b.get("bin_type"), CoverTypeT.CVGBIN))

    @staticmethod
    def _bin_name(it: VltItem) -> str:
        return it.get("bin") or it.hier.rpartition(".")[2] or "bin_%d" % it.line

    def _cross_members(self, cross, bins, cps) -> List[str]:
        """Infer the crossed coverpoints from the ``Cb`` bin tuples: position
        ``i`` belongs to the first coverpoint (not already used) whose bins
        contain every value seen at ``i``."""
        tuples = [b.get("Cb").split(",") for b in bins if b.get("Cb")]
        if not tuples:
            self._warn("cross %s has no Cb data; recorded with no coverpoints" % cross)
            return []
        width = len(tuples[0])
        chosen: List[str] = []
        for i in range(width):
            values = {t[i] for t in tuples if len(t) > i}
            match = next((n for n, (_, names) in cps.items()
                          if n not in chosen and values <= set(names)), None)
            if match is None:
                self._warn("cross %s: cannot identify coverpoint %d" % (cross, i))
                return chosen
            chosen.append(match)
        return chosen

    def _warn(self, msg: str):
        _log.warning(msg)
        self.warnings.append(msg)


def _unique(name: str, used: set) -> str:
    if name not in used:
        used.add(name)
        return name
    i = 1
    while "%s#%d" % (name, i) in used:
        i += 1
    name = "%s#%d" % (name, i)
    used.add(name)
    return name


class _Bisect:
    """Sorted (line, module) list; ``lookup`` returns the module with the
    greatest start line <= ``line`` (or the first one when none precede)."""

    def __init__(self):
        self._l: List[Tuple[int, str]] = []

    def add(self, line: int, module: str):
        self._l.append((line, module))
        self._l.sort()

    def lookup(self, line: int) -> Optional[str]:
        if not self._l:
            return None
        best = self._l[0][1]
        for start, module in self._l:
            if start <= line:
                best = module
            else:
                break
        return best
