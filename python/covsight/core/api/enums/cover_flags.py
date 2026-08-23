'''
Created on Jan 11, 2020

@author: ballance
'''
from enum import IntFlag

#: Layout of ``ucisFlagsT`` (UCIS 1.0 § 8.11), by bit range.
COVERITEMMASK_GENERAL = 0x0000FFFF   #: apply to every coveritem type
COVERITEMMASK_TYPED = 0x07FF0000     #: meaning depends on the coveritem's type
COVERITEMMASK_MARK = 0x08000000      #: one temporary-mark flag
COVERITEMMASK_USER = 0xF0000000      #: four flags reserved for user extension
FLAG_MASK = 0xFFFFFFFF


class CoverFlagsT(IntFlag):
    """General coveritem flags — the ``0x0000FFFF`` range of ``ucisFlagsT``.

    These apply to **every** coveritem type, so they can live in one flat enum.
    The ``0x07FF0000`` range cannot: UCIS says outright that "flag locations may
    be reused for non-intersecting type sets", so bit ``0x00010000`` means
    ``IS_FSM_RESET`` on an FSM bin, ``IS_BR_ELSE`` on a branch bin and
    ``BIN_IFF_EXISTS`` on a covergroup bin. Those live in the per-type enums
    below, because collapsing them into one enum would make three different
    facts indistinguishable.

    See Also:
        CoverData: Coverage data container using these flags
        UCIS LRM Section 8.11 "Coveritem Creation and Manipulation"
    """

    # -- data format (which member of the value union is live) -----------

    IS_32BIT = 0x00000001
    """Coverage data is 32-bit integer."""

    IS_64BIT = 0x00000002
    """Coverage data is 64-bit integer."""

    IS_VECTOR = 0x00000004
    """Coverage data is a bit vector (use the bitlen field)."""

    # -- optional fields -------------------------------------------------

    HAS_GOAL = 0x00000008
    """Goal field is included and meaningful."""

    HAS_WEIGHT = 0x00000010
    """Weight field is included and meaningful."""

    HAS_LIMIT = 0x00000400
    """Count-saturation limit is included."""

    HAS_COUNT = 0x00000800
    """A count is present in the cover-data value."""

    # -- exclusion -------------------------------------------------------
    #
    # Four separate reasons, deliberately not one bit: a coverage report has to
    # distinguish "the user excluded this" from "the tool did", and a merge has
    # to union them without losing which is which.

    EXCLUDE_PRAGMA = 0x00000020
    """Excluded by a source pragma."""

    EXCLUDE_FILE = 0x00000040
    """Excluded by an exclusion file; does not count in total coverage."""

    EXCLUDE_INST = 0x00000080
    """Instance-specific exclusion."""

    EXCLUDE_AUTO = 0x00000100
    """Automatic (tool-generated) exclusion."""

    CLEAR_PRAGMA = 0x00004000
    """A pragma exclusion that has been cleared."""

    # -- status ----------------------------------------------------------

    ENABLED = 0x00000200
    """Generic enabled flag; if disabled, still counts in total coverage."""

    IS_COVERED = 0x00001000
    """The coveritem has met its goal."""

    UOR_SAFE_COVERITEM = 0x00002000
    """Coveritem construction is Universal-Object-Recognition compliant."""

    # -- mark and user extension -----------------------------------------

    COVERFLAG_MARK = 0x08000000
    """Temporary mark, for traversal algorithms. Not persistent state."""


#: Any exclusion, from any source (UCIS_EXCLUDED). A consumer asking "is this
#: bin excluded?" wants this, not an individual reason.
EXCLUDED = (CoverFlagsT.EXCLUDE_FILE | CoverFlagsT.EXCLUDE_PRAGMA
            | CoverFlagsT.EXCLUDE_INST | CoverFlagsT.EXCLUDE_AUTO)


# --------------------------------------------------------------------------
# Type-qualified flags (0x07FF0000)
#
# One enum per non-intersecting coveritem-type set, because the *same bit*
# carries a different meaning in each. Interpreting these without knowing the
# coveritem's ``cover_type`` is not merely lossy, it is wrong.
# --------------------------------------------------------------------------

class AssertCoverFlagsT(IntFlag):
    """Type-qualified flags for ``UCIS_ASSERTBIN`` and ``UCIS_COVERBIN``."""

    HAS_ACTION = 0x00010000
    """ASSERTBIN: the assertion has an action block."""

    IS_TLW_ENABLED = 0x00020000
    """ASSERTBIN: transaction-level waveform logging enabled."""

    LOG_ON = 0x00040000
    """COVERBIN / ASSERTBIN: logging is on."""

    IS_EOS_NOTE = 0x00080000
    """COVERBIN / ASSERTBIN: end-of-simulation note."""


class FsmCoverFlagsT(IntFlag):
    """Type-qualified flags for ``UCIS_FSMBIN``."""

    IS_FSM_RESET = 0x00010000
    """The bin is the FSM's reset state."""

    IS_FSM_TRAN = 0x00020000
    """The bin is a transition rather than a state."""


class BranchCoverFlagsT(IntFlag):
    """Type-qualified flags for ``UCIS_BRANCHBIN``."""

    IS_BR_ELSE = 0x00010000
    """The bin is the else arm of a branch."""


class CvgBinCoverFlagsT(IntFlag):
    """Type-qualified flags for covergroup bins.

    Applies to ``UCIS_CVGBIN``, ``UCIS_IGNOREBIN``, ``UCIS_ILLEGALBIN`` and
    ``UCIS_DEFAULTBIN``.
    """

    BIN_IFF_EXISTS = 0x00010000
    """The bin carries an `iff` guard."""

    BIN_SAMPLE_TRUE = 0x00020000
    """The bin's sample condition was true."""


class CrossCoverFlagsT(IntFlag):
    """Type-qualified flags for bins of a ``UCIS_CROSS`` scope."""

    IS_CROSSAUTO = 0x00040000
    """The cross bin was generated automatically rather than declared."""


#: ``cover_type`` -> the enum that interprets its type-qualified bits. A reader
#: that has a coveritem's type can resolve ``flags & COVERITEMMASK_TYPED``
#: through this; without the type, those bits are uninterpretable.
def typed_flags_for(cover_type):
    """Return the type-qualified flag enum for *cover_type*, or None.

    None means the type defines no type-qualified flags, so the
    ``0x07FF0000`` bits should be zero and are not interpretable if they are not.
    """
    from covsight.core.api.enums.cover_type import CoverTypeT

    if cover_type in (CoverTypeT.ASSERTBIN, CoverTypeT.COVERBIN):
        return AssertCoverFlagsT
    if cover_type == CoverTypeT.FSMBIN:
        return FsmCoverFlagsT
    if cover_type == CoverTypeT.BRANCHBIN:
        return BranchCoverFlagsT
    if cover_type in (CoverTypeT.CVGBIN, CoverTypeT.IGNOREBIN,
                      CoverTypeT.ILLEGALBIN, CoverTypeT.DEFAULTBIN):
        return CvgBinCoverFlagsT
    return None
