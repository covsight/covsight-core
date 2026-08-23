// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// ux_text.hpp - turning caller data into legal XML text.
//
// Task I-1.3. Two things here are performance decisions rather than taste: the
// integer formatter is hand-rolled instead of snprintf, and escaping copies
// spans rather than characters (design §5.4). At ~100 bytes per point and up to
// 5.2 M points the emitter formats around 500 MB of text, and both of those
// showed up as the dominant cost in the C writer.
//
// The rest is correctness the caller should never have to think about
// (design §5.5): XML 1.0 forbids most control characters *even as numeric
// references*, so escaping & < > " is not sufficient for names carrying
// arbitrary bytes, and xsd:positiveInteger cannot be zero.
#pragma once

#include "ux_buf.hpp"

namespace UCIS_XML_NAMESPACE {

// Counts of everything the writer had to change on the caller's behalf. All of
// these are silent repairs of input that would otherwise produce an invalid
// document, so they are counted and reported rather than ignored.
//
// These count repairs *performed*, not distinct inputs repaired: a name that
// appears as both @name and @key is escaped twice and so counts twice. The
// number is a "did anything need fixing, and roughly how much" signal, and it
// is zero exactly when nothing was repaired -- which is what the fixture suite
// asserts.
struct Sanitize {
    uint64_t controlChars;  // replaced with '?'
    uint64_t badUtf8;       // malformed sequences replaced with '?'
    uint64_t clampedInts;   // positiveInteger fields raised to 1

    Sanitize() : controlChars(0), badUtf8(0), clampedInts(0) {}

    uint64_t total() const { return controlChars + badUtf8 + clampedInts; }
};

namespace detail {

enum : uint8_t {
    kPass = 0,
    kAmp = 1,
    kLt = 2,
    kGt = 3,
    kQuot = 4,
    kControl = 5,  // illegal in XML 1.0 in any form
    kNumRef = 6,   // legal, but must be a numeric reference to survive
                   // attribute-value normalization
    kHighBit = 7,  // start of a UTF-8 sequence, validated when enabled
};

struct EscapeTable {
    uint8_t c[256];

    constexpr EscapeTable() : c() {
        for (int i = 0; i < 256; ++i) c[i] = kPass;
        // C0 controls. \t \n \r are legal XML characters; the rest are not
        // representable at all and get replaced.
        for (int i = 0; i < 0x20; ++i) c[i] = kControl;
        c[0x09] = kNumRef;
        c[0x0A] = kNumRef;
        c[0x0D] = kNumRef;
        c['&'] = kAmp;
        c['<'] = kLt;
        c['>'] = kGt;
        c['"'] = kQuot;
#if UCIS_XML_SANITIZE_UTF8
        for (int i = 0x80; i < 0x100; ++i) c[i] = kHighBit;
#endif
    }
};

inline constexpr EscapeTable kEscape{};

// Length in bytes of the UTF-8 sequence led by `b`, or 0 if `b` cannot lead one.
inline uint32_t utf8Len(unsigned char b) {
    if (b < 0xC2) return 0;  // 0x80-0xBF are continuations; 0xC0/0xC1 overlong
    if (b < 0xE0) return 2;
    if (b < 0xF0) return 3;
    if (b < 0xF5) return 4;
    return 0;
}

// Validates the sequence starting at p (length n available). Returns its length
// on success, 0 if it is malformed and should be replaced.
inline uint32_t utf8Valid(const unsigned char* p, size_t n) {
    uint32_t need = utf8Len(p[0]);
    if (need == 0 || need > n) return 0;
    for (uint32_t i = 1; i < need; ++i)
        if ((p[i] & 0xC0) != 0x80) return 0;
    // Surrogates and over-long 3-byte forms.
    if (need == 3 && p[0] == 0xE0 && p[1] < 0xA0) return 0;
    if (need == 3 && p[0] == 0xED && p[1] >= 0xA0) return 0;
    if (need == 4 && p[0] == 0xF0 && p[1] < 0x90) return 0;
    if (need == 4 && p[0] == 0xF4 && p[1] >= 0x90) return 0;
    return need;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Integers.
// ---------------------------------------------------------------------------

// Longest uint64_t is 20 digits; longest int64_t is 20 with the sign.
inline void writeUInt(Out& o, uint64_t v) {
    char tmp[20];
    int n = 0;
    do {
        tmp[n++] = static_cast<char>('0' + (v % 10));
        v /= 10;
    } while (v);
    if (!o.need(static_cast<size_t>(n))) return;
    while (n) o.rawPut(tmp[--n]);
}

inline void writeInt(Out& o, int64_t v) {
    if (v < 0) {
        o.put('-');
        // Negating INT64_MIN is undefined; go through the unsigned domain.
        writeUInt(o, static_cast<uint64_t>(-(v + 1)) + 1);
    } else {
        writeUInt(o, static_cast<uint64_t>(v));
    }
}

// xsd:positiveInteger admits no zero, and tools do report line 0 for generated
// constructs. Clamping is the only option that keeps the document valid; it is
// counted so the caller can find out (design §5.5).
inline uint32_t positive(uint32_t v, Sanitize& s) {
    if (v == 0) { ++s.clampedInts; return 1; }
    return v;
}

// ---------------------------------------------------------------------------
// Escaping.
//
// The loop finds the next byte needing attention and memcpy's everything before
// it. HDL identifiers and file paths are overwhelmingly escape-free, so the
// common case is one memcpy per string.
// ---------------------------------------------------------------------------
inline void writeEscaped(Out& o, Text t, Sanitize& s) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(t.data);
    size_t n = t.size;
    size_t i = 0, span = 0;

    while (i < n) {
        uint8_t cls = detail::kEscape.c[p[i]];
        if (cls == detail::kPass) { ++i; continue; }

        if (cls == detail::kHighBit) {
            uint32_t seq = detail::utf8Valid(p + i, n - i);
            if (seq) { i += seq; continue; }
            // Malformed: fall through and replace this one byte.
        }

        if (i > span) o.write(t.data + span, i - span);

        switch (cls) {
        case detail::kAmp:  o.lit("&amp;"); break;
        case detail::kLt:   o.lit("&lt;"); break;
        case detail::kGt:   o.lit("&gt;"); break;
        case detail::kQuot: o.lit("&quot;"); break;
        case detail::kNumRef:
            // Only \t \n \r reach here, so a three-way literal beats formatting
            // -- and avoids the decimal/hex mix-up that a shared path invites.
            if (p[i] == 0x09) o.lit("&#x9;");
            else if (p[i] == 0x0A) o.lit("&#xA;");
            else o.lit("&#xD;");
            break;
        case detail::kControl:
            o.put('?');
            ++s.controlChars;
            break;
        default:  // malformed UTF-8 byte
            o.put('?');
            ++s.badUtf8;
            break;
        }
        span = ++i;
    }
    if (n > span) o.write(t.data + span, n - span);
}

// ---------------------------------------------------------------------------
// Attribute and element helpers. The renderers are almost entirely calls to
// these, so keeping them tight keeps the renderers readable.
// ---------------------------------------------------------------------------

inline void attr(Out& o, const char* name, size_t nameLen, Text value, Sanitize& s) {
    o.put(' ');
    o.write(name, nameLen);
    o.lit("=\"");
    writeEscaped(o, value, s);
    o.put('"');
}

template <size_t N>
inline void attr(Out& o, const char (&name)[N], Text value, Sanitize& s) {
    attr(o, name, N - 1, value, s);
}

template <size_t N>
inline void attrU(Out& o, const char (&name)[N], uint64_t value) {
    o.put(' ');
    o.write(name, N - 1);
    o.lit("=\"");
    writeUInt(o, value);
    o.put('"');
}

template <size_t N>
inline void attrI(Out& o, const char (&name)[N], int64_t value) {
    o.put(' ');
    o.write(name, N - 1);
    o.lit("=\"");
    writeInt(o, value);
    o.put('"');
}

template <size_t N>
inline void attrBool(Out& o, const char (&name)[N], bool value) {
    o.put(' ');
    o.write(name, N - 1);
    o.write(value ? "=\"true\"" : "=\"false\"", value ? 7 : 8);
}

}  // namespace UCIS_XML_NAMESPACE
