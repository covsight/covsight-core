"""Unit tests for src/ucis/ncdb/testplan.py."""
from __future__ import annotations

import json

import pytest

from covsight.core.ncdb.testplan import (
    CoverageBinding,
    CovergroupEntry,
    CoverpointEntry,
    Goal,
    ImportEntry,
    RequirementLink,
    Testplan,
    Testpoint,
    get_testplan,
    iter_testpoints,
    set_testplan,
)


# ── construction helpers ──────────────────────────────────────────────────────

def _make_plan() -> Testplan:
    tp = Testplan(source_file="uart.hjson")
    tp.add_testpoint(Testpoint(name="uart_reset",   stage="V1",
                               tests=["uart_smoke", "uart_init_*"]))
    tp.add_testpoint(Testpoint(name="uart_loopback", stage="V2",
                               tests=["uart_loopback_42", "uart_loopback_99"]))
    tp.add_testpoint(Testpoint(name="uart_na",       stage="V2",
                               na=True, tests=[]))
    tp.covergroups.append(CovergroupEntry(name="cg_uart_reset",
                                          desc="Reset coverage"))
    return tp


# ── basic construction ────────────────────────────────────────────────────────

class TestTestplanConstruction:
    def test_empty_plan(self):
        plan = Testplan()
        assert plan.format_version == 1
        assert plan.testpoints == []
        assert plan.covergroups == []

    def test_add_testpoint_invalidates_index(self):
        plan = Testplan()
        plan._indexed = True          # simulate already indexed
        plan.add_testpoint(Testpoint(name="t1", stage="V1"))
        assert plan._indexed is False

    def test_stages_ordered(self):
        plan = _make_plan()
        assert plan.stages() == ["V1", "V2"]

    def test_stages_custom_sorted_last(self):
        plan = Testplan()
        plan.add_testpoint(Testpoint(name="a", stage="V3"))
        plan.add_testpoint(Testpoint(name="b", stage="V1"))
        plan.add_testpoint(Testpoint(name="c", stage="CUSTOM"))
        assert plan.stages() == ["V1", "V3", "CUSTOM"]

    def test_testpoints_for_stage(self):
        plan = _make_plan()
        v1 = plan.testpointsForStage("V1")
        assert len(v1) == 1
        assert v1[0].name == "uart_reset"


# ── lookup ────────────────────────────────────────────────────────────────────

class TestTestpointLookup:
    def test_get_testpoint_by_name(self):
        plan = _make_plan()
        tp = plan.getTestpoint("uart_reset")
        assert tp is not None
        assert tp.name == "uart_reset"

    def test_get_testpoint_unknown(self):
        plan = _make_plan()
        assert plan.getTestpoint("nonexistent") is None

    def test_testpoint_for_test_exact(self):
        plan = _make_plan()
        tp = plan.testpointForTest("uart_smoke")
        assert tp is not None
        assert tp.name == "uart_reset"

    def test_testpoint_for_test_seed_strip(self):
        plan = _make_plan()
        # "uart_smoke_12345" → strip → "uart_smoke" → exact
        tp = plan.testpointForTest("uart_smoke_12345")
        assert tp is not None
        assert tp.name == "uart_reset"

    def test_testpoint_for_test_wildcard(self):
        plan = _make_plan()
        # "uart_init_*" matches "uart_init_fast"
        tp = plan.testpointForTest("uart_init_fast")
        assert tp is not None
        assert tp.name == "uart_reset"

    def test_testpoint_for_test_no_match(self):
        plan = _make_plan()
        assert plan.testpointForTest("spi_whatever") is None

    def test_testpoint_for_test_na_testpoint(self):
        plan = _make_plan()
        # na testpoint has no tests so nothing maps to it
        tp = plan.getTestpoint("uart_na")
        assert tp is not None
        assert tp.na is True
        assert plan.testpointForTest("uart_na") is None

    def test_wildcard_does_not_match_seed_strip_candidate(self):
        # Seed-strip (strategy 2) has higher priority than wildcard (strategy 3)
        plan = Testplan()
        plan.add_testpoint(Testpoint(name="exact", stage="V1",
                                     tests=["foo_bar"]))         # exact of stripped
        plan.add_testpoint(Testpoint(name="wild",  stage="V1",
                                     tests=["foo_*"]))           # wildcard
        tp = plan.testpointForTest("foo_bar_42")  # strip→foo_bar wins
        assert tp.name == "exact"


# ── serialization round-trip ──────────────────────────────────────────────────

class TestTestplanSerialization:
    def test_to_dict_keys(self):
        plan = _make_plan()
        d = plan.to_dict()
        assert "format_version" in d
        assert "testpoints" in d
        assert "covergroups" in d

    def test_serialize_is_compact_json(self):
        plan = _make_plan()
        data = plan.serialize()
        assert isinstance(data, bytes)
        # compact separators: no space after ',' or ':'
        text = data.decode()
        assert ", " not in text
        assert ": " not in text

    def test_roundtrip_all_fields(self):
        plan = Testplan(format_version=1, source_file="x.hjson",
                        import_timestamp="2024-01-01T00:00:00+00:00")
        plan.add_testpoint(Testpoint(
            name="tp1", stage="V2", desc="desc",
            tests=["t1", "t_*"], tags=["tag1"],
            na=False, source_template="t_{x}",
            requirements=[RequirementLink(system="ALM", project="P",
                                          item_id="REQ-1", url="http://x")],
        ))
        plan.covergroups.append(CovergroupEntry(name="cg1", desc="cg desc"))
        data = plan.serialize()
        plan2 = Testplan.from_bytes(data)
        assert plan2.format_version == 1
        assert plan2.source_file == "x.hjson"
        assert plan2.import_timestamp == "2024-01-01T00:00:00+00:00"
        assert len(plan2.testpoints) == 1
        tp2 = plan2.testpoints[0]
        assert tp2.name == "tp1"
        assert tp2.stage == "V2"
        assert tp2.tests == ["t1", "t_*"]
        assert tp2.tags == ["tag1"]
        assert tp2.source_template == "t_{x}"
        assert len(tp2.requirements) == 1
        req = tp2.requirements[0]
        assert req.system == "ALM"
        assert req.item_id == "REQ-1"
        assert len(plan2.covergroups) == 1

    def test_from_dict_missing_optional_fields(self):
        d = {"testpoints": [{"name": "tp", "stage": "V1"}]}
        plan = Testplan.from_dict(d)
        assert plan.format_version == 1
        assert plan.source_file == ""
        tp = plan.testpoints[0]
        assert tp.desc == ""
        assert tp.tests == []
        assert tp.na is False

    def test_from_bytes_roundtrip(self):
        plan = _make_plan()
        plan2 = Testplan.from_bytes(plan.serialize())
        assert len(plan2.testpoints) == len(plan.testpoints)
        assert plan2.covergroups[0].name == "cg_uart_reset"

    def test_save_and_load(self, tmp_path):
        plan = _make_plan()
        path = str(tmp_path / "plan.json")
        plan.save(path)
        plan2 = Testplan.load(path)
        assert plan2.source_file == "uart.hjson"
        assert len(plan2.testpoints) == 3


# ── stamp_import_time ─────────────────────────────────────────────────────────

class TestStampImportTime:
    def test_sets_non_empty_timestamp(self):
        plan = Testplan()
        assert plan.import_timestamp == ""
        plan.stamp_import_time()
        assert plan.import_timestamp != ""
        assert "T" in plan.import_timestamp  # ISO-8601 format


# ── module-level helpers ──────────────────────────────────────────────────────

class TestModuleHelpers:
    def test_get_testplan_from_duck_typed_db(self):
        class FakeDB:
            def getTestplan(self):
                return "my_plan"
        assert get_testplan(FakeDB()) == "my_plan"

    def test_get_testplan_returns_none_without_method(self):
        assert get_testplan(object()) is None

    def test_set_testplan_duck_typed(self):
        stored = []
        class FakeDB:
            def setTestplan(self, tp):
                stored.append(tp)
        set_testplan(FakeDB(), "plan_obj")
        assert stored == ["plan_obj"]

    def test_set_testplan_raises_without_method(self):
        with pytest.raises(TypeError):
            set_testplan(object(), "plan")


# ── Goal ──────────────────────────────────────────────────────────────────────

class TestGoal:
    def test_default_fields(self):
        g = Goal()
        assert g.id == ""
        assert g.title == ""
        assert g.goals == []
        assert g.testpoints == []
        assert g.custom == {}

    def test_nested_goals(self):
        child = Goal(id="child", title="Child goal")
        parent = Goal(id="parent", title="Parent", goals=[child])
        assert len(parent.goals) == 1
        assert parent.goals[0].id == "child"

    def test_goal_serialisation_round_trip(self):
        g = Goal(
            id="functional",
            title="Functional verification",
            desc="Verify all functional paths",
            owner="alice",
            priority="high",
            status="in_progress",
            tags=["nightly"],
            custom={"jira_epic": "UART-10"},
            testpoints=[Testpoint(name="uart_reset", stage="V1")],
            goals=[Goal(id="sub", title="Sub-goal")],
        )
        plan = Testplan(goals=[g])
        d = plan.to_dict()
        plan2 = Testplan.from_dict(d)
        g2 = plan2.goals[0]
        assert g2.id == "functional"
        assert g2.owner == "alice"
        assert g2.priority == "high"
        assert g2.status == "in_progress"
        assert g2.tags == ["nightly"]
        assert g2.custom == {"jira_epic": "UART-10"}
        assert len(g2.testpoints) == 1
        assert g2.testpoints[0].name == "uart_reset"
        assert len(g2.goals) == 1
        assert g2.goals[0].id == "sub"

    def test_deeply_nested_goals_serialise(self):
        deep = Goal(id="leaf", testpoints=[Testpoint(name="tp_leaf", stage="V3")])
        mid = Goal(id="mid", goals=[deep])
        top = Goal(id="top", goals=[mid])
        plan = Testplan(goals=[top])
        d = plan.to_dict()
        plan2 = Testplan.from_dict(d)
        leaf_tp = plan2.goals[0].goals[0].goals[0].testpoints[0]
        assert leaf_tp.name == "tp_leaf"


# ── CoverageBinding ───────────────────────────────────────────────────────────

class TestCoverageBinding:
    def test_all_nine_types_accepted(self):
        for t in CoverageBinding.TYPES:
            b = CoverageBinding(type=t, path="a.b.c")
            assert b.type == t

    def test_round_trip(self):
        tp = Testpoint(
            name="tp", stage="V1",
            coverage=[
                CoverageBinding(type="covergroup",  path="env.cg",    desc="main cg"),
                CoverageBinding(type="coverpoint",  path="env.cg.cp"),
                CoverageBinding(type="toggle",      path="tb.sig"),
                CoverageBinding(type="assertion",   path="tb.chk_err"),
                CoverageBinding(type="line",        path="dut.c:42"),
            ],
        )
        plan = Testplan(testpoints=[tp])
        plan2 = Testplan.from_dict(plan.to_dict())
        bindings = plan2.testpoints[0].coverage
        assert len(bindings) == 5
        assert bindings[0].type == "covergroup"
        assert bindings[0].path == "env.cg"
        assert bindings[0].desc == "main cg"
        assert bindings[1].type == "coverpoint"
        assert bindings[2].type == "toggle"

    def test_glob_path_round_trips(self):
        b = CoverageBinding(type="covergroup", path="env.*.cg")
        plan = Testplan(testpoints=[Testpoint(name="tp", stage="V1", coverage=[b])])
        plan2 = Testplan.from_dict(plan.to_dict())
        assert plan2.testpoints[0].coverage[0].path == "env.*.cg"


# ── Plan metadata ─────────────────────────────────────────────────────────────

class TestPlanMetadata:
    def test_name_owner_description_tags(self):
        plan = Testplan(
            name="uart",
            description="UART block verification",
            owner="dv-team",
            tags=["regression", "block"],
            schema="https://schema.covsight.io/testplan/v1",
        )
        d = plan.to_dict()
        assert d["name"] == "uart"
        assert d["description"] == "UART block verification"
        assert d["owner"] == "dv-team"
        assert d["tags"] == ["regression", "block"]
        assert d["schema"] == "https://schema.covsight.io/testplan/v1"

    def test_substitutions_round_trip(self):
        plan = Testplan(substitutions={"name": "uart", "baud": ["9600", "115200"]})
        plan2 = Testplan.from_dict(plan.to_dict())
        assert plan2.substitutions["name"] == "uart"
        assert plan2.substitutions["baud"] == ["9600", "115200"]

    def test_imports_round_trip(self):
        plan = Testplan(imports=[
            ImportEntry(path="common/csr.yaml", substitutions={"name": "uart"}),
        ])
        plan2 = Testplan.from_dict(plan.to_dict())
        assert len(plan2.imports) == 1
        assert plan2.imports[0].path == "common/csr.yaml"
        assert plan2.imports[0].substitutions == {"name": "uart"}

    def test_custom_round_trip(self):
        plan = Testplan(custom={"acme": {"priority": 1, "dv_doc": "http://x"}})
        plan2 = Testplan.from_dict(plan.to_dict())
        assert plan2.custom["acme"]["priority"] == 1

    def test_backward_compat_old_dict(self):
        # Dict from old serialiser: no new fields present
        old = {
            "format_version": 1,
            "source_file": "old.hjson",
            "import_timestamp": "2024-01-01T00:00:00+00:00",
            "testpoints": [{"name": "tp", "stage": "V1"}],
            "covergroups": [{"name": "cg", "desc": ""}],
        }
        plan = Testplan.from_dict(old)
        assert plan.name == ""
        assert plan.goals == []
        assert plan.substitutions == {}
        assert len(plan.testpoints) == 1
        assert len(plan.covergroups) == 1


# ── Extended Testpoint fields ──────────────────────────────────────────────────

class TestTestpointExtendedFields:
    def test_owner_priority_weight(self):
        tp = Testpoint(name="tp", stage="V1",
                       owner="bob", priority="high", weight=3)
        plan = Testplan(testpoints=[tp])
        plan2 = Testplan.from_dict(plan.to_dict())
        tp2 = plan2.testpoints[0]
        assert tp2.owner == "bob"
        assert tp2.priority == "high"
        assert tp2.weight == 3

    def test_custom_on_testpoint(self):
        tp = Testpoint(name="tp", stage="V1",
                       custom={"acme": {"sim_time": 300}})
        plan = Testplan(testpoints=[tp])
        plan2 = Testplan.from_dict(plan.to_dict())
        assert plan2.testpoints[0].custom == {"acme": {"sim_time": 300}}

    def test_weight_defaults_to_1(self):
        tp = Testpoint(name="tp", stage="V1")
        assert tp.weight == 1

    def test_from_dict_weight_missing_defaults_to_1(self):
        d = {"testpoints": [{"name": "tp", "stage": "V1"}]}
        plan = Testplan.from_dict(d)
        assert plan.testpoints[0].weight == 1


# ── CovergroupEntry extensions ────────────────────────────────────────────────

class TestCovergroupEntryExtended:
    def test_coverpoints_round_trip(self):
        cg = CovergroupEntry(
            name="uart_cg",
            desc="UART functional coverage",
            coverpoints=[
                CoverpointEntry(name="baud_rate_cp",
                                desc="Baud rate divisor",
                                path="env.uart_cg.baud_rate_cp"),
                CoverpointEntry(name="parity_cp",
                                path="env.uart_cg.parity_cp"),
            ],
            custom={"acme": {"review": "done"}},
        )
        plan = Testplan(covergroups=[cg])
        plan2 = Testplan.from_dict(plan.to_dict())
        cg2 = plan2.covergroups[0]
        assert cg2.name == "uart_cg"
        assert len(cg2.coverpoints) == 2
        assert cg2.coverpoints[0].name == "baud_rate_cp"
        assert cg2.coverpoints[0].path == "env.uart_cg.baud_rate_cp"
        assert cg2.custom == {"acme": {"review": "done"}}

    def test_coverpoints_default_empty(self):
        cg = CovergroupEntry(name="cg")
        assert cg.coverpoints == []
        assert cg.custom == {}


# ── iter_testpoints ───────────────────────────────────────────────────────────

class TestIterTestpoints:
    def test_top_level_only(self):
        plan = _make_plan()
        names = [tp.name for tp in iter_testpoints(plan)]
        assert names == ["uart_reset", "uart_loopback", "uart_na"]

    def test_goals_only(self):
        plan = Testplan(goals=[
            Goal(testpoints=[Testpoint(name="g1_tp", stage="V1")]),
            Goal(testpoints=[Testpoint(name="g2_tp", stage="V2")]),
        ])
        names = [tp.name for tp in iter_testpoints(plan)]
        assert names == ["g1_tp", "g2_tp"]

    def test_mixed_top_level_and_goals(self):
        plan = Testplan(
            testpoints=[Testpoint(name="top", stage="V1")],
            goals=[Goal(testpoints=[Testpoint(name="nested", stage="V2")])],
        )
        names = [tp.name for tp in iter_testpoints(plan)]
        assert names == ["top", "nested"]

    def test_deeply_nested(self):
        plan = Testplan(goals=[
            Goal(goals=[
                Goal(goals=[
                    Goal(testpoints=[Testpoint(name="deep", stage="V3")]),
                ]),
            ]),
        ])
        names = [tp.name for tp in iter_testpoints(plan)]
        assert names == ["deep"]

    def test_empty_plan_yields_nothing(self):
        assert list(iter_testpoints(Testplan())) == []

    def test_stage_search_includes_goal_testpoints(self):
        plan = Testplan(
            testpoints=[Testpoint(name="top_v1", stage="V1")],
            goals=[Goal(testpoints=[Testpoint(name="nested_v2", stage="V2")])],
        )
        v2 = plan.testpointsForStage("V2")
        assert len(v2) == 1
        assert v2[0].name == "nested_v2"

    def test_index_covers_goal_testpoints(self):
        plan = Testplan(goals=[
            Goal(testpoints=[Testpoint(name="g_tp", stage="V1",
                                       tests=["g_test"])]),
        ])
        tp = plan.getTestpoint("g_tp")
        assert tp is not None
        tp2 = plan.testpointForTest("g_test")
        assert tp2 is not None and tp2.name == "g_tp"


# ── Full extended round-trip ──────────────────────────────────────────────────

class TestFullRoundTrip:
    def test_all_fields_preserved(self):
        plan = Testplan(
            name="uart",
            description="UART block verification",
            owner="dv-team",
            tags=["regression"],
            schema="https://schema.covsight.io/testplan/v1",
            substitutions={"name": "uart"},
            imports=[ImportEntry(path="common/csr.yaml",
                                 substitutions={"name": "uart"})],
            source_file="uart.yaml",
            import_timestamp="2024-06-01T00:00:00+00:00",
            custom={"acme": {"block": "uart"}},
        )
        plan.testpoints.append(Testpoint(
            name="uart_reset", stage="V1",
            coverage=[CoverageBinding(type="covergroup",
                                      path="env.uart_reset_cg")],
            requirements=[RequirementLink(system="JIRA", project="UART",
                                          item_id="REQ-1")],
            owner="alice", priority="high", weight=2,
            custom={"acme": {"sim_time": 60}},
        ))
        plan.goals.append(Goal(
            id="functional", title="Functional",
            status="planned",
            testpoints=[Testpoint(
                name="uart_baud", stage="V2",
                coverage=[CoverageBinding(type="coverpoint",
                                          path="env.cg.baud_cp")],
            )],
            goals=[Goal(id="sub", title="Sub")],
            custom={"jira": "UART-10"},
        ))
        plan.covergroups.append(CovergroupEntry(
            name="uart_cg",
            coverpoints=[CoverpointEntry(name="baud_cp",
                                         path="env.uart_cg.baud_cp")],
        ))

        data = plan.serialize()
        plan2 = Testplan.from_bytes(data)

        assert plan2.name == "uart"
        assert plan2.owner == "dv-team"
        assert plan2.custom == {"acme": {"block": "uart"}}
        assert len(plan2.imports) == 1
        assert plan2.imports[0].path == "common/csr.yaml"

        tp = plan2.testpoints[0]
        assert tp.name == "uart_reset"
        assert tp.owner == "alice"
        assert tp.weight == 2
        assert tp.coverage[0].type == "covergroup"
        assert tp.requirements[0].item_id == "REQ-1"

        g = plan2.goals[0]
        assert g.id == "functional"
        assert g.status == "planned"
        assert g.testpoints[0].name == "uart_baud"
        assert g.goals[0].id == "sub"
        assert g.custom == {"jira": "UART-10"}

        cg = plan2.covergroups[0]
        assert cg.coverpoints[0].name == "baud_cp"
