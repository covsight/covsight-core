"""Parse ``docs/ucis-feature-catalog.md`` -- the human-readable narrative.

The catalog is prose plus markdown tables of the form::

    ## 4. Scope creation and management  (§ 8.5)

    | ID    | Feature                              | Spec ref |
    | ----- | ------------------------------------ | -------- |
    | S4.1  | `ucis_CreateScope(...)`              | § 8.5.2  |

Some tables title the third column "Notes / spec section" instead; the value is
treated as a spec reference when it starts with §, and as a note otherwise.

Two tools consume this: ``bootstrap_feature_registry.py`` (one-shot, catalog ->
YAML stubs) and ``sync_catalog.py --check`` (ongoing, catalog IDs vs registry
IDs). Sharing one parser means a catalog formatting change cannot make the two
disagree about what the catalog says.
"""

from __future__ import annotations

import dataclasses
import re
from pathlib import Path

from . import ids

SECTION_RE = re.compile(r"^##\s+(?P<number>\d+)\.\s+(?P<title>.+?)\s*$")
ROW_RE = re.compile(r"^\|\s*(?P<id>[A-Z]{1,3}[0-9]{0,2}\.[0-9]+[a-z]?)\s*\|(?P<rest>.*)$")
SPEC_IN_TITLE_RE = re.compile(r"\s*\((?P<spec>§[^)]*)\)\s*$")


@dataclasses.dataclass(frozen=True)
class CatalogEntry:
    id: str
    title: str
    spec_ref: str | None
    notes: str | None
    section_number: int
    section_title: str
    line: int


@dataclasses.dataclass(frozen=True)
class CatalogSection:
    number: int
    title: str
    spec_ref: str | None
    entries: tuple[CatalogEntry, ...]

    @property
    def slug(self) -> str:
        return slugify(self.title)

    @property
    def filename(self) -> str:
        return f"{self.number:02d}-{self.slug}.yaml"


def default_path() -> Path:
    from .registry import default_root

    return default_root().parents[1] / "ucis-feature-catalog.md"


def slugify(title: str, max_words: int = 3) -> str:
    """A short, stable file slug. Short because the file name is a label, not a
    summary -- ``17-statement-block.yaml`` beats
    ``17-statement-block-coverage-6-5-3-9-12.yaml``."""
    words = re.findall(r"[A-Za-z0-9]+", title.lower())
    drop = {"and", "or", "the", "a", "of", "to", "coverage", "handling"}
    kept = [w for w in words if w not in drop] or words
    return "-".join(kept[:max_words])


def parse(path: Path | str | None = None) -> list[CatalogSection]:
    path = Path(path) if path is not None else default_path()
    text = path.read_text(encoding="utf-8")

    sections: list[CatalogSection] = []
    number: int | None = None
    title = ""
    spec_ref: str | None = None
    entries: list[CatalogEntry] = []

    def flush() -> None:
        if number is not None:
            sections.append(
                CatalogSection(number=number, title=title, spec_ref=spec_ref,
                               entries=tuple(entries))
            )

    for lineno, line in enumerate(text.splitlines(), start=1):
        m = SECTION_RE.match(line)
        if m:
            flush()
            number = int(m["number"])
            raw_title = m["title"]
            spec_m = SPEC_IN_TITLE_RE.search(raw_title)
            spec_ref = spec_m["spec"].strip() if spec_m else None
            title = SPEC_IN_TITLE_RE.sub("", raw_title).strip()
            entries = []
            continue

        row = ROW_RE.match(line)
        if not row or number is None:
            continue

        cells = [c.strip() for c in row["rest"].split("|")]
        # A trailing empty cell from the closing pipe.
        while cells and not cells[-1]:
            cells.pop()
        feature = cells[0] if cells else ""
        third = cells[1] if len(cells) > 1 else ""

        entries.append(
            CatalogEntry(
                id=row["id"],
                title=feature,
                spec_ref=third or None if third.startswith("§") else None,
                notes=None if third.startswith("§") else (third or None),
                section_number=number,
                section_title=title,
                line=lineno,
            )
        )

    flush()
    return [s for s in sections if s.entries]


def entry_ids(sections: list[CatalogSection]) -> list[str]:
    return sorted((e.id for s in sections for e in s.entries), key=ids.sort_key)


def find_duplicates(sections: list[CatalogSection]) -> dict[str, list[CatalogEntry]]:
    seen: dict[str, list[CatalogEntry]] = {}
    for section in sections:
        for entry in section.entries:
            seen.setdefault(entry.id, []).append(entry)
    return {k: v for k, v in seen.items() if len(v) > 1}
