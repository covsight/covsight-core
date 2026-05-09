from dataclasses import dataclass, field
from enum import IntFlag, auto
from covsight.core.api import UCIS


class FormatDbFlags(IntFlag):
    Create = auto()
    Read = auto()
    Write = auto()


@dataclass
class FormatCapabilities:
    """Documents what UCIS data model features a format can represent."""
    can_read: bool = False
    can_write: bool = False
    functional_coverage: bool = False
    cross_coverage: bool = False
    ignore_illegal_bins: bool = False
    code_coverage: bool = False
    toggle_coverage: bool = False
    fsm_coverage: bool = False
    assertions: bool = False
    history_nodes: bool = False
    design_hierarchy: bool = False
    lossless: bool = False


class FormatDescDb(object):

    def __init__(self,
                 fmt_if: 'FormatIfDb',
                 name: str,
                 flags: FormatDbFlags,
                 description: str,
                 capabilities: FormatCapabilities = None):
        self._fmt_if = fmt_if
        self._name = name
        self._flags = flags
        self._description = description
        self._capabilities = capabilities or FormatCapabilities()

    @property
    def fmt_if(self):
        return self._fmt_if

    @property
    def name(self):
        return self._name

    @property
    def flags(self):
        return self._flags

    @property
    def description(self):
        return self._description

    @property
    def capabilities(self) -> FormatCapabilities:
        return self._capabilities


class FormatIfDb(object):

    def init(self, options):
        raise NotImplementedError("FormatIfDb.init not implemented by %s" % str(type(self)))

    def create(self, filename=None) -> UCIS:
        raise NotImplementedError("FormatIfDb.create not implemented by %s" % str(type(self)))

    def read(self, file_or_filename) -> UCIS:
        raise NotImplementedError("FormatIfDb.read not implemented by %s" % str(type(self)))

    def write(self, db: UCIS, file_or_filename) -> None:
        raise NotImplementedError("FormatIfDb.write not implemented by %s" % str(type(self)))
