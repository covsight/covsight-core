"""Unit tests for ucis.ncdb.testplan_export."""

import io
import json
import os
import tempfile
from xml.etree import ElementTree as ET

import pytest

from covsight.core.ncdb.testplan import Testplan, Testpoint
from covsight.core.ncdb.testplan_closure import TPStatus, TestpointResult
from covsight.core.ncdb.testplan_export import (
    export_junit_xml,
    export_github_annotations,
    export_summary_markdown,
)
from covsight.core.ncdb.reports import report_stage_gate


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

def _make_tp(name, stage="V1", desc=""):
    return Testpoint(name=name, stage=stage, tests=[name], desc=desc)


def _make_result(tp, status, pass_count=0, fail_count=0, matched=None):
    return TestpointResult(
        testpoint=tp,
        status=status,
        matched_tests=matched or [],
        pass_count=pass_count,
        fail_count=fail_count,
    )


@pytest.fixture
def mixed_results():
    return [
        _make_result(_make_tp("tp_pass", "V1"), TPStatus.CLOSED,
                     pass_count=5, matched=["tp_pass"]),
        _make_result(_make_tp("tp_fail", "V1"), TPStatus.FAILING,
                     fail_count=3, matched=["tp_fail"]),
        _make_result(_make_tp("tp_skip", "V2"), TPStatus.NOT_RUN),
        _make_result(_make_tp("tp_partial", "V2"), TPStatus.PARTIAL,
                     pass_count=2, fail_count=2, matched=["tp_partial"]),
        _make_result(_make_tp("tp_na", "V1"), TPStatus.NA),
    ]


@pytest.fixture
def simple_plan():
    plan = Testplan(source_file="test.hjson")
    for name, stage in [("tp_pass", "V1"), ("tp_fail", "V1"),
                         ("tp_skip", "V2"), ("tp_partial", "V2"),
                         ("tp_na", "V1")]:
        plan.add_testpoint(Testpoint(name=name, stage=stage, tests=[name]))
    return plan


# ---------------------------------------------------------------------------
# JUnit XML
# ---------------------------------------------------------------------------

class TestExportJunitXml:
    def test_creates_file(self, mixed_results, tmp_path):
        out = str(tmp_path / "results.xml")
        export_junit_xml(mixed_results, out)
        assert os.path.exists(out)

    def test_valid_xml(self, mixed_results, tmp_path):
        out = str(tmp_path / "results.xml")
        export_junit_xml(mixed_results, out)
        tree = ET.parse(out)
        root = tree.getroot()
        assert root.tag == "testsuite"

    def test_testcase_count(self, mixed_results, tmp_path):
        out = str(tmp_path / "results.xml")
        export_junit_xml(mixed_results, out)
        tree = ET.parse(out)
        cases = tree.findall(".//testcase")
        assert len(cases) == len(mixed_results)

    def test_failure_element_for_failing(self, mixed_results, tmp_path):
        out = str(tmp_path / "results.xml")
        export_junit_xml(mixed_results, out)
        tree = ET.parse(out)
        fail_tc = next(
            tc for tc in tree.findall(".//testcase")
            if tc.attrib["name"] == "tp_fail"
        )
        assert fail_tc.find("failure") is not None

    def test_skipped_element_for_not_run(self, mixed_results, tmp_path):
        out = str(tmp_path / "results.xml")
        export_junit_xml(mixed_results, out)
        tree = ET.parse(out)
        skip_tc = next(
            tc for tc in tree.findall(".//testcase")
            if tc.attrib["name"] == "tp_skip"
        )
        assert skip_tc.find("skipped") is not None

    def test_no_failure_for_closed(self, mixed_results, tmp_path):
        out = str(tmp_path / "results.xml")
        export_junit_xml(mixed_results, out)
        tree = ET.parse(out)
        pass_tc = next(
            tc for tc in tree.findall(".//testcase")
            if tc.attrib["name"] == "tp_pass"
        )
        assert pass_tc.find("failure") is None
        assert pass_tc.find("skipped") is None

    def test_suite_name_attribute(self, mixed_results, tmp_path):
        out = str(tmp_path / "results.xml")
        export_junit_xml(mixed_results, out, suite_name="my_suite")
        tree = ET.parse(out)
        assert tree.getroot().attrib["name"] == "my_suite"

    def test_failure_count_in_suite(self, mixed_results, tmp_path):
        out = str(tmp_path / "results.xml")
        export_junit_xml(mixed_results, out)
        tree = ET.parse(out)
        # tp_fail (FAILING) + tp_partial (PARTIAL) = 2 failures
        assert tree.getroot().attrib["failures"] == "2"

    def test_partial_gets_failure_element(self, mixed_results, tmp_path):
        out = str(tmp_path / "results.xml")
        export_junit_xml(mixed_results, out)
        tree = ET.parse(out)
        partial_tc = next(
            tc for tc in tree.findall(".//testcase")
            if tc.attrib["name"] == "tp_partial"
        )
        assert partial_tc.find("failure") is not None


# ---------------------------------------------------------------------------
# GitHub Annotations
# ---------------------------------------------------------------------------

class TestExportGithubAnnotations:
    def test_error_for_failing(self, mixed_results):
        buf = io.StringIO()
        export_github_annotations(mixed_results, output=buf)
        text = buf.getvalue()
        assert "::error" in text
        assert "tp_fail" in text

    def test_warning_for_not_run(self, mixed_results):
        buf = io.StringIO()
        export_github_annotations(mixed_results, output=buf)
        text = buf.getvalue()
        assert "::warning" in text
        assert "tp_skip" in text

    def test_warning_for_partial(self, mixed_results):
        buf = io.StringIO()
        export_github_annotations(mixed_results, output=buf)
        text = buf.getvalue()
        assert "tp_partial" in text

    def test_no_output_for_closed(self, mixed_results):
        buf = io.StringIO()
        export_github_annotations(mixed_results, output=buf)
        text = buf.getvalue()
        # tp_pass (CLOSED) should NOT produce any annotation
        lines_with_pass = [l for l in text.splitlines() if "tp_pass" in l]
        assert lines_with_pass == []

    def test_no_output_for_na(self, mixed_results):
        buf = io.StringIO()
        export_github_annotations(mixed_results, output=buf)
        text = buf.getvalue()
        lines_with_na = [l for l in text.splitlines() if "tp_na" in l]
        assert lines_with_na == []

    def test_custom_file_field(self, mixed_results):
        buf = io.StringIO()
        export_github_annotations(mixed_results, file="uart.hjson", output=buf)
        text = buf.getvalue()
        assert "file=uart.hjson" in text


# ---------------------------------------------------------------------------
# Markdown summary
# ---------------------------------------------------------------------------

class TestExportSummaryMarkdown:
    def test_returns_string(self, mixed_results):
        md = export_summary_markdown(mixed_results)
        assert isinstance(md, str)

    def test_contains_headline(self, mixed_results):
        md = export_summary_markdown(mixed_results)
        assert "## Testplan Closure Report" in md

    def test_contains_stage_table(self, mixed_results):
        md = export_summary_markdown(mixed_results)
        assert "| Stage" in md
        assert "| V1" in md or "V1" in md

    def test_contains_testpoint_table(self, mixed_results):
        md = export_summary_markdown(mixed_results)
        assert "| Testpoint" in md
        assert "tp_pass" in md

    def test_gate_verdict_included(self, mixed_results, simple_plan):
        gate = report_stage_gate(mixed_results, "V1", simple_plan)
        md = export_summary_markdown(mixed_results, stage_gate=gate)
        assert "Stage gate" in md
        assert "V1" in md

    def test_blocking_section_when_gate_fails(self, mixed_results, simple_plan):
        gate = report_stage_gate(mixed_results, "V2", simple_plan)
        md = export_summary_markdown(mixed_results, stage_gate=gate)
        if not gate.passed:
            assert "Blocking testpoints" in md

    def test_na_testpoints_excluded_from_table(self, mixed_results):
        md = export_summary_markdown(mixed_results)
        # tp_na (N/A) and UNIMPLEMENTED should not appear in testpoint table rows
        # The heading line "| Testpoint" is present but tp_na row should not be
        rows = [l for l in md.splitlines() if "tp_na" in l and "|" in l]
        assert rows == []


# ── coverage <properties> in JUnit XML ───────────────────────────────────────

class TestJUnitCoverageProperties:
    def _make_cov_result(self, tp, status, pct):
        from covsight.core.ncdb.testplan import CoverageBinding
        from covsight.core.ncdb.testplan_closure import CoverageResult
        tp.coverage = [CoverageBinding(type="covergroup", path="top.cg")]
        r = TestpointResult(
            testpoint=tp, status=status, matched_tests=["t"],
            pass_count=1 if status == TPStatus.CLOSED else 0, fail_count=0,
            coverage_results=[
                CoverageResult(
                    binding_type="covergroup",
                    path_pattern="top.cg",
                    matched_paths=["top.cg"],
                    coverage_pct=pct,
                )
            ],
        )
        return r

    def test_properties_written_when_coverage_available(self, tmp_path):
        tp = _make_tp("tp_cov", "V1")
        r = self._make_cov_result(tp, TPStatus.CLOSED, 87.5)
        out = str(tmp_path / "out.xml")
        export_junit_xml([r], out)
        tree = ET.parse(out)
        props = tree.findall(".//properties/property")
        assert props, "Expected <properties> element"
        assert any("coverage" in p.get("name", "") for p in props)

    def test_property_value_is_pct_string(self, tmp_path):
        tp = _make_tp("tp_cov", "V1")
        r = self._make_cov_result(tp, TPStatus.CLOSED, 75.0)
        out = str(tmp_path / "out.xml")
        export_junit_xml([r], out)
        tree = ET.parse(out)
        props = tree.findall(".//properties/property")
        values = [p.get("value") for p in props]
        assert any("75.0" in v for v in values)

    def test_no_properties_when_no_coverage(self, tmp_path):
        tp = _make_tp("tp_no_cov", "V1")
        r = _make_result(tp, TPStatus.CLOSED, pass_count=1, matched=["t"])
        out = str(tmp_path / "out.xml")
        export_junit_xml([r], out)
        tree = ET.parse(out)
        props = tree.findall(".//properties")
        assert props == []


# ── GitHub ::notice:: for partially-covered CLOSED testpoints ─────────────────

class TestGitHubNoticeForCoverage:
    def _closed_result_with_pct(self, name, pct):
        from covsight.core.ncdb.testplan import CoverageBinding
        from covsight.core.ncdb.testplan_closure import CoverageResult
        tp = _make_tp(name, "V1")
        tp.coverage = [CoverageBinding(type="covergroup", path="top.cg")]
        return TestpointResult(
            testpoint=tp, status=TPStatus.CLOSED, matched_tests=["t"],
            pass_count=1, fail_count=0,
            coverage_results=[
                CoverageResult("covergroup", "top.cg", ["top.cg"], pct)
            ],
        )

    def test_notice_emitted_for_partial_coverage(self):
        r = self._closed_result_with_pct("tp_low_cov", 60.0)
        buf = io.StringIO()
        export_github_annotations([r], output=buf)
        out = buf.getvalue()
        assert "::notice" in out
        assert "tp_low_cov" in out

    def test_no_notice_for_full_coverage(self):
        r = self._closed_result_with_pct("tp_full", 100.0)
        buf = io.StringIO()
        export_github_annotations([r], output=buf)
        assert buf.getvalue() == ""

    def test_no_notice_when_coverage_pct_none(self):
        from covsight.core.ncdb.testplan import CoverageBinding
        from covsight.core.ncdb.testplan_closure import CoverageResult
        tp = _make_tp("tp_no_pct", "V1")
        r = TestpointResult(
            testpoint=tp, status=TPStatus.CLOSED, matched_tests=["t"],
            pass_count=1, fail_count=0,
            coverage_results=[
                CoverageResult("covergroup", "top.cg", ["top.cg"], None)
            ],
        )
        buf = io.StringIO()
        export_github_annotations([r], output=buf)
        assert buf.getvalue() == ""


# ── markdown coverage section ─────────────────────────────────────────────────

class TestMarkdownCoverageSection:
    def _make_result_with_pct(self, name, pct):
        from covsight.core.ncdb.testplan import CoverageBinding
        from covsight.core.ncdb.testplan_closure import CoverageResult
        tp = _make_tp(name, "V1")
        tp.coverage = [CoverageBinding(type="covergroup", path="top.cg")]
        return TestpointResult(
            testpoint=tp, status=TPStatus.CLOSED, matched_tests=["t"],
            pass_count=1, fail_count=0,
            coverage_results=[
                CoverageResult("covergroup", "top.cg", ["top.cg"], pct)
            ],
        )

    def test_coverage_section_present_when_pct_available(self):
        r = self._make_result_with_pct("tp1", 87.5)
        md = export_summary_markdown([r])
        assert "Coverage" in md
        assert "87.5" in md

    def test_coverage_section_absent_when_no_pct(self):
        from covsight.core.ncdb.testplan import CoverageBinding
        from covsight.core.ncdb.testplan_closure import CoverageResult
        tp = _make_tp("tp1", "V1")
        r = TestpointResult(
            testpoint=tp, status=TPStatus.CLOSED, matched_tests=["t"],
            pass_count=1, fail_count=0,
            coverage_results=[
                CoverageResult("covergroup", "top.cg", [], None)
            ],
        )
        md = export_summary_markdown([r])
        assert "### Coverage" not in md

    def test_coverage_section_absent_when_no_coverage_results(self):
        tp = _make_tp("tp1", "V1")
        r = _make_result(tp, TPStatus.CLOSED, pass_count=1, matched=["t"])
        md = export_summary_markdown([r])
        assert "### Coverage" not in md

    def test_coverage_section_shows_binding_type(self):
        r = self._make_result_with_pct("tp1", 75.0)
        md = export_summary_markdown([r])
        assert "covergroup" in md

    def test_coverage_section_shows_path(self):
        r = self._make_result_with_pct("tp1", 75.0)
        md = export_summary_markdown([r])
        assert "top.cg" in md
