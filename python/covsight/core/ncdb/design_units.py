"""
design_units.json — design unit (DU) name-to-scope index.

Provides a fast lookup table mapping DU scope names to their DFS indices so
tools can locate design units without scanning the full scope tree.

Format:
  {"version": 1, "units": [
    {"name": "<str>", "idx": <int>, "type": <int>},
    ...
  ], "instances": [[<instance idx>, <DU idx>], ...]}

Only DU_ANY scopes are included.  *idx* is the DFS index from dfs_scope_list().

``instances`` (optional) links each INSTANCE scope to its design unit.
scope_tree.bin does not record an instance's DU, and without this list a
reader can only guess a sibling DU with the instance's own name -- right for
``top`` (module ``top``), wrong for ``u_fifo`` (module ``fifo``).  Readers
that predate the key ignore it.
"""

import json

from covsight.core.api import ScopeTypeT

from .dfs_util import dfs_scope_list

_VERSION = 1


class DesignUnitsWriter:
    """Serialize DU scope index to design_units.json bytes."""

    def serialize(self, db) -> bytes:
        units = []
        scopes = dfs_scope_list(db)
        du_idx = {}
        for idx, scope in enumerate(scopes):
            scope_type = scope.getScopeType()
            if ScopeTypeT.DU_ANY(scope_type):
                du_idx[id(scope)] = idx
                units.append({
                    "name": scope.getScopeName(),
                    "idx":  idx,
                    "type": int(scope_type),
                })
        if not units:
            return b""
        instances = []
        for idx, scope in enumerate(scopes):
            if scope.getScopeType() == ScopeTypeT.INSTANCE:
                du = scope.getInstanceDu() if hasattr(scope, "getInstanceDu") else None
                if du is not None and id(du) in du_idx:
                    instances.append([idx, du_idx[id(du)]])
        payload = {"version": _VERSION, "units": units}
        if instances:
            payload["instances"] = instances
        return json.dumps(payload, separators=(',', ':')).encode()


class DesignUnitsReader:
    """Deserialize design_units.json and build a name → scope lookup."""

    def link_instances(self, data: bytes, db) -> None:
        """Point each INSTANCE scope at the DU the ``instances`` list names."""
        if not data:
            return
        payload = json.loads(data.decode())
        pairs = payload.get("instances") if payload.get("version") == _VERSION else None
        if not pairs:
            return
        scopes = dfs_scope_list(db)
        for inst_idx, du_idx in pairs:
            if inst_idx < len(scopes) and du_idx < len(scopes):
                inst = scopes[inst_idx]
                if hasattr(inst, "m_du_scope"):
                    inst.m_du_scope = scopes[du_idx]

    def build_index(self, data: bytes, db) -> dict:
        """Return a {name: scope} dict from design_units.json *data*.

        Falls back to scanning dfs_scope_list() when *data* is empty so the
        method always returns a usable index.
        """
        if data:
            payload = json.loads(data.decode())
            if payload.get("version") == _VERSION:
                scopes = dfs_scope_list(db)
                index = {}
                for entry in payload.get("units", []):
                    idx = entry["idx"]
                    if idx < len(scopes):
                        index[entry["name"]] = scopes[idx]
                return index

        # Fallback: linear scan (used when design_units.json absent)
        index = {}
        for scope in dfs_scope_list(db):
            if ScopeTypeT.DU_ANY(scope.getScopeType()):
                index[scope.getScopeName()] = scope
        return index
