"""Multi-run NCDB: one schema, many count arrays.

A single-run `.cdb` repeats the entire schema -- scope tree, string table,
sources -- which is **63% of the file** on a `:hello` workload and still
~48% when counts are dense. Storing N runs as N files therefore pays for the
schema N times. This module stores them in one archive instead:

    scope_tree.bin, strings.bin, ...   the schema, written once
    counts.bin                         the squashed baseline
    counts/run-<id>.bin                one dense count array per live run
    runs.json                          the run table

Measured saving against N separate files: **2.5x on sparse counts, 1.7x-1.9x
on count-heavy data** (see ``docs/ncdb-multirun-scoping.md``). The band is
narrow because ``counts.bin`` saturates near half the file whatever the count
distribution.

Two properties make this cheap:

* An all-zero dense count array costs ~1.2 KB deflated for 1.24M bins, so a
  run that touched few bins costs almost nothing and there is no need for a
  second, sparse encoding.
* ``runs.json`` is additive. An archive without it is a single-run database,
  exactly as before, so every existing `.cdb` keeps working unchanged.

The baseline appears in the run table under :data:`BASELINE_RUN_ID`, so run
selection is uniform: there is no special case for "the squashed part".
"""

import json
import os
import re
import shutil
import tempfile
import zipfile

from .constants import MEMBER_COUNTS, MEMBER_MANIFEST
from .counts import CountsReader, CountsWriter
from .merge_ops import MEMBER_MERGE_OPS, MergeOpsReader, merge_counts

#: Run table member.  Absent ⇒ the archive holds a single run.
MEMBER_RUNS = "runs.json"

#: Directory holding per-run count arrays.
RUN_COUNTS_DIR = "counts/"

#: Run id of the squashed baseline held in ``counts.bin``.
BASELINE_RUN_ID = "squashed"

_RUNS_VERSION = 1
_MEMBER_RE = re.compile(r"^%srun-(\d+)\.bin$" % re.escape(RUN_COUNTS_DIR))


class RunTableError(Exception):
    """The run table is missing, malformed, or names an unknown run."""


def run_member_name(index: int) -> str:
    """Archive member for the *index*-th run's counts.

    Deliberately derived from an index, not from the run id. Run ids arrive
    from test names, seeds and CI job ids: sanitizing one into a filename
    invites both path traversal (``../../etc``) and, worse, *collisions* --
    ``a.b`` and ``a_b`` would sanitize to the same member and one run would
    silently overwrite the other. The run table maps id to member, so the
    member name carries no meaning and needs none.
    """
    return "%srun-%05d.bin" % (RUN_COUNTS_DIR, int(index))


def _next_member_index(table: "RunTable", taken=()) -> int:
    """Lowest run-member index not already used."""
    used = set(taken)
    for entry in table.entries:
        match = _MEMBER_RE.match(entry.get("member", ""))
        if match:
            used.add(int(match.group(1)))
    index = 0
    while index in used:
        index += 1
    return index


class RunTable:
    """The set of runs in an archive, in insertion order.

    Order is part of the contract: it drives history iteration, so a run that
    is replaced keeps its position rather than jumping to the end.
    """

    def __init__(self, entries=None):
        self.entries = list(entries or [])

    # -- construction ------------------------------------------------------

    @classmethod
    def deserialize(cls, data: bytes) -> "RunTable":
        if not data:
            return cls()
        try:
            payload = json.loads(data.decode())
        except (ValueError, UnicodeDecodeError) as exc:
            raise RunTableError("runs.json is not valid JSON") from exc
        if payload.get("version") != _RUNS_VERSION:
            raise RunTableError("unsupported runs.json version %r"
                                % payload.get("version"))
        return cls(payload.get("runs", []))

    def serialize(self) -> bytes:
        return json.dumps({"version": _RUNS_VERSION, "runs": self.entries},
                          separators=(',', ':')).encode()

    # -- queries -----------------------------------------------------------

    @property
    def run_ids(self):
        return tuple(entry["run_id"] for entry in self.entries)

    def get(self, run_id):
        for entry in self.entries:
            if entry["run_id"] == run_id:
                return entry
        return None

    def __contains__(self, run_id):
        return self.get(run_id) is not None

    def __len__(self):
        return len(self.entries)

    def __repr__(self):
        return "RunTable(%r)" % (list(self.run_ids),)

    # -- mutation ----------------------------------------------------------

    def upsert(self, run_id, member, **fields):
        """Add *run_id*, or replace it in place if already present."""
        entry = {"run_id": run_id, "member": member}
        entry.update(fields)
        for i, existing in enumerate(self.entries):
            if existing["run_id"] == run_id:
                self.entries[i] = entry
                return entry
        self.entries.append(entry)
        return entry

    def remove(self, run_id):
        """Drop *run_id*; returns its entry, or None if it was not there."""
        for i, entry in enumerate(self.entries):
            if entry["run_id"] == run_id:
                del self.entries[i]
                return entry
        return None


# --------------------------------------------------------------------------
# Reading
# --------------------------------------------------------------------------

def read_run_table(path_or_zip) -> RunTable:
    """The run table of an archive.

    A single-run archive has no ``runs.json``; one is synthesized naming the
    baseline, so callers never need to branch on "is this multi-run".
    """
    def _from(zf):
        names = zf.namelist()
        if MEMBER_RUNS in names:
            return RunTable.deserialize(zf.read(MEMBER_RUNS))
        table = RunTable()
        if MEMBER_COUNTS in names:
            table.upsert(BASELINE_RUN_ID, MEMBER_COUNTS)
        return table

    if isinstance(path_or_zip, zipfile.ZipFile):
        return _from(path_or_zip)
    with zipfile.ZipFile(str(path_or_zip), "r") as zf:
        return _from(zf)


def run_ids(path) -> tuple:
    """Run ids held in the archive at *path*."""
    return read_run_table(path).run_ids


def is_multirun(path) -> bool:
    """True if *path* holds more than one run."""
    return len(read_run_table(path)) > 1


def resolve_counts(zf: zipfile.ZipFile, runs=None):
    """Merge the count arrays of the selected runs.

    Args:
        zf: An open archive.
        runs: Run ids to include -- a single id, a sequence, or None for all.

    Returns:
        The merged count array.

    Raises:
        RunTableError: If a named run is not in the archive.
    """
    table = read_run_table(zf)
    if not table:
        return CountsReader().deserialize(zf.read(MEMBER_COUNTS))

    if runs is None:
        selected = list(table.run_ids)
    elif isinstance(runs, str):
        selected = [runs]
    else:
        selected = list(runs)

    missing = [r for r in selected if r not in table]
    if missing:
        raise RunTableError(
            "run(s) %s not in archive (has %s)"
            % (", ".join(map(repr, missing)),
               ", ".join(map(repr, table.run_ids)) or "none"))

    arrays = []
    names = zf.namelist()
    for run_id in selected:
        member = table.get(run_id)["member"]
        if member in names:
            arrays.append(CountsReader().deserialize(zf.read(member)))

    if not arrays:
        return []
    if len(arrays) == 1:
        return arrays[0]

    width = max(len(a) for a in arrays)
    arrays = [a if len(a) == width else list(a) + [0] * (width - len(a))
              for a in arrays]

    ops = {}
    if MEMBER_MERGE_OPS in names:
        ops = MergeOpsReader().deserialize(zf.read(MEMBER_MERGE_OPS))
    return merge_counts(arrays, ops)


# --------------------------------------------------------------------------
# Writing
# --------------------------------------------------------------------------

def append_run(path, db=None, run_id=None, *, counts=None,
               replace: bool = False, **fields) -> str:
    """Add one run's counts to the archive at *path*.

    The schema members are copied through untouched -- that is the whole point
    of the format -- so a second run costs one count array, not a second
    database.

    Args:
        path: Existing NCDB archive.
        db: A UCIS database to take counts from.  Ignored if *counts* is given.
        run_id: Identifier for this run.  Defaults to ``run-NNNN`` from the
            number of runs already present.
        counts: A count array, if it has already been computed.
        replace: Overwrite the run if it is already present.  Off by default:
            re-adding a run must not silently double-count it on read.
        **fields: Extra metadata recorded in the run table (test names,
            status, timestamps -- whatever the caller wants to keep).

    Returns:
        The run id written.

    Raises:
        FileExistsError: If *run_id* is present and *replace* is False.
        ValueError: If neither *db* nor *counts* is supplied, or the count
            array is a different length from the archive's.

    Note:
        ZIP has no in-place member replacement, so this rewrites the archive
        (copying members verbatim, without re-deriving anything). Appending N
        runs one at a time is therefore O(N²) in bytes moved; pass several runs
        to :func:`append_runs` to amortize it.
    """
    return append_runs(path, [(run_id, db, counts, fields)], replace=replace)[0]


def append_runs(path, runs, *, replace: bool = False) -> list:
    """Add several runs in one archive rewrite.

    Args:
        path: Existing NCDB archive.
        runs: Sequence of ``(run_id, db, counts, fields)`` tuples; *run_id*
            may be None to auto-name, and exactly one of *db* / *counts* must
            be supplied.
        replace: Overwrite runs that are already present.

    Returns:
        The run ids written, in order.
    """
    path = str(path)
    with zipfile.ZipFile(path, "r") as zf:
        existing_members = {name: zf.read(name) for name in zf.namelist()}
        table = read_run_table(zf)
        baseline_width = len(CountsReader().deserialize(
            existing_members.get(MEMBER_COUNTS, b"")))

    new_members = {}
    written = []
    claimed = set()
    for run_id, db, counts, fields in runs:
        if counts is None:
            if db is None:
                raise ValueError("append_run needs either db= or counts=")
            counts = _counts_of(db)
        counts = list(counts)

        if baseline_width and len(counts) != baseline_width:
            raise ValueError(
                "count array has %d bins but the archive has %d -- a run of a "
                "different design cannot share this schema"
                % (len(counts), baseline_width))

        if run_id is None:
            run_id = "run-%04d" % sum(
                1 for r in table.run_ids if r != BASELINE_RUN_ID)
        run_id = str(run_id)

        if run_id in table and not replace:
            raise FileExistsError(
                "run %r is already in %s; pass replace=True to overwrite it"
                % (run_id, path))

        existing = table.get(run_id)
        if existing is not None and existing["member"] != MEMBER_COUNTS:
            # Replacing a run reuses its member, so the archive does not grow
            # an orphan every time a run is re-uploaded.
            member = existing["member"]
        else:
            index = _next_member_index(table, claimed)
            claimed.add(index)
            member = run_member_name(index)

        new_members[member] = CountsWriter().serialize(counts)
        table.upsert(run_id, member,
                     nonzero=sum(1 for c in counts if c), **fields)
        written.append(run_id)

    _rewrite(path, existing_members, new_members, table)
    return written


def _counts_of(db) -> list:
    """The flat count array of *db*, in the canonical bin order."""
    from covsight.core.api import CoverTypeT
    from .dfs_util import dfs_scope_list

    counts = []
    for scope in dfs_scope_list(db):
        for item in scope.coverItems(CoverTypeT.ALL):
            try:
                counts.append(int(item.getCoverData().data or 0))
            except Exception:
                counts.append(0)
    return counts


def drop_runs(path, run_ids_to_drop) -> tuple:
    """Remove runs and their count arrays from the archive.

    Returns:
        The run ids remaining.

    Raises:
        RunTableError: If dropping every run would leave nothing readable.
    """
    path = str(path)
    to_drop = {str(r) for r in run_ids_to_drop}

    with zipfile.ZipFile(path, "r") as zf:
        members = {name: zf.read(name) for name in zf.namelist()}
        table = read_run_table(zf)

    remaining = [e for e in table.entries if e["run_id"] not in to_drop]
    if not remaining:
        raise RunTableError(
            "dropping %s would leave the archive with no runs"
            % ", ".join(sorted(to_drop)))

    for entry in table.entries:
        if entry["run_id"] in to_drop and entry["member"] != MEMBER_COUNTS:
            members.pop(entry["member"], None)

    table.entries = remaining
    _rewrite(path, members, {}, table)
    return table.run_ids


def _rewrite(path, existing_members, new_members, table):
    """Rewrite the archive with an updated run table, atomically.

    Written to a temporary file and moved into place: a crash mid-rewrite
    must not leave a truncated coverage database where a valid one was.
    """
    members = dict(existing_members)
    members.update(new_members)
    members[MEMBER_RUNS] = table.serialize()

    directory = os.path.dirname(os.path.abspath(path)) or "."
    handle, temp_path = tempfile.mkstemp(suffix=".cdb.tmp", dir=directory)
    os.close(handle)
    try:
        with zipfile.ZipFile(temp_path, "w",
                             compression=zipfile.ZIP_DEFLATED) as zf:
            # Manifest first, so a reader can identify the file from its head.
            if MEMBER_MANIFEST in members:
                zf.writestr(MEMBER_MANIFEST, members.pop(MEMBER_MANIFEST))
            for name in sorted(members):
                zf.writestr(name, members[name])
        shutil.move(temp_path, path)
    finally:
        if os.path.exists(temp_path):
            os.unlink(temp_path)
