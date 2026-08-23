"""L4: read a covsight dataset with nothing but the document and SQL.

The distinguishing test of the conformance plan. Every query below was written
from `docs/ucis-parquet-mapping.md` alone and answers one of the plan's six
questions. Nothing here imports a covsight *reader* -- no `ParquetUCIS`, no
`covsight.core.parquet.query`, no schema constants. If a query cannot be written
without opening our source, that is a **documentation defect** and gets filed as
one; it is the only mechanism that measures whether a stranger could implement
against this format.

The writer is imported to produce a fixture, because the question is whether our
*output* is readable from the spec, not whether we can also generate it from the
spec. `test_no_reader_api_is_used` pins that boundary.

Defects this level has already found and closed, all now in the mapping document:

* the dataset's physical layout -- directory per table, Hive `run_id=` partition
  directories, the manifest file name. It was documented only in
  `parquet/schema.py`'s docstring, which a stranger cannot read.
* the numeric encodings of `properties.object_kind`, `properties.prop_type` and
  `history_nodes.kind`.
* the reserved `covsight` `prop_id` forms (`attr:`, `tag:`, `assoc_tier`) and
  the tier values.
* `coveritems.flags` was described as carrying "exclusion/waiver". It does not:
  `ucisCoverDataFlagsT` defines no such bit. Exclusion is a *scope* flag, and
  question 6 is answered accordingly.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

duckdb = pytest.importorskip("duckdb", reason="the spec-only reader uses DuckDB")
pytest.importorskip("pyarrow", reason="writing the fixture needs pyarrow")

from ..fixtures.feature_complete import build_feature_complete  # noqa: E402

# From "Dataset layout on disk". Definition tables are unpartitioned; measurement
# tables carry one Hive partition directory per run.
DEFINITION_TABLES = ("scopes", "coveritems", "cross_operands", "properties",
                     "source_files")
MEASUREMENT_TABLES = ("counts", "history_nodes", "history_props",
                      "test_cover_assoc", "formal")
MANIFEST = "_covsight_parquet.json"

# From "Enumerated column encodings".
OBJECT_KIND_SCOPE = 0
OBJECT_KIND_COVERITEM = 1
PROP_TYPE_STRING = 3
HISTORY_KIND_TEST = 1

# From "The UCIS property vocabulary" -- `scopes.source` is a `ucisSourceT`
# value, and UCIS 1.0 numbers that enum consecutively from UCIS_VHDL = 0, so
# UCIS_SV is 2. The corpus is SystemVerilog throughout.
SOURCE_SV = 2

# Normative in UCIS 1.0 § 8.5 / § 8.11, and repeated in the mapping document's
# "Flag bits are type-qualified" section.
UCIS_SCOPE_EXCLUDED = 0x200
UCIS_SCOPE_PRAGMA_EXCLUDED = 0x400
UCIS_EXCLUDE_PRAGMA = 0x20
UCIS_EXCLUDED = 0x1E0          # PRAGMA | FILE | INST | AUTO
TYPED_BIT_0 = 0x00010000       # first bit of the type-qualified range


@pytest.fixture(scope="module")
def dataset(tmp_path_factory):
    """Three runs written by covsight; read below with SQL only."""
    from covsight.core.parquet import ParquetWriter

    path = tmp_path_factory.mktemp("l4") / "dataset"
    writer = ParquetWriter(path)
    for i, counts in enumerate([(1, 0, 7), (2, 5, 0), (4, 1, 1)]):
        writer.write(build_feature_complete(counts=counts, tag="r%d" % i),
                     run_id="r%d" % i)
    return path


@pytest.fixture(scope="module")
def con(dataset):
    """A DuckDB connection with each table registered as a view.

    The registration itself is written from the layout section: a definition
    table is one file, a measurement table is a recursive glob with Hive
    partitioning so `run_id` materializes as a column.
    """
    connection = duckdb.connect()
    for table in DEFINITION_TABLES:
        connection.execute(
            f"CREATE VIEW {table} AS SELECT * FROM "
            f"read_parquet('{dataset}/{table}/*.parquet')"
        )
    for table in MEASUREMENT_TABLES:
        connection.execute(
            f"CREATE VIEW {table} AS SELECT * FROM "
            f"read_parquet('{dataset}/{table}/**/*.parquet', hive_partitioning = true)"
        )
    return connection


from covsight.core.conformance import ucis_feature  # noqa: E402


# -- the boundary ------------------------------------------------------


def test_no_reader_api_is_used():
    """This module may write a dataset; it may not read one through covsight.

    Asserted rather than trusted, because the value of every other test here
    depends on it and an incidental import would be invisible in review.
    """
    import ast

    # Over the import graph rather than the text: a substring scan would match
    # this test's own allowlist, and would miss `import covsight.core.parquet as
    # p` besides.
    allowed = {
        ("covsight.core.parquet", "ParquetWriter"),   # writes the fixture
        ("covsight.core.conformance", "ucis_feature"),  # the marker
    }
    tree = ast.parse(Path(__file__).read_text(encoding="utf-8"))
    imported = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.ImportFrom) and (node.module or "").startswith(
            "covsight"
        ):
            for alias in node.names:
                imported.add((node.module, alias.name))
        elif isinstance(node, ast.Import):
            for alias in node.names:
                assert not alias.name.startswith("covsight"), (
                    f"plain `import {alias.name}` bypasses this check"
                )

    extra = imported - allowed
    assert not extra, (
        f"the spec-only reader imports {sorted(extra)} from covsight. Every "
        f"query here must come from docs/ucis-parquet-mapping.md, not from our "
        f"reader -- that is the only thing this level measures."
    )


def test_the_manifest_identifies_the_format(dataset):
    """A reader must be able to recognise a dataset before opening it."""
    manifest = json.loads((dataset / MANIFEST).read_text(encoding="utf-8"))
    assert manifest["format"] == "covsight-parquet"
    # Major version only. The document says a reader MUST refuse a *major*
    # version it does not implement; asserting the full string here would make
    # every additive minor change look like a compatibility break to the one
    # test that reads the dataset as a third party would.
    assert manifest["schema_version"].split(".")[0] == "1"
    assert [r["run_id"] for r in manifest["runs"]] == ["r0", "r1", "r2"]


# -- question 1: total and hit bins for a named design unit -------------


@ucis_feature("DB.1", "S4.1", "C6.1", level="L4", surface="query")
def test_q1_total_and_hit_bins_for_a_design_unit(con):
    """Bins belong to scopes; scopes name their design unit in `du_id`, and the
    counts for one run live in that run's partition."""
    total, hit = con.execute(
        """
        SELECT count(*),
               count(*) FILTER (WHERE c.count > 0)
        FROM coveritems ci
        JOIN scopes s ON s.unique_id = ci.scope_id
        JOIN counts c ON c.coveritem_id = ci.coveritem_id
        WHERE s.du_id = (SELECT unique_id FROM scopes WHERE name = ?)
          AND c.run_id = ?
        """,
        ["work.dut", "r0"],
    ).fetchone()

    all_bins = con.execute("SELECT count(*) FROM coveritems").fetchone()[0]
    assert total == all_bins, (
        "every bin in this corpus belongs to the one design unit, so the du_id "
        "join must reach all of them; a join that silently dropped rows would "
        "under-report coverage rather than fail"
    )
    assert 0 < hit < total, "the corpus is meant to have both hit and unhit bins"


# -- question 2: the full scope path of a bin ---------------------------


@ucis_feature("S4.15", "IT.1", level="L4", surface="query")
def test_q2_full_scope_path_of_a_bin(con):
    """Walk `parent_id` up the scope tree.

    Recursion is the only way: the document deliberately stores the *local*
    name in `scopes.name`, and a hierarchical name is not a column.
    """
    path = con.execute(
        """
        WITH RECURSIVE up(unique_id, parent_id, name, depth) AS (
            SELECT s.unique_id, s.parent_id, s.name, 0
            FROM coveritems ci
            JOIN scopes s ON s.unique_id = ci.scope_id
            WHERE ci.name = ?
          UNION ALL
            SELECT p.unique_id, p.parent_id, p.name, up.depth + 1
            FROM up JOIN scopes p ON p.unique_id = up.parent_id
        )
        SELECT string_agg(name, '/' ORDER BY depth DESC) FROM up
        """,
        ["IDLE"],
    ).fetchone()[0]

    assert path == "top/ctrl_fsm/states"


# -- question 3: per-run counts and a merged subset ---------------------


@ucis_feature("X.14", "DB.1", "H11.1", level="L4", surface="merge")
def test_q3_per_run_and_merged_counts(con):
    """The merge is a `GROUP BY ... SUM` over selected partitions -- exactly what
    the document says it is, with no covsight code in the loop."""
    per_run = dict(con.execute(
        """
        SELECT c.run_id, sum(c.count)
        FROM counts c
        JOIN coveritems ci ON ci.coveritem_id = c.coveritem_id
        JOIN scopes s ON s.unique_id = ci.scope_id
        WHERE s.name = ? GROUP BY c.run_id ORDER BY c.run_id
        """,
        ["addr"],
    ).fetchall())

    # The corpus varies the three covergroup bins per run; the rest are constant.
    assert per_run["r0"] != per_run["r1"] != per_run["r2"]

    merged = con.execute(
        """
        SELECT sum(c.count)
        FROM counts c
        JOIN coveritems ci ON ci.coveritem_id = c.coveritem_id
        JOIN scopes s ON s.unique_id = ci.scope_id
        WHERE s.name = ? AND c.run_id IN ('r0', 'r1')
        """,
        ["addr"],
    ).fetchone()[0]

    assert merged == per_run["r0"] + per_run["r1"], (
        "merging a run subset is a sum over its partitions"
    )


# -- question 4: which tests contributed to a bin -----------------------


@ucis_feature("HL.6", "HL.7", "X.7", level="L4", surface="query")
def test_q4_contributing_tests_including_the_tier(con):
    """`test_cover_assoc` only holds the SPARSE tier.

    The document is explicit that ALL and NEVER are a `covsight:assoc_tier`
    property on the scope, so a query that reads only the assoc table reports
    "no tests contributed" for an ALL-tier scope -- the opposite of the truth.
    This checks both halves.
    """
    rows = con.execute(
        """
        SELECT a.run_id, h.logical_name AS test_name, a.count
        FROM test_cover_assoc a
        JOIN history_nodes h
          ON h.node_id = a.test_id AND h.run_id = a.run_id
        JOIN coveritems ci ON ci.coveritem_id = a.coveritem_id
        WHERE h.kind = ?
        ORDER BY a.run_id
        """,
        [HISTORY_KIND_TEST],
    ).fetchall()
    assert rows, "the corpus records sparse associations"
    assert all(r[1] for r in rows), "each association names a test"

    # The tier is a property row, not a schema fork.
    tiers = con.execute(
        """
        SELECT DISTINCT i64 FROM properties
        WHERE namespace = 'covsight' AND prop_id = 'assoc_tier'
        """
    ).fetchall()
    assert all(t[0] in (0, 1, 2) for t in tiers), (
        f"assoc_tier must be one of NEVER/ALL/SPARSE, got {tiers}"
    )


# -- question 5: a standard property and a UCIS+ property ---------------


@ucis_feature("DB.2", "DB.6", "A9.1", "X.9", "X.11", level="L4", surface="query")
def test_q5_standard_and_extension_properties_on_a_scope(con):
    """One EAV table serves both, discriminated by `namespace`.

    A reader that filters to `namespace = 'ucis'` still sees a complete standard
    property space, and a reader that does not recognise `covsight` must
    preserve those rows rather than drop them -- which is what makes the format
    extensible without a fork.
    """
    du_id = con.execute(
        "SELECT unique_id FROM scopes WHERE name = 'work.dut'"
    ).fetchone()[0]

    standard = dict(con.execute(
        """
        SELECT prop_id, i64 FROM properties
        WHERE object_id = ? AND object_kind = ? AND namespace = 'ucis'
        """,
        [du_id, OBJECT_KIND_SCOPE],
    ).fetchall())
    # SCOPE_IS_UNDER_DU is an ordinary EAV row. SCOPE_NAME and
    # SCOPE_SOURCE_TYPE deliberately are not: they are promoted to
    # `scopes.name` and `scopes.source`, and a promoted property is a column
    # *instead of* a row, never both. Looking for either here is the mistake
    # the document's promoted-property table exists to prevent.
    assert "SCOPE_IS_UNDER_DU" in standard
    assert "SCOPE_NAME" not in standard
    assert "SCOPE_SOURCE_TYPE" not in standard
    name, source, promoted = con.execute(
        "SELECT name, source, promoted_props FROM scopes WHERE unique_id = ?",
        [du_id],
    ).fetchone()
    assert name == "work.dut"
    # Bit 4 of promoted_props is SCOPE_SOURCE_TYPE; without it, `source = 0`
    # and "this backend cannot answer SCOPE_SOURCE_TYPE" would read alike.
    assert promoted & (1 << 4)
    assert source == SOURCE_SV

    extension = dict(con.execute(
        """
        SELECT prop_id, str FROM properties
        WHERE object_id = ? AND object_kind = ? AND namespace = 'covsight'
        """,
        [du_id, OBJECT_KIND_SCOPE],
    ).fetchall())
    assert extension["attr:origin"] == "handwritten"
    assert "tag:rtl" in extension, "a tag is a key with a constant value"

    # object_kind is what lets the same table carry bin properties.
    bin_attrs = con.execute(
        """
        SELECT count(*) FROM properties
        WHERE object_kind = ? AND namespace = 'covsight'
          AND prop_id LIKE 'attr:%'
        """,
        [OBJECT_KIND_COVERITEM],
    ).fetchone()[0]
    assert bin_attrs > 0, "the corpus puts attributes on cover bins too"


# -- question 6: which bins are excluded --------------------------------


@ucis_feature("SF.10", "SF.11", "C6.6", "X.1", level="L4", surface="query")
def test_q6_excluded_bins(con):
    """Exclusion is carried at both granularities, and both are queryable.

    A bin is excluded if it carries any `UCIS_EXCLUDED` bit itself, or if its
    owning scope is excluded. Both halves matter: a consumer that checks only
    the scope misses a pragma on one bin, and one that checks only the bin
    misses a whole excluded module.
    """
    per_bin = con.execute(
        """
        SELECT ci.name FROM coveritems ci
        WHERE (ci.flags & ?) != 0
        """,
        [UCIS_EXCLUDED],
    ).fetchall()
    assert [r[0] for r in per_bin] == ["waived"], (
        "the corpus excludes exactly one bin, by pragma"
    )

    # The reason, not just the fact -- four separate bits exist so a report can
    # tell a user exclusion from a tool-generated one.
    reason = con.execute(
        "SELECT (flags & ?) != 0 FROM coveritems WHERE name = 'waived'",
        [UCIS_EXCLUDE_PRAGMA],
    ).fetchone()[0]
    assert reason is True

    # Scope-level exclusion is the other half of the same question.
    scope_excluded = con.execute(
        """
        SELECT count(*)
        FROM coveritems ci
        JOIN scopes s ON s.unique_id = ci.scope_id
        WHERE (s.flags & ?) != 0 OR (s.flags & ?) != 0
        """,
        [UCIS_SCOPE_EXCLUDED, UCIS_SCOPE_PRAGMA_EXCLUDED],
    ).fetchone()[0]
    assert scope_excluded == 0, "the corpus excludes no whole scope"


@ucis_feature("C6.6", "FS.7", "BR.3", "CG.12", "CG.13", level="L4",
              surface="query")
def test_the_same_flag_bit_means_different_things_by_type(con):
    """The type-qualified range, asserted rather than described.

    Bit `0x00010000` is `IS_FSM_RESET` on an FSM bin, `IS_BR_ELSE` on a branch
    bin and `BIN_IFF_EXISTS` on a covergroup bin. A reader that resolves those
    bits without joining to `cover_type` is not slightly lossy -- it reports
    three unrelated facts as the same one.
    """
    rows = dict(con.execute(
        """
        SELECT name, cover_type FROM coveritems
        WHERE (flags & ?) != 0 AND name IN ('IDLE', 'else', 'guarded')
        """,
        [TYPED_BIT_0],
    ).fetchall())

    assert set(rows) == {"IDLE", "else", "guarded"}, (
        f"expected three bins sharing bit {TYPED_BIT_0:#x}, got {sorted(rows)}"
    )
    # Same bit, three different cover types -- which is the whole point.
    assert len(set(rows.values())) == 3


# -- a whole-format sanity check ---------------------------------------


def test_every_documented_table_exists_and_is_readable(con):
    """The layout section lists nine tables. All nine must be there.

    A table documented but not written is as much a defect as the reverse: a
    reader written from the document would query it and fail.
    """
    for table in DEFINITION_TABLES + MEASUREMENT_TABLES:
        count = con.execute(f"SELECT count(*) FROM {table}").fetchone()[0]
        assert count > 0, f"{table} is documented but empty"


def test_definition_tables_carry_no_run_id(con):
    """The split the document rests on: definitions are run-independent.

    If a definition table were partitioned by run, appending a run would rewrite
    it, and "append" would secretly be a rewrite.
    """
    for table in DEFINITION_TABLES:
        columns = {row[0] for row in
                   con.execute(f"DESCRIBE {table}").fetchall()}
        assert "run_id" not in columns, f"{table} is partitioned by run"


# -- the handle properties, resolved with SQL alone ---------------------


@ucis_feature("PH.1", "PH.2", "PH.3", "PH.4", "PH.5", level="L4",
              surface="query")
def test_handle_properties_resolve_through_foreign_keys(con):
    """UCIS handle properties are navigation, and the mapping answers them with
    joins rather than with `properties` rows.

    Written from "The UCIS property vocabulary", which is the only place that
    says so: a reader looking for `SCOPE_PARENT` in the EAV table finds nothing
    and would conclude the relation is not carried, when it is a column.
    """
    parent, top = con.execute(
        """
        WITH RECURSIVE up(unique_id, parent_id, depth) AS (
            SELECT unique_id, parent_id, 0 FROM scopes WHERE name = 'addr_cg'
            UNION ALL
            SELECT s.unique_id, s.parent_id, up.depth + 1
            FROM scopes s JOIN up ON s.unique_id = up.parent_id
        )
        SELECT
            (SELECT unique_id FROM up WHERE depth = 1),          -- SCOPE_PARENT
            (SELECT unique_id FROM up WHERE parent_id IS NULL)   -- SCOPE_TOP
        """
    ).fetchone()
    assert parent is not None and top is not None
    assert con.execute(
        "SELECT name FROM scopes WHERE unique_id = ?", [parent]
    ).fetchone()[0] == "top"
    assert con.execute(
        "SELECT parent_id FROM scopes WHERE unique_id = ?", [top]
    ).fetchone()[0] is None

    # INSTANCE_DU: the instance's design unit, by unique_id (not by name).
    du_name = con.execute(
        """
        SELECT du.name FROM scopes i
        JOIN scopes du ON du.unique_id = i.instance_du_id
        WHERE i.name = 'top'
        """
    ).fetchone()[0]
    assert du_name == "work.dut"

    # HIST_NODE_PARENT / HIST_NODE_ROOT: the same shape one table over.
    roots = con.execute(
        "SELECT count(*) FROM history_nodes WHERE parent_id IS NULL"
    ).fetchone()[0]
    assert roots > 0, (
        "every history node claims a parent, so no root is reachable and the "
        "tree cannot be walked"
    )
    assert con.execute(
        """
        SELECT count(*) FROM history_nodes h
        WHERE h.parent_id IS NOT NULL
          AND NOT EXISTS (SELECT 1 FROM history_nodes p
                          WHERE p.node_id = h.parent_id)
        """
    ).fetchone()[0] == 0, "a history node points at a parent that is not there"


# -- the crossed-coverpoint list ----------------------------------------


@ucis_feature("CG.11", "S4.19", "PI.7", "PS.12", level="L4", surface="query")
def test_a_cross_names_its_operands_in_order(con):
    """`ucis_GetIthCrossedCvp` and `UCIS_INT_NUM_CROSSED_CVPS`, in SQL.

    Written from "Crossed coverpoints" in the mapping document. The ordering is
    the part worth proving from outside: a cross of (addr, data) and a cross of
    (data, addr) have the same operand *set* and different bin semantics, so a
    reader that recovers the set without the order has not recovered the cross.
    """
    rows = con.execute(
        """
        SELECT x.operand_index, x.name, cp.name
        FROM cross_operands x
        JOIN scopes c  ON c.unique_id = x.cross_id
        LEFT JOIN scopes cp ON cp.unique_id = x.coverpoint_id
        WHERE c.name = 'addr_x_data'
        ORDER BY x.operand_index
        """
    ).fetchall()

    # UCIS_INT_NUM_CROSSED_CVPS is a count of these rows, not a stored number:
    # a cross with a stored count that disagreed with its operand rows would be
    # unresolvable, so the mapping does not offer the chance.
    assert len(rows) == 2
    assert [r[0] for r in rows] == [0, 1], "operand_index must be dense from 0"
    assert [r[1] for r in rows] == ["addr", "data"]
    # The handle resolves to a real coverpoint scope, and it is the one the
    # name claims -- the check that separates a carried reference from a label.
    assert [r[2] for r in rows] == ["addr", "data"]

    assert con.execute(
        """
        SELECT count(*) FROM cross_operands x
        WHERE x.coverpoint_id IS NOT NULL
          AND NOT EXISTS (SELECT 1 FROM scopes s
                          WHERE s.unique_id = x.coverpoint_id)
        """
    ).fetchone()[0] == 0, "a cross operand points at a scope that is not there"
