"""Formal verification status enumeration (UCIS §8.19.3)."""

from enum import IntEnum


class FormalStatusT(IntEnum):
    NONE         = 0  # No formal info (default)
    FAILURE      = 1  # Assertion fails
    PROOF        = 2  # Proven to never fail
    VACUOUS      = 3  # Assertion is vacuous
    INCONCLUSIVE = 4  # Proof failed to complete
    ASSUMPTION   = 5  # Assertion is an assume
    CONFLICT     = 6  # Data merge conflict


#: How informative a status is, when two runs disagree without contradicting.
#: A completed proof beats an incomplete one; a status that carries no
#: information loses to one that does.
_INFORMATIVENESS = {
    FormalStatusT.NONE: 0,
    FormalStatusT.ASSUMPTION: 1,
    FormalStatusT.VACUOUS: 2,
    FormalStatusT.INCONCLUSIVE: 3,
    FormalStatusT.PROOF: 4,
    FormalStatusT.FAILURE: 5,
    FormalStatusT.CONFLICT: 6,
}

#: Statuses that are definitive claims about the assertion.  Two different
#: definitive claims cannot both be true, so combining them is a conflict.
_DEFINITIVE = frozenset({FormalStatusT.PROOF, FormalStatusT.FAILURE})


def merge_formal_statuses(statuses) -> int:
    """Merge formal statuses from several runs into one.

    Formal status is not additive and it is not a simple precedence either:
    UCIS reserves :attr:`FormalStatusT.CONFLICT` for exactly this situation, so
    a run that *proved* an assertion and a run that *failed* it merge to
    ``CONFLICT`` rather than silently letting one win.  Reporting a clean proof
    for an assertion some run observed failing would be the worst possible
    answer.

    Rules, in order:

    1. ``NONE`` carries no information and is ignored.
    2. If nothing is left, the result is ``NONE``.
    3. If the surviving statuses include more than one *definitive* claim
       (``PROOF`` and ``FAILURE``), the result is ``CONFLICT``.
    4. Otherwise the most informative status wins.

    Args:
        statuses: An iterable of ``FormalStatusT`` values or ints.

    Returns:
        The merged status, as an int.

    Example:
        >>> merge_formal_statuses([FormalStatusT.PROOF, FormalStatusT.PROOF])
        2
        >>> merge_formal_statuses([FormalStatusT.PROOF, FormalStatusT.FAILURE])
        6
        >>> merge_formal_statuses([FormalStatusT.NONE, FormalStatusT.INCONCLUSIVE])
        4
    """
    known = []
    for status in statuses:
        if status is None:
            continue
        try:
            value = FormalStatusT(int(status))
        except ValueError:
            continue
        if value != FormalStatusT.NONE:
            known.append(value)

    if not known:
        return int(FormalStatusT.NONE)
    if len(set(known) & _DEFINITIVE) > 1:
        return int(FormalStatusT.CONFLICT)
    return int(max(known, key=lambda s: _INFORMATIVENESS.get(s, 0)))


def formal_status_rank(status) -> int:
    """How informative *status* is; higher wins when there is no conflict."""
    try:
        return _INFORMATIVENESS.get(FormalStatusT(int(status)), 0)
    except (TypeError, ValueError):
        return 0
