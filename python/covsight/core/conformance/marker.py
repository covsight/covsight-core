"""The ``@ucis_feature`` marker and the pytest plugin that harvests it.

Usage::

    from covsight.core.conformance import ucis_feature

    @ucis_feature("S4.4", "X.1", level="L2", surface="merge")
    def test_cross_with_exclusions_merges(...):
        ...

The plugin does three jobs, in this order of importance:

1. **Validate at collection.** An unknown, superseded, or misspelled ID is a
   collection error. A typo that silently produced an uncredited test would be
   invisible in exactly the way this whole scheme exists to prevent.
2. **Emit evidence.** ``--conformance-json=PATH`` writes the collected
   tag -> test mapping, annotated with each test's outcome.
3. **Record collection completeness.** ``collected_full`` is true only for a
   whole-suite run. The generator refuses to rewrite the matrix without it, so
   ``pytest -k cross`` followed by a regeneration cannot delete 250 rows.

Evidence is **collection-based, outcome-annotated** (plan decision 4): the
committed matrix reflects which tests *exist*, not which ones *passed*, because
the latter varies with installed extras and would make the file churn between
environments. Outcomes ride along for CI to report on.

pytest is imported lazily throughout: the package's only hard dependency is
PyYAML, and ``covsight.core.conformance`` is importable in a non-dev install.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any

LEVELS = ("L0", "L1", "L2", "L3", "L4", "L5")
EVIDENCE_SCHEMA = 1

MARKER_NAME = "ucis_feature"
MARKER_HELP = (
    "ucis_feature(*ids, level, surface, backend): conformance evidence for "
    "registry feature IDs (docs/conformance/features/)"
)


def ucis_feature(
    *feature_ids: str,
    level: str,
    surface: str | None = None,
    backend: str | None = None,
):
    """Tag a test as evidence for one or more registry feature IDs.

    ``level`` is required and explicit rather than derived from the test's
    directory: several tagged tests live in ``tests/parquet/`` and always will
    (plan decision 3).
    """
    import pytest

    if not feature_ids:
        raise ValueError("@ucis_feature needs at least one feature ID")
    if level not in LEVELS:
        raise ValueError(f"@ucis_feature level must be one of {LEVELS}, got {level!r}")
    for fid in feature_ids:
        if not isinstance(fid, str):
            raise TypeError(f"@ucis_feature IDs must be strings, got {fid!r}")

    return pytest.mark.ucis_feature(
        *feature_ids, level=level, surface=surface, backend=backend
    )


# -- pytest plugin ------------------------------------------------------


class ConformancePlugin:
    def __init__(self, config):
        self.config = config
        self.output = config.getoption("conformance_json")
        # nodeid -> record; outcomes are filled in as tests run.
        self.records: dict[str, dict[str, Any]] = {}
        self.collected_full = True
        self.incomplete_reasons: list[str] = []

    # -- completeness ---------------------------------------------------

    def _note_partial(self, reason: str) -> None:
        self.collected_full = False
        if reason not in self.incomplete_reasons:
            self.incomplete_reasons.append(reason)

    def _check_invocation(self) -> None:
        opt = self.config.option
        if getattr(opt, "keyword", ""):
            self._note_partial(f"-k {opt.keyword!r}")
        if getattr(opt, "markexpr", ""):
            self._note_partial(f"-m {opt.markexpr!r}")
        if getattr(opt, "deselect", None):
            self._note_partial("--deselect")
        # --ignore narrows collection exactly as an explicit path argument does,
        # and is the form a dev reaches for to skip a slow or broken directory.
        # It leaves `config.args` untouched, so nothing else here would see it.
        for name in ("ignore", "ignore_glob"):
            if getattr(opt, name, None):
                self._note_partial(f"--{name.replace('_', '-')}")
        if getattr(opt, "last_failed", False) or getattr(opt, "failedfirst", False):
            self._note_partial("--lf/--ff")
        # Explicit path or nodeid arguments mean a subset was requested.
        # config.args is what pytest resolved to collect from, and equals the
        # ini testpaths when the user named nothing -- unlike the raw invocation
        # args, where the value of an option such as `-k cross` looks exactly
        # like a path.
        paths = list(self.config.args)
        testpaths = [str(p) for p in (self.config.getini("testpaths") or [])]
        if testpaths and paths != testpaths:
            self._note_partial(f"explicit path args: {paths}")

    # -- hooks ----------------------------------------------------------

    def pytest_collection_modifyitems(self, session, config, items):
        import pytest

        self._check_invocation()

        tagged = [i for i in items if i.get_closest_marker(MARKER_NAME) is not None]
        if not tagged and not self.output:
            # Nothing to validate and no evidence requested; do not make an
            # unrelated pytest run depend on the registry being present.
            return

        try:
            from . import registry as registry_mod

            reg = registry_mod.load()
        except Exception as exc:  # registry broken -> fail loudly, not silently
            raise pytest.UsageError(
                f"conformance registry could not be loaded, so @ucis_feature IDs "
                f"cannot be validated: {exc}"
            ) from exc

        errors: list[str] = []
        for item in tagged:
            for mark in item.iter_markers(name=MARKER_NAME):
                record = self._record_for(item, mark, reg, errors)
                if record is not None:
                    self.records.setdefault(item.nodeid, record)

        if errors:
            raise pytest.UsageError(
                "invalid @ucis_feature markers:\n  " + "\n  ".join(errors)
            )

    def _record_for(self, item, mark, reg, errors: list[str]):
        level = mark.kwargs.get("level")
        if level not in LEVELS:
            errors.append(f"{item.nodeid}: level={level!r} is not one of {LEVELS}")
            return None

        resolved: list[str] = []
        for fid in mark.args:
            try:
                canonical = reg.resolve(fid)
            except KeyError:
                errors.append(
                    f"{item.nodeid}: unknown feature ID {fid!r} -- not in "
                    f"docs/conformance/features/"
                )
                continue
            feat = reg.get(canonical)
            if not feat.active:
                errors.append(
                    f"{item.nodeid}: feature {fid!r} is superseded by "
                    f"{feat.superseded_by!r}; tag the successor"
                )
                continue
            resolved.append(canonical)

        if not resolved:
            return None

        return {
            "features": sorted(set(resolved)),
            "level": level,
            "surface": mark.kwargs.get("surface"),
            "backend": mark.kwargs.get("backend") or _backend_of(item),
            "outcome": "not run",
        }

    def pytest_deselected(self, items):
        if items:
            self._note_partial(f"{len(items)} deselected item(s)")

    def pytest_collectreport(self, report):
        if report.failed:
            self._note_partial(f"collection error in {report.nodeid or '<root>'}")

    def pytest_runtest_logreport(self, report):
        record = self.records.get(report.nodeid)
        if record is None:
            return
        if report.when == "setup" and report.skipped:
            record["outcome"] = "skipped"
        elif report.when == "call":
            record["outcome"] = report.outcome
        elif report.when in ("setup", "teardown") and report.failed:
            record["outcome"] = "error"

    def pytest_sessionfinish(self, session, exitstatus):
        if not self.output:
            return
        path = Path(self.output)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(self.build_evidence(), indent=2) + "\n", encoding="utf-8")

    # -- evidence -------------------------------------------------------

    def build_evidence(self) -> dict[str, Any]:
        import sys

        features: dict[str, list[dict[str, Any]]] = {}
        for nodeid, record in self.records.items():
            for fid in record["features"]:
                features.setdefault(fid, []).append(
                    {
                        "nodeid": nodeid,
                        "level": record["level"],
                        "surface": record["surface"],
                        "backend": record["backend"],
                        "outcome": record["outcome"],
                    }
                )
        for entries in features.values():
            entries.sort(key=lambda e: (e["nodeid"], e["level"]))

        return {
            "schema": EVIDENCE_SCHEMA,
            "collected_full": self.collected_full,
            "incomplete_reasons": list(self.incomplete_reasons),
            "run": {
                "python": ".".join(str(p) for p in sys.version_info[:3]),
                "extras": _detected_extras(),
            },
            "features": {fid: features[fid] for fid in sorted(features)},
        }


def _backend_of(item) -> str | None:
    """Best-effort backend attribution from the test's parameterization.

    A cross-backend test parameterized over the ``backend`` fixture should
    credit each backend it actually ran against, without repeating that in the
    marker.
    """
    callspec = getattr(item, "callspec", None)
    if callspec is None:
        return None
    value = callspec.params.get("backend")
    if isinstance(value, (tuple, list)) and value:
        return str(value[0])
    if isinstance(value, str):
        return value
    return None


def _detected_extras() -> list[str]:
    """Which optional extras were importable, for the run report only.

    Recorded so a CI reader can tell "no test exists" from "the test could not
    run here". It never affects the committed matrix.
    """
    import importlib.util

    probes = {"parquet": "pyarrow", "iceberg": "pyiceberg", "duckdb": "duckdb",
              "validate": "jsonschema", "accel": "cffi"}
    found = []
    for extra, module in probes.items():
        try:
            if importlib.util.find_spec(module) is not None:
                found.append(extra)
        except (ImportError, ValueError):
            pass
    return found


def pytest_addoption(parser):
    group = parser.getgroup("conformance")
    group.addoption(
        "--conformance-json",
        action="store",
        default=os.environ.get("COVSIGHT_CONFORMANCE_JSON"),
        metavar="PATH",
        help="write UCIS conformance evidence (collected @ucis_feature tags) to PATH",
    )


def pytest_configure(config):
    config.addinivalue_line("markers", MARKER_HELP)
    config.pluginmanager.register(ConformancePlugin(config), "covsight-conformance")
