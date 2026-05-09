'''
Created on Jan 12, 2020

@author: ballance
'''
from covsight.core.api import CoverData
from covsight.core.api.cover_item import CoverItem
from covsight.core.api import FlagsT
from covsight.core.api.instance_scope import InstanceScope
from covsight.core.api import IntProperty
from covsight.core.mem.mem_cover_item import MemCoverItem
from covsight.core.mem.mem_scope import MemScope
from covsight.core.api import ScopeTypeT
from covsight.core.api import SourceInfo
from covsight.core.api import SourceT
from covsight.core.api import ToggleDirT
from covsight.core.api import ToggleMetricT
from covsight.core.api import ToggleTypeT
from covsight.core.api.unimpl_error import UnimplError
from covsight.core.mem.mem_covergroup import MemCovergroup
from covsight.core.mem.mem_code_scope import MemBlockScope, MemBranchScope, MemToggleScope


class MemInstanceScope(MemScope,InstanceScope):
    
    def __init__(
            self,
            parent : 'MemInstanceScope',
            name : str,
            srcinfo : SourceInfo,
            weight : int,
            source : SourceT,
            type : ScopeTypeT,
            du_scope : 'MemScope',
            flags : FlagsT
            ):
        MemScope.__init__(self, parent, name, srcinfo, weight, source, type, flags)
        InstanceScope.__init__(self)
            
        self.m_du_scope = du_scope
        self.m_cover_item_l = []
        
    def getInstanceDu(self) -> 'Scope':
        return self.m_du_scope
        
    def createScope(self, 
        name:str, 
        srcinfo:SourceInfo, 
        weight:int, 
        source : SourceT, 
        type : ScopeTypeT, 
        flags : FlagsT) -> 'Scope':
        itype = int(type)
        if (itype & int(ScopeTypeT.COVERGROUP)) != 0:
            ret = MemCovergroup(self, name, srcinfo, weight, source)
        elif (itype & int(ScopeTypeT.BLOCK)) != 0:
            ret = MemBlockScope(self, name, srcinfo, weight, source, flags)
        elif (itype & int(ScopeTypeT.BRANCH)) != 0:
            ret = MemBranchScope(self, name, srcinfo, weight, source, flags)
        elif (itype & int(ScopeTypeT.TOGGLE)) != 0:
            ret = MemToggleScope(self, name, srcinfo, weight, source, flags)
        elif (itype & int(ScopeTypeT.FSM)) != 0:
            from covsight.core.mem.mem_fsm_scope import MemFSMScope
            ret = MemFSMScope(self, name, srcinfo, weight, source, flags)
        else:
            # Generic fallback for other scope types
            ret = MemScope(self, name, srcinfo, weight, source, type, flags)

        self.addChild(ret)        
        return ret

    def createNextCover(self, 
        name:str, 
        data:CoverData, 
        sourceinfo:SourceInfo)->int:
        ret = len(self.m_cover_item_l)
        ci = MemCoverItem(self, name, data, sourceinfo)
        self.m_cover_item_l.append(ci)
        # Also track in parent's m_cover_items for coverItems() iteration
        from covsight.core.mem.mem_cover_index import MemCoverIndex
        self.m_cover_items.append(MemCoverIndex(name, data, sourceinfo))
        return ret
    
    def createToggle(self,
                    name : str,
                    canonical_name : str,
                    flags : FlagsT,
                    toggle_metric : ToggleMetricT,
                    toggle_type : ToggleTypeT,
                    toggle_dir : ToggleDirT) -> 'Scope':
        from covsight.core.mem.mem_toggle_instance_scope import MemToggleInstanceScope
        ret = MemToggleInstanceScope(self, name, canonical_name,
                flags, toggle_metric, toggle_type, toggle_dir)
        self.addChild(ret)
        return ret
    
    def getIthCoverItem(self, i)->CoverItem:
        return self.m_cover_item_l[i]
   
    
