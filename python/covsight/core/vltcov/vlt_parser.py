"""Parser for Verilator's ``coverage.dat`` (``# SystemC::Coverage-3``) format.

Each data line is ``C '<keys>' <count>`` where ``<keys>`` is a sequence of
``\\001key\\002value`` pairs.  Keys seen from Verilator 5.x:

====== =====================================================================
``t``  kind: line, branch, expr, toggle, fsm_state, fsm_arc, user, covergroup
``page`` ``v_<kind>/<module>`` (``v_covergroup/<covergroup type>``)
``f``  source file            ``l`` line           ``n`` column
``h``  hierarchy (``top.u_*`` when instances are aggregated)
``o``  comment: block/if/else, toggle ``sig[i]:0->1``, expr term, FSM label
``S``  line ranges the point spans (``17-18,20``)
``bin`` covergroup bin; ``cross=1`` + ``Cb=a,b`` for cross bins;
       ``bin_type`` = default / ignore / illegal
``Fv`` FSM variable (full path), ``Ff``/``Ft`` FSM from/to state
====== =====================================================================
"""
import re
from dataclasses import dataclass, field
from typing import Dict, Iterable, List, Optional, TextIO

_LINE_RE = re.compile(r"^C\s+'(.*)'\s+(\d+)\s*$")

# Older Verilator / pyucis-written files used ``v_funccov`` for covergroups.
_PAGE_KINDS = (
    ("v_covergroup", "covergroup"),
    ("v_funccov", "covergroup"),
    ("v_fsm_state", "fsm_state"),
    ("v_fsm_arc", "fsm_arc"),
    ("v_line", "line"),
    ("v_branch", "branch"),
    ("v_expr", "expr"),
    ("v_toggle", "toggle"),
    ("v_user", "user"),
)


@dataclass
class VltItem:
    """One coverage point from a ``coverage.dat`` file."""
    attrs: Dict[str, str] = field(default_factory=dict)
    count: int = 0

    def get(self, key: str, default: str = "") -> str:
        return self.attrs.get(key, default)

    @property
    def kind(self) -> str:
        """Normalized coverage kind (``line``, ``toggle``, ``covergroup``, ...)."""
        page = self.attrs.get("page", "")
        for prefix, kind in _PAGE_KINDS:
            if page == prefix or page.startswith(prefix + "/"):
                return kind
        t = self.attrs.get("t", "")
        return "covergroup" if t == "funccov" else t

    @property
    def page_scope(self) -> str:
        """Module (or covergroup type) name from ``page``."""
        _, _, rest = self.attrs.get("page", "").partition("/")
        return rest

    @property
    def filename(self) -> str:
        return self.attrs.get("f", "")

    @property
    def line(self) -> int:
        return _to_int(self.attrs.get("l"))

    @property
    def col(self) -> int:
        return _to_int(self.attrs.get("n"))

    @property
    def hier(self) -> str:
        return self.attrs.get("h", "")

    @property
    def comment(self) -> str:
        return self.attrs.get("o", "")


def _to_int(s: Optional[str]) -> int:
    try:
        return int(s)
    except (TypeError, ValueError):
        return 0


def decode_keys(compact: str) -> Dict[str, str]:
    """Decode ``\\001k\\002v\\001k\\002v`` into a dict."""
    ret = {}
    for pair in compact.split("\001"):
        if not pair:
            continue
        key, sep, value = pair.partition("\002")
        if sep:
            ret[key] = value
    return ret


class VltParseError(Exception):
    pass


class VltParser:
    """Parses ``coverage.dat`` content into :class:`VltItem` objects.

    Lines that do not parse are recorded in :attr:`errors` as
    ``(lineno, text)`` rather than silently dropped.
    """

    def __init__(self):
        self.errors: List[tuple] = []
        self.header: Optional[str] = None

    def parse_file(self, path: str) -> List[VltItem]:
        with open(path, "r", encoding="utf-8", errors="replace") as fp:
            return self.parse(fp)

    def parse(self, lines: Iterable[str]) -> List[VltItem]:
        items = []
        for lineno, line in enumerate(lines, 1):
            line = line.rstrip("\r\n")
            if not line.strip():
                continue
            if line.startswith("#"):
                if self.header is None:
                    self.header = line
                continue
            item = self.parse_line(line)
            if item is None:
                self.errors.append((lineno, line))
            else:
                items.append(item)
        return items

    @staticmethod
    def parse_line(line: str) -> Optional[VltItem]:
        m = _LINE_RE.match(line)
        if m is None:
            return None
        return VltItem(decode_keys(m.group(1)), int(m.group(2)))


def is_vlt_coverage(path_or_fp) -> bool:
    """True when the content starts with a ``# SystemC::Coverage`` header."""
    if isinstance(path_or_fp, str):
        try:
            with open(path_or_fp, "rb") as fp:
                head = fp.read(64)
        except OSError:
            return False
    else:
        head = path_or_fp.read(64)
        if isinstance(head, str):
            head = head.encode()
    return head.lstrip().startswith(b"# SystemC::Coverage")
