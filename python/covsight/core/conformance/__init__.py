"""UCIS conformance machinery: the feature registry, test markers, doc claims.

Design rule: **humans commit intent, tools compute status.** The registry
(``docs/conformance/features/``) records which UCIS features each backend is
supposed to support and why anything is excluded. Test markers record which
features a test exercises. The mapping document records which features each
rule section carries. ``tools/gen_conformance_matrix.py`` joins the three into
``docs/ucis-parquet-feature-map.md``; nothing in that file is hand-written.

See ``docs/ucis-conformance-structure-plan.md``.
"""

from .ids import BadFeatureId, expand_glob, is_glob, is_id, parse, sort_key
from .marker import ucis_feature
from .registry import (
    BackendScope,
    Feature,
    Registry,
    RegistryError,
    Section,
    load,
    default_root,
    schema_path,
)

__all__ = [
    "BackendScope",
    "BadFeatureId",
    "Feature",
    "Registry",
    "RegistryError",
    "Section",
    "default_root",
    "expand_glob",
    "is_glob",
    "is_id",
    "load",
    "parse",
    "schema_path",
    "sort_key",
    "ucis_feature",
]
