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

"""UCIS typed-property access for toggle scopes.

Shared by both toggle scope classes. There are two because a toggle under a
DU-linked instance is an instance scope and a toggle elsewhere is a plain
scope, and only the plain one used to implement these properties -- so
``getIntProperty(UCIS_INT_TOGGLE_TYPE)`` raised ``UnimplError`` for every
toggle in a real design hierarchy, and the metric, type and direction never
reached any backend that reads through the property API. Keeping the
implementation in one place is what stops the two from drifting again.
"""

from covsight.core.api import CoverTypeT, IntProperty, StrProperty
from covsight.core.api import ToggleDirT, ToggleMetricT, ToggleTypeT


class ToggleProperties:
    """Maps the toggle accessors onto the UCIS typed-property interface.

    Expects the host class to provide ``_toggle_type``, ``_toggle_dir``,
    ``_toggle_metric``, ``_canonical_name`` and ``m_cover_items``.
    """

    def getIntProperty(self, coverindex, property):
        if property == IntProperty.TOGGLE_TYPE:
            return int(self._toggle_type)
        elif property == IntProperty.TOGGLE_DIR:
            return int(self._toggle_dir)
        elif property == IntProperty.TOGGLE_METRIC:
            return int(self._toggle_metric)
        elif property == IntProperty.TOGGLE_COVERED:
            # Derived from the counts, never stored: a toggle is covered when
            # both directions have been seen.
            return 1 if (self._toggled("0->1", "01")
                         and self._toggled("1->0", "10")) else 0
        return super().getIntProperty(coverindex, property)

    def setIntProperty(self, coverindex, property, value):
        if property == IntProperty.TOGGLE_TYPE:
            self._toggle_type = ToggleTypeT(value)
        elif property == IntProperty.TOGGLE_DIR:
            self._toggle_dir = ToggleDirT(value)
        elif property == IntProperty.TOGGLE_METRIC:
            self._toggle_metric = ToggleMetricT(value)
        else:
            super().setIntProperty(coverindex, property, value)

    def getStringProperty(self, coverindex, property):
        if property == StrProperty.TOGGLE_CANON_NAME:
            return self._canonical_name
        return super().getStringProperty(coverindex, property)

    def setStringProperty(self, coverindex, property, value):
        if property == StrProperty.TOGGLE_CANON_NAME:
            self._canonical_name = value
        else:
            super().setStringProperty(coverindex, property, value)

    # -- helpers ---------------------------------------------------------

    def _toggle_bins(self, *names):
        """Bin counts matching any of *names*.

        Goes through the public cover-item accessors rather than the private
        attributes: the two toggle scope classes hold different cover-item
        implementations (``m_data``/``m_name`` on one, ``data``/``name`` on the
        other), and reaching past the API is what made them diverge.
        """
        for item in self.coverItems(CoverTypeT.TOGGLEBIN):
            data = item.getCoverData()
            if data is None or data.type != CoverTypeT.TOGGLEBIN:
                continue
            name = item.getName() or ""
            if any(n in name for n in names):
                yield int(data.data or 0)

    def _toggled(self, *names) -> bool:
        return any(count > 0 for count in self._toggle_bins(*names))

    def _total_toggle(self, *names) -> int:
        return sum(self._toggle_bins(*names))
