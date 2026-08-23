// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// ux_key.hpp - generating the @key attribute the schema requires nearly
// everywhere.
//
// Task I-1.8, decision D-3. @key is not schema-constrained, but it is merge
// identity downstream, so generating it is a real decision rather than a
// convenience (design §4.1).
//
// The rule: the key is the caller-visible name. That is what makes two runs of
// the same design merge. Two things that carry one name inside one container
// are a genuine ambiguity, so the second gets "#2", the third "#3", and a
// warning is counted -- silently merging them would be worse, and the fixture
// suite asserts the count is zero.
//
// Not chosen: hashing the fact tuple. It produces keys that are stable but
// meaningless to a human reading a diff, and the diff is where merge-identity
// bugs are actually found.
//
// Keys are generated during rendering, which happens after the scope's records
// have been sorted into their content-derived order, so the suffix a given fact
// receives does not depend on the order the caller emitted facts in.
#pragma once

#include "ux_arena.hpp"

namespace UCIS_XML_NAMESPACE {

// One key namespace: a scope, a toggle object, a covergroup instance. Cleared
// and reused as the renderer moves between containers.
class KeyGen {
public:
    // Returns the occurrence number of `name` in this namespace: 1 the first
    // time, 2 the second, and so on. Anything above 1 is a collision.
    uint32_t occurrence(Text name, Status& st) {
        // The map stores a slot index rather than the count itself: getOrAdd
        // cannot update a value in place, so the running count lives in a side
        // vector and the map remembers where.
        bool inserted = false;
        uint32_t slot = bumps_.len;
        uint32_t got = counts_.getOrAdd(arena_, name, slot, &inserted);
        if (got == StrMap::kAbsent) { st.fail(Err::OutOfMemory); return 1; }
        if (inserted) {
            if (bumps_.push(1) == UINT32_MAX) { st.fail(Err::OutOfMemory); return 1; }
            return 1;
        }
        uint32_t n = ++bumps_[got];
        st.noteKeyCollision();
        return n;
    }

    // Writes `name`, plus "#N" when this is the Nth occurrence.
    void writeKey(Out& o, Text name, Status& st) {
        uint32_t n = occurrence(name, st);
        writeEscaped(o, name, st.sanitize);
        if (n > 1) {
            o.put('#');
            writeUInt(o, n);
        }
    }

    void clear() {
        counts_.release();
        arena_.release();
        bumps_.release();
    }

private:
    StrArena arena_;
    StrMap counts_;
    Vec<uint32_t> bumps_;
};

}  // namespace UCIS_XML_NAMESPACE
