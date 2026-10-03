'''
Created on Jan 5, 2021

@author: mballance
'''
import copy
from collections import OrderedDict
from typing import Dict, List, Optional

from covsight.core.api import CoverTypeT
from covsight.core.api import HistoryNodeKind
from covsight.core.api import ScopeTypeT
from covsight.core.api import SourceInfo
from covsight.core.api import SourceT
from covsight.core.api import UCIS

_ALL = 0xFFFFFFFFFFFFFFFF

#: Covergroup options copied from the first source (getter, setter).
_CVG_OPTIONS = (
    ("getAtLeast", "setAtLeast"), ("getAutoBinMax", "setAutoBinMax"),
    ("getPerInstance", "setPerInstance"),
    ("getMergeInstances", "setMergeInstances"),
    ("getDetectOverlap", "setDetectOverlap"), ("getStrobe", "setStrobe"),
    ("getComment", "setComment"),
)

#: Toggle-scope metadata copied from the first source.
_TOGGLE_META = (
    ("getCanonicalName", "setCanonicalName"),
    ("getToggleMetric", "setToggleMetric"), ("getToggleType", "setToggleType"),
    ("getToggleDir", "setToggleDir"), ("getNumBits", "setNumBits"),
)


class DbMerger(object):
    """Merge UCIS databases into one: the structural union of their scope
    trees, with the hit counts of matching cover items summed.

    Scopes match by (type, name) under matching parents, and cover items by
    (name, cover type) within matching scopes; a name repeated under one
    parent matches by occurrence.  Everything else -- flags, source
    locations, weights, goals, at_least, attributes, tags, covergroup options,
    toggle metadata -- comes from the first source that has the scope or item.
    Every scope and cover-item type is merged; none is special-cased away.
    """

    def __init__(self):
        self.dst_db = None

    def merge(self, dst_db, src_db_l: List[UCIS]):
        self.dst_db = dst_db
        self._du_m: Dict[int, object] = {}       # id(src DU) -> dst DU
        self._du_name_m: Dict[tuple, object] = {}  # (type, name) -> dst DU
        self._merge_children(dst_db, list(src_db_l))
        self._merge_history_nodes(dst_db, src_db_l)

    # ------------------------------------------------------------------
    # Scopes
    # ------------------------------------------------------------------
    @staticmethod
    def _group(items, key):
        """``OrderedDict`` of key -> [obj per source]; the n-th repeat of a
        key within one source pairs with the n-th repeat in the others."""
        groups: Dict[tuple, List] = OrderedDict()
        for i, objs in enumerate(items):
            seen: Dict[tuple, int] = {}
            for obj in objs:
                k = key(obj)
                n = seen.get(k, 0)
                seen[k] = n + 1
                groups.setdefault(k + (n,), [None] * len(items))[i] = obj
        return groups

    def _merge_children(self, dst_parent, src_parents):
        children = [list(p.scopes(ScopeTypeT.ALL)) if p is not None else []
                    for p in src_parents]
        groups = self._group(
            children, lambda s: (int(s.getScopeType()), s.getScopeName()))

        # Design units first: instances refer to them.
        order = sorted(groups.items(),
                       key=lambda kv: not ScopeTypeT.DU_ANY(ScopeTypeT(kv[0][0])))
        coverpoints: Dict[str, object] = {}
        for (stype, name, _), srcs in order:
            stype = ScopeTypeT(stype)
            src0 = next(s for s in srcs if s is not None)
            dst = self._create_scope(dst_parent, src0, stype, coverpoints)
            if stype == ScopeTypeT.COVERPOINT:
                coverpoints[name] = dst
            if ScopeTypeT.DU_ANY(stype):
                for s in srcs:
                    if s is not None:
                        self._du_m[id(s)] = dst
                self._du_name_m[(int(stype), name)] = dst
            self._copy_scope_data(dst, srcs)
            self._merge_items(dst, srcs)
            self._merge_children(dst, srcs)
            if stype == ScopeTypeT.FSM and hasattr(dst, "_states"):
                from covsight.core.ncdb.fsm import FsmReader
                FsmReader()._rebuild(dst, {})

    def _create_scope(self, dst_parent, src, stype, coverpoints):
        name = src.getScopeName()
        srcinfo = self._srcinfo(src.getSourceInfo())
        weight = src.getWeight() if hasattr(src, "getWeight") else 1
        source = self._source_type(src)
        flags = getattr(src, "m_flags", 0) or 0

        if stype == ScopeTypeT.INSTANCE:
            return dst_parent.createInstance(
                name, srcinfo, weight, source, stype,
                self._dst_du(src.getInstanceDu()), flags)
        if stype == ScopeTypeT.CROSS:
            points = []
            for i in range(src.getNumCrossedCoverpoints()):
                cp_name = src.getIthCrossedCoverpoint(i).getScopeName()
                if cp_name not in coverpoints:
                    raise Exception("Cannot find coverpoint %s when creating cross %s" % (
                        cp_name, name))
                points.append(coverpoints[cp_name])
            return dst_parent.createCross(name, srcinfo, weight, source, points)
        if stype == ScopeTypeT.COVERGROUP and hasattr(dst_parent, "createCovergroup"):
            return dst_parent.createCovergroup(name, srcinfo, weight, source)
        if stype == ScopeTypeT.COVERPOINT and hasattr(dst_parent, "createCoverpoint"):
            return dst_parent.createCoverpoint(name, srcinfo, weight, source)
        if stype == ScopeTypeT.COVERINSTANCE and hasattr(dst_parent, "createCoverInstance"):
            return dst_parent.createCoverInstance(name, srcinfo, weight, source)
        return dst_parent.createScope(name, srcinfo, weight, source, stype, flags)

    def _dst_du(self, src_du):
        if src_du is None:
            return None
        dst = self._du_m.get(id(src_du))
        if dst is None:
            key = (int(src_du.getScopeType()), src_du.getScopeName())
            dst = self._du_name_m.get(key)
        if dst is None:
            # The DU is not in the source's tree (a detached placeholder):
            # give it a home at the top level.
            dst = self.dst_db.createScope(
                src_du.getScopeName(), self._srcinfo(src_du.getSourceInfo()),
                src_du.getWeight(), self._source_type(src_du),
                src_du.getScopeType(), getattr(src_du, "m_flags", 0) or 0)
            self._du_m[id(src_du)] = dst
            self._du_name_m[(int(src_du.getScopeType()), src_du.getScopeName())] = dst
        return dst

    @staticmethod
    def _source_type(scope):
        st = getattr(scope, "m_source", None)
        return st if st is not None else SourceT.NONE

    def _copy_scope_data(self, dst, srcs):
        src0 = next(s for s in srcs if s is not None)
        if hasattr(src0, "getGoal") and hasattr(dst, "setGoal"):
            goal = src0.getGoal()
            if goal is not None and goal != -1:
                dst.setGoal(goal)
        for getter, setter in _CVG_OPTIONS + _TOGGLE_META:
            if hasattr(src0, getter) and hasattr(dst, setter):
                value = getattr(src0, getter)()
                if value is not None:
                    getattr(dst, setter)(value)
        if hasattr(dst, "setAttribute"):
            for s in reversed([s for s in srcs if s is not None]):
                if hasattr(s, "getAttributes"):
                    for k, v in s.getAttributes().items():
                        dst.setAttribute(k, v)
        if hasattr(dst, "addTag"):
            for s in srcs:
                if s is not None and hasattr(s, "getTags"):
                    for t in s.getTags():
                        dst.addTag(t)

    # ------------------------------------------------------------------
    # Cover items
    # ------------------------------------------------------------------
    def _merge_items(self, dst, srcs):
        items = [list(s.coverItems(_ALL)) if s is not None else [] for s in srcs]
        if not any(items):
            return
        groups = self._group(
            items, lambda ci: (ci.getName(), int(ci.getCoverData().type)))
        merged = []
        for (name, _, _), cis in groups.items():
            present = [ci for ci in cis if ci is not None]
            cd = copy.copy(present[0].getCoverData())
            cd.data = sum(ci.getCoverData().data for ci in present)
            srcinfo = next((ci.getSourceInfo() for ci in present
                            if ci.getSourceInfo() is not None), None)
            dst.createNextCover(name, cd, self._srcinfo(srcinfo))
            merged.append(present)

        # createNextCover returns an index or an item depending on the scope
        # class, so pick the new items up by position.
        created = list(dst.coverItems(_ALL))[-len(merged):]
        for dst_ci, present in zip(created, merged):
            if not hasattr(dst_ci, "setAttribute"):
                continue
            for ci in reversed(present):
                if hasattr(ci, "getAttributes"):
                    for k, v in ci.getAttributes().items():
                        dst_ci.setAttribute(k, v)

    def _srcinfo(self, si) -> Optional[SourceInfo]:
        """``si`` with its file handle moved into the destination database."""
        if si is None or si.file is None:
            return si
        fh = self.dst_db.createFileHandle(si.file.getFileName(), None)
        return SourceInfo(fh, si.line, si.token)

    # ------------------------------------------------------------------
    # History
    # ------------------------------------------------------------------
    def _merge_history_nodes(self, dst_db, src_db_l: List[UCIS]):
        """Copy history nodes from all source databases into *dst_db*."""
        def _node_key(n):
            return getattr(n, 'history_id', id(n))

        for db in src_db_l:
            src_nodes = list(db.historyNodes(HistoryNodeKind.ALL))
            src_to_dst = {}

            def _sort_key(n):
                depth = 0
                p = n.getParent()
                while p is not None:
                    depth += 1
                    p = p.getParent()
                return depth

            for src_hn in sorted(src_nodes, key=_sort_key):
                src_parent = src_hn.getParent()
                dst_parent = src_to_dst.get(_node_key(src_parent)) if src_parent is not None else None
                dst_hn = dst_db.createHistoryNode(
                    dst_parent,
                    src_hn.getLogicalName(),
                    src_hn.getPhysicalName(),
                    src_hn.getKind()
                )
                src_to_dst[_node_key(src_hn)] = dst_hn
                dst_hn.setTestStatus(src_hn.getTestStatus())
                if src_hn.getSimTime() is not None:
                    dst_hn.setSimTime(src_hn.getSimTime())
                if src_hn.getTimeUnit() is not None:
                    dst_hn.setTimeUnit(src_hn.getTimeUnit())
                if src_hn.getRunCwd() is not None:
                    dst_hn.setRunCwd(src_hn.getRunCwd())
                if src_hn.getCpuTime() is not None:
                    dst_hn.setCpuTime(src_hn.getCpuTime())
                if src_hn.getSeed() is not None:
                    dst_hn.setSeed(src_hn.getSeed())
                if src_hn.getCmd() is not None:
                    dst_hn.setCmd(src_hn.getCmd())
                if src_hn.getDate() is not None:
                    dst_hn.setDate(src_hn.getDate())
                if src_hn.getUserName() is not None:
                    dst_hn.setUserName(src_hn.getUserName())
                if src_hn.getToolCategory() is not None:
                    dst_hn.setToolCategory(src_hn.getToolCategory())
                if src_hn.getVendorId() is not None:
                    dst_hn.setVendorId(src_hn.getVendorId())
                if src_hn.getVendorTool() is not None:
                    dst_hn.setVendorTool(src_hn.getVendorTool())
                if src_hn.getVendorToolVersion() is not None:
                    dst_hn.setVendorToolVersion(src_hn.getVendorToolVersion())
                if src_hn.getComment() is not None:
                    dst_hn.setComment(src_hn.getComment())
