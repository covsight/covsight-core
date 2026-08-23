"""Load and validate the UCIS conformance feature registry.

The registry is the key space: ``docs/conformance/features/*.yaml``, one file
per catalog section. It records **intent only** -- what each backend is
supposed to do with each UCIS feature. It deliberately carries no status, no
test names and no checkmarks; those are computed by
``tools/gen_conformance_matrix.py`` from collected tests and doc claims.

Structural validation happens here, unconditionally, because every consumer
(generator, sync tool, pytest plugin) depends on it. The JSON Schema in
``docs/conformance/features.schema.json`` is the published contract for
third-party tooling and is checked by the L0 tests when ``jsonschema`` is
installed; the two are kept in agreement by an L0 test that runs both.
"""

from __future__ import annotations

import dataclasses
import os
import re
from pathlib import Path
from typing import Iterator, Mapping, Sequence

import yaml

from . import ids

SCOPES = ("core", "extended", "out_of_scope", "deferred", "unclassified")
IMPLEMENTED_SCOPES = ("core", "extended")
NEEDS_REASON = ("out_of_scope", "deferred")
KINDS = ("feature", "combination", "negative")
SURFACES = ("write", "read", "merge", "query", "append", "compact")
DEFAULT_APPLIES_TO = ("write", "read")

_FILENAME_RE = re.compile(r"^(?P<number>[0-9]{2})-(?P<slug>[a-z0-9-]+)\.yaml$")

# Fields a human might reach for when they want to record a result rather than
# an intention. Rejecting them by name gives a better error than "additional
# property not allowed", and this is the rule the whole design rests on.
FORBIDDEN_FIELDS = {
    "status": "status is computed from tests and doc claims, never written by hand",
    "tested": "test evidence comes from @ucis_feature markers, not the registry",
    "documented": "doc coverage comes from <!-- ucis-features: --> claims",
    "coverage": "coverage numbers live in the generated summary",
    "state": "did you mean 'scope' (intent) or 'lifecycle' (superseded)?",
}


class RegistryError(Exception):
    """A registry file is malformed. Always carries file and entry context."""


@dataclasses.dataclass(frozen=True)
class BackendScope:
    backend: str
    scope: str
    reason: str | None = None
    adr: str | None = None
    issue: str | None = None

    @property
    def implemented(self) -> bool:
        """True when the backend is expected to support this feature.

        Only implemented (backend, feature) pairs get a computed status; the
        rest render as an explicit out-of-scope row with the reason inline.
        """
        return self.scope in IMPLEMENTED_SCOPES


@dataclasses.dataclass(frozen=True)
class Feature:
    id: str
    title: str
    kind: str
    backends: Mapping[str, BackendScope]
    spec_ref: str | None = None
    composes: tuple[str, ...] = ()
    applies_to: tuple[str, ...] = DEFAULT_APPLIES_TO
    aliases: tuple[str, ...] = ()
    lifecycle: str = "active"
    superseded_by: str | None = None
    notes: str | None = None
    section_number: int = 0
    section_title: str = ""
    source_file: str = ""

    @property
    def active(self) -> bool:
        return self.lifecycle == "active"

    def sort_key(self) -> tuple:
        return (self.section_number, ids.sort_key(self.id))


@dataclasses.dataclass(frozen=True)
class Section:
    number: int
    title: str
    spec_ref: str | None
    notes: str | None
    source_file: str
    features: tuple[Feature, ...]


class Registry:
    """The loaded registry: sections in catalog order, features by ID."""

    def __init__(self, sections: Sequence[Section], root: Path):
        self.root = root
        self.sections = tuple(sorted(sections, key=lambda s: s.number))
        self._by_id: dict[str, Feature] = {}
        self._alias_to_id: dict[str, str] = {}
        for section in self.sections:
            for feat in section.features:
                self._by_id[feat.id] = feat
                for alias in feat.aliases:
                    self._alias_to_id[alias] = feat.id

    # -- lookup ---------------------------------------------------------

    def __len__(self) -> int:
        return len(self._by_id)

    def __contains__(self, fid: str) -> bool:
        return fid in self._by_id or fid in self._alias_to_id

    def __iter__(self) -> Iterator[Feature]:
        for section in self.sections:
            yield from section.features

    @property
    def ids(self) -> list[str]:
        return sorted(self._by_id, key=ids.sort_key)

    def resolve(self, fid: str) -> str:
        """Map an alias to its current ID; pass a known ID through."""
        fid = fid.strip()
        if fid in self._by_id:
            return fid
        if fid in self._alias_to_id:
            return self._alias_to_id[fid]
        raise KeyError(fid)

    def get(self, fid: str) -> Feature:
        return self._by_id[self.resolve(fid)]

    def backends(self) -> list[str]:
        """Every backend name mentioned anywhere, sorted."""
        names: set[str] = set()
        for feat in self:
            names.update(feat.backends)
        return sorted(names)

    def unclassified(self, backend: str) -> list[Feature]:
        return [
            f
            for f in self
            if f.active
            and (backend not in f.backends or f.backends[backend].scope == "unclassified")
        ]


# -- loading ------------------------------------------------------------


def default_root() -> Path:
    """Locate ``docs/conformance/features``.

    Repo-relative by default, matching how ``tests/conftest.py`` already finds
    ``testdata/``. The environment override exists so a future standalone
    conformance package can point at installed package data without any caller
    changing (see plan decision 1).
    """
    override = os.environ.get("COVSIGHT_CONFORMANCE_REGISTRY")
    if override:
        return Path(override)
    return Path(__file__).resolve().parents[4] / "docs" / "conformance" / "features"


def schema_path() -> Path:
    return default_root().parent / "features.schema.json"


def load(root: Path | str | None = None) -> Registry:
    root = Path(root) if root is not None else default_root()
    if not root.is_dir():
        raise RegistryError(f"registry directory not found: {root}")

    paths = sorted(root.glob("*.yaml"))
    if not paths:
        raise RegistryError(f"no registry files in {root}")

    sections: list[Section] = []
    seen_ids: dict[str, str] = {}
    seen_sections: dict[int, str] = {}

    for path in paths:
        section = _load_section(path)
        prior = seen_sections.get(section.number)
        if prior is not None:
            raise RegistryError(
                f"section number {section.number} used by both {prior} and {path.name}"
            )
        seen_sections[section.number] = path.name

        for feat in section.features:
            for key in (feat.id, *feat.aliases):
                prior_file = seen_ids.get(key)
                if prior_file is not None:
                    raise RegistryError(
                        f"{path.name}: feature ID {key!r} already defined in "
                        f"{prior_file}. IDs are immutable and unique across the "
                        f"whole registry."
                    )
                seen_ids[key] = path.name
        sections.append(section)

    registry = Registry(sections, root)
    _validate_cross_references(registry)
    return registry


def _load_section(path: Path) -> Section:
    m = _FILENAME_RE.match(path.name)
    if not m:
        raise RegistryError(
            f"{path.name}: registry files are named NN-slug.yaml (e.g. 04-scopes.yaml)"
        )

    try:
        doc = yaml.safe_load(path.read_text(encoding="utf-8"))
    except yaml.YAMLError as exc:
        raise RegistryError(f"{path.name}: {exc}") from exc

    if not isinstance(doc, dict):
        raise RegistryError(f"{path.name}: top level must be a mapping")

    _reject_unknown(path.name, "<top level>", doc, {"section", "features"})

    raw_section = doc.get("section")
    if not isinstance(raw_section, dict):
        raise RegistryError(f"{path.name}: missing 'section' mapping")
    _reject_unknown(
        path.name, "section", raw_section, {"number", "title", "spec_ref", "notes"}
    )

    number = raw_section.get("number")
    if not isinstance(number, int):
        raise RegistryError(f"{path.name}: section.number must be an integer")
    if number != int(m["number"]):
        raise RegistryError(
            f"{path.name}: section.number is {number} but the file name says "
            f"{int(m['number'])}"
        )

    title = raw_section.get("title")
    if not isinstance(title, str) or not title.strip():
        raise RegistryError(f"{path.name}: section.title is required")

    raw_features = doc.get("features")
    if not isinstance(raw_features, list) or not raw_features:
        raise RegistryError(f"{path.name}: 'features' must be a non-empty list")

    features = tuple(
        _load_feature(path.name, number, title, entry) for entry in raw_features
    )
    return Section(
        number=number,
        title=title,
        spec_ref=raw_section.get("spec_ref"),
        notes=raw_section.get("notes"),
        source_file=path.name,
        features=features,
    )


_FEATURE_FIELDS = {
    "id",
    "title",
    "spec_ref",
    "kind",
    "composes",
    "applies_to",
    "aliases",
    "lifecycle",
    "superseded_by",
    "backends",
    "notes",
}


def _load_feature(fname: str, section_number: int, section_title: str, entry) -> Feature:
    if not isinstance(entry, dict):
        raise RegistryError(f"{fname}: each feature must be a mapping, got {type(entry).__name__}")

    fid = entry.get("id")
    if not isinstance(fid, str) or not ids.is_id(fid):
        raise RegistryError(f"{fname}: bad or missing feature id {fid!r}")

    where = f"{fname}:{fid}"
    _reject_unknown(fname, fid, entry, _FEATURE_FIELDS)

    title = entry.get("title")
    if not isinstance(title, str) or not title.strip():
        raise RegistryError(f"{where}: 'title' is required")

    kind = entry.get("kind", "feature")
    if kind not in KINDS:
        raise RegistryError(f"{where}: kind must be one of {KINDS}, got {kind!r}")

    composes = tuple(entry.get("composes") or ())
    for cid in composes:
        if not ids.is_id(cid):
            raise RegistryError(f"{where}: composes entry {cid!r} is not a feature ID")
    if kind == "combination" and len(composes) < 2:
        raise RegistryError(f"{where}: kind 'combination' needs 'composes' with >= 2 IDs")
    if kind != "combination" and composes:
        raise RegistryError(f"{where}: 'composes' only applies to kind 'combination'")

    applies_to = tuple(entry.get("applies_to") or DEFAULT_APPLIES_TO)
    for surface in applies_to:
        if surface not in SURFACES:
            raise RegistryError(f"{where}: unknown surface {surface!r}; expected {SURFACES}")

    aliases = tuple(entry.get("aliases") or ())
    for alias in aliases:
        if not ids.is_id(alias):
            raise RegistryError(f"{where}: alias {alias!r} is not a feature ID")

    lifecycle = entry.get("lifecycle", "active")
    if lifecycle not in ("active", "superseded"):
        raise RegistryError(f"{where}: lifecycle must be 'active' or 'superseded'")
    superseded_by = entry.get("superseded_by")
    if lifecycle == "superseded" and not superseded_by:
        raise RegistryError(f"{where}: superseded entries must name 'superseded_by'")
    if superseded_by is not None and not ids.is_id(superseded_by):
        raise RegistryError(f"{where}: superseded_by {superseded_by!r} is not a feature ID")

    raw_backends = entry.get("backends")
    if not isinstance(raw_backends, dict) or not raw_backends:
        raise RegistryError(f"{where}: 'backends' must be a non-empty mapping")

    backends = {
        name: _load_backend(where, name, value) for name, value in raw_backends.items()
    }

    return Feature(
        id=fid,
        title=title,
        kind=kind,
        backends=backends,
        spec_ref=entry.get("spec_ref"),
        composes=composes,
        applies_to=applies_to,
        aliases=aliases,
        lifecycle=lifecycle,
        superseded_by=superseded_by,
        notes=entry.get("notes"),
        section_number=section_number,
        section_title=section_title,
        source_file=fname,
    )


def _load_backend(where: str, name: str, value) -> BackendScope:
    if not isinstance(value, dict):
        raise RegistryError(f"{where}: backends.{name} must be a mapping with a 'scope'")
    _reject_unknown(where, f"backends.{name}", value, {"scope", "reason", "adr", "issue"})

    scope = value.get("scope")
    if scope not in SCOPES:
        raise RegistryError(
            f"{where}: backends.{name}.scope must be one of {SCOPES}, got {scope!r}"
        )
    reason = value.get("reason")
    if scope in NEEDS_REASON and not (isinstance(reason, str) and reason.strip()):
        raise RegistryError(
            f"{where}: backends.{name}.scope is {scope!r} and therefore needs a "
            f"'reason'. An undocumented exclusion is the one thing a third-party "
            f"implementer cannot work around."
        )
    if scope == "deferred" and not (value.get("adr") or value.get("issue")):
        raise RegistryError(
            f"{where}: backends.{name} is deferred and needs an 'adr' or 'issue' "
            f"so the decision is re-reviewable"
        )
    return BackendScope(
        backend=name,
        scope=scope,
        reason=reason,
        adr=value.get("adr"),
        issue=value.get("issue"),
    )


def _reject_unknown(fname: str, where: str, mapping: Mapping, allowed: set[str]) -> None:
    for key in mapping:
        if key in allowed:
            continue
        hint = FORBIDDEN_FIELDS.get(key)
        if hint:
            raise RegistryError(f"{fname}:{where}: field {key!r} is not allowed here -- {hint}")
        raise RegistryError(
            f"{fname}:{where}: unknown field {key!r}; allowed: {sorted(allowed)}"
        )


def _validate_cross_references(registry: Registry) -> None:
    for feat in registry:
        for cid in feat.composes:
            if cid not in registry:
                raise RegistryError(
                    f"{feat.source_file}:{feat.id}: composes unknown feature {cid!r}"
                )
        if feat.superseded_by and feat.superseded_by not in registry:
            raise RegistryError(
                f"{feat.source_file}:{feat.id}: superseded_by unknown feature "
                f"{feat.superseded_by!r}"
            )
