'''
Created on Mar 24, 2020

@author: ballance
'''
from typing import List
from covsight.core.api.cross import Cross
from covsight.core.api import (
    IntProperty, ScopeTypeT, SourceInfo, SourceT, StrProperty,
)
from covsight.core.api.unimpl_error import UnimplError
from covsight.core.mem.mem_coverpoint import MemCoverpoint

class MemCross(MemCoverpoint,Cross):
    
    def __init__(self,
                 parent,
                 name : str,
                 srcinfo : SourceInfo,
                 weight : int,
                 source : SourceT,
                 coverpoints : List['MemCoverpoint'] = None
                 ):
        MemCoverpoint.__init__(self, parent, name, srcinfo, weight, source)
        self.m_type = ScopeTypeT.CROSS
        Cross.__init__(self)
        
        self.coverpoints = coverpoints if coverpoints is not None else []
        
    def getNumCrossedCoverpoints(self)->int:
        if self.coverpoints:
            return len(self.coverpoints)
        # A cross read back from a source that could only state names holds no
        # coverpoint objects, but the name UCIS_STR_ITH_CROSSED_CVP_NAME
        # carries is still an operand.  Counting it keeps the two ways of
        # asking -- the property and the operand API -- from disagreeing.
        return 1 if self._stated_name() is not None else 0

    def getIthCrossedCoverpoint(self, index)->'Coverpoint':
        if self.coverpoints:
            return self.coverpoints[index]
        if index == 0 and self._stated_name() is not None:
            # Named but unresolved: the name is readable through the string
            # property, and inventing a coverpoint here would be worse.
            return None
        raise IndexError(
            "cross %r has no crossed coverpoint %d" % (self.m_name, index))

    def _stated_name(self):
        """The crossed-coverpoint name set explicitly, if any."""
        return super().getStringProperty(
            -1, StrProperty.ITH_CROSSED_CVP_NAME)

    def getIntProperty(self, coverindex, property)->int:
        if property == IntProperty.NUM_CROSSED_CVPS:
            return len(self.coverpoints)
        return super().getIntProperty(coverindex, property)

    def setIntProperty(self, coverindex, property, value):
        if property == IntProperty.NUM_CROSSED_CVPS:
            # Read-only in UCIS (§ 8.3.1): the count is a fact about the
            # operand list, and accepting a value that disagreed with it would
            # give the cross two answers to the same question.
            raise UnimplError(
                "UCIS_INT_NUM_CROSSED_CVPS is read-only; it follows the "
                "coverpoints passed to createCross()")
        return super().setIntProperty(coverindex, property, value)

    def getStringProperty(self, coverindex, property)->str:
        if property == StrProperty.ITH_CROSSED_CVP_NAME:
            # `coverindex` is the *i* of ucis_GetIthCrossedCvp here, not a
            # cover-bin index -- this property is the only one that reads it
            # that way, which is why it cannot ride the generic path.
            index = 0 if coverindex is None or coverindex < 0 else coverindex
            if index < len(self.coverpoints):
                return self.coverpoints[index].getScopeName()
            if self.coverpoints:
                return None
            # No operand list: fall back to whatever was set explicitly, which
            # is all a caller that has only the string property can express.
        return super().getStringProperty(coverindex, property)

    