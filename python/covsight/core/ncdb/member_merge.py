"""Member-level merge operations for the same-schema fast path.

``NcdbMerger`` merges two archives without decoding either into an object
graph -- that is what makes it fast.  The members it touches therefore have to
be merged *in their serialized form*, which is what this module does.

Members fall into three groups:

**Structural** (``scope_tree``, ``strings``, ``sources``, ``toggle``, ``fsm``,
``cross``, ``design_units``, ``properties``) describe the shape of the design.
The same-schema path is entered only when every source shares a
``schema_hash``, so these are identical by construction and are copied from the
first source.

**Measured** (``counts``, ``formal``) come *from a run* and must be combined:
counts add (except where the merge-ops member says otherwise), formal status
merges by the UCIS conflict rules.

**Annotational** (``attrs``, ``tags``, ``coveritem_flags``) are user- or
tool-applied and can legitimately differ between sources, so they union.
``coveritem_flags`` in particular carries exclusions and waivers -- dropping it
silently changes closure numbers, which is why it gets a real merge rather than
a copy.

Every function here takes a list of serialized member payloads (in source
order) and returns one payload, so the merger stays a byte-level operation.
"""

import json

from covsight.core.api.enums import merge_formal_statuses

from .varint import encode_varint, decode_varint

_FLAGS_VERSION = 1
_FORMAL_VERSION = 1
_TAGS_VERSION = 1


# --------------------------------------------------------------------------
# coveritem_flags.bin -- union
# --------------------------------------------------------------------------

def merge_coveritem_flags(payloads) -> bytes:
    """Union per-coveritem flags across sources (bitwise OR).

    Flags carry exclusion and waiver state.  A bin excluded in *any* source is
    excluded in the merge: exclusion is an assertion about the bin, not a
    measurement, so losing it would silently inflate coverage.
    """
    merged = {}
    for data in payloads:
        for index, flags in _decode_flags(data):
            merged[index] = merged.get(index, 0) | flags
    if not merged:
        return b""

    buf = bytearray()
    buf.extend(encode_varint(_FLAGS_VERSION))
    buf.extend(encode_varint(len(merged)))
    previous = 0
    for index in sorted(merged):
        buf.extend(encode_varint(index - previous))
        buf.extend(encode_varint(merged[index]))
        previous = index
    return bytes(buf)


def _decode_flags(data):
    """Yield ``(coveritem_index, flags)`` from a ``coveritem_flags.bin``."""
    if not data:
        return
    offset = 0
    version, offset = decode_varint(data, offset)
    if version != _FLAGS_VERSION:
        return
    count, offset = decode_varint(data, offset)
    previous = 0
    for _ in range(count):
        delta, offset = decode_varint(data, offset)
        flags, offset = decode_varint(data, offset)
        previous += delta
        yield previous, flags


# --------------------------------------------------------------------------
# formal.bin -- status by UCIS conflict rules
# --------------------------------------------------------------------------

def merge_formal(payloads) -> bytes:
    """Merge formal results across sources.

    Status uses :func:`~covsight.core.api.enums.merge_formal_statuses`, so a
    bin proved in one run and failed in another becomes ``CONFLICT`` rather
    than whichever run was written last.  Radius takes the maximum (the
    deepest proof reached) and the witness follows the winning status.
    """
    from covsight.core.api.enums import FormalStatusT, formal_status_rank

    collected = {}
    for data in payloads:
        if not data:
            continue
        try:
            payload = json.loads(data.decode())
        except (ValueError, UnicodeDecodeError):
            continue
        if payload.get("version") != _FORMAL_VERSION:
            continue
        for entry in payload.get("entries", []):
            collected.setdefault(entry["idx"], []).append(entry)

    entries = []
    for index in sorted(collected):
        found = collected[index]
        status = merge_formal_statuses(e.get("status", 0) for e in found)
        radius = max((e.get("radius", 0) or 0) for e in found)

        # The witness belongs to the run whose status won; on a conflict there
        # is no single winning run, so the first witness is kept for triage.
        witness = None
        if status == int(FormalStatusT.CONFLICT):
            witness = next((e.get("witness") for e in found
                            if e.get("witness")), None)
        else:
            best = None
            for entry in found:
                if entry.get("status", 0) != status:
                    continue
                if best is None or formal_status_rank(entry.get("status", 0)) \
                        > formal_status_rank(best.get("status", 0)):
                    best = entry
            witness = (best or {}).get("witness")

        merged = {"idx": index}
        if status:
            merged["status"] = status
        if radius:
            merged["radius"] = radius
        if witness is not None:
            merged["witness"] = witness
        if len(merged) > 1:
            entries.append(merged)

    if not entries:
        return b""
    return json.dumps({"version": _FORMAL_VERSION, "entries": entries},
                      separators=(',', ':')).encode()


# --------------------------------------------------------------------------
# tags.json -- union
# --------------------------------------------------------------------------

def merge_tags(payloads) -> bytes:
    """Union per-scope tags across sources."""
    merged = {}
    for data in payloads:
        if not data:
            continue
        try:
            payload = json.loads(data.decode())
        except (ValueError, UnicodeDecodeError):
            continue
        if payload.get("version") != _TAGS_VERSION:
            continue
        for entry in payload.get("entries", []):
            merged.setdefault(entry["idx"], set()).update(entry.get("tags", []))

    entries = [{"idx": index, "tags": sorted(merged[index])}
               for index in sorted(merged) if merged[index]]
    if not entries:
        return b""
    return json.dumps({"version": _TAGS_VERSION, "entries": entries},
                      separators=(',', ':')).encode()


# --------------------------------------------------------------------------
# attrs.bin -- union, first source wins a key collision
# --------------------------------------------------------------------------

def merge_attrs(payloads) -> bytes:
    """Union user-defined attributes across sources.

    On a key collision the first source wins, which is arbitrary but
    *deterministic* -- re-running a merge must not shuffle the answer.
    """
    scopes, coveritems, history = {}, {}, {}
    global_attrs = {}
    version = None

    for data in payloads:
        if not data:
            continue
        try:
            payload = json.loads(data.decode())
        except (ValueError, UnicodeDecodeError):
            continue
        version = payload.get("version", version)

        # v1 stored scope attrs under "entries"; v2 splits by object kind.
        for entry in payload.get("entries", []) + payload.get("scopes", []):
            _merge_into(scopes.setdefault(entry["idx"], {}),
                        entry.get("attrs", {}))
        for entry in payload.get("coveritems", []):
            key = (entry.get("scope_idx"), entry.get("ci_idx"))
            _merge_into(coveritems.setdefault(key, {}), entry.get("attrs", {}))
        for entry in payload.get("history", []):
            _merge_into(history.setdefault(entry["idx"], {}),
                        entry.get("attrs", {}))
        _merge_into(global_attrs, payload.get("global", {}) or {})

    if not (scopes or coveritems or history or global_attrs):
        return b""

    out = {"version": 2}
    if scopes:
        out["scopes"] = [{"idx": i, "attrs": scopes[i]}
                         for i in sorted(scopes) if scopes[i]]
    if coveritems:
        out["coveritems"] = [
            {"scope_idx": s, "ci_idx": c, "attrs": coveritems[(s, c)]}
            for (s, c) in sorted(coveritems) if coveritems[(s, c)]]
    if history:
        out["history"] = [{"idx": i, "attrs": history[i]}
                          for i in sorted(history) if history[i]]
    if global_attrs:
        out["global"] = global_attrs
    return json.dumps(out, separators=(',', ':')).encode()


def _merge_into(target: dict, incoming: dict):
    for key, value in (incoming or {}).items():
        target.setdefault(key, value)


# --------------------------------------------------------------------------
# contrib/ -- renumber, never collide
# --------------------------------------------------------------------------

def renumber_contrib(per_source_members, history_offsets) -> dict:
    """Re-key contribution members onto the merged history numbering.

    Contributions are named ``contrib/{history_idx}.bin``, and every
    single-run database numbers its history from zero -- so merging N sources
    collides every member with the same name.  Keeping a dict keyed by name
    does not merely lose the others: the survivor is read back under the
    *wrong test's* index, silently reattributing one run's coverage to another
    run's test.

    Each source's contributions are therefore shifted by that source's offset
    in the merged history list, which is exactly how the history nodes
    themselves are combined.

    Args:
        per_source_members: One ``{member_name: bytes}`` dict per source, in
            source order.
        history_offsets: The index at which each source's history nodes start
            in the merged history list, in the same order.

    Returns:
        ``{member_name: bytes}`` with no collisions.
    """
    out = {}
    for members, offset in zip(per_source_members, history_offsets):
        for name, data in members.items():
            index = _contrib_index(name)
            if index is None:
                continue
            out["contrib/%d.bin" % (index + offset)] = data
    return out


def _contrib_index(member_name):
    """The history index encoded in a ``contrib/N.bin`` name, or None."""
    if not member_name.startswith("contrib/"):
        return None
    basename = member_name[len("contrib/"):]
    if basename.endswith(".bin"):
        basename = basename[:-len(".bin")]
    try:
        return int(basename)
    except ValueError:
        return None
