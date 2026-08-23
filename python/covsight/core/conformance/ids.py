"""Feature-ID parsing, ordering, and glob matching.

IDs come in two shapes, both inherited from ``docs/ucis-feature-catalog.md``:

    S4.4    prefix letters + catalog section number, then an ordinal
    ST.17   prefix letters only (enum-token tables), then an ordinal

Both are opaque keys as far as the registry is concerned -- nothing downstream
may infer a section number from the digits in an ID, because ``ST.*`` has none.
The parse exists for *ordering* and for ``prefix.*`` glob claims, not for
deriving facts.
"""

from __future__ import annotations

import re
from typing import Iterable, NamedTuple

ID_RE = re.compile(r"^(?P<prefix>[A-Z]{1,3}[0-9]{0,2})\.(?P<ordinal>[0-9]+)(?P<suffix>[a-z]?)$")
GLOB_RE = re.compile(r"^(?P<prefix>[A-Z]{1,3}[0-9]{0,2})\.\*$")


class ParsedId(NamedTuple):
    prefix: str
    ordinal: int
    suffix: str

    @property
    def text(self) -> str:
        return f"{self.prefix}.{self.ordinal}{self.suffix}"


class BadFeatureId(ValueError):
    pass


def parse(fid: str) -> ParsedId:
    m = ID_RE.match(fid.strip())
    if not m:
        raise BadFeatureId(
            f"{fid!r} is not a feature ID (expected e.g. 'S4.4', 'ST.17', 'X.1')"
        )
    return ParsedId(m["prefix"], int(m["ordinal"]), m["suffix"])


def is_id(text: str) -> bool:
    return bool(ID_RE.match(text.strip()))


def is_glob(text: str) -> bool:
    return bool(GLOB_RE.match(text.strip()))


def sort_key(fid: str) -> tuple:
    """Stable ordering: by prefix, then numerically by ordinal.

    Numeric ordinal matters -- lexical sorting puts S4.10 before S4.2 and makes
    the generated matrix read as if it were shuffled.
    """
    p = parse(fid)
    return (p.prefix, p.ordinal, p.suffix)


def expand_glob(pattern: str, known: Iterable[str]) -> list[str]:
    """Expand ``PREFIX.*`` against the known ID space.

    Used for doc claims: the 39 scope-type IDs are genuinely carried by one
    column and should not need 39 comment entries. An exact ID passes through
    unchanged so callers can treat claims uniformly.
    """
    text = pattern.strip()
    m = GLOB_RE.match(text)
    if not m:
        if not is_id(text):
            raise BadFeatureId(f"{pattern!r} is neither a feature ID nor a PREFIX.* glob")
        return [text]
    prefix = m["prefix"]
    return sorted((k for k in known if parse(k).prefix == prefix), key=sort_key)
