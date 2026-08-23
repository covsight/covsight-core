"""
NcdbWriter — serialize a UCIS model to a ZIP .cdb (NCDB) file.
"""

import zipfile
from datetime import datetime, timezone

from .string_table import StringTable
from .scope_tree import ScopeTreeWriter
from .counts import CountsWriter
from .history import HistoryWriter
from .sources import SourcesWriter
from .attrs import AttrsWriter
from .tags import TagsWriter
from .properties import PropertiesWriter
from .toggle import ToggleWriter
from .fsm import FsmWriter
from .cross import CrossWriter
from .contrib import ContribWriter
from .formal import FormalWriter
from .coveritem_flags import CoveritemFlagsWriter
from .merge_ops import MergeOpsWriter, MEMBER_MERGE_OPS
from .design_units import DesignUnitsWriter
from .manifest import Manifest
from .constants import (
    MEMBER_MANIFEST, MEMBER_STRINGS, MEMBER_SCOPE_TREE,
    MEMBER_COUNTS, MEMBER_HISTORY, MEMBER_SOURCES,
    MEMBER_ATTRS, MEMBER_TAGS, MEMBER_PROPERTIES, MEMBER_TOGGLE, MEMBER_FSM,
    MEMBER_CROSS, MEMBER_DESIGN_UNITS, MEMBER_FORMAL,
    MEMBER_COVERITEM_FLAGS, MEMBER_TESTPLAN, MEMBER_WAIVERS,
    MEMBER_ISSUES, MEMBER_ISSUES_META, MEMBER_ISSUES_HISTORY,
    HISTORY_FORMAT_V2,
)

from covsight.core.api import HistoryNodeKind


class NcdbWriter:
    """Write a UCIS database to an NCDB .cdb ZIP file."""

    def write(self, db, path: str) -> None:
        """Serialize *db* (UCIS) to the file at *path*."""
        string_table = StringTable()
        file_handles: list = []

        # 1. Serialize scope tree (populates string_table, file_handles, counts)
        st_writer = ScopeTreeWriter(string_table, file_handles)
        scope_tree_bytes = st_writer.write(db)
        counts = st_writer.counts_list

        # 2. Serialize counts
        counts_bytes = CountsWriter().serialize(counts)

        # 3. Serialize strings
        strings_bytes = string_table.serialize()

        # 4. History nodes
        history_nodes = list(db.historyNodes(HistoryNodeKind.ALL
                                              if hasattr(HistoryNodeKind, 'ALL')
                                              else HistoryNodeKind.TEST))
        # Collect both TEST and MERGE nodes
        try:
            all_nodes = (
                list(db.historyNodes(HistoryNodeKind.TEST)) +
                list(db.historyNodes(HistoryNodeKind.MERGE))
            )
        except Exception:
            all_nodes = list(db.historyNodes(HistoryNodeKind.TEST))
        history_bytes = HistoryWriter().serialize(all_nodes)

        # 5. Source files
        # Use file handles discovered during scope_tree walk; fall back to db.getSourceFiles()
        if not file_handles:
            try:
                file_handles = list(db.getSourceFiles())
            except Exception:
                file_handles = []
        sources_bytes = SourcesWriter().serialize(file_handles)

        # 6. Sparse optional members
        attrs_bytes   = AttrsWriter().serialize(db)
        tags_bytes    = TagsWriter().serialize(db)
        props_bytes   = PropertiesWriter().serialize(db)
        toggle_bytes  = ToggleWriter().serialize(db)
        fsm_bytes     = FsmWriter().serialize(db)
        cross_bytes   = CrossWriter().serialize(db)
        du_bytes      = DesignUnitsWriter().serialize(db)
        contrib_members = ContribWriter().serialize(db)
        formal_bytes  = FormalWriter().serialize(db)
        ci_flags_bytes = CoveritemFlagsWriter().serialize(db)
        # Non-additive bins, so a merge can be type-aware without
        # decoding the scope tree.  Empty for almost every design.
        merge_ops_bytes = MergeOpsWriter().serialize(db)

        # 7. Manifest
        manifest = Manifest.build(db, scope_tree_bytes, counts, all_nodes,
                                  strings_bytes)

        # Check for v2 binary history members (from NcdbUCIS.get_v2_members)
        v2_members = {}
        if hasattr(db, 'get_v2_members'):
            v2_members = db.get_v2_members()
        if v2_members:
            manifest.history_format = HISTORY_FORMAT_V2

        manifest_bytes = manifest.serialize()

        # 8. Write ZIP
        with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
            zf.writestr(MEMBER_MANIFEST,   manifest_bytes)
            zf.writestr(MEMBER_STRINGS,    strings_bytes)
            zf.writestr(MEMBER_SCOPE_TREE, scope_tree_bytes)
            zf.writestr(MEMBER_COUNTS,     counts_bytes)
            zf.writestr(MEMBER_HISTORY,    history_bytes)
            zf.writestr(MEMBER_SOURCES,    sources_bytes)
            _EMPTY_ATTRS_V2 = b'{"version":2,"scopes":[],"coveritems":[],"history":[],"global":{}}'
            if attrs_bytes not in (b'{"version":1,"entries":[]}', _EMPTY_ATTRS_V2):
                zf.writestr(MEMBER_ATTRS, attrs_bytes)
            if tags_bytes != b'{"version":1,"entries":[]}':
                zf.writestr(MEMBER_TAGS, tags_bytes)
            if props_bytes:
                zf.writestr(MEMBER_PROPERTIES, props_bytes)
            if toggle_bytes:
                zf.writestr(MEMBER_TOGGLE, toggle_bytes)
            if fsm_bytes:
                zf.writestr(MEMBER_FSM, fsm_bytes)
            if cross_bytes:
                zf.writestr(MEMBER_CROSS, cross_bytes)
            if du_bytes:
                zf.writestr(MEMBER_DESIGN_UNITS, du_bytes)
            for member_name, member_bytes in contrib_members.items():
                zf.writestr(member_name, member_bytes)
            if formal_bytes:
                zf.writestr(MEMBER_FORMAL, formal_bytes)
            if ci_flags_bytes:
                zf.writestr(MEMBER_COVERITEM_FLAGS, ci_flags_bytes)
            if merge_ops_bytes:
                zf.writestr(MEMBER_MERGE_OPS, merge_ops_bytes)
            # v2 binary history members (stored uncompressed — pre-compressed)
            for member_name, member_bytes in v2_members.items():
                zf.writestr(member_name, member_bytes,
                            compress_type=zipfile.ZIP_STORED)
            # Testplan (optional)
            testplan = getattr(db, '_testplan', None)
            if testplan is not None:
                zf.writestr(MEMBER_TESTPLAN, testplan.serialize())
            # Waivers (optional)
            waivers = getattr(db, '_waivers', None)
            if waivers is not None:
                zf.writestr(MEMBER_WAIVERS, waivers.serialize())
            # Issues (optional — only write if dirty)
            issues = getattr(db, '_issues', None)
            if issues is not None and getattr(db, '_issues_dirty', False):
                zf.writestr(MEMBER_ISSUES, issues.serialize())
            # Issues meta (optional — only write if dirty)
            issues_meta = getattr(db, '_issues_meta', None)
            if issues_meta is not None and getattr(db, '_issues_meta_dirty', False):
                zf.writestr(MEMBER_ISSUES_META, issues_meta.serialize())
            # Issues history (optional — only write if dirty)
            issues_history_raw = getattr(db, '_issues_history_raw', b'')
            if issues_history_raw and getattr(db, '_issues_history_dirty', False):
                zf.writestr(MEMBER_ISSUES_HISTORY, issues_history_raw,
                            compress_type=zipfile.ZIP_STORED)
