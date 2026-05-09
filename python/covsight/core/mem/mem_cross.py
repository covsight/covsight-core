'''
Created on Mar 24, 2020

@author: ballance
'''
from typing import List
from covsight.core.api.cross import Cross
from covsight.core.api import ScopeTypeT, SourceInfo, SourceT
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
        return len(self.coverpoints)
    
    def getIthCrossedCoverpoint(self, index)->'Coverpoint':
        return self.coverpoints[index]
    
    