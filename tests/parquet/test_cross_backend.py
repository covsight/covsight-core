"""Test 4 -- cross-backend equivalence.

The claim the whole design rests on is "tools work unchanged".  That is only
credible if the same source, loaded through three different backends, answers
the object API identically -- so ``MemUCIS`` is the oracle and NCDB and Parquet
are both compared against it, in iteration order.
"""

import pytest

from covsight.core.api import (
    CoverTypeT, HistoryNodeKind, IntProperty, ScopeTypeT,
)
from covsight.core.conformance import ucis_feature
from covsight.core.parquet import ParquetUCIS, ParquetWriter

from .conftest import build_db, shape_facts


@pytest.fixture
def three_backends(tmp_path):
    """The same database as ``MemUCIS``, NCDB and Parquet."""
    from covsight.core.ncdb.ncdb_reader import NcdbReader
    from covsight.core.ncdb.ncdb_writer import NcdbWriter

    mem = build_db()

    ncdb_path = tmp_path / "cov.cdb"
    NcdbWriter().write(mem, str(ncdb_path))
    ncdb = NcdbReader().read(str(ncdb_path))

    parquet_path = tmp_path / "cov.parquet"
    ParquetWriter(parquet_path).write(mem, run_id="r0")
    parquet = ParquetUCIS(parquet_path)

    return {"mem": mem, "ncdb": ncdb, "parquet": parquet}


@ucis_feature("S4.1", "C6.1", level="L2")
def test_scope_and_bin_structure_agrees(three_backends):
    """Hierarchy, iteration order, bin names/types and counts."""
    oracle = shape_facts(three_backends["mem"])
    assert shape_facts(three_backends["ncdb"]) == oracle
    assert shape_facts(three_backends["parquet"]) == oracle


@ucis_feature("IT.1", "IT.2", level="L2")
def test_scope_iteration_order_agrees(three_backends):
    def order(db):
        out = []

        def visit(scope):
            out.append((scope.getScopeName(), int(scope.getScopeType())))
            for child in scope.scopes(ScopeTypeT.ALL):
                visit(child)
        for top in db.scopes(ScopeTypeT.ALL):
            visit(top)
        return out

    oracle = order(three_backends["mem"])
    assert order(three_backends["ncdb"]) == oracle
    assert order(three_backends["parquet"]) == oracle


@ucis_feature("IT.4", "IT.5", level="L2")
def test_cover_iteration_order_agrees(three_backends):
    def order(db):
        out = []

        def visit(scope, path):
            for i, item in enumerate(scope.coverItems(CoverTypeT.ALL)):
                out.append((path, i, item.getName()))
            for child in scope.scopes(ScopeTypeT.ALL):
                visit(child, "%s/%s" % (path, child.getScopeName()))
        for top in db.scopes(ScopeTypeT.ALL):
            visit(top, top.getScopeName())
        return out

    oracle = order(three_backends["mem"])
    assert order(three_backends["ncdb"]) == oracle
    assert order(three_backends["parquet"]) == oracle


@ucis_feature("IT.4", level="L2")
def test_type_mask_filtering_agrees(three_backends):
    """A mask must select the same bins in every backend."""
    for mask in (CoverTypeT.CVGBIN, CoverTypeT.STMTBIN, CoverTypeT.TOGGLEBIN,
                 CoverTypeT.ALL):
        counts = {name: _bin_names(db, mask)
                  for name, db in three_backends.items()}
        assert counts["ncdb"] == counts["mem"], mask
        assert counts["parquet"] == counts["mem"], mask


@ucis_feature("IT.1", level="L2")
def test_scope_mask_filtering_agrees(three_backends):
    for mask in (ScopeTypeT.INSTANCE, ScopeTypeT.COVERGROUP,
                 ScopeTypeT.DU_MODULE, ScopeTypeT.ALL):
        selected = {name: [s.getScopeName() for s in db.scopes(mask)]
                    for name, db in three_backends.items()}
        assert selected["ncdb"] == selected["mem"], mask
        assert selected["parquet"] == selected["mem"], mask


@ucis_feature("CG.9", level="L2")
def test_covergroup_properties_agree(three_backends):
    scopes = {name: _find(db, "addr_cg")
              for name, db in three_backends.items()}
    for prop in (IntProperty.CVG_ATLEAST, IntProperty.CVG_AUTOBINMAX,
                 IntProperty.CVG_PERINSTANCE):
        values = {name: _try_prop(scope, prop)
                  for name, scope in scopes.items()}
        assert values["parquet"] == values["mem"], prop.name


@ucis_feature("DB.6", "X.6", level="L2")
def test_property_support_agrees(three_backends):
    """Whether a property is *answerable* must agree, not just its value.

    A backend that invents a plausible default where the oracle raises has
    quietly changed the data model.
    """
    for scope_name in ("top", "addr_cg", "data_valid", "block1"):
        scopes = {name: _find(db, scope_name)
                  for name, db in three_backends.items()}
        for prop in IntProperty:
            support = {name: _try_prop(scope, prop) is not _UNSUPPORTED
                       for name, scope in scopes.items()}
            assert support["parquet"] == support["mem"], \
                "%s.%s: mem=%s parquet=%s" % (
                    scope_name, prop.name, support["mem"], support["parquet"])


@ucis_feature("H11.1", "H11.3", "H11.5", level="L2")
def test_history_agrees(three_backends):
    def history(db):
        return [(n.getLogicalName(), int(n.getKind()))
                for n in db.historyNodes(HistoryNodeKind.TEST)]

    oracle = history(three_backends["mem"])
    assert history(three_backends["ncdb"]) == oracle
    assert history(three_backends["parquet"]) == oracle


@ucis_feature("HL.6", "HL.7", level="L2")
def test_test_associations_agree(three_backends):
    def contributions(db):
        api = db.get_test_coverage_api()
        return [(c.test_name, c.total_items, c.total_contribution)
                for c in api.get_all_test_contributions()]

    oracle = contributions(three_backends["mem"])
    assert oracle
    assert contributions(three_backends["parquet"]) == oracle


@ucis_feature("FM.1", "FM.2", "FM.3", level="L2")
def test_formal_data_agrees(three_backends):
    for index in range(6):
        expected = three_backends["mem"].get_formal_data(index)
        assert three_backends["parquet"].get_formal_data(index) == expected


_UNSUPPORTED = object()


def _try_prop(scope, prop):
    if scope is None:
        return None
    try:
        return scope.getIntProperty(-1, prop)
    except Exception:
        return _UNSUPPORTED


def _bin_names(db, mask):
    out = []

    def visit(scope):
        out.extend(item.getName() for item in scope.coverItems(mask))
        for child in scope.scopes(ScopeTypeT.ALL):
            visit(child)
    for top in db.scopes(ScopeTypeT.ALL):
        visit(top)
    return out


def _find(db, name):
    def visit(scope):
        for child in scope.scopes(ScopeTypeT.ALL):
            if child.getScopeName() == name:
                return child
            found = visit(child)
            if found is not None:
                return found
        return None
    return visit(db)
