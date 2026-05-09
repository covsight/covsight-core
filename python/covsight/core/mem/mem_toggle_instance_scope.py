from covsight.core.mem.mem_instance_scope import MemInstanceScope
from covsight.core.api import SourceT
from covsight.core.api import ScopeTypeT
from covsight.core.api import ToggleMetricT
from covsight.core.api import ToggleTypeT
from covsight.core.api import ToggleDirT

class MemToggleInstanceScope(MemInstanceScope):
    
    def __init__(self,
                parent,
                name,
                canonical_name,
                flags,
                toggle_metric,
                toggle_type,
                toggle_dir):
        super().__init__(parent, name, None, 0, SourceT.NONE, ScopeTypeT.TOGGLE, None, flags)
        self._canonical_name = canonical_name if canonical_name else name
        self._toggle_metric = toggle_metric
        self._toggle_type = toggle_type
        self._toggle_dir = toggle_dir

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