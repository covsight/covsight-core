"""UCIS-XML format plugin -- ships with covsight-core (always available)."""
from covsight.core.ext.format_db import (
    FormatCapabilities, FormatDbFlags, FormatDescDb, FormatIfDb,
)
from covsight.core.api import UCIS


class XmlFormatPlugin:
    @staticmethod
    def describe() -> FormatDescDb:
        return FormatDescDb(
            name="xml",
            fmt_if=XmlFormatIf(),
            # Read only until the writer port lands; the C writer
            # (c/ucis-writer) is the write path for integrators today.
            flags=FormatDbFlags.Read,
            description="UCIS 1.0 XML interchange format",
            capabilities=FormatCapabilities(
                can_read=True, can_write=False,
                functional_coverage=True, cross_coverage=True,
                ignore_illegal_bins=True,
                code_coverage=True, toggle_coverage=True,
                fsm_coverage=True, assertions=True,
                history_nodes=True, design_hierarchy=True,
                # UCIS-XML is an interchange format, not covsight's own: it has
                # no place for waivers, testplan links, or formal-status detail
                # beyond what Chapter 9 defines. Round-tripping through it is
                # lossy for anything the schema does not name.
                lossless=False,
            ),
        )


class XmlFormatIf(FormatIfDb):
    def read(self, file_or_filename) -> UCIS:
        from covsight.core.xml.xml_reader import XmlReader
        return XmlReader().read(file_or_filename)
