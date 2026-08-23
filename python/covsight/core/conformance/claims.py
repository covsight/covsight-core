"""Extract feature claims from the mapping document.

A rule section of ``docs/ucis-parquet-mapping.md`` declares which UCIS features
it carries, in an HTML comment directly under its heading::

    ### `scopes`
    <!-- ucis-features: S4.1, S4.2, S4.4, ST.*, F10.3 -->

The comment is invisible in rendered markdown and in the Sphinx mirror, so the
published document is unchanged. ``PREFIX.*`` claims a whole prefix, which the
39 scope-type IDs need -- they are genuinely carried by one column, and 39
comment entries would be noise that nobody would maintain.

The claim is the *documentation* half of the matrix. Without it a feature that
the writer implements and a test exercises still shows as undocumented, which is
the state (orange in the generated matrix) that breaks a third-party implementer
reading only the spec.
"""

from __future__ import annotations

import dataclasses
import re
from pathlib import Path

from . import ids

CLAIM_RE = re.compile(r"^<!--\s*ucis-features:\s*(?P<body>.*?)\s*-->\s*$")
HEADING_RE = re.compile(r"^(?P<hashes>#{1,6})\s+(?P<text>.+?)\s*$")
FENCE_RE = re.compile(r"^\s*(```|~~~)")


@dataclasses.dataclass(frozen=True)
class Claim:
    heading: str
    anchor: str
    level: int
    patterns: tuple[str, ...]
    line: int
    document: str

    def resolve(self, known: list[str]) -> list[str]:
        out: list[str] = []
        for pattern in self.patterns:
            out.extend(ids.expand_glob(pattern, known))
        return sorted(set(out), key=ids.sort_key)


class ClaimError(Exception):
    pass


def default_path() -> Path:
    from .registry import default_root

    return default_root().parents[1] / "ucis-parquet-mapping.md"


def slugify(heading: str) -> str:
    """GitHub-flavoured heading anchor.

    Backticks and inline code are stripped, punctuation dropped, spaces become
    hyphens. Matching GitHub matters because the generated matrix links into the
    mapping document as rendered on the forge.
    """
    text = heading.strip()
    text = re.sub(r"`([^`]*)`", r"\1", text)          # inline code -> its text
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)  # links -> their text
    text = text.lower()
    text = re.sub(r"[^\w\s-]", "", text, flags=re.UNICODE)
    # One hyphen per whitespace character, not per run: GitHub turns "a & b"
    # into "a--b" because the ampersand is dropped and both spaces survive.
    # Collapsing runs here would emit links that 404 on the forge.
    return re.sub(r"\s", "-", text.strip())


def parse(path: Path | str | None = None) -> list[Claim]:
    path = Path(path) if path is not None else default_path()
    document = path.name
    text = path.read_text(encoding="utf-8")

    claims: list[Claim] = []
    heading = ""
    level = 0
    in_fence = False
    seen_anchors: dict[str, int] = {}

    for lineno, line in enumerate(text.splitlines(), start=1):
        if FENCE_RE.match(line):
            in_fence = not in_fence
            continue
        if in_fence:
            continue

        h = HEADING_RE.match(line)
        if h:
            heading = h["text"]
            level = len(h["hashes"])
            anchor = slugify(heading)
            # GitHub disambiguates repeats with -1, -2, ...; a claim under an
            # ambiguous heading would link to the wrong place, so refuse.
            seen_anchors[anchor] = seen_anchors.get(anchor, 0) + 1
            continue

        c = CLAIM_RE.match(line)
        if not c:
            continue

        if not heading:
            raise ClaimError(
                f"{document}:{lineno}: ucis-features claim before any heading"
            )

        patterns = tuple(p.strip() for p in c["body"].split(",") if p.strip())
        if not patterns:
            raise ClaimError(f"{document}:{lineno}: empty ucis-features claim")
        for pattern in patterns:
            if not (ids.is_id(pattern) or ids.is_glob(pattern)):
                raise ClaimError(
                    f"{document}:{lineno}: {pattern!r} is neither a feature ID nor "
                    f"a PREFIX.* glob"
                )

        anchor = slugify(heading)
        if seen_anchors.get(anchor, 0) > 1:
            raise ClaimError(
                f"{document}:{lineno}: heading {heading!r} is not unique, so its "
                f"anchor #{anchor} is ambiguous. Rename one of them -- the matrix "
                f"links here."
            )

        claims.append(
            Claim(
                heading=heading,
                anchor=anchor,
                level=level,
                patterns=patterns,
                line=lineno,
                document=document,
            )
        )

    return claims


def index(claims: list[Claim], known: list[str]) -> dict[str, list[Claim]]:
    """feature ID -> claiming sections. Unknown IDs are reported by the caller.

    A feature may be claimed by more than one section (a scope type carried by
    both the ``scopes`` table and the XML mapping, say); all claims are kept so
    the matrix can link to each.
    """
    out: dict[str, list[Claim]] = {}
    for claim in claims:
        for fid in claim.resolve(known):
            out.setdefault(fid, []).append(claim)
    return out


def unknown_ids(claims: list[Claim], known: list[str]) -> dict[str, list[Claim]]:
    """Claims naming IDs (or prefixes) that do not exist in the registry.

    A glob that expands to nothing counts: ``ST.*`` quietly matching zero IDs
    after a prefix rename would silently un-document 39 features.
    """
    known_set = set(known)
    out: dict[str, list[Claim]] = {}
    for claim in claims:
        for pattern in claim.patterns:
            if ids.is_glob(pattern):
                if not ids.expand_glob(pattern, known):
                    out.setdefault(pattern, []).append(claim)
            elif pattern not in known_set:
                out.setdefault(pattern, []).append(claim)
    return out
