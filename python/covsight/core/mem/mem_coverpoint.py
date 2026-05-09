'''
Created on Mar 22, 2020

@author: ballance
'''
from covsight.core.api.cover_index import CoverIndex
from covsight.core.api.coverpoint import Coverpoint
from covsight.core.mem.mem_cvg_scope import MemCvgScope
from covsight.core.api import ScopeTypeT, SourceInfo


class MemCoverpoint(MemCvgScope,Coverpoint):
    
    def __init__(self,
                 parent,
                 name : str,
                 srcinfo : SourceInfo,
                 weight : int,
                 source):
        MemCvgScope.__init__(self, parent, name, srcinfo, weight, source, 
                             ScopeTypeT.COVERPOINT, 0)
        Coverpoint.__init__(self)
        
        