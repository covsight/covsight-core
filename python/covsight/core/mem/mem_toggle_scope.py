# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#  http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

"""In-memory toggle coverage scope."""

from covsight.core.mem.mem_scope import MemScope
from covsight.core.mem.mem_toggle_props import ToggleProperties
from covsight.core.api import ScopeTypeT
from covsight.core.api import SourceT
from covsight.core.api import FlagsT
from covsight.core.api import ToggleDirT
from covsight.core.api import ToggleMetricT
from covsight.core.api import ToggleTypeT
from covsight.core.api import IntProperty
from covsight.core.api import StrProperty


class MemToggleScope(ToggleProperties, MemScope):
    """In-memory implementation of a toggle coverage scope.

    Represents a single signal with toggle coverage tracking.
    Bins are created via createNextCover() with TOGGLEBIN cover type.
    """

    def __init__(self, parent, name, srcinfo, weight, source, flags=0):
        super().__init__(parent, name, srcinfo, weight, source,
                         ScopeTypeT.TOGGLE, flags)
        self._canonical_name = name
        self._toggle_metric = ToggleMetricT._2STOGGLE
        self._toggle_type = ToggleTypeT.NET
        self._toggle_dir = ToggleDirT.INTERNAL
        self._num_bits = 1

    # --- Canonical name ---

    def getCanonicalName(self) -> str:
        return self._canonical_name

    def setCanonicalName(self, name: str):
        self._canonical_name = name

    # --- Toggle metric, type, direction ---

    def getToggleMetric(self) -> ToggleMetricT:
        return self._toggle_metric

    def setToggleMetric(self, metric: ToggleMetricT):
        self._toggle_metric = metric

    def getToggleType(self) -> ToggleTypeT:
        return self._toggle_type

    def setToggleType(self, t: ToggleTypeT):
        self._toggle_type = t

    def getToggleDir(self) -> ToggleDirT:
        return self._toggle_dir

    def setToggleDir(self, d: ToggleDirT):
        self._toggle_dir = d

    def getNumBits(self) -> int:
        return self._num_bits

    def setNumBits(self, n: int):
        self._num_bits = n

    # --- Aggregate counts from cover items ---

    def getTotalToggle01(self) -> int:
        """Sum of all 0->1 transition counts across bins."""
        return self._total_toggle("0->1", "01")

    def getTotalToggle10(self) -> int:
        """Sum of all 1->0 transition counts across bins."""
        return self._total_toggle("1->0", "10")
