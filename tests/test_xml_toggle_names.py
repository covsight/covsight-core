"""UCIS-XML toggle bins use the canonical names shared with other importers:
``<from>-><to>`` for a scalar, ``<bit>:<from>-><to>`` for a vector bit."""
from covsight.core.api.enums import CoverTypeT, ScopeTypeT
from covsight.core.xml.xml_reader import XmlReader

XML = """<?xml version="1.0" encoding="utf-8"?>
<UCIS ucisVersion="1.0" writtenBy="t" writtenTime="2026-01-01T00:00:00">
<sourceFiles fileName="rtl/fifo.sv" id="1"/>
<historyNodes historyNodeId="0" logicalName="run1" testStatus="true"
  date="2026-01-01T00:00:00" toolCategory="UCIS:simulator" ucisVersion="1.0"
  vendorId="t" vendorTool="t" vendorToolVersion="1.0"/>
<instanceCoverages name="top" key="top" instanceId="1" moduleName="fifo">
<id file="1" line="3" inlineCount="1"/>
<toggleCoverage>
 <toggleObject name="valid" key="valid"><id file="1" line="9" inlineCount="1"/>
  <toggleBit name="0" key="0">
   <toggle from="0" to="1"><bin><contents coverageCount="4"/></bin></toggle>
   <toggle from="1" to="0"><bin><contents coverageCount="0"/></bin></toggle>
  </toggleBit>
 </toggleObject>
 <toggleObject name="data" key="data"><id file="1" line="10" inlineCount="1"/>
  <toggleBit name="0" key="0">
   <toggle from="0" to="1"><bin><contents coverageCount="1"/></bin></toggle>
   <toggle from="1" to="0"><bin><contents coverageCount="2"/></bin></toggle>
  </toggleBit>
  <toggleBit name="1" key="1">
   <toggle from="0" to="1"><bin><contents coverageCount="3"/></bin></toggle>
   <toggle from="1" to="0"><bin><contents coverageCount="0"/></bin></toggle>
  </toggleBit>
 </toggleObject>
</toggleCoverage>
</instanceCoverages>
</UCIS>
"""


def test_toggle_bin_names(tmp_path):
    path = tmp_path / "t.xml"
    path.write_text(XML)
    db = XmlReader().read(str(path))
    (inst,) = [s for s in db.scopes(ScopeTypeT.INSTANCE)]
    bins = {s.getScopeName(): {c.getName(): c.getCoverData().data
                               for c in s.coverItems(CoverTypeT.ALL)}
            for s in inst.scopes(ScopeTypeT.TOGGLE)}
    assert bins == {
        "valid": {"0->1": 4, "1->0": 0},
        "data": {"0:0->1": 1, "0:1->0": 2, "1:0->1": 3, "1:1->0": 0},
    }
