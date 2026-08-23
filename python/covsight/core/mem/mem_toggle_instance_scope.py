"""A toggle scope under a DU-linked instance.

Separate from ``MemToggleScope`` only because its base differs; the toggle
behaviour itself comes from the shared ``ToggleProperties`` mixin so the two
cannot drift.
"""

from covsight.core.mem.mem_instance_scope import MemInstanceScope
from covsight.core.mem.mem_toggle_props import ToggleProperties
from covsight.core.api import SourceT
from covsight.core.api import ScopeTypeT
from covsight.core.api import ToggleMetricT
from covsight.core.api import ToggleTypeT
from covsight.core.api import ToggleDirT

class MemToggleInstanceScope(ToggleProperties, MemInstanceScope):

    def __init__(self,
                parent,
                name,
                canonical_name,
                flags,
                toggle_metric=None,
                toggle_type=None,
                toggle_dir=None):
        super().__init__(parent, name, None, 0, SourceT.NONE, ScopeTypeT.TOGGLE, None, flags)
        self._canonical_name = canonical_name if canonical_name else name
        # Defaults match MemToggleScope: a caller that passes None gets the
        # same toggle as it would outside an instance, not a scope whose
        # properties raise on read.
        self._toggle_metric = (ToggleMetricT._2STOGGLE if toggle_metric is None
                               else toggle_metric)
        self._toggle_type = ToggleTypeT.NET if toggle_type is None else toggle_type
        self._toggle_dir = (ToggleDirT.INTERNAL if toggle_dir is None
                            else toggle_dir)
        self._num_bits = 1

    def getNumBits(self) -> int:
        return self._num_bits

    def setNumBits(self, n: int):
        self._num_bits = n

    def getCanonicalName(self) -> str:
        return self._canonical_name

    def setCanonicalName(self, name: str):
        self._canonical_name = name

    def getToggleMetric(self):
        return self._toggle_metric

    def setToggleMetric(self, metric):
        self._toggle_metric = metric

    def getToggleType(self):
        return self._toggle_type

    def setToggleType(self, ttype):
        self._toggle_type = ttype

    def getToggleDir(self):
        return self._toggle_dir

    def setToggleDir(self, dir):
        self._toggle_dir = dir