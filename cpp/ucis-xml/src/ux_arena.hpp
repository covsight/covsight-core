// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// ux_arena.hpp - the string arena and the string-keyed map the staging layer is
// built on.
//
// Task I-1.7 (first half; the record vectors are in ux_stage.hpp). Both are
// hand-rolled for the reason given in design §9.4: the core must never grow a
// std:: member, and a std::unordered_map<std::string, uint32_t> here would be
// both the compile-time cost and the per-node allocation this design is trying
// to avoid.
#pragma once

#include "ux_error.hpp"

namespace UCIS_XML_NAMESPACE {

// A string stored in a StrArena. Str{0,0} is the empty string; offset 0 is
// reserved so that a default-constructed Str is safely empty.
struct Str {
    uint32_t off;
    uint32_t len;

    Str() : off(0), len(0) {}
    Str(uint32_t o, uint32_t l) : off(o), len(l) {}

    bool empty() const { return len == 0; }
};

// ---------------------------------------------------------------------------
// StrArena: append-only character storage.
//
// Text returned by get() points into the arena's buffer, which moves when the
// arena grows. That is safe for the way the writer uses it -- every string is
// copied in during staging, and nothing reads until staging for that scope is
// complete -- but it does mean a Text from get() must not be held across an
// add().
// ---------------------------------------------------------------------------
class StrArena {
public:
    StrArena() { buf_.push('\0'); }

    Str add(Text t) {
        if (t.size == 0) return Str();
        uint32_t off = buf_.len;
        if (!buf_.reserve(buf_.len + static_cast<uint32_t>(t.size))) return Str();
        std::memcpy(buf_.ptr + off, t.data, t.size);
        buf_.len += static_cast<uint32_t>(t.size);
        return Str(off, static_cast<uint32_t>(t.size));
    }

    Text get(Str s) const { return Text(buf_.ptr + s.off, s.len); }

    int compare(Str a, Str b) const { return UCIS_XML_NAMESPACE::compare(get(a), get(b)); }
    bool equal(Str a, Str b) const { return UCIS_XML_NAMESPACE::equal(get(a), get(b)); }

    void clear() { buf_.len = 1; }
    void release() { buf_.release(); buf_.push('\0'); }

    bool oom() const { return buf_.oom; }
    size_t bytes() const { return buf_.bytes(); }
    uint32_t size() const { return buf_.len; }

private:
    Vec<char> buf_;
};

inline uint64_t hashText(Text t) {
    // FNV-1a. Not a strong hash, but the keys are identifiers and paths and the
    // map is only ever used for exact-match lookup.
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < t.size; ++i) {
        h ^= static_cast<unsigned char>(t.data[i]);
        h *= 1099511628211ull;
    }
    return h;
}

// ---------------------------------------------------------------------------
// StrMap: open-addressed string -> uint32 map over a StrArena.
//
// Linear probing with a power-of-two capacity kept at most half full. Values
// are uint32 ids; UINT32_MAX means absent, so it cannot be stored.
// ---------------------------------------------------------------------------
class StrMap {
public:
    static const uint32_t kAbsent = UINT32_MAX;

    StrMap() : count_(0) {}

    // Returns the existing value for `t`, or inserts `value` and returns it.
    // `inserted` reports which happened. Strings are copied into `arena`.
    uint32_t getOrAdd(StrArena& arena, Text t, uint32_t value, bool* inserted) {
        if (!grow(arena)) { if (inserted) *inserted = false; return kAbsent; }
        uint64_t h = hashText(t);
        uint32_t mask = slots_.len - 1;
        uint32_t i = static_cast<uint32_t>(h) & mask;
        while (slots_[i].value != kAbsent) {
            if (slots_[i].hash == h && UCIS_XML_NAMESPACE::equal(arena.get(slots_[i].key), t)) {
                if (inserted) *inserted = false;
                return slots_[i].value;
            }
            i = (i + 1) & mask;
        }
        Slot s;
        s.hash = h;
        s.key = arena.add(t);
        s.value = value;
        slots_[i] = s;
        ++count_;
        if (inserted) *inserted = true;
        return value;
    }

    uint32_t find(const StrArena& arena, Text t) const {
        if (slots_.len == 0) return kAbsent;
        uint64_t h = hashText(t);
        uint32_t mask = slots_.len - 1;
        uint32_t i = static_cast<uint32_t>(h) & mask;
        while (slots_[i].value != kAbsent) {
            if (slots_[i].hash == h && UCIS_XML_NAMESPACE::equal(arena.get(slots_[i].key), t))
                return slots_[i].value;
            i = (i + 1) & mask;
        }
        return kAbsent;
    }

    uint32_t count() const { return count_; }
    bool oom() const { return slots_.oom; }

    void clear() {
        for (uint32_t i = 0; i < slots_.len; ++i) slots_[i] = Slot();
        count_ = 0;
    }

    void release() { slots_.release(); count_ = 0; }

private:
    struct Slot {
        uint64_t hash;
        Str key;
        uint32_t value;
        Slot() : hash(0), value(kAbsent) {}
    };

    bool grow(StrArena& arena) {
        if (slots_.len && count_ * 2 < slots_.len) return true;
        uint32_t want = slots_.len ? slots_.len * 2 : 64;

        Vec<Slot> next;
        if (!next.reserve(want)) return false;
        next.len = want;
        for (uint32_t i = 0; i < want; ++i) next[i] = Slot();

        uint32_t mask = want - 1;
        for (uint32_t i = 0; i < slots_.len; ++i) {
            if (slots_[i].value == kAbsent) continue;
            uint32_t j = static_cast<uint32_t>(slots_[i].hash) & mask;
            while (next[j].value != kAbsent) j = (j + 1) & mask;
            next[j] = slots_[i];
        }
        // Hand the buffers over; Vec's destructor frees whichever ends up in
        // `next`, which after the swap is the old table.
        Slot* p = slots_.ptr; uint32_t l = slots_.len, c = slots_.cap;
        slots_.ptr = next.ptr; slots_.len = next.len; slots_.cap = next.cap;
        next.ptr = p; next.len = l; next.cap = c;
        (void)arena;
        return true;
    }

    Vec<Slot> slots_;
    uint32_t count_;
};

}  // namespace UCIS_XML_NAMESPACE
