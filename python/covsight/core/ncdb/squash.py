"""Squash — bake runs into the baseline and reclaim their arrays.

The counterpart of :mod:`covsight.core.ncdb.multirun`: runs accumulate as
separate count arrays, and periodically the old ones are folded into
``counts.bin`` and dropped. Storage then tracks *retained* runs rather than
every run ever uploaded.

This is NCDB's twin of the Parquet backend's ``merge.compact()``, and the
format anticipated it long before this module existed — ``contrib_index.bin``
already carries a ``squash_watermark`` ("highest run_id already baked into
counts.bin") and ``squash_log.bin`` is already an append-only audit of squash
operations. What was missing was the operation itself.

One capability the Parquet side does not have: a **merge policy**. Squashing
with :data:`POLICY_PASS_ONLY` folds in only the runs whose tests passed, so a
failing run's coverage never silently props up a closure number.

What a squash keeps and what it costs:

* Coverage is exact — the baseline is the type-aware merge of what went in.
* The squash is recorded: which runs, which policy, when.
* Per-run count arrays for the squashed runs are gone. That is the point;
  ``keep_runs=True`` folds them in without dropping them, which saves nothing.
"""

import zipfile
from datetime import datetime, timezone

from .constants import MEMBER_COUNTS, MEMBER_SQUASH_LOG
from .contrib_index import (
    POLICY_ALL, POLICY_EXCLUDE_ERROR_RERUN, POLICY_PASS_ONLY, POLICY_STRICT,
)
from .counts import CountsReader, CountsWriter
from .merge_ops import MEMBER_MERGE_OPS, MergeOpsReader, merge_counts
from .multirun import (
    BASELINE_RUN_ID, RunTableError, _rewrite, read_run_table,
)
from .squash_log import SquashLog

__all__ = [
    "squash", "squash_plan", "read_squash_log", "SquashResult",
    "POLICY_ALL", "POLICY_PASS_ONLY", "POLICY_EXCLUDE_ERROR_RERUN",
    "POLICY_STRICT",
]


class SquashResult:
    """What a squash folded in, and what is left."""

    def __init__(self, squashed, remaining, policy, num_bins):
        self.squashed = tuple(squashed)
        self.remaining = tuple(remaining)
        self.policy = policy
        self.num_bins = num_bins

    @property
    def num_squashed(self) -> int:
        return len(self.squashed)

    def __repr__(self):
        return ("SquashResult(squashed=%d, remaining=%d, bins=%d)"
                % (self.num_squashed, len(self.remaining), self.num_bins))


def squash_plan(path, keep_recent: int = 0, runs=None):
    """Which runs a retention policy would squash, without doing it.

    A retention policy is usually "keep the last N runs individually
    queryable, fold everything older into the baseline". This reports that
    split so it can be scheduled and reviewed before anything is destroyed.

    Args:
        path: NCDB archive.
        keep_recent: Number of most-recent runs to leave un-squashed.
        runs: Restrict the plan to these run ids.

    Returns:
        ``(to_squash, to_keep)`` — run ids, in archive order. The baseline is
        never in *to_squash*: it is what everything is squashed *into*.
    """
    table = read_run_table(path)
    candidates = [r for r in table.run_ids if r != BASELINE_RUN_ID]
    if runs is not None:
        wanted = {str(r) for r in runs}
        candidates = [r for r in candidates if r in wanted]

    if keep_recent <= 0:
        return tuple(candidates), ()
    if keep_recent >= len(candidates):
        return (), tuple(candidates)
    return tuple(candidates[:-keep_recent]), tuple(candidates[-keep_recent:])


def squash(path, runs=None, *, keep_recent: int = 0,
           policy: int = POLICY_ALL, keep_runs: bool = False,
           timestamp: int = None) -> SquashResult:
    """Fold runs into the baseline and drop their arrays.

    Args:
        path: NCDB archive to squash, in place.
        runs: Run ids to fold in; None with *keep_recent* selects by
            retention, None without it selects every non-baseline run.
        keep_recent: Leave this many of the most recent runs un-squashed.
        policy: A ``POLICY_*`` constant, recorded in the squash log.
            :data:`POLICY_PASS_ONLY` and stricter variants fold in only the
            runs the run table marks as passing.
        keep_runs: Fold the runs in without dropping their arrays. Loses
            nothing and saves nothing; useful for verifying a squash before
            committing to it.
        timestamp: Unix time recorded in the log; defaults to now.

    Returns:
        A :class:`SquashResult`.

    Raises:
        RunTableError: If a named run is absent, or nothing is selected.
    """
    path = str(path)
    to_squash, _keep = squash_plan(path, keep_recent=keep_recent, runs=runs)

    with zipfile.ZipFile(path, "r") as zf:
        members = {name: zf.read(name) for name in zf.namelist()}
        table = read_run_table(zf)

    if runs is not None:
        missing = [r for r in map(str, runs) if r not in table]
        if missing:
            raise RunTableError(
                "run(s) %s not in archive (has %s)"
                % (", ".join(map(repr, missing)),
                   ", ".join(map(repr, table.run_ids))))

    to_squash = [r for r in to_squash if _policy_admits(table.get(r), policy)]
    if not to_squash:
        raise RunTableError(
            "no runs selected to squash in %s (policy %d left nothing)"
            % (path, policy))

    # The baseline is one of the inputs: squashing is folding *into* it.
    arrays = []
    baseline = members.get(MEMBER_COUNTS, b"")
    if baseline:
        arrays.append(CountsReader().deserialize(baseline))
    for run_id in to_squash:
        member = table.get(run_id)["member"]
        if member in members and member != MEMBER_COUNTS:
            arrays.append(CountsReader().deserialize(members[member]))

    ops = {}
    if MEMBER_MERGE_OPS in members:
        ops = MergeOpsReader().deserialize(members[MEMBER_MERGE_OPS])

    width = max((len(a) for a in arrays), default=0)
    arrays = [a if len(a) == width else list(a) + [0] * (width - len(a))
              for a in arrays]
    merged = merge_counts(arrays, ops) if arrays else []

    members[MEMBER_COUNTS] = CountsWriter().serialize(merged)

    if not keep_runs:
        for run_id in to_squash:
            entry = table.get(run_id)
            if entry["member"] != MEMBER_COUNTS:
                members.pop(entry["member"], None)
            table.remove(run_id)

    # The baseline must exist in the table once there is one.
    if BASELINE_RUN_ID not in table:
        table.entries.insert(0, {"run_id": BASELINE_RUN_ID,
                                 "member": MEMBER_COUNTS})

    members[MEMBER_SQUASH_LOG] = _append_log(
        members.get(MEMBER_SQUASH_LOG, b""), to_squash, policy, timestamp)

    _rewrite(path, members, {}, table)
    return SquashResult(squashed=to_squash, remaining=table.run_ids,
                        policy=policy, num_bins=len(merged))


def _policy_admits(entry, policy) -> bool:
    """Whether *entry* may be folded in under *policy*.

    A run is admitted unless the table says it failed: coverage from a failing
    test is exactly what a pass-only policy exists to keep out of the baseline.
    Runs recorded without a status are admitted -- absence of evidence is not
    a failure -- which keeps the policy usable on archives that never recorded
    one.
    """
    if policy == POLICY_ALL or entry is None:
        return True
    status = entry.get("status")
    if status is None:
        return True
    if policy in (POLICY_PASS_ONLY, POLICY_EXCLUDE_ERROR_RERUN):
        return bool(status == "pass")
    if policy == POLICY_STRICT:
        return bool(status == "pass" and not entry.get("rerun"))
    return True


def _append_log(existing: bytes, squashed, policy, timestamp) -> bytes:
    """Append one entry to the append-only squash audit trail."""
    log = SquashLog.deserialize(existing) if existing else SquashLog()
    if timestamp is None:
        timestamp = int(datetime.now(timezone.utc).timestamp())
    log.append(ts=int(timestamp), policy=int(policy),
               from_run=0, to_run=len(squashed),
               num_runs=len(squashed), pass_runs=len(squashed))
    return log.serialize()


def read_squash_log(path):
    """The squash history of an archive, oldest first."""
    with zipfile.ZipFile(str(path), "r") as zf:
        if MEMBER_SQUASH_LOG not in zf.namelist():
            return []
        return SquashLog.deserialize(zf.read(MEMBER_SQUASH_LOG)).entries()
