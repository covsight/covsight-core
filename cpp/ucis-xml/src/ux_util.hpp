// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// ux_util.hpp - the small vocabulary types the rest of the writer is built from:
// a string view, a POD dynamic array, a source location, and the enums that
// appear in the caller-facing API.
//
// Task I-1.10. Nothing here allocates except Vec, and Vec is deliberately a
// realloc over malloc rather than std::vector: the no-std::-member invariant of
// design §9.4 is what keeps a header-only writer cheap to compile.
#pragma once

#include "ux_config.hpp"

// <initializer_list> is a core-language requirement -- a braced-init-list needs
// it -- so it is not part of what UCIS_XML_NO_STL turns off. <string> is.
#include <initializer_list>

#if !UCIS_XML_NO_STL
#include <string>
#endif

namespace UCIS_XML_NAMESPACE {

/**
 * A non-owning view of caller string data -- the string type of the whole API.
 *
 * Converts implicitly from ``const char*``, from an explicit
 * ``(pointer, length)`` pair, and from ``std::string`` unless
 * ``UCIS_XML_NO_STL`` is set. The length form is the one to use for text that
 * is not NUL-terminated, such as a field sliced out of a parse buffer.
 *
 * Everything passed in is copied into the recording arena before the call
 * returns, so a ``Text`` never has to outlive the call it appears in. It is
 * safe to build one over a buffer you are about to overwrite.
 */
struct Text {
    /** Start of the character data; never null, empty strings point at "". */
    const char* data;
    /** Length in bytes, not characters. */
    size_t size;

    /** Constructs an empty string. */
    Text() : data(""), size(0) {}

    /**
     * Constructs from a NUL-terminated string.
     *
     * :param s: The string; ``nullptr`` is treated as empty.
     */
    Text(const char* s) : data(s ? s : ""), size(s ? std::strlen(s) : 0) {}

    /**
     * Constructs from a pointer and an explicit length.
     *
     * :param s: Start of the data; ``nullptr`` is treated as empty.
     * :param n: Length in bytes. The data need not be NUL-terminated.
     */
    Text(const char* s, size_t n) : data(s ? s : ""), size(s ? n : 0) {}

#if !UCIS_XML_NO_STL
    /**
     * Constructs from a ``std::string``. Unavailable under ``UCIS_XML_NO_STL``.
     *
     * :param s: The string.
     */
    Text(const std::string& s) : data(s.c_str()), size(s.size()) {}
#endif

    /** :return: ``true`` if the string has no characters. */
    bool empty() const { return size == 0; }
};

inline int compare(Text a, Text b) {
    size_t n = a.size < b.size ? a.size : b.size;
    int c = n ? std::memcmp(a.data, b.data, n) : 0;
    if (c) return c;
    return a.size < b.size ? -1 : (a.size > b.size ? 1 : 0);
}

inline bool equal(Text a, Text b) {
    return a.size == b.size && (a.size == 0 || std::memcmp(a.data, b.data, a.size) == 0);
}

// ---------------------------------------------------------------------------
// Vec<T>: a growable array of trivially copyable T.
//
// Deliberately minimal. `push` can fail (out of memory); rather than propagate
// a status through every call site, a failed Vec latches `oom` and drops the
// element, and the writer checks `oom` once per scope. Losing coverage records
// on an out-of-memory condition is bad, but taking down a simulation is worse,
// and design §5.5 is explicit that this path may not abort.
// ---------------------------------------------------------------------------
template <typename T>
struct Vec {
    T* ptr;
    uint32_t len;
    uint32_t cap;
    bool oom;

    Vec() : ptr(nullptr), len(0), cap(0), oom(false) {}
    ~Vec() { std::free(ptr); }

    Vec(const Vec&) = delete;
    Vec& operator=(const Vec&) = delete;

    void clear() { len = 0; }

    void release() {
        std::free(ptr);
        ptr = nullptr;
        len = cap = 0;
        oom = false;
    }

    bool reserve(uint32_t want) {
        if (want <= cap) return true;
        uint32_t n = cap ? cap : 16;
        while (n < want) {
            // Overflow guard: a scope that needs 2^31 records has bigger
            // problems, but silently wrapping would be a heap overflow.
            if (n > 0x7fffffffu) { oom = true; return false; }
            n *= 2;
        }
        T* p = static_cast<T*>(std::realloc(ptr, static_cast<size_t>(n) * sizeof(T)));
        if (!p) { oom = true; return false; }
        ptr = p;
        cap = n;
        return true;
    }

    // Returns the index of the appended element, or UINT32_MAX on failure.
    uint32_t push(const T& v) {
        if (len == cap && !reserve(len + 1)) return UINT32_MAX;
        ptr[len] = v;
        return len++;
    }

    T& operator[](uint32_t i) { return ptr[i]; }
    const T& operator[](uint32_t i) const { return ptr[i]; }

    size_t bytes() const { return static_cast<size_t>(cap) * sizeof(T); }
};

// ---------------------------------------------------------------------------
// Sorting.
//
// std::sort would pull in <algorithm> and a comparator-template instantiation
// per record type; this is a plain merge sort over an index array, which is
// what the renderers want anyway (they walk records in sorted order without
// moving them, so a record's index stays a stable handle for the whole scope).
//
// Merge sort rather than quicksort because it is stable: two facts that compare
// equal keep their emission order, which makes duplicate facts render in a
// defined order instead of an arbitrary one.
// ---------------------------------------------------------------------------
template <typename Less>
inline void mergeSortIndex(uint32_t* idx, uint32_t* scratch, uint32_t n, Less less) {
    if (n < 2) return;
    for (uint32_t width = 1; width < n; width *= 2) {
        for (uint32_t lo = 0; lo < n; lo += 2 * width) {
            uint32_t mid = lo + width < n ? lo + width : n;
            uint32_t hi = lo + 2 * width < n ? lo + 2 * width : n;
            uint32_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi)
                scratch[k++] = less(idx[j], idx[i]) ? idx[j++] : idx[i++];
            while (i < mid) scratch[k++] = idx[i++];
            while (j < hi) scratch[k++] = idx[j++];
        }
        for (uint32_t i = 0; i < n; ++i) idx[i] = scratch[i];
    }
}

// ---------------------------------------------------------------------------
// Caller-facing enums and small structs.
// ---------------------------------------------------------------------------

/**
 * A source position: a path, a line, and which inlined copy it is.
 *
 * ``line`` and ``inlineCount`` are ``xsd:positiveInteger`` in the schema, so a
 * zero is clamped to 1 with a warning rather than producing an invalid
 * document. Tools do report line 0 for generated constructs.
 */
struct Loc {
    /** Source path, interned against the file table. */
    Text file;
    /** 1-based line number. */
    uint32_t line;
    /** Which inlined copy of the construct this is. */
    uint32_t inlineCount;

    /** Constructs an empty location: no file, line 1, first copy. */
    Loc() : line(1), inlineCount(1) {}

    /**
     * Constructs a location in the first inlined copy.
     *
     * :param f: Source path.
     * :param l: 1-based line number.
     */
    Loc(Text f, uint32_t l) : file(f), line(l), inlineCount(1) {}

    /**
     * Constructs a fully specified location.
     *
     * :param f: Source path.
     * :param l: 1-based line number.
     * :param ic: Which inlined copy this is.
     */
    Loc(Text f, uint32_t l, uint32_t ic) : file(f), line(l), inlineCount(ic) {}
};

/**
 * Which direction a bit toggled.
 *
 * ``TOGGLE`` carries free-form ``@from``/``@to`` strings; these are the two a
 * code-coverage tool actually reports. For anything else -- four-state values,
 * a tool with its own encoding -- use the :cpp:func:`Scope::toggle` overload
 * taking explicit endpoints.
 */
enum class Edge : uint8_t {
    /** A 0 to 1 transition. */
    Rise,
    /** A 1 to 0 transition. */
    Fall,
};

/** What kind of coverpoint bin this is, emitted as ``COVERPOINT_BIN/@type``. */
enum class BinType : uint8_t {
    /** An ordinary bin that counts towards coverage. */
    Bins,
    /** Catches samples no other bin claims. */
    Default,
    /** Samples landing here count towards nothing. */
    Ignore,
    /** Sampling here is an error, not coverage. */
    Illegal,
};

/** How a user attribute's value should be interpreted, emitted as ``@type``. */
enum class AttrType : uint8_t {
    /** A signed integer. */
    Int,
    /** A single-precision float. */
    Float,
    /** A double-precision float. */
    Double,
    /** Text; the default everywhere it is optional. */
    Str,
    /** A bit vector. */
    Bits,
    /** A 64-bit signed integer. */
    Int64,
};

/**
 * The seven per-kind coverage containers a scope can hold, in the order
 * ``INSTANCE_COVERAGE`` declares them.
 *
 * Used by :cpp:func:`Scope::metricMode` and :cpp:func:`Scope::weight`, which
 * the schema puts on the container rather than on individual items.
 */
enum class CovKind : uint8_t {
    /** ``toggleCoverage``. */
    Toggle = 0,
    /** ``blockCoverage``: statements, blocks or processes. */
    Block,
    /** ``conditionCoverage``: expressions. */
    Condition,
    /** ``branchCoverage``. */
    Branch,
    /** ``fsmCoverage``. */
    Fsm,
    /** ``assertionCoverage``. */
    Assertion,
    /** ``covergroupCoverage``. */
    Covergroup,
    /** Not a kind; the number of kinds, for sizing arrays. */
    Count,
};

/**
 * How the source-file table is built.
 *
 * The schema puts ``sourceFiles+`` before everything, so file ids must exist
 * before the first scope is written. This is the one genuine "you must do a
 * pass first" constraint in the format, and it is a named choice rather than a
 * hidden default.
 */
enum class Sources : uint8_t {
    /**
     * Hand over a file list before the first scope. The default.
     *
     * A superset is fine -- unreferenced ``sourceFiles`` entries are
     * schema-valid -- so this is the compile file list you already have, not a
     * pre-scan of your coverage data. The only mode with no overhead at all.
     */
    UpFront,
    /**
     * Intern paths as they are seen and write the table at ``close()``.
     *
     * Asks nothing of the caller, at the cost of spooling scope bodies until
     * the table is known.
     */
    Deferred,
};

inline const char* toString(BinType t) {
    switch (t) {
    case BinType::Default: return "default";
    case BinType::Ignore:  return "ignore";
    case BinType::Illegal: return "illegal";
    default:               return "bins";
    }
}

inline const char* toString(AttrType t) {
    switch (t) {
    case AttrType::Int:    return "int";
    case AttrType::Float:  return "float";
    case AttrType::Double: return "double";
    case AttrType::Bits:   return "bits";
    case AttrType::Int64:  return "int64";
    default:               return "str";
    }
}

}  // namespace UCIS_XML_NAMESPACE
