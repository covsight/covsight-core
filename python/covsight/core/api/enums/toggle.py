'''
Created on Jan 12, 2020

@author: ballance
'''
from enum import IntEnum, auto

class ToggleMetricT(IntEnum):
    """Toggle coverage metric types.
    
    Defines the type of toggle metric being measured. Different metrics track
    different aspects of signal transitions and state changes.
    
    Metric types:
    - **NOBINS**: Scope has no local bins (hierarchical container)
    - **ENUM**: Enumerated state tracking (UCIS:ENUM)
    - **TRANSITION**: State transition tracking (UCIS:TRANSITION)
    - **_2STOGGLE**: 2-state toggle (0→1, 1→0) (UCIS:2STOGGLE)
    - **ZTOGGLE**: High-impedance toggle tracking (UCIS:ZTOGGLE)
    - **XTOGGLE**: Unknown value toggle tracking (UCIS:XTOGGLE)
    
    Example:
        >>> # Standard 2-state toggle
        >>> toggle = instance.createToggle(
        ...     name="data_bus",
        ...     srcinfo=src_info,
        ...     canonicalName="top.data",
        ...     metric=ToggleMetricT._2STOGGLE)
        >>>
        >>> # Tri-state signal with Z tracking
        >>> toggle = instance.createToggle(
        ...     name="tristate_bus",
        ...     srcinfo=src_info,
        ...     canonicalName="top.ts_data",
        ...     metric=ToggleMetricT.ZTOGGLE)
        
    See Also:
        ToggleTypeT: Toggle type (NET vs REG)
        ToggleDirT: Signal direction
        UCIS LRM Section 6.6 "Toggle Coverage"
    """
    
    NOBINS     = 1
    """Toggle scope has no local bins (hierarchical container only)."""
    
    ENUM       = auto()
    """Enumerated state tracking (UCIS:ENUM)."""
    
    TRANSITION = auto()
    """State transition tracking (UCIS:TRANSITION)."""
    
    _2STOGGLE  = auto()
    """2-state toggle: 0→1 and 1→0 transitions (UCIS:2STOGGLE)."""
    
    ZTOGGLE    = auto()
    """High-impedance toggle tracking (UCIS:ZTOGGLE)."""
    
    XTOGGLE    = auto()
    """Unknown value toggle tracking (UCIS:XTOGGLE)."""
'''
Created on Jan 12, 2020

@author: ballance
'''
from enum import IntEnum

class ToggleTypeT(IntEnum):
    """Toggle coverage type enumeration.
    
    Defines the type of signal being measured for toggle coverage. Toggle
    coverage tracks transitions (0→1 and 1→0) on digital signals to ensure
    all bits in a design are exercised.
    
    Toggle types distinguish between:
    - **NET**: Continuous assignment signals (wires, nets)
    - **REG**: Registered signals (registers, flip-flops)
    
    This distinction is important because nets and registers have different
    behavioral characteristics and may require different coverage strategies.
    
    Example:
        >>> # Create toggle coverage for a register
        >>> toggle_scope = instance.createToggle(
        ...     name="data_reg",
        ...     srcinfo=src_info,
        ...     canonicalName="top.u_core.data_reg",
        ...     toggleType=ToggleTypeT.REG)
        >>>
        >>> # Create toggle coverage for a wire
        >>> toggle_scope = instance.createToggle(
        ...     name="ready_wire",
        ...     srcinfo=src_info,
        ...     canonicalName="top.u_core.ready",
        ...     toggleType=ToggleTypeT.NET)
        >>>
        >>> # Query toggle type
        >>> ttype = toggle_scope.getToggleType()
        >>> if ttype == ToggleTypeT.REG:
        ...     print("Measuring registered signal")
        
    See Also:
        Scope.createToggle(): Create toggle coverage scopes
        ScopeTypeT.TOGGLE: Toggle scope type
        CoverTypeT.TOGGLEBIN: Toggle bin coverage type
        UCIS LRM Section 6.6 "Toggle Coverage"
    """
    
    NET = 1
    """
    Net/wire signal (continuous assignment).
    
    Toggle coverage for combinational signals, wires, and continuous
    assignments. These signals change value through continuous assignments
    rather than being clocked into registers.
    """
    
    REG = 2
    """
    Registered signal (flip-flop, latch).
    
    Toggle coverage for sequential signals that are stored in flip-flops
    or latches. These signals are typically clocked and hold state.
    """
'''
Created on Jan 12, 2020

@author: ballance
'''
from enum import IntEnum, auto

class ToggleDirT(IntEnum):
    """Toggle signal direction enumeration.
    
    Defines the direction or port type of signals being measured for toggle
    coverage. Direction affects how toggle coverage is analyzed and reported.
    
    Signal directions include:
    - **INTERNAL**: Internal wires or variables (non-port)
    - **IN**: Input ports
    - **OUT**: Output ports  
    - **INOUT**: Bidirectional ports
    
    Example:
        >>> # Create toggle for output port
        >>> toggle_scope = instance.createToggle(
        ...     name="data_out",
        ...     srcinfo=src_info,
        ...     canonicalName="top.data_out",
        ...     direction=ToggleDirT.OUT)
        >>>
        >>> # Internal signal
        >>> toggle_scope = instance.createToggle(
        ...     name="state_reg",
        ...     srcinfo=src_info,
        ...     canonicalName="top.u_core.state",
        ...     direction=ToggleDirT.INTERNAL)
        
    See Also:
        ToggleTypeT: Toggle type (NET vs REG)
        ScopeTypeT.TOGGLE: Toggle scope type
        UCIS LRM Section 6.6 "Toggle Coverage"
    """
    
    INTERNAL = 1
    """Non-port signal: internal wire or variable within a module."""
    
    IN = auto()
    """Input port signal."""
    
    OUT = auto()
    """Output port signal."""
    
    INOUT = auto()
    """Bidirectional (inout) port signal."""
