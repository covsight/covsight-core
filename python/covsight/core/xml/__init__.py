"""UCIS-XML support -- a first-class format of covsight-core.

Promoted from the `covsight-xml` plugin package that MIGRATION_FROM_PYUCIS.md
originally planned. UCIS-XML is the format integrators produce precisely
because it is a published standard they can code against without reading our
documentation, which makes it the front door rather than one backend among
several. A front door should not be an optional install, and unlike Parquet
(pyarrow) or Iceberg (pyiceberg) this one needs no dependency to justify the
split -- reading and writing use only the standard library.
"""

from covsight.core.xml.validator import (
    UcisXmlValidationError,
    validate_ucis_xml,
)
from covsight.core.xml.xml_reader import XmlReader

__all__ = [
    "UcisXmlValidationError",
    "XmlReader",
    "validate_ucis_xml",
]
