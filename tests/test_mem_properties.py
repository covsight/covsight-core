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

"""
Tests for integer properties on cover items (IntProperty.COVER_GOAL, COVER_WEIGHT).
"""

import pytest
from covsight.core.api import (
    CoverData, CoverFlagsT, CoverTypeT, FlagsT, HistoryNodeKind,
    IntProperty, ScopeTypeT, SourceInfo, SourceT, TestStatusT,
    ToggleDirT, ToggleMetricT, ToggleTypeT,
)
from covsight.core.api.test_data import TestData
from covsight.core.mem import MemFactory

def _make_coverpoint(db):
    fh = db.createFileHandle("design.sv", "/rtl")
    du = db.createScope("work.dut", SourceInfo(fh, 1, 0),
                        1, SourceT.SV, ScopeTypeT.DU_MODULE,
                        FlagsT.SCOPE_UNDER_DU | FlagsT.INST_ONCE)
    inst = db.createInstance("dut", None, 1, SourceT.SV,
                             ScopeTypeT.INSTANCE, du, FlagsT.INST_ONCE)
    cg = inst.createCovergroup("cg", SourceInfo(fh, 5, 0), 1, SourceT.SV)
    cgi = cg.createCoverInstance("cg", SourceInfo(fh, 5, 0), 1, SourceT.SV)
    cp = cgi.createCoverpoint("cp", SourceInfo(fh, 6, 0), 1, SourceT.SV)
    return cp, fh

class TestApiCoverProperties:
    """Tests for IntProperty on coverpoints and cover items"""

    def test_coverpoint_goal(self, backend):
        """Coverpoint COVER_GOAL can be set and retrieved via IntProperty"""
        backend_name, create_db, write_db, read_db, temp_file = backend
        if backend_name == "xml":
            pytest.skip("XML backend IntProperty on coverpoints not supported")
        db = create_db()
        cp, _ = _make_coverpoint(db)
        cp.setAtLeast(10)
        assert cp.getAtLeast() == 10

    def test_coverpoint_weight(self, backend):
        """Coverpoint weight can be set and retrieved"""
        backend_name, create_db, write_db, read_db, temp_file = backend
        db = create_db()
        cp, _ = _make_coverpoint(db)
        cp.setWeight(5)
        assert cp.getWeight() == 5

    def test_cover_bin_data_properties(self, backend):
        """Cover bin CoverData goal and count are accessible"""
        backend_name, create_db, write_db, read_db, temp_file = backend
        if backend_name == "xml":
            pytest.skip("XML backend does not support cover bin data write")
        db = create_db()
        cp, fh = _make_coverpoint(db)
        cd = CoverData(CoverTypeT.CVGBIN, 0)
        cd.data = 7
        cd.goal = 4
        bin_item = cp.createNextCover("bin_a", cd, SourceInfo(fh, 7, 0))
        assert bin_item.getCoverData().data == 7
        assert bin_item.getCoverData().goal == 4
