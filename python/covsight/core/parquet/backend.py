"""Parquet-backed UCIS backend -- the read side of the object API.

A tool that only uses the UCIS object API cannot tell a Parquet-backed
database from an NCDB-backed or in-memory one; that equivalence is what makes
this a *backend* rather than an export format.

Read-only by design.  The write path is a batch loader
(:mod:`covsight.core.parquet.writer`), matching how coverage is actually
produced and consumed: built at the edge, queried centrally.  Streaming
``ucis_ScopeCreate`` against a live dataset is Phase 2 of the mapping doc and
deliberately out of scope here -- the creation methods raise
:class:`~covsight.core.api.unimpl_error.UnimplError` rather than pretend.
"""

from covsight.core.api import (
    CoverData, CoverTypeT, FileHandle, HistoryNode, HistoryNodeKind,
    IntProperty, RealProperty, Scope, ScopeTypeT, SourceInfo, StrProperty,
    UCIS,
)
from covsight.core.api.cover_index import CoverIndex
from covsight.core.api.cross import Cross
from covsight.core.api.unimpl_error import UnimplError

from covsight.core.parquet import schema as sch
from covsight.core.parquet.query import MaskedCursor, ParquetDataset


class ParquetFileHandle(FileHandle):
    """A source-file reference resolved out of the ``source_files`` table."""

    def __init__(self, filename):
        self._filename = filename

    def getFileName(self) -> str:
        return self._filename


class _PropertyView:
    """Shared typed-property resolution: promoted column, then EAV row.

    Encodes the mapping doc's access rule -- hot properties come from the
    promoted columns, the long tail hits ``properties`` (lazily, per table) --
    and the promotion bitmask decides whether a column is even eligible, so
    "the source backend does not support this property" survives the round
    trip as a raise rather than as a plausible-looking default.
    """

    _kind = None

    def _prop_row(self, namespace, prop_id):
        raise NotImplementedError

    def _promoted_value(self, column):
        raise NotImplementedError

    def _promoted_mask(self) -> int:
        raise NotImplementedError

    def _resolve(self, prop, ptype):
        for i, (candidate, cand_type, column) in enumerate(
                sch.promoted_properties(self._kind)):
            if candidate is not prop or cand_type != ptype:
                continue
            if not (self._promoted_mask() >> i) & 1:
                raise UnimplError()
            return self._promoted_value(column)
        row = self._prop_row(sch.NS_UCIS, prop.name)
        if row is None:
            raise UnimplError()
        return row

    def getIntProperty(self, coverindex: int, property: IntProperty) -> int:
        value = self._resolve(property, "int")
        if isinstance(value, dict):
            return value["i64"]
        return value

    def getRealProperty(self, coverindex: int,
                        property: RealProperty) -> float:
        value = self._resolve(property, "real")
        if isinstance(value, dict):
            return value["f64"]
        return value

    def getStringProperty(self, coverindex: int,
                          property: StrProperty) -> str:
        try:
            value = self._resolve(property, "str")
        except UnimplError:
            # An unset string property reads as absent, not unsupported --
            # matching the in-memory backend, which returns None for any key
            # it has no value for.
            return None
        if isinstance(value, dict):
            return value["str"]
        return value

    def getHandleProperty(self, coverindex: int, property):
        row = self._prop_row(sch.NS_UCIS, property.name)
        if row is None:
            raise UnimplError()
        return self._db.scope(row["handle"])

    # -- UCIS+ extension namespace ----------------------------------------

    def getAttribute(self, key: str):
        row = self._prop_row(sch.NS_COVSIGHT, sch.PROP_ATTR_PREFIX + key)
        return None if row is None else row["str"]

    def getAttributes(self) -> dict:
        out = {}
        for (namespace, prop_id), row in self._all_props().items():
            if namespace == sch.NS_COVSIGHT and \
                    prop_id.startswith(sch.PROP_ATTR_PREFIX):
                out[prop_id[len(sch.PROP_ATTR_PREFIX):]] = row["str"]
        return out

    def getTags(self) -> set:
        out = set()
        for (namespace, prop_id), _row in self._all_props().items():
            if namespace == sch.NS_COVSIGHT and \
                    prop_id.startswith(sch.PROP_TAG_PREFIX):
                out.add(prop_id[len(sch.PROP_TAG_PREFIX):])
        return out

    def hasTag(self, tag_name: str) -> bool:
        return self._prop_row(
            sch.NS_COVSIGHT, sch.PROP_TAG_PREFIX + tag_name) is not None

    def _all_props(self) -> dict:
        raise NotImplementedError

    # -- mutation ---------------------------------------------------------

    def _read_only(self, what):
        return UnimplError(
            "%s is read-only; rewrite the dataset with ParquetWriter" % what)

    def setIntProperty(self, coverindex, property, value):
        raise self._read_only("Parquet backend")

    def setRealProperty(self, coverindex, property, value):
        raise self._read_only("Parquet backend")

    def setStringProperty(self, coverindex, property, value):
        raise self._read_only("Parquet backend")

    def setHandleProperty(self, coverindex, property, value):
        raise self._read_only("Parquet backend")

    def setAttribute(self, key, value):
        raise self._read_only("Parquet backend")

    def deleteAttribute(self, key):
        raise self._read_only("Parquet backend")

    def addTag(self, tag_name):
        raise self._read_only("Parquet backend")

    def removeTag(self, tag_name):
        raise self._read_only("Parquet backend")


class ParquetCoverIndex(CoverIndex):
    """A cover bin: definition from ``coveritems``, count from ``counts``.

    The count is the *merged* value over the selected runs -- which is why a
    tool reading a 64-run dataset needs no merge step at all.
    """

    def __init__(self, db, row):
        CoverIndex.__init__(self)
        self._db = db
        self._row = row

    @property
    def coveritem_id(self) -> int:
        return self._row["coveritem_id"]

    def getName(self) -> str:
        return self._row["name"]

    def getCoverData(self) -> CoverData:
        data = CoverData(CoverTypeT(self._row["cover_type"]),
                         self._row["flags"] or 0)
        data.data = self._db.dataset.counts().get(self.coveritem_id, 0)
        for field in ("goal", "weight", "limit", "bitlen", "at_least"):
            value = self._row.get(field)
            if value is not None:
                setattr(data, field, value)
        return data

    def getSourceInfo(self) -> SourceInfo:
        return self._db._source_info(self._row, self._scope_du_id())

    def _scope_du_id(self):
        scope_row = self._db.dataset.scope_by_uid().get(self._row["scope_id"])
        return scope_row["du_id"] if scope_row else None

    def getCoverFlags(self) -> int:
        return self._row["flags"] or 0

    def getAttribute(self, key: str):
        props = self._db._props_for(sch.ObjectKind.COVERITEM,
                                   str(self.coveritem_id))
        row = props.get((sch.NS_COVSIGHT, sch.PROP_ATTR_PREFIX + key))
        return None if row is None else row["str"]

    def getAttributes(self) -> dict:
        props = self._db._props_for(sch.ObjectKind.COVERITEM,
                                    str(self.coveritem_id))
        return {prop_id[len(sch.PROP_ATTR_PREFIX):]: row["str"]
                for (namespace, prop_id), row in props.items()
                if namespace == sch.NS_COVSIGHT
                and prop_id.startswith(sch.PROP_ATTR_PREFIX)}

    def incrementCover(self, amt=1):
        raise UnimplError(
            "Parquet backend is read-only; append a new run instead of "
            "mutating counts")

    def setCoverData(self, data):
        raise UnimplError("Parquet backend is read-only")

    def setCoverFlags(self, flags):
        raise UnimplError("Parquet backend is read-only")

    def setAttribute(self, key, value):
        raise UnimplError("Parquet backend is read-only")


class ParquetScope(_PropertyView, Scope):
    """A scope reconstructed from the ``scopes`` table."""

    _kind = sch.ObjectKind.SCOPE

    def __init__(self, db, row):
        # Deliberately not Scope.__init__: it calls setGoal(), which a
        # read-only scope rejects.
        self._db = db
        self._row = row

    # -- identity and shape ------------------------------------------------

    @property
    def unique_id(self) -> str:
        return self._row["unique_id"]

    def getScopeName(self) -> str:
        return self._row["name"]

    def getScopeType(self) -> ScopeTypeT:
        return ScopeTypeT(self._row["scope_type"])

    def getFlags(self):
        return self._row["flags"] or 0

    def getWeight(self):
        return self._row["weight"]

    def getGoal(self) -> int:
        return self._row["goal"]

    def getSourceInfo(self) -> SourceInfo:
        return self._db._source_info(self._row, self._row["du_id"])

    def getInstanceDu(self):
        return self._db.scope(self._row["instance_du_id"])

    def getSignature(self):
        """Design-unit signature (``UCIS_STR_DU_SIGNATURE``)."""
        return self.getStringProperty(-1, StrProperty.DU_SIGNATURE)

    def getHierName(self, separator=None) -> str:
        """Full hierarchical path, walked from the stored parent links."""
        sep = separator or self._db.getPathSeparator()
        parts, row = [], self._row
        while row is not None:
            parts.append(row["name"] or "")
            row = self._db.dataset.scope_by_uid().get(row["parent_id"])
        return sep.join(reversed(parts))

    # -- iteration ---------------------------------------------------------

    def scopes(self, mask):
        children = self._db.dataset.children_of().get(self.unique_id, [])
        return MaskedCursor(children, mask, "scope_type", self._db.scope_of_row)

    def coverItems(self, mask):
        items = self._db.dataset.coveritems_of().get(self.unique_id, [])
        return MaskedCursor(items, mask, "cover_type",
                            lambda row: ParquetCoverIndex(self._db, row))

    # -- property plumbing -------------------------------------------------

    def _all_props(self):
        return self._db._props_for(sch.ObjectKind.SCOPE, self.unique_id)

    def _prop_row(self, namespace, prop_id):
        return self._all_props().get((namespace, prop_id))

    def _promoted_mask(self) -> int:
        return self._row.get("promoted_props") or 0

    def _promoted_value(self, column):
        if sch.is_derived(column):
            return self._derived_value(column)
        return self._row[column]

    def _derived_value(self, column):
        """Promoted properties that are computed, not stored.

        Each is derived from data that already exists somewhere else in the
        dataset -- this read's counts, or the ``cross_operands`` rows -- so
        storing the value again would be a second copy free to disagree.
        """
        if column == sch.DERIVED_TOGGLE_COVERED:
            return self._toggle_covered()
        if column == sch.DERIVED_NUM_CROSSED_CVPS:
            return len(self._cross_operands())
        if column == sch.DERIVED_ITH_CROSSED_CVP_NAME:
            return self._crossed_name(0)
        raise UnimplError("unknown derived property %r" % (column,))

    def _cross_operands(self):
        """This scope's crossed-coverpoint rows, in cross order."""
        return self._db.dataset.cross_operands_of().get(self.unique_id, [])

    def _crossed_name(self, index):
        operands = self._cross_operands()
        if index < 0 or index >= len(operands):
            return None
        return operands[index]["name"]

    def _toggle_covered(self) -> int:
        counts = self._db.dataset.counts()
        rising = falling = False
        for row in self._db.dataset.coveritems_of().get(self.unique_id, []):
            if int(row["cover_type"] or 0) != int(CoverTypeT.TOGGLEBIN):
                continue
            if not counts.get(row["coveritem_id"], 0) > 0:
                continue
            name = row["name"] or ""
            if "0->1" in name or "01" in name:
                rising = True
            if "1->0" in name or "10" in name:
                falling = True
        return 1 if (rising and falling) else 0

    def getStringProperty(self, coverindex, property):
        if property == StrProperty.ITH_CROSSED_CVP_NAME:
            # The only UCIS string property whose `coverindex` selects
            # something other than a cover bin: here it is the *i* of
            # `ucis_GetIthCrossedCvp`.  -1 keeps the legacy single-name
            # reading, which is all a pre-`cross_operands` writer could store.
            return self._crossed_name(0 if coverindex is None or coverindex < 0
                                      else coverindex)
        if property == StrProperty.SCOPE_NAME:
            return self.getScopeName()
        if property == StrProperty.UNIQUE_ID:
            return self.unique_id
        if property == StrProperty.SCOPE_HIER_NAME:
            return self.getHierName()
        return _PropertyView.getStringProperty(self, coverindex, property)

    def getIntProperty(self, coverindex, property):
        if coverindex is not None and coverindex >= 0:
            return self._cover_int_property(coverindex, property)
        return _PropertyView.getIntProperty(self, coverindex, property)

    def _cover_int_property(self, coverindex, property):
        items = self._db.dataset.coveritems_of().get(self.unique_id, [])
        if coverindex >= len(items):
            raise UnimplError("cover index %d out of range" % coverindex)
        data = ParquetCoverIndex(self._db, items[coverindex]).getCoverData()
        if property == IntProperty.COVER_GOAL:
            return data.goal
        if property == IntProperty.COVER_WEIGHT:
            return data.weight
        if property == IntProperty.COVER_LIMIT:
            return data.limit
        raise UnimplError()

    # -- creation (read-only) ---------------------------------------------

    def setWeight(self, w):
        raise self._read_only("Parquet backend")

    def setGoal(self, goal):
        raise self._read_only("Parquet backend")

    def createScope(self, *args, **kwargs):
        raise self._read_only("Parquet backend")

    def createInstance(self, *args, **kwargs):
        raise self._read_only("Parquet backend")

    def createCovergroup(self, *args, **kwargs):
        raise self._read_only("Parquet backend")

    def createToggle(self, *args, **kwargs):
        raise self._read_only("Parquet backend")

    def createNextCover(self, *args, **kwargs):
        raise self._read_only("Parquet backend")

    def removeCover(self, coverindex):
        raise self._read_only("Parquet backend")

    def __repr__(self):
        return "ParquetScope(%r)" % (self.unique_id,)


class ParquetCross(ParquetScope, Cross):
    """A ``UCIS_CROSS`` scope, which also answers the operand-list API.

    Read back as a ``Cross`` rather than a plain scope so a consumer can ask
    ``ucis_GetIthCrossedCvp``'s question of a Parquet dataset exactly as it
    would of the in-memory backend -- the operand list is stored, so hiding it
    behind a type that cannot express it would throw it away on the way out.
    """

    def getNumCrossedCoverpoints(self) -> int:
        return len(self._cross_operands())

    def getIthCrossedCoverpoint(self, index):
        operands = self._cross_operands()
        if index < 0 or index >= len(operands):
            raise IndexError(
                "cross %r has %d crossed coverpoint(s); no index %d"
                % (self.getScopeName(), len(operands), index))
        # None when the source gave a name but no resolvable handle -- the
        # name is still readable through UCIS_STR_ITH_CROSSED_CVP_NAME.
        return self._db.scope(operands[index]["coverpoint_id"])


class ParquetHistoryNode(_PropertyView, HistoryNode):
    """A history node: per-run metadata, read from its run's partition."""

    _kind = sch.ObjectKind.HISTORY_NODE

    def __init__(self, db, row):
        self._db = db
        self._row = row

    @property
    def node_id(self) -> str:
        return self._row["node_id"]

    @property
    def run_id(self) -> str:
        return self._row.get(sch.RUN_PARTITION_COL)

    def getParent(self):
        return self._db.history_node(self._row["parent_id"])

    def getLogicalName(self) -> str:
        return self._row["logical_name"]

    def getPhysicalName(self) -> str:
        return self._row["physical_name"]

    def getKind(self) -> HistoryNodeKind:
        kind = self._row["kind"]
        return None if kind is None else HistoryNodeKind(kind)

    def getTestStatus(self):
        return self._row["status"]

    def getSimTime(self) -> float:
        return self._row["sim_time"]

    def getTimeUnit(self) -> str:
        return self._row["time_unit"]

    def getRunCwd(self) -> str:
        return self._row["run_cwd"]

    def getCpuTime(self) -> float:
        return self._row["cpu_time"]

    def getSeed(self) -> str:
        return self._row["seed"]

    def getCmd(self) -> str:
        return self._row["cmdline"]

    def getArgs(self):
        return self._row["args"]

    def getCompulsory(self):
        return self._row["compulsory"]

    def getDate(self):
        date = self._row["date"]
        return date if date is not None else self._row["date_int"]

    def getUserName(self) -> str:
        return self._row["user_name"]

    def getCost(self):
        return self._row["cost"]

    def getToolCategory(self) -> str:
        return self._row["tool_category"]

    def getUCISVersion(self) -> str:
        return self._row["ucis_version"]

    def getVendorId(self) -> str:
        return self._row["vendor_id"]

    def getVendorTool(self) -> str:
        return self._row["vendor_tool"]

    def getVendorToolVersion(self) -> str:
        return self._row["vendor_tool_version"]

    def getSameTests(self):
        return self._row["same_tests"]

    def getComment(self):
        return self._row["comment"]

    def _all_props(self):
        return self._db.dataset.history_properties_of().get(
            (int(sch.ObjectKind.HISTORY_NODE), self.node_id), {})

    def _prop_row(self, namespace, prop_id):
        return self._all_props().get((namespace, prop_id))

    def _promoted_mask(self) -> int:
        return self._row.get("promoted_props") or 0

    def _promoted_value(self, column):
        return self._row[column]

    def __repr__(self):
        return "ParquetHistoryNode(%r)" % (self.node_id,)


class ParquetUCIS(UCIS):
    """A UCIS database backed by a Parquet dataset.

    Args:
        path: Dataset directory.
        runs: Runs to expose -- a run id, a sequence of ids, or None for all.
            With more than one selected, counts read as the virtual merge of
            those runs, so a merged view costs no data movement.

    Example:
        >>> db = ParquetUCIS("cov.parquet")              # all runs, merged
        >>> one = ParquetUCIS("cov.parquet", runs="run-0003")   # one run
    """

    def __init__(self, path, runs=None):
        UCIS.__init__(self)
        self.dataset = ParquetDataset(path, runs=runs)
        self.path = str(path)
        self._scopes = {}
        self._history = {}
        self._flat_ids = None

    # -- database metadata -------------------------------------------------

    def _meta(self, key, default=None):
        value = self.dataset.db_metadata.get(key)
        return default if value is None else value

    def getAPIVersion(self) -> str:
        return self._meta("api_version", "1.0")

    def getDBVersion(self):
        return self.dataset.schema_version

    def getWrittenBy(self) -> str:
        return self._meta("written_by")

    def getWrittenTime(self) -> int:
        return self._meta("written_time", 0)

    def getPathSeparator(self) -> str:
        return self._meta("path_separator", "/")

    def isModified(self):
        return False

    def modifiedSinceSim(self):
        return False

    def getNumTests(self) -> int:
        return sum(1 for row in self.dataset.history_rows()
                   if row["kind"] == int(HistoryNodeKind.TEST))

    @property
    def run_ids(self):
        """Every run in the dataset, whether or not it is selected."""
        return self.dataset.run_ids

    @property
    def selected_runs(self):
        """The runs this view merges."""
        return tuple(self.dataset.selection)

    # -- root scope behaviour ---------------------------------------------

    def getScopeName(self) -> str:
        return ""

    def getScopeType(self) -> ScopeTypeT:
        return ScopeTypeT.RESERVEDSCOPE

    def getScopeName_(self):  # pragma: no cover - defensive alias
        return ""

    def getWeight(self):
        return 1

    def getGoal(self) -> int:
        return -1

    def setGoal(self, goal):
        # UCIS.__init__ does not set a goal, but Scope's contract allows the
        # call; a read-only database has nothing to store it in.
        raise UnimplError("Parquet backend is read-only")

    def getFlags(self):
        return 0

    def getSourceInfo(self):
        return None

    def scopes(self, mask):
        top = self.dataset.children_of().get(None, [])
        return MaskedCursor(top, mask, "scope_type", self.scope_of_row)

    def coverItems(self, mask):
        return iter(())

    def getIntProperty(self, coverindex, property):
        if property == IntProperty.NUM_TESTS:
            return self.getNumTests()
        if property == IntProperty.IS_MODIFIED:
            return 0
        if property == IntProperty.MODIFIED_SINCE_SIM:
            return 0
        raise UnimplError()

    def getStringProperty(self, coverindex, property):
        if property == StrProperty.FILE_NAME:
            return self.path
        return (self.dataset.db_metadata.get("str_properties") or {}).get(
            property.name)

    def setStringProperty(self, coverindex, property, value):
        raise UnimplError("Parquet backend is read-only")

    def setIntProperty(self, coverindex, property, value):
        raise UnimplError("Parquet backend is read-only")

    # -- object cache ------------------------------------------------------

    def scope_of_row(self, row) -> ParquetScope:
        """Wrapper for a scope row, cached so handle identity is stable."""
        uid = row["unique_id"]
        scope = self._scopes.get(uid)
        if scope is None:
            cls = (ParquetCross
                   if ScopeTypeT(row["scope_type"]) & ScopeTypeT.CROSS
                   else ParquetScope)
            scope = cls(self, row)
            self._scopes[uid] = scope
        return scope

    def scope(self, unique_id):
        """The scope with *unique_id*, or None."""
        if unique_id is None:
            return None
        row = self.dataset.scope_by_uid().get(unique_id)
        return None if row is None else self.scope_of_row(row)

    def history_node(self, node_id):
        if node_id is None:
            return None
        node = self._history.get(node_id)
        if node is None:
            for row in self.dataset.history_rows():
                if row["node_id"] == node_id:
                    node = ParquetHistoryNode(self, row)
                    self._history[node_id] = node
                    break
        return node

    def _props_for(self, kind, object_id):
        return self.dataset.properties_of().get((int(kind), object_id), {})

    def _source_info(self, row, du_id):
        """Rebuild a ``SourceInfo`` from the DU-local file id and position."""
        if row.get("source_line") is None and row.get("source_col") is None \
                and row.get("source_file_id") is None:
            return None
        path = self.dataset.source_path(du_id, row.get("source_file_id"))
        handle = ParquetFileHandle(path) if path is not None else None
        return SourceInfo(handle, row.get("source_line"),
                          row.get("source_col"))

    # -- lookups -----------------------------------------------------------

    def matchScopeByUniqueId(self, uid: str):
        """O(1) -- ``unique_id`` is the primary key, not a scanned attribute."""
        return self.scope(uid)

    def matchCoverByUniqueId(self, uid: str):
        for row in self.dataset.table("coveritems").to_pylist():
            if str(row["coveritem_id"]) == str(uid):
                return (self.scope(row["scope_id"]), row["local_index"])
            props = self._props_for(sch.ObjectKind.COVERITEM,
                                    str(row["coveritem_id"]))
            named = props.get((sch.NS_UCIS, StrProperty.UNIQUE_ID.name))
            if named is not None and named["str"] == uid:
                return (self.scope(row["scope_id"]), row["local_index"])
        return (None, -1)

    # -- history and sources ----------------------------------------------

    def historyNodes(self, kind: HistoryNodeKind):
        for row in self.dataset.history_rows():
            node = self.history_node(row["node_id"])
            if node is None:
                continue
            if kind == HistoryNodeKind.ALL or node.getKind() == kind:
                yield node

    def getSourceFiles(self):
        return [ParquetFileHandle(path) for path in self.dataset.source_paths()]

    def getCoverInstances(self):
        return [scope for scope in self.scopes(ScopeTypeT.ALL)
                if scope.getScopeType() == ScopeTypeT.INSTANCE]

    def get_formal_data(self, bin_index: int):
        """Merged formal result for the bin at flat *bin_index*, or None.

        Same signature as the in-memory backend, so a formal report does not
        care which backend it is reading.  Across several runs the status is
        the precedence merge, not whichever run happened to be written last.
        """
        flat = self._flat_coveritem_ids()
        if not 0 <= bin_index < len(flat):
            return None
        return self.dataset.formal_data().get(flat[bin_index])

    def _flat_coveritem_ids(self):
        """Cover bin ids in flat-bin-index order (``dfs_ordinal``, then local)."""
        if getattr(self, "_flat_ids", None) is None:
            flat = []
            for scope_row in self.dataset.scope_rows():
                for item in self.dataset.coveritems_of().get(
                        scope_row["unique_id"], []):
                    flat.append(item["coveritem_id"])
            self._flat_ids = flat
        return self._flat_ids

    def get_test_coverage_api(self):
        """Per-test contribution queries over the association table."""
        from covsight.core.parquet.test_coverage import ParquetTestCoverage
        return ParquetTestCoverage(self)

    # -- lifecycle ---------------------------------------------------------

    def write(self, file: str, scope=None, recurse: bool = True,
              covertype: int = -1):
        """Write this database out as a new Parquet dataset.

        Partial writes (*scope* / *covertype* filters) are not implemented:
        selecting a subtree is a query concern, and doing it half-way here
        would silently drop data.
        """
        if scope is not None or covertype != -1 or not recurse:
            raise UnimplError(
                "partial write is not supported by the Parquet backend")
        from covsight.core.parquet.writer import ParquetWriter
        writer = ParquetWriter(file)
        for run_id in self.selected_runs:
            single = ParquetUCIS(self.path, runs=run_id)
            writer.write(single, run_id=run_id)
        return file

    def close(self):
        self.dataset._tables.clear()
        self.dataset._indexes.clear()
        self._scopes.clear()
        self._history.clear()
        self._flat_ids = None

    # -- creation (read-only) ---------------------------------------------

    def createScope(self, *args, **kwargs):
        raise UnimplError("Parquet backend is read-only")

    def createInstance(self, *args, **kwargs):
        raise UnimplError("Parquet backend is read-only")

    def createFileHandle(self, filename, workdir):
        raise UnimplError("Parquet backend is read-only")

    def createHistoryNode(self, parent, logicalname, physicalname, kind):
        raise UnimplError("Parquet backend is read-only")

    def removeScope(self, scope):
        raise UnimplError("Parquet backend is read-only")

    def __repr__(self):
        return "ParquetUCIS(%r, runs=%r)" % (self.path,
                                             list(self.selected_runs))


def open_dataset(path, runs=None) -> ParquetUCIS:
    """Open the Parquet dataset at *path* as a UCIS database."""
    return ParquetUCIS(path, runs=runs)
