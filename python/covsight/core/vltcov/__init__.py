"""Verilator ``coverage.dat`` import (format name ``vltcov``)."""
from covsight.core.vltcov.vlt_parser import (
    VltItem, VltParser, decode_keys, is_vlt_coverage,
)
from covsight.core.vltcov.vlt_mapper import VltToUcis

#: Revision of the coverage.dat -> UCIS mapping.  Bumped whenever the same
#: input would produce a different database, so caches of converted output
#: (e.g. the covsight.Import dv-flow task) know to reconvert.
MAPPING_REVISION = 2


def read_vlt_coverage(path: str, include_std: bool = False,
                      test_name: str = None):
    """Read a ``coverage.dat`` file into a MemUCIS database."""
    parser = VltParser()
    items = parser.parse_file(path)
    mapper = VltToUcis(include_std=include_std)
    for lineno, text in parser.errors:
        mapper._warn("%s:%d: unparseable line: %.80s" % (path, lineno, text))
    return mapper.map(items, test_name=test_name, physical_name=path)
