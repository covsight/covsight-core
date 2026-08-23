"""UCIS-XML schema validation against the UCIS 1.0 XSD.

Replaces pyucis's `ucis/xml/__init__.py`, which validated through `lxml`'s
`XMLSchema`. Core has no lxml, so this uses `xmlschema` -- the same validator
`tests/ucis_writer` already runs the C writer's output through, which means one
implementation decides "valid" for both writers.

`xmlschema` is an optional dependency: reading and writing UCIS-XML work
without it, and only explicit validation requires it.
"""

from __future__ import annotations

import io
import os
import xml.etree.ElementTree as ET
from typing import Any

_SCHEMA_PATH = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "schema", "ucis.xsd")

_schema = None


class UcisXmlValidationError(Exception):
    """A document did not validate against the UCIS 1.0 schema."""


def _load_schema():
    global _schema
    if _schema is None:
        try:
            import xmlschema
        except ImportError as e:
            raise ImportError(
                "UCIS-XML validation requires the 'xmlschema' package; "
                "install covsight-core with the [validate] extra"
            ) from e
        _schema = xmlschema.XMLSchema(_SCHEMA_PATH)
    return _schema


def strip_namespaces(root: ET.Element) -> ET.Element:
    """Remove namespace qualification from every tag in the tree.

    The published schema says elements should be namespace-qualified; the
    published *examples* are not, and real tools emit both. Rather than accept
    one and reject the other, qualification is dropped before validation --
    which is what pyucis did, and what the reader does on its own input.
    """
    for elem in root.iter():
        if not isinstance(elem.tag, str):
            continue          # comments and processing instructions
        i = elem.tag.find('}')
        if i >= 0:
            elem.tag = elem.tag[i + 1:]
    return root


def validate_ucis_xml(file_or_filename: Any) -> bool:
    """Validate a UCIS-XML document. Returns True, or raises.

    Raises UcisXmlValidationError with the schema's own diagnostic on failure,
    and ImportError if `xmlschema` is not installed.
    """
    schema = _load_schema()

    tree = ET.parse(file_or_filename)
    strip_namespaces(tree.getroot())

    # Validate the namespace-stripped tree rather than the file on disk, so
    # that a qualified document is judged by the same rules as a bare one.
    buf = io.BytesIO()
    tree.write(buf, encoding="utf-8", xml_declaration=True)
    buf.seek(0)

    try:
        schema.validate(buf)
    except Exception as e:                     # xmlschema raises several types
        raise UcisXmlValidationError(str(e)) from e
    return True
