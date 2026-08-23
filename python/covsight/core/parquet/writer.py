"""Write a UCIS database out as a run-partitioned Parquet dataset.

The writer walks *any* UCIS backend through the public object API -- it never
reaches into a backend's internals -- so ``MemUCIS``, an NCDB-backed database
and a Parquet-backed database all convert identically.

Multi-run is the ground state (decision 1 of the implementation plan): a
dataset is a *database*, and each :meth:`ParquetWriter.write` call appends one
``run_id`` partition to the measurement tables.  Definition tables are
run-independent; appending a second run checks them for consistency and leaves
the existing files untouched.
"""

import json
import os
import shutil

import pyarrow as pa
import pyarrow.parquet as pq

from covsight.core.api import (
    CoverTypeT, HandleProperty, HistoryNodeKind, IntProperty, RealProperty,
    ScopeTypeT, StrProperty,
)

from covsight.core.parquet import schema as sch
from covsight.core.parquet.identity import (
    CoveritemIdAllocator, UidAllocator, history_node_id,
)


class DefinitionMismatch(Exception):
    """Raised when a run being appended disagrees with the stored definitions.

    Bin definitions (type, goal, weight, name) must agree across the runs in a
    dataset; a mismatch is tool-version schema drift, and silently picking one
    side would corrupt every merge that follows.
    """


# --------------------------------------------------------------------------
# Property harvesting
# --------------------------------------------------------------------------

# Properties the reader recomputes, so storing them would be pure duplication
# (and, for the count-derived ones, actively wrong).  Keeping this set plus the
# promoted-column set tight is what stops the EAV table from dominating dataset
# size -- the risk decision 3 says the benchmark should watch.
_SKIP_INT_PROPS = frozenset({
    # Database-level, recorded in the dataset manifest rather than per scope.
    IntProperty.IS_MODIFIED,
    IntProperty.MODIFIED_SINCE_SIM,
    IntProperty.NUM_TESTS,
})

_SKIP_STR_PROPS = frozenset({
    StrProperty.SCOPE_NAME,      # `scopes.name`
    StrProperty.UNIQUE_ID,       # `scopes.unique_id` (the PK)
    StrProperty.SCOPE_HIER_NAME,  # derived by walking parents
    StrProperty.FILE_NAME,       # database-level
})

_SKIP_REAL_PROPS = frozenset({
    RealProperty.CVG_INST_AVERAGE,   # derived from instance coverage
})


class _PropSink:
    """Accumulates ``properties``-table rows."""

    def __init__(self):
        self.object_id = []
        self.object_kind = []
        self.namespace = []
        self.prop_id = []
        self.prop_type = []
        self.i64 = []
        self.f64 = []
        self.str = []
        self.handle = []

    def add(self, object_id, kind, namespace, prop_id, prop_type,
            i64=None, f64=None, s=None, handle=None):
        self.object_id.append(str(object_id))
        self.object_kind.append(int(kind))
        self.namespace.append(namespace)
        self.prop_id.append(str(prop_id))
        self.prop_type.append(int(prop_type))
        self.i64.append(i64)
        self.f64.append(f64)
        self.str.append(s)
        self.handle.append(handle)

    def table(self, name="properties") -> pa.Table:
        schema = sch.with_metadata(sch.table_schema(name), name)
        return pa.table({
            "object_id": self.object_id,
            "object_kind": self.object_kind,
            "namespace": self.namespace,
            "prop_id": self.prop_id,
            "prop_type": self.prop_type,
            "i64": self.i64,
            "f64": self.f64,
            "str": self.str,
            "handle": self.handle,
        }, schema=schema)

    def __len__(self):
        return len(self.object_id)


def _harvest_properties(obj, object_id, kind, sink: _PropSink, uid_of=None):
    """Probe the typed-property API and record everything that answers.

    Probing rather than enumerating a hand-written per-type list is what keeps
    the writer backend-agnostic and the round-trip lossless: a property a
    backend supports is stored whether or not this module knows what it means.
    Unsupported properties raise (``UnimplError``) and are skipped.

    Returns:
        The ``promoted_props`` bitmask -- bit *i* set iff the object answered
        promoted property *i* for this object kind.  Promoted properties are
        not written as rows; the bitmask plus the column carries them.
    """
    promoted = sch.promoted_properties(kind)
    promoted_mask = 0
    skip_promoted = {("int" if isinstance(p, IntProperty) else
                      "real" if isinstance(p, RealProperty) else
                      "str", p) for p, _t, _c in promoted}

    for i, (prop, ptype, _col) in enumerate(promoted):
        getter = {"int": obj.getIntProperty, "str": obj.getStringProperty,
                  "real": obj.getRealProperty}[ptype]
        try:
            value = getter(-1, prop)
        except Exception:
            continue
        if value is not None:
            promoted_mask |= 1 << i

    for prop in IntProperty:
        if prop in _SKIP_INT_PROPS or ("int", prop) in skip_promoted:
            continue
        try:
            value = obj.getIntProperty(-1, prop)
        except Exception:
            continue
        if value is None:
            continue
        sink.add(object_id, kind, sch.NS_UCIS, prop.name,
                 sch.PropType.INT64, i64=int(value))

    for prop in StrProperty:
        if prop in _SKIP_STR_PROPS or ("str", prop) in skip_promoted:
            continue
        try:
            value = obj.getStringProperty(-1, prop)
        except Exception:
            continue
        if value is None:
            continue
        sink.add(object_id, kind, sch.NS_UCIS, prop.name,
                 sch.PropType.STRING, s=str(value))

    for prop in RealProperty:
        if prop in _SKIP_REAL_PROPS or ("real", prop) in skip_promoted:
            continue
        try:
            value = obj.getRealProperty(-1, prop)
        except Exception:
            continue
        if value is None:
            continue
        sink.add(object_id, kind, sch.NS_UCIS, prop.name,
                 sch.PropType.REAL, f64=float(value))

    for prop in HandleProperty:
        try:
            value = obj.getHandleProperty(-1, prop)
        except Exception:
            continue
        if value is None:
            continue
        target = uid_of(value) if uid_of is not None else None
        if target is None:
            continue
        sink.add(object_id, kind, sch.NS_UCIS, prop.name,
                 sch.PropType.HANDLE, handle=target)

    # UCIS+ extensions: user attributes and tags ship as rows in a non-`ucis`
    # namespace, never as new columns.
    try:
        attrs = obj.getAttributes() or {}
    except Exception:
        attrs = {}
    for key in sorted(attrs):
        sink.add(object_id, kind, sch.NS_COVSIGHT,
                 sch.PROP_ATTR_PREFIX + str(key), sch.PropType.STRING,
                 s=None if attrs[key] is None else str(attrs[key]))

    try:
        tags = obj.getTags() or set()
    except Exception:
        tags = set()
    for tag in sorted(tags):
        sink.add(object_id, kind, sch.NS_COVSIGHT,
                 sch.PROP_TAG_PREFIX + str(tag), sch.PropType.INT, i64=1)

    return promoted_mask


# --------------------------------------------------------------------------
# Writer
# --------------------------------------------------------------------------

class ParquetWriter:
    """Emit a UCIS database as a Parquet dataset.

    Args:
        path: Dataset directory.  Created if absent; reused (appended to) if it
            already holds a dataset.
        compression: Parquet codec -- ``"snappy"`` for speed, ``"zstd"`` for
            size.  The benchmark reports both so the size comparison against
            NCDB is not cherry-picked.
        compression_level: Codec level, e.g. 19 for zstd.  None uses the
            codec default.
    """

    def __init__(self, path, compression="snappy", compression_level=None):
        self.path = str(path)
        self.compression = compression
        self.compression_level = compression_level

    # -- public API -------------------------------------------------------

    def write(self, db, run_id=None, *, replace_run=False) -> str:
        """Append *db* to the dataset as one run.

        Args:
            db: Any UCIS database.
            run_id: Identifier for this run.  Defaults to ``run-NNNN`` using
                the number of runs already in the dataset, so repeated writes
                land in distinct partitions.
            replace_run: Overwrite the partition if *run_id* already exists.
                Off by default: re-loading a run must not double-count, and
                silently replacing data is not the same guarantee.

        Returns:
            The ``run_id`` that was written.

        Raises:
            DefinitionMismatch: If the definition tables derived from *db*
                disagree with those already stored in the dataset.
            FileExistsError: If *run_id* is already present and *replace_run*
                is False.
        """
        manifest = self._read_manifest()
        existing_runs = [r["run_id"] for r in manifest.get("runs", [])]

        if run_id is None:
            run_id = "run-%04d" % len(existing_runs)
        run_id = str(run_id)

        if run_id in existing_runs and not replace_run:
            raise FileExistsError(
                "run_id %r already present in %s; pass replace_run=True to "
                "overwrite it" % (run_id, self.path))

        walk = _Walk(db, run_id)
        walk.run()

        defs = {
            "scopes": walk.scopes_table(),
            "coveritems": walk.coveritems_table(),
            "cross_operands": walk.cross_operands_table(),
            "properties": walk.properties_table(),
            "source_files": walk.source_files_table(),
        }
        measures = {
            "counts": walk.counts_table(),
            "history_nodes": walk.history_nodes_table(),
            "history_props": walk.history_props_table(),
            "test_cover_assoc": walk.assoc_table(),
            "formal": walk.formal_table(),
        }

        self.write_definitions(defs, allow_existing=bool(existing_runs))
        self.write_run(run_id, measures, db_metadata=walk.db_metadata())
        return run_id

    def write_definitions(self, defs: dict, *, allow_existing=False):
        """Write the definition tables, or verify they already match.

        Args:
            defs: Table name -> Arrow table, covering
                :data:`~covsight.core.parquet.schema.DEFINITION_TABLES`.
            allow_existing: The dataset already holds runs, so the definitions
                must be checked rather than written.

        Raises:
            DefinitionMismatch: If *defs* disagrees with what is stored.
        """
        os.makedirs(self.path, exist_ok=True)
        if allow_existing:
            self._check_definitions(defs)
            return
        for name, table in defs.items():
            self._write_table(self._def_path(name), table)

    def write_run(self, run_id, measures: dict, *, db_metadata=None):
        """Write one run's measurement partitions and update the manifest."""
        os.makedirs(self.path, exist_ok=True)
        for name in sch.MEASUREMENT_TABLES:
            table = measures.get(name)
            if table is None:
                table = sch.empty_table(name)
            part = self._run_path(name, run_id)
            if os.path.isdir(part):
                shutil.rmtree(part)
            self._write_table(part, table)

        manifest = self._read_manifest()
        runs = list(manifest.get("runs", []))
        entry = {
            "run_id": run_id,
            "num_counts": measures["counts"].num_rows,
            "num_tests": measures["history_nodes"].num_rows,
            "num_assoc": measures["test_cover_assoc"].num_rows,
        }
        # Replace in place rather than re-appending: run order drives history
        # iteration order, so moving a replaced run to the end would silently
        # reorder `historyNodes()`.
        for i, existing in enumerate(runs):
            if existing["run_id"] == run_id:
                runs[i] = entry
                break
        else:
            runs.append(entry)
        manifest["runs"] = runs
        manifest["schema_version"] = sch.SCHEMA_VERSION
        manifest["format"] = sch.DATASET_FORMAT
        manifest["compression"] = self.compression
        if db_metadata is not None:
            manifest.setdefault("db", db_metadata)
        for name in ("scopes", "coveritems"):
            path = os.path.join(self._def_path(name), "data.parquet")
            if os.path.exists(path):
                manifest["num_" + name] = pq.read_metadata(path).num_rows
        self._write_manifest(manifest)
        return run_id

    # -- layout -----------------------------------------------------------

    def _def_path(self, name):
        return os.path.join(self.path, name)

    def _run_path(self, name, run_id):
        return os.path.join(self.path, name,
                            "%s=%s" % (sch.RUN_PARTITION_COL, run_id))

    def _write_table(self, directory, table: pa.Table):
        os.makedirs(directory, exist_ok=True)
        kwargs = {"compression": self.compression}
        if self.compression_level is not None:
            kwargs["compression_level"] = self.compression_level

        name = _table_name(table)
        delta = [c for c in sch.delta_columns(name)
                 if c in table.schema.names]
        if delta:
            # Dense ascending ids delta-encode to almost nothing; dictionary
            # encoding them costs several times the data they identify.
            kwargs["column_encoding"] = {c: "DELTA_BINARY_PACKED"
                                         for c in delta}
            kwargs["use_dictionary"] = [c for c in sch.dictionary_columns(name)
                                        if c in table.schema.names]
        else:
            kwargs["use_dictionary"] = True

        pq.write_table(table, os.path.join(directory, "data.parquet"),
                       **kwargs)

    def _manifest_path(self):
        return os.path.join(self.path, sch.MANIFEST_NAME)

    def _read_manifest(self) -> dict:
        try:
            with open(self._manifest_path()) as fp:
                return json.load(fp)
        except (OSError, ValueError):
            return {}

    def _write_manifest(self, manifest: dict):
        with open(self._manifest_path(), "w") as fp:
            json.dump(manifest, fp, indent=2, sort_keys=True)

    # -- definition consistency -------------------------------------------

    def _check_definitions(self, defs):
        """Assert the incoming definitions match what the dataset already has.

        Compared on content, not on file bytes: the stored file was written
        with whatever codec was in force at the time, and a codec change is
        not schema drift.
        """
        for name, table in defs.items():
            path = os.path.join(self._def_path(name), "data.parquet")
            if not os.path.exists(path):
                if table.num_rows == 0:
                    # A table added in a later schema version: the dataset
                    # predates it and this run has nothing to put in it, which
                    # is agreement, not drift.  A *non-empty* table still
                    # fails -- that run does carry structure the dataset
                    # cannot hold.
                    continue
                raise DefinitionMismatch(
                    "dataset %s has runs but no %s definition table"
                    % (self.path, name))
            stored = pq.read_table(path)
            if stored.num_rows != table.num_rows:
                raise DefinitionMismatch(
                    "%s: run defines %d rows, dataset has %d -- bin "
                    "definitions must agree across runs in one dataset"
                    % (name, table.num_rows, stored.num_rows))
            if stored.schema.names != table.schema.names:
                raise DefinitionMismatch(
                    "%s: column set differs from the stored definition table"
                    % name)

            # Arrow's comparison runs in C++.  Materializing both tables as
            # Python lists to compare them would make every upload O(bins) in
            # the interpreter -- on a 1.2M-bin design that dominated the cost
            # of appending a run.
            if stored.equals(table, check_metadata=False):
                continue

            # Only on mismatch: find the offending column for the message.
            for col in stored.schema.names:
                if not stored.column(col).equals(table.column(col)):
                    raise DefinitionMismatch(
                        "%s.%s differs from the stored definition table -- "
                        "tool-version schema drift?" % (name, col))
            raise DefinitionMismatch(
                "%s differs from the stored definition table (column types "
                "or chunking)" % name)


# --------------------------------------------------------------------------
# The DFS walk
# --------------------------------------------------------------------------

class _Walk:
    """One pass over a UCIS database, accumulating columnar batches.

    A single DFS assigns ``dfs_ordinal`` (canonical ``ucis_ScopeIterate``
    order), resolves DU-local file ids, derives identities, and harvests
    properties -- so a conversion costs one traversal, not one per table.
    """

    def __init__(self, db, run_id):
        self.db = db
        self.run_id = str(run_id)
        self.uids = UidAllocator()
        self.coveritem_ids = CoveritemIdAllocator()
        self.props = _PropSink()
        self.hist_props = _PropSink()

        # scopes
        self.s_uid, self.s_parent, self.s_ord = [], [], []
        self.s_type, self.s_flags, self.s_name = [], [], []
        self.s_du, self.s_line, self.s_col = [], [], []
        self.s_file, self.s_srcpath = [], []
        self.s_weight, self.s_goal, self.s_source = [], [], []
        self.s_inst_du, self.s_promoted = [], []

        # coveritems + counts
        self.c_id, self.c_scope, self.c_local = [], [], []
        self.c_type, self.c_flags, self.c_name = [], [], []
        self.c_at_least, self.c_weight, self.c_goal = [], [], []
        self.c_limit, self.c_bitlen = [], []
        self.c_line, self.c_col = [], []
        self.c_file, self.c_srcpath = [], []
        self.n_id, self.n_count = [], []

        # cross operands: (cross_uid, index, coverpoint object or None, name)
        self.x_cross, self.x_index, self.x_point, self.x_name = [], [], [], []

        # source files, keyed (du_id, path) -> DU-local id
        self._file_ids = {}
        self.f_du, self.f_id, self.f_path = [], [], []

        # history
        self.h_rows = []
        self._hist_ids = {}
        self._hist_promoted = {}

        # test<->cover associations
        self.a_test, self.a_item, self.a_count = [], [], []

        # formal results
        self.fm_id, self.fm_status, self.fm_radius, self.fm_witness = \
            [], [], [], []

        self._scope_by_obj = {}
        self._ordinal = 0

    # -- driver -----------------------------------------------------------

    def run(self):
        for top in self.db.scopes(ScopeTypeT.ALL):
            self._visit(top, None)
        self._resolve_instance_dus()
        self._resolve_cross_operands()
        self._assign_design_units()
        self._assign_file_ids()
        self._collect_history()
        self._collect_assoc()
        self._collect_formal()

    def _resolve_instance_dus(self):
        """Turn held DU object references into unique-ids.

        Deferred to a second pass because a design unit can appear later in
        DFS order than an instance of it (the same forward reference NCDB
        fixes up on read).
        """
        self.s_inst_du = [
            self._scope_by_obj.get(id(du)) if du is not None else None
            for du in self.s_inst_du
        ]

    def _resolve_cross_operands(self):
        """Turn held coverpoint references into unique-ids.

        Same second pass as the instance→DU link, and for the same reason: a
        cross may name a coverpoint the walk has not reached yet.  UCIS does
        not require the operands to be siblings of the cross, so "coverpoints
        come first within a covergroup" is a convention, not a guarantee.
        """
        self.x_point = [
            self._scope_by_obj.get(id(point)) if point is not None else None
            for point in self.x_point
        ]

    def _assign_design_units(self):
        """Attribute every scope to an owning design unit.

        A DU owns itself; an *instance* is attributed to the DU it
        instantiates, not to whatever it happens to sit under -- otherwise a
        whole instance tree lands in no DU at all and per-DU rollups come back
        empty.  Everything else inherits from its parent.

        Runs after :meth:`_resolve_instance_dus` because the instance→DU link
        can be a forward reference, and before :meth:`_assign_file_ids`
        because file ids are DU-local (ADR 0001).
        """
        du_by_uid = {}
        for i, uid in enumerate(self.s_uid):
            stype = self.s_type[i]
            if ScopeTypeT.DU_ANY(ScopeTypeT(stype)):
                du_id = uid
            elif self.s_inst_du[i]:
                du_id = self.s_inst_du[i]
            else:
                # Parent precedes child in DFS order, so this is resolved.
                du_id = du_by_uid.get(self.s_parent[i], "")
            du_by_uid[uid] = du_id
            self.s_du.append(du_id or None)
        self._du_by_uid = du_by_uid

    def _assign_file_ids(self):
        """Allocate DU-local file ids, scopes first then cover bins.

        Deterministic order, so two writes of the same database produce
        byte-identical definition tables -- which is what lets a second run be
        appended without rewriting them.
        """
        for i, path in enumerate(self.s_srcpath):
            self.s_file.append(
                None if path is None
                else self._file_id(self.s_du[i] or "", path))
        for i, path in enumerate(self.c_srcpath):
            self.c_file.append(
                None if path is None
                else self._file_id(
                    self._du_by_uid.get(self.c_scope[i]) or "", path))

    def _visit(self, scope, parent_uid):
        uid = self.uids.allocate(scope, parent_uid)
        self._scope_by_obj[id(scope)] = uid

        self.s_uid.append(uid)
        self.s_parent.append(parent_uid)
        self.s_ord.append(self._ordinal)
        self._ordinal += 1
        self.s_type.append(int(scope.getScopeType()))
        self.s_flags.append(_scope_flags(scope))
        self.s_name.append(scope.getScopeName())
        self.s_weight.append(_int_or_none(_safe(scope.getWeight)))
        self.s_goal.append(_int_or_none(_safe(scope.getGoal)))
        # Through the property API, not `m_source`: reaching past the API is
        # what let the toggle accessors diverge, and a non-Mem source has no
        # such attribute at all.  This is promoted bit 4.
        self.s_source.append(_int_or_none(_safe(
            getattr(scope, "getIntProperty", None),
            -1, IntProperty.SCOPE_SOURCE_TYPE)))

        path, line, col = self._srcinfo(_safe(scope.getSourceInfo))
        self.s_srcpath.append(path)
        self.s_line.append(line)
        self.s_col.append(col)

        # Resolved after the walk: a DU may be serialized *after* the instance
        # that references it, so the uid is not known yet.
        self.s_inst_du.append(_safe(getattr(scope, "getInstanceDu", None)))

        self.s_promoted.append(_harvest_properties(
            scope, uid, sch.ObjectKind.SCOPE, self.props,
            uid_of=lambda s: self._scope_by_obj.get(id(s))))

        self._visit_cross_operands(scope, uid)
        self._visit_coveritems(scope, uid)

        for child in scope.scopes(ScopeTypeT.ALL):
            self._visit(child, uid)

    def _visit_cross_operands(self, scope, scope_uid):
        """Record the operand list of a cross, by handle and by name.

        Both, because a source may offer either: the object API
        (``getNumCrossedCoverpoints`` / ``getIthCrossedCoverpoint``, i.e.
        ``ucis_GetIthCrossedCvp``) resolves to a scope, while a source that
        only answers ``UCIS_STR_ITH_CROSSED_CVP_NAME`` gives a name and no
        handle.  A name with no resolvable scope still tells a consumer what
        was crossed, so it is stored rather than dropped.

        Sets the promoted bits for the two properties this table derives, so a
        source that exposes its operands only through the object API still
        reads back as answering ``UCIS_INT_NUM_CROSSED_CVPS`` -- the property
        is read-only in UCIS and a derivable fact should not depend on which
        of two equivalent APIs the source happened to implement.
        """
        count = _safe(getattr(scope, "getNumCrossedCoverpoints", None))
        if count is None:
            count = _int_or_none(_safe(
                getattr(scope, "getIntProperty", None),
                -1, IntProperty.NUM_CROSSED_CVPS))
        if count is not None:
            # Including zero: "this cross crosses nothing" and "this source
            # cannot say" are different answers, and the promoted bit is the
            # only thing that keeps them apart on the way back.
            self._promote(IntProperty.NUM_CROSSED_CVPS, "int")
        if not count:
            # A source with no operand API at all may still hold the single
            # name the string property can carry; one operand is better than
            # none, and this is the pre-`cross_operands` state of the world.
            name = _str_or_none(_safe(
                getattr(scope, "getStringProperty", None),
                -1, StrProperty.ITH_CROSSED_CVP_NAME))
            if name is None:
                return
            self.x_cross.append(scope_uid)
            self.x_index.append(0)
            self.x_point.append(None)
            self.x_name.append(name)
            self._promote(StrProperty.ITH_CROSSED_CVP_NAME, "str")
            return

        for i in range(int(count)):
            point = _safe(getattr(scope, "getIthCrossedCoverpoint", None), i)
            name = None
            if point is not None:
                name = _str_or_none(_safe(point.getScopeName))
            if name is None:
                name = _str_or_none(_safe(
                    getattr(scope, "getStringProperty", None),
                    i, StrProperty.ITH_CROSSED_CVP_NAME))
            self.x_cross.append(scope_uid)
            self.x_index.append(i)
            self.x_point.append(point)      # resolved to a uid after the walk
            self.x_name.append(name)
            if name is not None:
                self._promote(StrProperty.ITH_CROSSED_CVP_NAME, "str")

    def _promote(self, prop, ptype):
        """Set the promoted bit for *prop* on the scope currently being visited.

        By lookup, not by a literal bit number: the promoted list is
        append-only and the index is its position, so hard-coding one here
        would be a second, silently drifting copy of the format.
        """
        for i, (candidate, cand_type, _col) in enumerate(
                sch.promoted_properties(sch.ObjectKind.SCOPE)):
            if candidate is prop and cand_type == ptype:
                self.s_promoted[-1] |= 1 << i
                return

    def _visit_coveritems(self, scope, scope_uid):
        for local_index, ci in enumerate(scope.coverItems(CoverTypeT.ALL)):
            cid = self.coveritem_ids.allocate(scope_uid, local_index)

            data = _safe(ci.getCoverData)
            self.c_id.append(cid)
            self.c_scope.append(scope_uid)
            self.c_local.append(local_index)
            self.c_type.append(_int_or_none(getattr(data, "type", None)) or 0)
            self.c_flags.append(_int_or_none(getattr(data, "flags", None)))
            self.c_name.append(_safe(ci.getName))
            self.c_at_least.append(_int_or_none(getattr(data, "at_least", None)))
            self.c_weight.append(_int_or_none(getattr(data, "weight", None)))
            self.c_goal.append(_int_or_none(getattr(data, "goal", None)))
            self.c_limit.append(_int_or_none(getattr(data, "limit", None)))
            self.c_bitlen.append(_int_or_none(getattr(data, "bitlen", None)))

            path, line, col = self._srcinfo(_safe(ci.getSourceInfo))
            self.c_srcpath.append(path)
            self.c_line.append(line)
            self.c_col.append(col)

            self.n_id.append(cid)
            self.n_count.append(int(getattr(data, "data", 0) or 0))

            # Cover bins are not `Obj`s in this API, so only the extension
            # namespace applies to them.
            attrs = _safe(getattr(ci, "getAttributes", None)) or {}
            for key in sorted(attrs):
                self.props.add(cid, sch.ObjectKind.COVERITEM, sch.NS_COVSIGHT,
                               sch.PROP_ATTR_PREFIX + str(key),
                               sch.PropType.STRING,
                               s=None if attrs[key] is None else str(attrs[key]))

    # -- source files ------------------------------------------------------

    def _srcinfo(self, srcinfo):
        """Extract ``(path, line, col)``; the file *id* is assigned later.

        Distinguishes "no source info" (all None) from "source info with no
        file" (a path of None but a real line), because the two rebuild into
        different objects on read.
        """
        if srcinfo is None:
            return None, None, None
        path = None
        fh = getattr(srcinfo, "file", None)
        if fh is not None:
            path = _safe(getattr(fh, "getFileName", None))
        return (str(path) if path else None,
                _int_or_none(getattr(srcinfo, "line", None)),
                _int_or_none(getattr(srcinfo, "token", None)))

    def _file_id(self, du_id, path):
        """Allocate a DU-local file id (ADR 0001: file ids are per-DU)."""
        key = (du_id, path)
        if key in self._file_ids:
            return self._file_ids[key]
        next_id = sum(1 for (d, _) in self._file_ids if d == du_id)
        self._file_ids[key] = next_id
        self.f_du.append(du_id)
        self.f_id.append(next_id)
        self.f_path.append(path)
        return next_id

    # -- history and associations ------------------------------------------

    def _collect_history(self):
        nodes = []
        for kind in HistoryNodeKind:
            try:
                nodes.extend(self.db.historyNodes(kind))
            except Exception:
                continue
        # De-duplicate while preserving first-seen order: some backends return
        # the same node under more than one kind filter.
        seen, ordered = set(), []
        for node in nodes:
            if id(node) in seen:
                continue
            seen.add(id(node))
            ordered.append(node)

        for local_index, node in enumerate(ordered):
            node_id = history_node_id(self.run_id, local_index,
                                      _safe(node.getLogicalName))
            self._hist_ids[id(node)] = (node_id, local_index)
            self.h_rows.append((node_id, local_index, node))

        for node_id, local_index, node in self.h_rows:
            self._hist_promoted[node_id] = _harvest_properties(
                node, node_id, sch.ObjectKind.HISTORY_NODE, self.hist_props)

    def _collect_assoc(self):
        """Materialize SPARSE test↔cover associations.

        ALL/NEVER tiers are not rows -- they are a per-scope
        ``covsight:assoc_tier`` property -- so nothing is emitted for them here.
        """
        per_test = getattr(self.db, "_per_test_data", None)
        if not per_test:
            return
        flat = self._flat_coveritem_ids()
        by_index = {}
        for node_id, local_index, _node in self.h_rows:
            by_index[local_index] = node_id
        for hist_idx, bins in sorted(per_test.items()):
            test_id = by_index.get(hist_idx)
            if test_id is None:
                continue
            for bin_index, count in sorted(bins.items()):
                if 0 <= bin_index < len(flat):
                    self.a_test.append(test_id)
                    self.a_item.append(flat[bin_index])
                    self.a_count.append(int(count))

    def _collect_formal(self):
        """Formal-verification results, keyed like the associations."""
        getter = getattr(self.db, "get_formal_data", None)
        if getter is None:
            return
        for bin_index, cid in enumerate(self._flat_coveritem_ids()):
            entry = _safe(getter, bin_index)
            if not entry:
                continue
            self.fm_id.append(cid)
            self.fm_status.append(_int_or_none(entry.get("status")))
            self.fm_radius.append(_int_or_none(entry.get("radius")))
            self.fm_witness.append(_str_or_none(entry.get("witness")))

    def _flat_coveritem_ids(self):
        """Cover bin ids in flat-bin-index order.

        Per-test associations and formal results are keyed by the same flat bin
        index that ``counts.bin`` uses.  That ordering comes from NCDB's
        ``dfs_util``, which differs from a naive DFS in two ways: it folds
        toggle-pair BRANCH scopes into a single slot, and it emits the two bins
        of such a pair in the canonical ``0 -> 1``, ``1 -> 0`` order rather than
        the order they were created in.

        Parquet ``local_index`` values, by contrast, are assigned in
        ``coverItems()`` order.  For a toggle pair created the other way round
        those two orders disagree, so the mapping has to go through the bin
        *name* rather than through position -- otherwise flat bin 0 resolves to
        the id of ``1 -> 0`` and every association on that pair is recorded
        against the wrong bin.
        """
        try:
            from covsight.core.ncdb.dfs_util import dfs_scope_list, scope_bin_names
            scopes = dfs_scope_list(self.db)
        except Exception:
            return list(self.c_id)

        flat = []
        for scope in scopes:
            uid = self._scope_by_obj.get(id(scope))
            if uid is None:
                continue
            local_by_name = {}
            for local_index, ci in enumerate(scope.coverItems(CoverTypeT.ALL)):
                local_by_name.setdefault(_safe(ci.getName), local_index)
            for position, name in enumerate(scope_bin_names(scope)):
                # Fall back to position when a name is missing or ambiguous,
                # which is the non-toggle case and already correct there.
                local_index = local_by_name.get(name, position)
                cid = self.coveritem_ids.lookup(uid, local_index)
                if cid is not None:
                    flat.append(cid)
        return flat

    # -- table builders ----------------------------------------------------

    def scopes_table(self) -> pa.Table:
        name = "scopes"
        schema = sch.with_metadata(sch.table_schema(name), name)
        return pa.table({
            "unique_id": self.s_uid,
            "parent_id": self.s_parent,
            "dfs_ordinal": self.s_ord,
            "scope_type": self.s_type,
            "flags": self.s_flags,
            "name": self.s_name,
            "du_id": self.s_du,
            "source_file_id": self.s_file,
            "source_line": self.s_line,
            "source_col": self.s_col,
            "weight": self.s_weight,
            "goal": self.s_goal,
            "source": self.s_source,
            "instance_du_id": self.s_inst_du,
            "promoted_props": self.s_promoted,
        }, schema=schema)

    def coveritems_table(self) -> pa.Table:
        name = "coveritems"
        schema = sch.with_metadata(sch.table_schema(name), name)
        return pa.table({
            "coveritem_id": self.c_id,
            "scope_id": self.c_scope,
            "local_index": self.c_local,
            "cover_type": self.c_type,
            "flags": self.c_flags,
            "name": self.c_name,
            "at_least": self.c_at_least,
            "weight": self.c_weight,
            "goal": self.c_goal,
            "limit": self.c_limit,
            "bitlen": self.c_bitlen,
            "source_file_id": self.c_file,
            "source_line": self.c_line,
            "source_col": self.c_col,
        }, schema=schema)

    def cross_operands_table(self) -> pa.Table:
        name = "cross_operands"
        schema = sch.with_metadata(sch.table_schema(name), name)
        return pa.table({
            "cross_id": self.x_cross,
            "operand_index": self.x_index,
            "coverpoint_id": self.x_point,
            "name": self.x_name,
        }, schema=schema)

    def counts_table(self) -> pa.Table:
        name = "counts"
        schema = sch.with_metadata(sch.table_schema(name), name)
        return pa.table({"coveritem_id": self.n_id, "count": self.n_count},
                        schema=schema)

    def properties_table(self) -> pa.Table:
        return self.props.table("properties")

    def history_props_table(self) -> pa.Table:
        return self.hist_props.table("history_props")

    def source_files_table(self) -> pa.Table:
        name = "source_files"
        schema = sch.with_metadata(sch.table_schema(name), name)
        return pa.table({"du_id": self.f_du, "file_id": self.f_id,
                         "path": self.f_path}, schema=schema)

    def assoc_table(self) -> pa.Table:
        name = "test_cover_assoc"
        schema = sch.with_metadata(sch.table_schema(name), name)
        return pa.table({"test_id": self.a_test, "coveritem_id": self.a_item,
                         "count": self.a_count}, schema=schema)

    def formal_table(self) -> pa.Table:
        name = "formal"
        schema = sch.with_metadata(sch.table_schema(name), name)
        return pa.table({"coveritem_id": self.fm_id, "status": self.fm_status,
                         "radius": self.fm_radius,
                         "witness": self.fm_witness}, schema=schema)

    def history_nodes_table(self) -> pa.Table:
        name = "history_nodes"
        schema = sch.with_metadata(sch.table_schema(name), name)
        cols = {f.name: [] for f in schema}
        for node_id, local_index, node in self.h_rows:
            parent = _safe(node.getParent)
            parent_entry = self._hist_ids.get(id(parent)) if parent else None
            parent_id = parent_entry[0] if parent_entry else None
            cols["node_id"].append(node_id)
            cols["parent_id"].append(parent_id)
            cols["kind"].append(_int_or_none(_safe(node.getKind)))
            cols["local_index"].append(local_index)
            cols["logical_name"].append(_str_or_none(_safe(node.getLogicalName)))
            cols["physical_name"].append(_str_or_none(_safe(node.getPhysicalName)))
            cols["tool_category"].append(_str_or_none(_safe(node.getToolCategory)))
            cols["ucis_version"].append(_str_or_none(_safe(node.getUCISVersion)))
            cols["vendor_id"].append(_str_or_none(_safe(node.getVendorId)))
            cols["vendor_tool"].append(_str_or_none(_safe(node.getVendorTool)))
            cols["vendor_tool_version"].append(
                _str_or_none(_safe(node.getVendorToolVersion)))
            cols["cmdline"].append(_str_or_none(_safe(node.getCmd)))
            cols["args"].append(_str_or_none(_safe(node.getArgs)))
            cols["run_cwd"].append(_str_or_none(_safe(node.getRunCwd)))
            cols["host_os"].append(None)
            cols["user_name"].append(_str_or_none(_safe(node.getUserName)))
            cols["seed"].append(_str_or_none(_safe(node.getSeed)))
            cols["time_unit"].append(_str_or_none(_safe(node.getTimeUnit)))
            cols["comment"].append(_str_or_none(_safe(node.getComment)))
            date = _safe(node.getDate)
            cols["date"].append(date if isinstance(date, str) else None)
            cols["date_int"].append(
                None if isinstance(date, str) else _int_or_none(date))
            cols["compulsory"].append(_int_or_none(_safe(node.getCompulsory)))
            cols["same_tests"].append(_int_or_none(_safe(node.getSameTests)))
            cols["sim_time"].append(_float_or_none(_safe(node.getSimTime)))
            cols["cpu_time"].append(_float_or_none(_safe(node.getCpuTime)))
            cols["cost"].append(_float_or_none(_safe(node.getCost)))
            cols["status"].append(_int_or_none(_safe(node.getTestStatus)))
            cols["promoted_props"].append(self._hist_promoted.get(node_id, 0))
        return pa.table(cols, schema=schema)

    def db_metadata(self) -> dict:
        """Database-level metadata -- kept in the manifest, not in a table."""
        meta = {
            "api_version": _str_or_none(_safe(self.db.getAPIVersion)),
            "written_by": _str_or_none(_safe(self.db.getWrittenBy)),
            "written_time": _int_or_none(_safe(self.db.getWrittenTime)),
            "path_separator": _str_or_none(_safe(self.db.getPathSeparator)),
        }
        str_props = {}
        for prop in StrProperty:
            if prop in _SKIP_STR_PROPS:
                continue
            try:
                value = self.db.getStringProperty(-1, prop)
            except Exception:
                continue
            if value is not None:
                str_props[prop.name] = str(value)
        meta["str_properties"] = str_props
        return meta


# --------------------------------------------------------------------------
# helpers
# --------------------------------------------------------------------------

def _safe(fn, *args):
    """Call *fn*, returning None if it is absent or unimplemented."""
    if fn is None:
        return None
    try:
        return fn(*args)
    except Exception:
        return None


def _table_name(table: pa.Table) -> str:
    """The logical table a batch belongs to, from its schema metadata."""
    metadata = table.schema.metadata or {}
    return (metadata.get(b"covsight.table") or b"").decode()


def _scope_flags(scope):
    """Scope flags, falling back to the stored field.

    ``Scope.getFlags`` is declared but no backend implements it, so the flags
    bitfield is only reachable through the field the constructor populates.
    Preferring the accessor keeps the writer correct once a backend does.
    """
    value = _int_or_none(_safe(getattr(scope, "getFlags", None)))
    if value is not None:
        return value
    return _int_or_none(getattr(scope, "m_flags", None))


def _int_or_none(value):
    if value is None or isinstance(value, str):
        return None
    if isinstance(value, bool):
        return int(value)
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def _float_or_none(value):
    if value is None:
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _str_or_none(value):
    if value is None:
        return None
    if isinstance(value, (list, tuple)):
        return " ".join(str(v) for v in value)
    return str(value)


def write_dataset(db, path, run_id=None, *, compression="snappy",
                  compression_level=None, replace_run=False) -> str:
    """Convenience wrapper: write *db* into the dataset at *path* as one run."""
    writer = ParquetWriter(path, compression=compression,
                           compression_level=compression_level)
    return writer.write(db, run_id=run_id, replace_run=replace_run)
