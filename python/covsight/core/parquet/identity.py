"""Stable identity for scopes and cover bins.

UCIS handles (``ucisScopeT``) are process-local pointers.  The only identity
that survives a round-trip, a merge, or a backend swap is a *derived* one, so
both ids below are pure functions of the coverage structure -- never a row
autonumber, never a Python ``hash()`` (which is salted per process).
"""

#: Separator used when composing a scope path into a unique id.
PATH_SEP = "/"

#: Separator between a scope path and its type discriminator.
TYPE_SEP = "#"


def derive_scope_uid(parent_uid, name, scope_type) -> str:
    """Derive a scope ``unique_id`` from its position and type.

    UCIS requires ``(name, type)`` to be unique among siblings, so the
    parent path plus the type discriminator is unique by construction.

    Args:
        parent_uid: Parent's unique id, or None/"" for a top-level scope.
        name: Local scope name.
        scope_type: ``ScopeTypeT`` value (or int).

    Returns:
        A stable unique-id string.
    """
    prefix = parent_uid or ""
    return "%s%s%s%s%x" % (prefix, PATH_SEP, name or "", TYPE_SEP,
                           int(scope_type))


def scope_uid(scope, parent_uid) -> str:
    """The unique id to store for *scope*.

    Honours ``UCIS_STR_UNIQUE_ID`` when the backend supplies one, otherwise
    derives it.  A backend-supplied id is preferred because it is what a tool
    would use to re-open the handle by identity.
    """
    from covsight.core.api import StrProperty
    try:
        uid = scope.getStringProperty(-1, StrProperty.UNIQUE_ID)
    except Exception:
        uid = None
    if uid:
        return uid
    return derive_scope_uid(parent_uid, scope.getScopeName(),
                            scope.getScopeType())


def coveritem_natural_key(scope_unique_id: str, local_index: int) -> tuple:
    """The natural key of a cover bin: ``(scope unique_id, local_index)``.

    Both halves are stored as columns on ``coveritems``, so this is the
    identity that survives a *definition change* -- matching a bin between two
    tool versions, or against a database whose bin set has shifted.
    """
    return (scope_unique_id, local_index)


class CoveritemIdAllocator:
    """Assigns dense ``coveritem_id`` surrogates in definition order.

    A digest of the natural key would also be stable, but it is *incompressible*
    -- measured on a 1.2M-bin design, two columns of 63-bit hashes cost ~20 MB
    of a 24 MB dataset, because random integers defeat both delta encoding and
    the compressor.  Dense ordinals delta-encode to almost nothing.

    Nothing is lost by this: the surrogate only has to be stable across the
    writes and merges of one definition set, and definition equality is
    *enforced* before any merge (see ``DefinitionMismatch``).  Identity that has
    to outlive a definition change uses :func:`coveritem_natural_key`, whose
    columns are stored alongside.

    Ids are non-negative so engines without unsigned types cannot misread them.
    """

    def __init__(self):
        self._next = 0
        self._by_key = {}

    def allocate(self, scope_unique_id: str, local_index: int) -> int:
        key = coveritem_natural_key(scope_unique_id, local_index)
        if key in self._by_key:
            raise IdCollision(
                "cover bin %r appears twice; (scope, local_index) is the "
                "natural key and must be unique" % (key,))
        self._by_key[key] = self._next
        self._next += 1
        return self._by_key[key]

    def lookup(self, scope_unique_id: str, local_index: int):
        """The id assigned to a natural key, or None."""
        return self._by_key.get(
            coveritem_natural_key(scope_unique_id, local_index))


def history_node_id(run_id: str, local_index: int, logical_name) -> str:
    """Derive a globally-unique history-node id.

    History nodes are run-scoped, so the run id is part of the identity: two
    runs may both contain a test called ``smoke`` without colliding.
    """
    return "%s%s%d:%s" % (run_id, PATH_SEP, local_index, logical_name or "")


class IdCollision(Exception):
    """Raised when two distinct objects derive the same id.

    A silent collision would corrupt every merge that followed, so it must be
    loud rather than merely unlikely.
    """


class UidAllocator:
    """Assigns scope unique-ids, guaranteeing PK uniqueness.

    Derived ids are unique by construction, and backend-supplied ids normally
    are too -- but the two can be mixed within one database, so uniqueness is
    enforced rather than assumed.
    """

    def __init__(self):
        self._seen = {}

    def allocate(self, scope, parent_uid: str) -> str:
        uid = scope_uid(scope, parent_uid)
        if uid in self._seen:
            # Disambiguate deterministically on first-seen order.
            base, n = uid, 1
            while uid in self._seen:
                uid = "%s~%d" % (base, n)
                n += 1
        self._seen[uid] = scope
        return uid
