"""Per-test contribution queries over the association table.

Mirrors :class:`covsight.core.mem.mem_test_coverage.MemTestCoverage` -- same
dataclasses, same flat-bin-index keys -- so a report written against the
in-memory backend runs unchanged over a Parquet dataset.

Query-time merge is what makes this interesting: the association rows of every
run stay in the dataset, so "which test covered this bin" is answerable across
a whole regression, not just within one run.
"""

from covsight.core.mem.mem_test_coverage import (
    CoverItemTestInfo, TestCoverageInfo,
)


class ParquetTestCoverage:
    """Per-test contribution analysis for a Parquet-backed database."""

    def __init__(self, db):
        self._db = db
        self._flat = None
        self._by_test = None

    # -- indexes -----------------------------------------------------------

    def _flat_ids(self):
        """Cover bin ids in flat-bin-index order (``dfs_ordinal``, then local)."""
        if self._flat is None:
            dataset = self._db.dataset
            flat = []
            for scope_row in dataset.scope_rows():
                for item in dataset.coveritems_of().get(
                        scope_row["unique_id"], []):
                    flat.append(item["coveritem_id"])
            self._flat = flat
        return self._flat

    def _index(self):
        """``history_idx`` -> {flat bin index: count}."""
        if self._by_test is not None:
            return self._by_test
        position = {cid: i for i, cid in enumerate(self._flat_ids())}
        by_node = {}
        for row in self._db.dataset.assoc_rows():
            index = position.get(row["coveritem_id"])
            if index is None:
                continue
            by_node.setdefault(row["test_id"], {})[index] = row["count"] or 0

        # Flat history index, matching `historyNodes` order.
        self._by_test = {}
        self._names = {}
        for i, node_row in enumerate(self._db.dataset.history_rows()):
            node_id = node_row["node_id"]
            self._names[i] = node_row["logical_name"]
            if node_id in by_node:
                self._by_test[i] = by_node[node_id]
        return self._by_test

    def _history_name(self, history_idx):
        self._index()
        return self._names.get(history_idx)

    def _total_bins(self) -> int:
        return len(self._flat_ids())

    # -- public API --------------------------------------------------------

    def has_test_associations(self) -> bool:
        return bool(self._index())

    def get_tests_for_coveritem(self, bin_index: int) -> CoverItemTestInfo:
        tests, total_hits = [], 0
        for hist_idx, bin_counts in self._index().items():
            count = bin_counts.get(bin_index, 0)
            if count:
                name = self._history_name(hist_idx) or str(hist_idx)
                tests.append((hist_idx, name, count))
                total_hits += count
        tests.sort(key=lambda t: t[2], reverse=True)
        return CoverItemTestInfo(bin_index=bin_index, total_hits=total_hits,
                                 tests=tests)

    def get_coveritems_for_test(self, history_idx: int):
        return sorted(self._index().get(history_idx, {}).keys())

    def get_unique_coveritems(self, history_idx: int):
        index = self._index()
        mine = set(index.get(history_idx, {}))
        others = set()
        for idx, bin_counts in index.items():
            if idx != history_idx:
                others.update(bin_counts)
        return sorted(mine - others)

    def get_test_contribution(self, history_idx: int):
        name = self._history_name(history_idx)
        if name is None:
            return None
        bin_counts = self._index().get(history_idx, {})
        total_bins = self._total_bins()
        return TestCoverageInfo(
            history_idx=history_idx,
            test_name=name,
            total_items=len(bin_counts),
            unique_items=len(self.get_unique_coveritems(history_idx)),
            total_contribution=sum(bin_counts.values()),
            coverage_percent=(len(bin_counts) / total_bins * 100)
            if total_bins > 0 else 0.0,
        )

    def get_all_test_contributions(self):
        results = []
        for hist_idx in self._index():
            info = self.get_test_contribution(hist_idx)
            if info and info.total_items > 0:
                results.append(info)
        results.sort(key=lambda x: x.total_items, reverse=True)
        return results
