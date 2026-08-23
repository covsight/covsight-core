// ucis_xml.hpp - schema-valid UCIS-XML coverage output, in one header.
//
// GENERATED FILE - DO NOT EDIT.
// Regenerate with:
//   python3 tools/amalgamate.py --manifest cpp/ucis-xml/amalgam.toml
//                               --output cpp/ucis-xml/include/ucis_xml.hpp
// Sources: cpp/ucis-xml/src/
//
// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// Header-only and dependency-free. Requires C++17. Include it anywhere; there
// is no implementation macro and nothing to link.
//
//     #include "ucis_xml.hpp"
//     namespace ux = ucisxml;
//
//     ux::CoverageWriter cov;
//     cov.openFile("coverage.xml");
//     cov.tool(ux::Tool().name("mytool").version("1.0").vendorId("MINE"));
//     cov.test(ux::Test().name("run1").passed(true));
//     cov.sources({"rtl/alu.sv"});
//     {
//         ux::Scope s = cov.scope("top.u_alu", "alu");
//         s.line("rtl/alu.sv", 42, 17);
//     }
//     if (!cov.close()) fprintf(stderr, "%s\n", cov.error());

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <cstdio>
#include <new>
#include <ctime>

// ==== ux_config.hpp ===========================================================

// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// ux_config.hpp - compile-time configuration. Every knob is #ifndef-guarded so a
// consumer can set it on the command line or before including the header.
//
// See docs/ucis-xml-cpp-impl-plan.md task I-1.1.


// The namespace everything lives in. A consumer vendoring two versions of this
// header into one binary renames one of them.
#ifndef UCIS_XML_NAMESPACE
#define UCIS_XML_NAMESPACE ucisxml
#endif

// Version of the writer itself, not of UCIS.
#define UCIS_XML_VERSION_MAJOR 0
#define UCIS_XML_VERSION_MINOR 1
#define UCIS_XML_VERSION_PATCH 0
#define UCIS_XML_VERSION_STRING "0.1.0"

// The UCIS revision whose Chapter 9 schema this emits.
#define UCIS_XML_UCIS_VERSION "1.0"

// Output buffer size. The sink is called once per full buffer, so this is the
// I/O granularity, not a limit on anything.
#ifndef UCIS_XML_BUFSZ
#define UCIS_XML_BUFSZ (64u * 1024u)
#endif

// Contract violations (design §5.5) are latched as errors and every subsequent
// call becomes a no-op -- a coverage write must never take a simulation down.
// The test suite defines UCIS_XML_ASSERT so violations abort loudly instead.
#ifndef UCIS_XML_ASSERT
#define UCIS_XML_HAVE_ASSERT 0
#else
#undef UCIS_XML_HAVE_ASSERT
#define UCIS_XML_HAVE_ASSERT 1
#endif

// std::string / std::string_view conveniences at the API boundary. Define
// UCIS_XML_NO_STL to build with no C++ standard library headers at all beyond
// the C ones above; the core never had a std:: member either way (design §9.4).
#ifndef UCIS_XML_NO_STL
#define UCIS_XML_NO_STL 0
#endif

// FILE*-backed sinks and the spool's temp-file spill.
#ifndef UCIS_XML_NO_STDIO
#define UCIS_XML_NO_STDIO 0
#endif

// A gzip sink built on zlib. Off by default: the header has no dependencies
// unless the consumer asks for one (design §5.4).
#ifndef UCIS_XML_ENABLE_ZLIB
#define UCIS_XML_ENABLE_ZLIB 0
#endif

// Replace malformed UTF-8 sequences in caller strings. Control-character
// sanitization is unconditional -- XML 1.0 forbids those outright -- but a
// caller that guarantees valid UTF-8 can skip this scan.
#ifndef UCIS_XML_SANITIZE_UTF8
#define UCIS_XML_SANITIZE_UTF8 1
#endif

// ==== ux_util.hpp =============================================================

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


// <initializer_list> is a core-language requirement -- a braced-init-list needs
// it -- so it is not part of what UCIS_XML_NO_STL turns off. <string> is.

#if !UCIS_XML_NO_STL
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

// ==== ux_buf.hpp ==============================================================

// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// ux_buf.hpp - the output side: a Sink function pointer and the buffered writer
// that calls it.
//
// Task I-1.2. The sink is a C function pointer called once per full buffer
// (design §5.4), which is why compression can stay outside the header: a caller
// with zlib already linked wraps gzwrite in ~15 lines, and a caller without one
// pays nothing for the option.


#if !UCIS_XML_NO_STDIO
#endif

namespace UCIS_XML_NAMESPACE {

/**
 * Where the document's bytes go: a C function pointer and its context.
 *
 * Called once per full 64 KiB buffer, so the indirection costs nothing and
 * compression stays outside the header. A caller with zlib already linked
 * wraps ``gzwrite`` in about fifteen lines:
 *
 * .. code-block:: cpp
 *
 *    static int gzSink(void* ctx, const char* p, size_t n) {
 *        return gzwrite(static_cast<gzFile>(ctx), p, n) == (int)n ? 0 : 1;
 *    }
 *    ux::Sink sink(gzfile, &gzSink);
 *
 * A sink that reports failure latches :cpp:enumerator:`Err::SinkFailed`, and
 * the writer stops calling it -- continuing to call a broken sink several
 * thousand more times helps nobody.
 */
struct Sink {
    /** Passed back to ``write`` unchanged. */
    void* ctx;
    /** Writes ``size`` bytes from ``data``; returns 0 on success. */
    int (*write)(void* ctx, const char* data, size_t size);

    /** Constructs an unusable sink; opening a writer with it fails. */
    Sink() : ctx(nullptr), write(nullptr) {}

    /**
     * Constructs a sink from a callback and its context.
     *
     * :param c: Context pointer, passed back on every call.
     * :param w: The write callback; returns 0 on success.
     */
    Sink(void* c, int (*w)(void*, const char*, size_t)) : ctx(c), write(w) {}

    /** :return: ``true`` if a write callback was supplied. */
    bool valid() const { return write != nullptr; }
};

// ---------------------------------------------------------------------------
// Out: a fixed-size staging buffer in front of a Sink.
//
// `failed` is sticky. Once the sink has reported an error there is nothing
// useful to do with the remaining output, and continuing to call a broken sink
// several thousand more times helps nobody.
// ---------------------------------------------------------------------------
struct Out {
    Sink sink;
    char* buf;
    size_t len;
    size_t cap;
    bool failed;
    uint64_t written;  // total bytes handed to the sink, for diagnostics

    Out() : buf(nullptr), len(0), cap(0), failed(false), written(0) {}
    ~Out() { std::free(buf); }

    Out(const Out&) = delete;
    Out& operator=(const Out&) = delete;

    bool open(Sink s, size_t bufsz = UCIS_XML_BUFSZ) {
        sink = s;
        if (!buf) {
            buf = static_cast<char*>(std::malloc(bufsz));
            if (!buf) { failed = true; return false; }
            cap = bufsz;
        }
        len = 0;
        return true;
    }

    bool flush() {
        if (failed) return false;
        if (len == 0) return true;
        if (!sink.valid()) { failed = true; return false; }
        if (sink.write(sink.ctx, buf, len) != 0) { failed = true; return false; }
        written += len;
        len = 0;
        return true;
    }

    // Guarantee `n` contiguous bytes in the buffer, flushing if needed. Callers
    // use this for short tokens they then write with `put`/`raw`; anything
    // longer than the buffer goes through `write` instead.
    bool need(size_t n) {
        if (failed) return false;
        if (cap - len >= n) return true;
        if (!flush()) return false;
        return cap - len >= n;
    }

    void put(char c) {
        if (len == cap && !flush()) return;
        buf[len++] = c;
    }

    // Unchecked: only legal directly after a successful need().
    void rawPut(char c) { buf[len++] = c; }
    void rawCopy(const char* p, size_t n) {
        std::memcpy(buf + len, p, n);
        len += n;
    }

    void write(const char* p, size_t n) {
        if (failed || n == 0) return;
        if (n >= cap) {
            // Larger than the whole buffer: flush what we have and hand it to
            // the sink directly rather than looping through the buffer.
            if (!flush()) return;
            if (!sink.valid() || sink.write(sink.ctx, p, n) != 0) { failed = true; return; }
            written += n;
            return;
        }
        if (cap - len < n && !flush()) return;
        std::memcpy(buf + len, p, n);
        len += n;
    }

    void write(Text t) { write(t.data, t.size); }

    // Literal string of known length; the common case in the renderers.
    template <size_t N>
    void lit(const char (&s)[N]) { write(s, N - 1); }
};

// ---------------------------------------------------------------------------
// Built-in sinks.
// ---------------------------------------------------------------------------

/**
 * A sink that collects the whole document in memory.
 *
 * For tests, and for any caller that wants the document as a buffer rather
 * than a file -- to post it somewhere, or to compress it itself.
 *
 * .. code-block:: cpp
 *
 *    ux::MemorySink out;
 *    ux::CoverageWriter cov;
 *    cov.open(out.sink());
 *    // ... record ...
 *    cov.close();
 *    fwrite(out.begin(), 1, out.size(), stdout);
 */
struct MemorySink {
    /** The collected bytes. */
    Vec<char> data;

    // Internal: the Sink callback.
    static int writeFn(void* ctx, const char* p, size_t n) {
        Vec<char>& v = static_cast<MemorySink*>(ctx)->data;
        if (!v.reserve(v.len + static_cast<uint32_t>(n))) return 1;
        std::memcpy(v.ptr + v.len, p, n);
        v.len += static_cast<uint32_t>(n);
        return 0;
    }

    /** :return: A :cpp:struct:`Sink` writing into this buffer. */
    Sink sink() { return Sink(this, &writeFn); }

    /** :return: Start of the collected bytes; not NUL-terminated. */
    const char* begin() const { return data.ptr; }

    /** :return: How many bytes have been collected. */
    size_t size() const { return data.len; }
};

#if !UCIS_XML_NO_STDIO
/**
 * A sink that writes to a ``FILE*``.
 *
 * :cpp:func:`CoverageWriter::openFile` uses one internally, so reach for this
 * only when you need to write to a stream you already have -- a pipe, or
 * ``stdout``.
 *
 * Unavailable when ``UCIS_XML_NO_STDIO`` is set.
 */
struct FileSink {
    /** The stream, or null when closed. */
    std::FILE* fp;
    /** Whether this sink opened the stream and must close it. */
    bool owned;

    /** Constructs a closed sink. */
    FileSink() : fp(nullptr), owned(false) {}

    /** Closes the stream if this sink opened it. */
    ~FileSink() { close(); }

    FileSink(const FileSink&) = delete;
    FileSink& operator=(const FileSink&) = delete;

    /**
     * Opens a file for writing, replacing any stream already held.
     *
     * :param path: File to create or truncate.
     * :return: ``true`` on success.
     */
    bool open(const char* path) {
        close();
        fp = std::fopen(path, "wb");
        owned = fp != nullptr;
        return owned;
    }

    /**
     * Takes a stream the caller owns, such as ``stdout`` or a pipe.
     *
     * The stream is *not* closed by this sink; that stays the caller's.
     *
     * :param f: The stream.
     */
    void adopt(std::FILE* f) { close(); fp = f; owned = false; }

    /** Closes the stream if this sink opened it. Idempotent. */
    void close() {
        if (fp && owned) std::fclose(fp);
        fp = nullptr;
        owned = false;
    }

    // Internal: the Sink callback.
    static int writeFn(void* ctx, const char* p, size_t n) {
        std::FILE* f = static_cast<FileSink*>(ctx)->fp;
        return (f && std::fwrite(p, 1, n, f) == n) ? 0 : 1;
    }

    /** :return: A :cpp:struct:`Sink` writing to this stream. */
    Sink sink() { return Sink(this, &writeFn); }
};
#endif  // !UCIS_XML_NO_STDIO

}  // namespace UCIS_XML_NAMESPACE

// ==== ux_text.hpp =============================================================

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

// ==== ux_error.hpp ============================================================

// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// ux_error.hpp - the first-error latch and the warning counters.
//
// Task I-1.4. Design §5.5 is the whole specification: no exceptions, no abort,
// no writes to stderr. A coverage write must never take a simulation down, so
// the writer latches the first error, turns every subsequent operation into a
// no-op, and reports at close(). Building the test suite with UCIS_XML_ASSERT
// defined turns contract violations into loud aborts instead, which is how the
// contract tests (T-5) catch them.


namespace UCIS_XML_NAMESPACE {

/**
 * Everything the writer can fail with, from
 * :cpp:func:`CoverageWriter::errorCode`.
 *
 * An enum as well as a message, so a caller can react in code rather than by
 * matching strings. Each value's cause and fix is documented on the
 * troubleshooting page, and the message text itself always names the fix where
 * there is one.
 *
 * The *first* error is the one kept -- it is the one that explains the others
 * -- and every call after it becomes a no-op.
 */
enum class Err : uint8_t {
    /** Nothing has failed. */
    None = 0,
    /** The output sink reported a write failure. */
    SinkFailed,
    /** An allocation failed; some records may be lost. */
    OutOfMemory,
    /** A scope was reopened, or closed out of order. */
    ScopeReopened,
    /** A scope was still open at ``close()``. */
    ScopeNotClosed,
    /** A path first seen after the first scope, under
        :cpp:enumerator:`Sources::UpFront`. */
    LateSourceFile,
    /** Statement, block and process coverage in one scope; ``BLOCK_COVERAGE``
        is an ``xsd:choice``. */
    MixedBlockForms,
    /** An FSM transition named an undeclared state. */
    UnknownState,
    /** A cross named a coverpoint not in its covergroup. */
    UnknownCoverpoint,
    /** No scopes; ``UCIS`` requires ``instanceCoverages+``. */
    EmptyDocument,
    /** ``tool()`` and ``test()`` were not both set. */
    MissingHistory,
    /** An argument was invalid. */
    BadArgument,
    /** A call arrived after ``close()``. */
    ClosedWriter,
};

inline const char* toString(Err e) {
    switch (e) {
    case Err::SinkFailed:        return "sink write failed";
    case Err::OutOfMemory:       return "out of memory";
    case Err::ScopeReopened:     return "scope reopened";
    case Err::ScopeNotClosed:    return "scope still open at close()";
    case Err::LateSourceFile:    return "source file first seen after a scope opened";
    case Err::MixedBlockForms:   return "block coverage mixes statement, block and process forms";
    case Err::UnknownState:      return "transition names an undeclared state";
    case Err::UnknownCoverpoint: return "cross names a coverpoint not in this covergroup";
    case Err::EmptyDocument:     return "document has no coverage scopes";
    case Err::MissingHistory:    return "tool() and test() must both be set";
    case Err::BadArgument:       return "invalid argument";
    case Err::ClosedWriter:      return "writer is already closed";
    default:                     return "";
    }
}

// A bounded message buffer. 512 bytes holds the longest message the writer
// produces with two full hierarchical names in it; anything longer is truncated
// rather than allocated, because the error path must not be able to fail.
class Message {
public:
    Message() : len_(0) { buf_[0] = '\0'; }

    void clear() { len_ = 0; buf_[0] = '\0'; }

    Message& add(Text t) {
        size_t n = t.size;
        if (n > kCap - 1 - len_) n = kCap - 1 - len_;
        std::memcpy(buf_ + len_, t.data, n);
        len_ += n;
        buf_[len_] = '\0';
        return *this;
    }

    Message& add(const char* s) { return add(Text(s)); }

    // Quoted, so an empty or space-bearing name is still legible in the message.
    Message& quoted(Text t) { return add("\"").add(t).add("\""); }

    Message& add(uint64_t v) {
        char tmp[20];
        int n = 0;
        do { tmp[n++] = static_cast<char>('0' + (v % 10)); v /= 10; } while (v);
        while (n) add(Text(&tmp[--n], 1));
        return *this;
    }

    const char* c_str() const { return buf_; }
    bool empty() const { return len_ == 0; }

private:
    static const size_t kCap = 512;
    char buf_[kCap];
    size_t len_;
};

// ---------------------------------------------------------------------------
// Status: the latch itself.
// ---------------------------------------------------------------------------
class Status {
public:
    Status() : code_(Err::None), keyCollisions_(0), warnings_(0) {}

    bool ok() const { return code_ == Err::None; }
    Err code() const { return code_; }
    const char* error() const { return message_.c_str(); }

    // Total repairs and complaints: sanitized characters, clamped integers, key
    // collisions, and configuration warnings. The fixture suite asserts zero.
    uint64_t warnings() const { return warnings_ + keyCollisions_ + sanitize.total(); }
    uint64_t keyCollisions() const { return keyCollisions_; }

    void noteKeyCollision() { ++keyCollisions_; }
    void warn() { ++warnings_; }

    // Latches `e` if nothing has failed yet, and returns false so call sites can
    // `return fail(...)`. The first error is kept because it is the one that
    // explains the others.
    bool fail(Err e) {
        if (code_ != Err::None) return false;
        code_ = e;
        message_.clear();
        message_.add(toString(e));
        onFail();
        return false;
    }

    // Begins a message for `e` and returns it so the caller can append context.
    // Callers must finish building before the next fail().
    Message& failWith(Err e) {
        if (code_ == Err::None) {
            code_ = e;
            message_.clear();
            return message_;
        }
        // Already failed: hand back a scratch buffer so callers can append
        // unconditionally without overwriting the first, explanatory error.
        scratch_.clear();
        return scratch_;
    }

    // Call after failWith() has finished composing, so UCIS_XML_ASSERT reports
    // the full message rather than the bare code.
    void latched() { onFail(); }

    Sanitize sanitize;

private:
    void onFail() {
#if UCIS_XML_HAVE_ASSERT
        UCIS_XML_ASSERT(message_.c_str());
#endif
    }

    Err code_;
    Message message_;
    Message scratch_;
    uint64_t keyCollisions_;
    uint64_t warnings_;
};

}  // namespace UCIS_XML_NAMESPACE

// ==== ux_arena.hpp ============================================================

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

// ==== ux_key.hpp ==============================================================

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

// ==== ux_intern.hpp ===========================================================

// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// ux_intern.hpp - the source-file table, and the two modes for building it.
//
// Task I-1.5. This is the one place the caller is exposed to a real constraint
// of the format: the schema puts sourceFiles+ before everything, so file ids
// must exist before the first scope is written (design §3.1).
//
// Two facts make that cheap rather than onerous, and both are pinned by T-7:
//   1. The table may list files no coverage item references -- there is no
//      xsd:key/keyref anywhere in ucis.xsd -- so "register your files first"
//      means "hand over the compile file list you already have", superset and
//      all. It does not mean "pre-scan your coverage data".
//   2. Deferring the table does not prevent compression; it makes compression
//      the cheap path, because gzip members concatenate (RFC 1952 §2.2). See
//      ux_spool.hpp.


namespace UCIS_XML_NAMESPACE {

// Every document has this as file id 1, referenced by any fact that carries no
// path of its own. STATEMENT_ID/@file is required and is a positiveInteger, so
// there has to be somewhere for such facts to point; inventing a synthetic
// entry is better than silently attributing them to whichever file happened to
// be registered first. It also satisfies sourceFiles' minOccurs="1" for a
// document whose producer knows no paths at all.
inline Text unknownFileName() { return Text("(unknown)", 9); }

class FileTable {
public:
    FileTable() : mode_(Sources::UpFront), frozen_(false) {
        // Reserve id 1 before anything else can claim it.
        bool ins = false;
        map_.getOrAdd(arena_, unknownFileName(), 1, &ins);
        order_.push(arena_.add(unknownFileName()));
    }

    void setMode(Sources m) { mode_ = m; }
    Sources mode() const { return mode_; }

    // True once the first scope has been opened, after which UpFront mode can
    // no longer accept a new path.
    bool frozen() const { return frozen_; }
    void freeze() { frozen_ = true; }

    // Registers `path` up front. Duplicates are folded, which is what makes a
    // caller's "list every file I compiled" list safe to pass verbatim.
    uint32_t declare(Text path, Status& st) {
        if (path.empty()) return 1;
        bool inserted = false;
        uint32_t id = static_cast<uint32_t>(order_.len) + 1;
        uint32_t got = map_.getOrAdd(arena_, path, id, &inserted);
        if (got == StrMap::kAbsent) { st.fail(Err::OutOfMemory); return 1; }
        if (inserted) order_.push(arena_.add(path));
        return got;
    }

    // Resolves a path seen while emitting coverage. Under UpFront a path that
    // was not declared is a contract violation with a message naming the fix;
    // under Deferred it is simply interned.
    uint32_t resolve(Text path, Text scopeName, Status& st) {
        if (path.empty()) return 1;
        uint32_t id = map_.find(arena_, path);
        if (id != StrMap::kAbsent) return id;

        if (mode_ == Sources::Deferred || !frozen_) return declare(path, st);

        Message& m = st.failWith(Err::LateSourceFile);
        m.add("source file ").quoted(path).add(" first seen after scope ")
         .quoted(scopeName)
         .add("; pass it to cov.sources(), or construct with Sources::Deferred");
        st.latched();
        return 1;
    }

    uint32_t count() const { return order_.len; }
    Text path(uint32_t index) const { return arena_.get(order_[index]); }

    bool oom() const { return arena_.oom() || map_.oom() || order_.oom; }

private:
    StrArena arena_;
    StrMap map_;
    Vec<Str> order_;  // index i holds the path with id i+1
    Sources mode_;
    bool frozen_;
};

}  // namespace UCIS_XML_NAMESPACE

// ==== ux_stage.hpp ============================================================

// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// ux_stage.hpp - the per-scope staging arena: one POD record per coverage fact,
// their strings in a bump arena, and the sorted index arrays the renderers walk.
//
// Task I-1.7. This is the mechanism the whole caller-experience change rests on
// (design §5). The caller emits facts in whatever order suits its own data
// structures; when the scope closes, the writer sorts them and renders one
// complete <instanceCoverages> element in the schema's required order. Every
// ordering rule that rev 1 imposed on the caller becomes a sort key here.
//
// The measurement that makes this affordable (design §5.1, reproduced by
// bench/coverage/scope_distribution.py): OpenTitan's largest single scope is
// 37,404 points, so peak staging is about 4 MB against a design whose names
// alone are 230 MB.
//
// Ordering is content-derived, never arrival-derived (decision D-11). A scope's
// output depends only on the set of facts in it, which is what makes T-3 --
// shuffle the emission order, get byte-identical output -- a meaningful test
// rather than a tautology. Sequences that are inherently ordered (an expression's
// subExpr list, a cross bin's index tuple, the state path of an FSM transition)
// keep the order they were given, because there the order is the information.


namespace UCIS_XML_NAMESPACE {

/**
 * Sampling options for a covergroup instance, coverpoint or cross.
 *
 * One struct rather than three: ``CGINST_OPTIONS``, ``COVERPOINT_OPTIONS`` and
 * ``CROSS_OPTIONS`` overlap heavily. Each object emits only the attributes its
 * own type declares, and silently ignores the rest -- so ``autoBinMax`` on a
 * cross, or ``perInstance`` on a coverpoint, does nothing.
 *
 * Attributes equal to their schema default are omitted from the output. Note
 * that a coverpoint, cross and covergroup instance each *require* an
 * ``options`` child, so a fully defaulted one still costs ten bytes --
 * ``<options/>`` -- and is never skipped.
 */
struct Options {
    uint64_t weight_;
    uint64_t goal_;
    uint64_t atLeast_;
    uint64_t autoBinMax_;
    uint64_t crossNumPrintMissing_;
    bool detectOverlap_;
    bool perInstance_;
    bool mergeInstances_;
    Text comment_;

    /** Constructs with every option at its schema default. */
    Options()
        : weight_(1), goal_(100), atLeast_(1), autoBinMax_(64),
          crossNumPrintMissing_(0), detectOverlap_(false), perInstance_(false),
          mergeInstances_(false) {}

    /**
     * Sets this object's weight when coverage is aggregated.
     *
     * :param v: The weight. Defaults to 1, which is omitted from the output.
     * :return: ``*this``, for chaining.
     */
    Options& weight(uint64_t v) { weight_ = v; return *this; }

    /**
     * Sets the coverage percentage considered complete.
     *
     * :param v: The goal. Defaults to 100.
     * :return: ``*this``, for chaining.
     */
    Options& goal(uint64_t v) { goal_ = v; return *this; }

    /**
     * Sets how many hits a bin needs before it counts as covered.
     *
     * :param v: The threshold. Defaults to 1.
     * :return: ``*this``, for chaining.
     */
    Options& atLeast(uint64_t v) { atLeast_ = v; return *this; }

    /**
     * Sets how many automatic bins may be created. Coverpoints and covergroup
     * instances only.
     *
     * :param v: The cap. Defaults to 64.
     * :return: ``*this``, for chaining.
     */
    Options& autoBinMax(uint64_t v) { autoBinMax_ = v; return *this; }

    /**
     * Sets how many uncovered cross combinations a report should list. Crosses
     * and covergroup instances only.
     *
     * :param v: The count. Defaults to 0.
     * :return: ``*this``, for chaining.
     */
    Options& crossNumPrintMissing(uint64_t v) { crossNumPrintMissing_ = v; return *this; }

    /**
     * Asks for a warning when bin ranges overlap. Coverpoints and covergroup
     * instances only.
     *
     * :param v: ``true`` to detect overlaps. Defaults to ``false``.
     * :return: ``*this``, for chaining.
     */
    Options& detectOverlap(bool v) { detectOverlap_ = v; return *this; }

    /**
     * Asks for coverage to be tracked per instance rather than per type.
     * Covergroup instances only.
     *
     * :param v: ``true`` for per-instance. Defaults to ``false``.
     * :return: ``*this``, for chaining.
     */
    Options& perInstance(bool v) { perInstance_ = v; return *this; }

    /**
     * Asks for instances of this covergroup to be merged. Covergroup instances
     * only.
     *
     * :param v: ``true`` to merge. Defaults to ``false``.
     * :return: ``*this``, for chaining.
     */
    Options& mergeInstances(bool v) { mergeInstances_ = v; return *this; }

    /**
     * Sets a free-form comment.
     *
     * :param v: The comment. Defaults to empty, which is omitted.
     * :return: ``*this``, for chaining.
     */
    Options& comment(Text v) { comment_ = v; return *this; }
};

namespace stage {

// Resolved source position: `file` is a source-file id, not a path.
struct SLoc {
    uint32_t file;
    uint32_t line;
    uint32_t inlineCount;

    SLoc() : file(1), line(1), inlineCount(1) {}
};

inline int cmp(const SLoc& a, const SLoc& b) {
    if (a.file != b.file) return a.file < b.file ? -1 : 1;
    if (a.line != b.line) return a.line < b.line ? -1 : 1;
    if (a.inlineCount != b.inlineCount) return a.inlineCount < b.inlineCount ? -1 : 1;
    return 0;
}

// Staged form of Options; `comment` has been copied into the arena.
struct SOpts {
    uint64_t weight, goal, atLeast, autoBinMax, crossNumPrintMissing;
    bool detectOverlap, perInstance, mergeInstances;
    Str comment;

    SOpts()
        : weight(1), goal(100), atLeast(1), autoBinMax(64), crossNumPrintMissing(0),
          detectOverlap(false), perInstance(false), mergeInstances(false) {}
};

struct UserAttr {
    Str key, value;
    AttrType type;
    uint32_t next;  // kNone-terminated, in the order the caller added them
};

// The uncommon attributes of an object or bin: alias, exclusion, goal, weight,
// and user attributes. Kept in a side table so the common record stays small --
// 37 K toggle records paying 48 bytes each for attributes almost none of them
// carry is exactly the cost this design cannot afford.
struct Extra {
    Str alias;
    Str excludedReason;
    uint64_t goal;    // kNoGoal when unset
    uint64_t weight;  // kNoWeight when unset
    bool excluded;
    // Head and tail of this object's user-attribute list. A tail pointer rather
    // than prepend-and-reverse, so attributes render in the order they were
    // added without a second pass.
    uint32_t attrHead;
    uint32_t attrTail;

    Extra()
        : goal(kNoGoal), weight(kNoWeight), excluded(false),
          attrHead(kNone), attrTail(kNone) {}

    static const uint64_t kNoGoal = UINT64_MAX;
    static const uint64_t kNoWeight = UINT64_MAX;
    static const uint32_t kNone = UINT32_MAX;
};

constexpr uint32_t kNone = UINT32_MAX;

// --- code coverage -------------------------------------------------------

struct RToggle {
    Str sig, bit, from, to;
    uint64_t count;
    SLoc loc;
    uint32_t slot;
};

// What is known about a signal beyond its toggle counts: where it is declared,
// and its bit range. Both optional and independent -- a tool may know the
// declaration site without the range, or the range without the site.
struct RSignal {
    Str sig;
    SLoc loc;
    int64_t left, right;
    bool downto;
    bool hasLoc;
    bool hasDim;
};

struct RStmt {
    SLoc loc;
    Str name;  // -> contents/@nameComponent
    uint64_t count;
    uint32_t slot;
};

struct RBlock {
    uint32_t parent;   // kNone for a top-level block
    uint32_t process;  // index into procs, or kNone
    Str name;
    SLoc id;
    uint64_t count;
    uint32_t slot;
};

struct RProcess {
    Str type;
    uint32_t slot;
};

struct RBlockStmt {
    uint32_t block;
    SLoc id;
};

struct RExpr {
    uint32_t parent;  // kNone for a top-level expression
    SLoc loc;
    Str name, exprString, stmtType;
    uint32_t subCount;
    uint32_t slot;
};

// EXPR's subExpr list. A separate record rather than a run in strPool because
// two expressions can be built at the same time, which would interleave their
// sub-expressions and leave neither contiguous.
struct RSubExpr {
    uint32_t expr;
    uint32_t seq;  // evaluation order; the order is the meaning
    Str text;
};

struct RExprBin {
    uint32_t expr;
    Str name;
    uint64_t count;
    uint32_t slot;
};

struct RBrStmt {
    SLoc loc;
    Str expr, stmtType;
    uint32_t parentArm;  // kNone unless nested inside another statement's arm
    uint32_t slot;
};

struct RBrArm {
    uint32_t stmt;
    SLoc loc;
    Str name;
    uint64_t count;
    uint32_t slot;
};

// --- functional coverage -------------------------------------------------

struct RFsm {
    Str name, type;
    uint32_t width;
    uint32_t slot;
};

struct RFsmState {
    uint32_t fsm;
    Str name, value;
    uint64_t count;
    uint32_t slot;
};

struct RFsmTrans {
    uint32_t fsm;
    uint32_t first, n;  // state names in strPool; the order is the transition
    uint64_t count;
    uint32_t slot;
};

// ASSERTION's eight optional bins, in the schema's declared order. Holding them
// as an array indexed by that order is what lets the caller call attempts() and
// passes() in any order and still get one legal sequence out.
enum AssertBin : uint8_t {
    kCover = 0, kPass, kFail, kVacuous, kDisabled, kAttempt, kActive, kPeakActive,
    kAssertBinCount
};

struct RAssert {
    Str name, kind;
    uint64_t bins[kAssertBinCount];
    uint32_t binSlot[kAssertBinCount];
    uint8_t present;  // bitmask over AssertBin
    uint32_t slot;
};

struct RCg {
    Str name, typeName, moduleName;
    SLoc instLoc, typeLoc;
    bool hasInstLoc, hasTypeLoc;
    SOpts opts;
    uint32_t slot;
};

struct RCgParm {
    uint32_t cg;
    uint32_t seq;  // declaration order; cgParms is a caller-ordered list
    Str name, value;
};

struct RCp {
    uint32_t cg;
    Str name, exprString;
    SOpts opts;
    uint32_t slot;
};

struct RCpBin {
    uint32_t cp;
    Str name;
    BinType type;
    uint64_t count;
    int64_t from, to;
    uint32_t seqFirst, seqCount;  // into intPool; a sequence bin when non-zero
    uint32_t slot;
};

struct RCross {
    uint32_t cg;
    Str name;
    SOpts opts;
    uint32_t exprFirst, exprCount;  // into strPool
    uint32_t slot;
};

struct RCrossBin {
    uint32_t cross;
    Str name, type;
    uint64_t count;
    uint32_t idxFirst, idxCount;  // into intPool; the tuple order is the meaning
    uint32_t slot;
};

struct RParam {
    Str name, value;
    uint32_t seq;
};

// ---------------------------------------------------------------------------
// Stage: everything staged for one scope.
//
// Vectors rather than one heterogeneous record array. The design document
// describes a single POD record sorted by (kind, group, ordinal); splitting by
// kind is the same thing with the kind key folded into the choice of vector,
// and it lets each renderer read a typed record instead of unpacking a union.
// ---------------------------------------------------------------------------
struct Stage {
    StrArena arena;

    // The document's file table and error latch. Held here rather than only on
    // Scope so that *every* handle can turn a path into a source-file id: a
    // covergroup's declaration site is as much a source location as a
    // statement's, and should not need a different route to get one.
    FileTable* files;
    Status* status;

    Str name, moduleName, alias;
    SLoc id;
    uint32_t instanceId;
    uint32_t parentInstanceId;  // kNone at the top level
    uint32_t slot;              // the scope's own alias/exclusion/userAttrs

    // metricAttributes live on the per-kind container (toggleCoverage,
    // blockCoverage, ...), not on the individual items, so they are stored per
    // kind rather than per record.
    Str metricMode[static_cast<uint32_t>(CovKind::Count)];
    uint64_t kindWeight[static_cast<uint32_t>(CovKind::Count)];

    Vec<RParam> params;
    Vec<RToggle> toggles;
    Vec<RSignal> signals;
    Vec<RStmt> stmts;
    Vec<RProcess> procs;
    Vec<RBlock> blocks;
    Vec<RBlockStmt> blockStmts;
    Vec<RExpr> exprs;
    Vec<RSubExpr> subExprs;
    Vec<RExprBin> exprBins;
    Vec<RBrStmt> brStmts;
    Vec<RBrArm> brArms;
    Vec<RFsm> fsms;
    Vec<RFsmState> fsmStates;
    Vec<RFsmTrans> fsmTrans;
    Vec<RAssert> asserts;
    Vec<RCg> cgs;
    Vec<RCgParm> cgParms;
    Vec<RCp> cps;
    Vec<RCpBin> cpBins;
    Vec<RCross> crosses;
    Vec<RCrossBin> crossBins;

    Vec<Str> strPool;
    Vec<int64_t> intPool;

    // Attribute side tables. extras[0] is a canonical empty entry so that a
    // slot value of 0 means "nothing set" without a branch at every use.
    Vec<uint32_t> slots;
    Vec<Extra> extras;
    Vec<UserAttr> userAttrs;

    // Shared merge-sort scratch; see sortInto().
    Vec<uint32_t> scratch;

    // Resolves a path against the file table, reporting against this scope's
    // name if it turns out to be a late arrival under Sources::UpFront.
    SLoc resolve(Text file, uint32_t line, uint32_t inlineCount) {
        SLoc l;
        l.file = (files && status) ? files->resolve(file, text(name), *status) : 1;
        l.line = line;
        l.inlineCount = inlineCount;
        return l;
    }

    Stage()
        : files(nullptr), status(nullptr), instanceId(0),
          parentInstanceId(kNone), slot(0) {
        extras.push(Extra());
        slot = newSlot();
        for (uint32_t i = 0; i < static_cast<uint32_t>(CovKind::Count); ++i)
            kindWeight[i] = Extra::kNoWeight;
    }

    Str str(Text t) { return arena.add(t); }
    Text text(Str s) const { return arena.get(s); }

    // Every bin-bearing record gets a slot. The indirection through `slots`
    // rather than a direct index into `extras` is what makes BinRef safe: the
    // record vectors are sorted (and reallocated) later, but a slot index never
    // moves, so a BinRef handed back to the caller stays valid for the life of
    // the scope.
    uint32_t newSlot() { return slots.push(0); }

    Extra& extraFor(uint32_t slot) {
        if (slot >= slots.len) return extras[0];
        if (slots[slot] == 0) {
            uint32_t i = extras.push(Extra());
            if (i == UINT32_MAX) return extras[0];
            slots[slot] = i;
        }
        return extras[slots[slot]];
    }

    const Extra& extraAt(uint32_t slot) const {
        if (slot >= slots.len || slots[slot] == 0) return extras[0];
        return extras[slots[slot]];
    }

    void addUserAttr(uint32_t slot, Text key, Text value, AttrType type) {
        UserAttr a;
        a.key = str(key);
        a.value = str(value);
        a.type = type;
        a.next = kNone;
        uint32_t i = userAttrs.push(a);
        if (i == UINT32_MAX) return;
        Extra& e = extraFor(slot);
        if (e.attrTail == kNone) e.attrHead = i;
        else userAttrs[e.attrTail].next = i;
        e.attrTail = i;
    }

    bool oom() const {
        return arena.oom() || toggles.oom || stmts.oom || blocks.oom || exprs.oom ||
               exprBins.oom || subExprs.oom || brStmts.oom || brArms.oom || fsms.oom || fsmStates.oom ||
               fsmTrans.oom || asserts.oom || cgs.oom || cps.oom || cpBins.oom ||
               crosses.oom || crossBins.oom || slots.oom || extras.oom ||
               userAttrs.oom || strPool.oom || intPool.oom || params.oom;
    }

    // Approximate staged footprint, checked against WriterOptions::stagingLimit.
    size_t bytes() const {
        return arena.bytes() + toggles.bytes() + stmts.bytes() + blocks.bytes() +
               blockStmts.bytes() + exprs.bytes() + exprBins.bytes() + subExprs.bytes() +
               brStmts.bytes() + brArms.bytes() + fsms.bytes() + fsmStates.bytes() +
               fsmTrans.bytes() + asserts.bytes() + cgs.bytes() + cgParms.bytes() +
               cps.bytes() + cpBins.bytes() + crosses.bytes() + crossBins.bytes() +
               strPool.bytes() + intPool.bytes() + slots.bytes() + extras.bytes() +
               userAttrs.bytes() + params.bytes() + signals.bytes() + procs.bytes();
    }

    // Fills `out` with 0..n-1 sorted by `less`. The destination is a parameter
    // rather than a member because the renderers nest: a covergroup's
    // coverpoints are sorted while the walk over sorted covergroups is still in
    // progress. The scratch buffer is safely shared, since it is only live
    // inside a single mergeSortIndex call.
    template <typename Less>
    bool sortInto(Vec<uint32_t>& out, uint32_t n, Less less) {
        out.clear();
        if (n == 0) return true;
        if (!out.reserve(n) || !scratch.reserve(n)) return false;
        out.len = n;
        scratch.len = n;
        for (uint32_t i = 0; i < n; ++i) out[i] = i;
        mergeSortIndex(out.ptr, scratch.ptr, n, less);
        return true;
    }
};

// Copies caller Options into their staged form; shared by covergroup,
// coverpoint and cross, which is why Options is one struct rather than three.
inline SOpts toStageOpts(Stage& s, const Options& o) {
    SOpts r;
    r.weight = o.weight_;
    r.goal = o.goal_;
    r.atLeast = o.atLeast_;
    r.autoBinMax = o.autoBinMax_;
    r.crossNumPrintMissing = o.crossNumPrintMissing_;
    r.detectOverlap = o.detectOverlap_;
    r.perInstance = o.perInstance_;
    r.mergeInstances = o.mergeInstances_;
    r.comment = s.str(o.comment_);
    return r;
}

}  // namespace stage
}  // namespace UCIS_XML_NAMESPACE

// ==== ux_render.hpp ===========================================================

// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// ux_render.hpp - the element fragments every coverage kind shares: source-id
// elements, BIN, the attribute groups, USER_ATTR, and the options children.
//
// Two rules from design §8 are implemented here rather than in each renderer,
// because they are what make XML viable at these sizes at all:
//   * attributes equal to their schema default are omitted (§8.2);
//   * COVERPOINT, CROSS and CGINSTANCE each *require* an options child, so the
//     required child costs ten bytes, "<options/>", and must not be skipped.


namespace UCIS_XML_NAMESPACE {
namespace stage {

// STATEMENT_ID and LINE_ID, under whichever element name the context requires
// (`id`, `blockId`, `statementId`, `cginstSourceId`, `cgSourceId`).
inline void renderId(Out& o, Text tag, const SLoc& loc, Status& st) {
    o.put('<');
    o.write(tag);
    attrU(o, "file", positive(loc.file, st.sanitize));
    attrU(o, "line", positive(loc.line, st.sanitize));
    attrU(o, "inlineCount", positive(loc.inlineCount, st.sanitize));
    o.lit("/>");
}

// binAttributes: alias, coverageCountGoal, excluded, excludedReason, weight.
// objAttributes is the same minus coverageCountGoal, so one function with a
// flag covers both.
inline void renderAttrGroup(Out& o, const Stage& sg, uint32_t slot, bool isBin,
                            Status& st) {
    const Extra& e = sg.extraAt(slot);
    if (!e.alias.empty()) attr(o, "alias", sg.text(e.alias), st.sanitize);
    if (isBin && e.goal != Extra::kNoGoal) attrU(o, "coverageCountGoal", e.goal);
    if (e.excluded) attrBool(o, "excluded", true);
    if (!e.excludedReason.empty())
        attr(o, "excludedReason", sg.text(e.excludedReason), st.sanitize);
    if (e.weight != Extra::kNoWeight) attrU(o, "weight", e.weight);
}

inline void renderUserAttrs(Out& o, const Stage& sg, uint32_t slot, Status& st) {
    uint32_t i = sg.extraAt(slot).attrHead;
    while (i != kNone) {
        const UserAttr& a = sg.userAttrs[i];
        o.lit("<userAttr");
        attr(o, "key", sg.text(a.key), st.sanitize);
        attr(o, "type", Text(toString(a.type)), st.sanitize);
        o.put('>');
        writeEscaped(o, sg.text(a.value), st.sanitize);
        o.lit("</userAttr>");
        i = a.next;
    }
}

// BIN_CONTENTS. `name` becomes @nameComponent, which is where a tool's
// per-point comment belongs -- UCIS has no @name on BIN itself.
inline void renderContents(Out& o, const Stage& sg, Str name, uint64_t count,
                           Status& st) {
    o.lit("<contents");
    attrU(o, "coverageCount", count);
    if (!name.empty()) attr(o, "nameComponent", sg.text(name), st.sanitize);
    o.lit("/>");
}

// A complete BIN under the element name the context requires: `bin`,
// `blockBin`, `branchBin`, `stateBin`, `transitionBin`, or one of ASSERTION's
// eight.
inline void renderBin(Out& o, const Stage& sg, Text tag, Str name, uint64_t count,
                      uint32_t slot, Status& st) {
    o.put('<');
    o.write(tag);
    renderAttrGroup(o, sg, slot, /*isBin=*/true, st);
    o.put('>');
    renderContents(o, sg, name, count, st);
    renderUserAttrs(o, sg, slot, st);
    o.lit("</");
    o.write(tag);
    o.put('>');
}

// Which of the shared options attributes a given element actually declares.
enum OptsKind : uint8_t { kOptsCoverpoint, kOptsCross, kOptsCgInstance };

// The required <options/> child. Everything defaulted collapses to ten bytes.
inline void renderOptions(Out& o, const Stage& sg, const SOpts& op, OptsKind kind,
                          Status& st) {
    o.lit("<options");
    if (op.weight != 1) attrU(o, "weight", op.weight);
    if (op.goal != 100) attrU(o, "goal", op.goal);
    if (!op.comment.empty()) attr(o, "comment", sg.text(op.comment), st.sanitize);
    if (op.atLeast != 1) attrU(o, "at_least", op.atLeast);
    if (kind != kOptsCross) {
        if (op.detectOverlap) attrBool(o, "detect_overlap", true);
        if (op.autoBinMax != 64) attrU(o, "auto_bin_max", op.autoBinMax);
    }
    if (kind != kOptsCoverpoint && op.crossNumPrintMissing != 0)
        attrU(o, "cross_num_print_missing", op.crossNumPrintMissing);
    if (kind == kOptsCgInstance) {
        if (op.perInstance) attrBool(o, "per_instance", true);
        if (op.mergeInstances) attrBool(o, "merge_instances", true);
    }
    o.lit("/>");
}

// metricAttributes, carried by every per-kind coverage container.
inline void renderMetricAttrs(Out& o, const Stage& sg, CovKind kind, Status& st) {
    uint32_t k = static_cast<uint32_t>(kind);
    if (!sg.metricMode[k].empty())
        attr(o, "metricMode", sg.text(sg.metricMode[k]), st.sanitize);
    if (sg.kindWeight[k] != Extra::kNoWeight) attrU(o, "weight", sg.kindWeight[k]);
}

}  // namespace stage
}  // namespace UCIS_XML_NAMESPACE

// ==== ux_render_code.hpp ======================================================

// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// ux_render_code.hpp - the code-coverage renderers: toggle, block, condition,
// branch. Tasks I-2.1 through I-2.4.
//
// Each takes the staged facts for one scope and produces one per-kind container
// element. All the grouping and ordering the caller was spared in design §3
// happens here, and it happens on content -- names, locations, edges -- never on
// the order facts arrived in (decision D-11).
//
// Where the schema requires a container to be non-empty (EXPR needs bin+,
// COVERPOINT needs coverpointBin+), an empty one is dropped with a warning
// rather than filled with a fabricated zero-count bin. Inventing coverage data
// to satisfy a cardinality rule would be worse than omitting a construct the
// caller gave no data for.


namespace UCIS_XML_NAMESPACE {
namespace stage {

// ---------------------------------------------------------------------------
// Toggle (I-2.1)
//
// toggleCoverage > toggleObject > toggleBit > toggle > bin. Grouping facts on
// (signal, bit) is what produces the group-by-bit layout design §8.3 measured
// at 0.800x gzipped -- it is not a mode, it is the only layout this structure
// can produce.
// ---------------------------------------------------------------------------
inline void renderToggle(Out& o, Stage& sg, Status& st) {
    const uint32_t n = sg.toggles.len;
    if (n == 0) return;

    Vec<uint32_t> ord;
    if (!sg.sortInto(ord, n, [&sg](uint32_t a, uint32_t b) {
            const RToggle& x = sg.toggles[a];
            const RToggle& y = sg.toggles[b];
            int c = sg.arena.compare(x.sig, y.sig);
            if (c) return c < 0;
            c = sg.arena.compare(x.bit, y.bit);
            if (c) return c < 0;
            c = sg.arena.compare(x.from, y.from);
            if (c) return c < 0;
            return sg.arena.compare(x.to, y.to) < 0;
        })) {
        st.fail(Err::OutOfMemory);
        return;
    }

    // Per-signal detail -- declaration site and bit range -- is matched to
    // objects by a merge walk rather than a lookup per object: both sides are
    // sorted by signal name, so this is linear where a scan per object would be
    // quadratic in a scope with 37 K toggles.
    Vec<uint32_t> sigOrd;
    if (sg.signals.len &&
        !sg.sortInto(sigOrd, sg.signals.len, [&sg](uint32_t a, uint32_t b) {
            return sg.arena.compare(sg.signals[a].sig, sg.signals[b].sig) < 0;
        })) {
        st.fail(Err::OutOfMemory);
        return;
    }
    uint32_t sigAt = 0;

    KeyGen objKeys, bitKeys;

    o.lit("<toggleCoverage");
    renderMetricAttrs(o, sg, CovKind::Toggle, st);
    o.put('>');

    for (uint32_t i = 0; i < n;) {
        const Str sig = sg.toggles[ord[i]].sig;

        // [i, objEnd) is one signal.
        uint32_t objEnd = i;
        while (objEnd < n && sg.arena.equal(sg.toggles[ord[objEnd]].sig, sig)) ++objEnd;

        o.lit("<toggleObject");
        attr(o, "name", sg.text(sig), st.sanitize);
        o.lit(" key=\"");
        objKeys.writeKey(o, sg.text(sig), st);
        o.put('"');
        o.put('>');

        // dimension* comes before id in TOGGLE_OBJECT's sequence.
        while (sigAt < sigOrd.len &&
               sg.arena.compare(sg.signals[sigOrd[sigAt]].sig, sig) < 0)
            ++sigAt;

        // The object's id is the signal's declaration site when the caller gave
        // one, and the enclosing scope's otherwise. A toggle fact is about a
        // signal, not about a line, so there is nowhere else for it to come
        // from -- which is why s.signal() takes a location.
        SLoc objLoc = sg.toggles[ord[i]].loc;
        uint32_t look = sigAt;
        while (look < sigOrd.len && sg.arena.equal(sg.signals[sigOrd[look]].sig, sig)) {
            if (sg.signals[sigOrd[look]].hasLoc) {
                objLoc = sg.signals[sigOrd[look]].loc;
                break;
            }
            ++look;
        }

        while (sigAt < sigOrd.len && sg.arena.equal(sg.signals[sigOrd[sigAt]].sig, sig)) {
            const RSignal& d = sg.signals[sigOrd[sigAt]];
            if (d.hasDim) {
                o.lit("<dimension");
                attrI(o, "left", d.left);
                attrI(o, "right", d.right);
                attrBool(o, "downto", d.downto);
                o.lit("/>");
            }
            ++sigAt;
        }

        renderId(o, "id", objLoc, st);

        bitKeys.clear();
        for (uint32_t j = i; j < objEnd;) {
            const Str bit = sg.toggles[ord[j]].bit;
            uint32_t bitEnd = j;
            while (bitEnd < objEnd && sg.arena.equal(sg.toggles[ord[bitEnd]].bit, bit))
                ++bitEnd;

            o.lit("<toggleBit");
            attr(o, "name", sg.text(bit), st.sanitize);
            o.lit(" key=\"");
            bitKeys.writeKey(o, sg.text(bit), st);
            o.put('"');
            o.put('>');

            for (uint32_t k = j; k < bitEnd; ++k) {
                const RToggle& t = sg.toggles[ord[k]];
                o.lit("<toggle");
                attr(o, "from", sg.text(t.from), st.sanitize);
                attr(o, "to", sg.text(t.to), st.sanitize);
                o.put('>');
                renderBin(o, sg, "bin", Str(), t.count, t.slot, st);
                o.lit("</toggle>");
            }

            o.lit("</toggleBit>");
            j = bitEnd;
        }

        o.lit("</toggleObject>");
        i = objEnd;
    }

    o.lit("</toggleCoverage>");
}

// ---------------------------------------------------------------------------
// Block (I-2.2)
//
// BLOCK_COVERAGE is an xsd:choice: a scope emits process+, block+ or
// statement+, never a mixture. The vocabulary API's s.line() produces the
// statement arm, which is what a line-coverage tool wants; s.block() and
// s.process() exist for tools with real block coverage.
// ---------------------------------------------------------------------------
inline void renderBlockChildren(Out& o, Stage& sg, uint32_t parent,
                                const Vec<uint32_t>& ord, Status& st);

// One BLOCK. The element is <block> at the top of blockCoverage or inside a
// <process>, and <hierarchicalBlock> when nested inside another block -- the
// same complex type under two names.
inline void renderOneBlock(Out& o, Stage& sg, uint32_t bi, Text tag,
                           const Vec<uint32_t>& ord, Status& st) {
    const RBlock& b = sg.blocks[bi];
    o.put('<');
    o.write(tag);
    renderAttrGroup(o, sg, b.slot, /*isBin=*/false, st);
    o.put('>');

    // BLOCK's sequence: statementId*, hierarchicalBlock*, blockBin, blockId.
    // Holding blockBin and blockId back until after the nested blocks is one of
    // the ordering rules design §4 lists as the writer's problem.
    Vec<uint32_t> stmts;
    for (uint32_t i = 0; i < sg.blockStmts.len; ++i)
        if (sg.blockStmts[i].block == bi) stmts.push(i);
    Vec<uint32_t> sord;
    if (!sg.sortInto(sord, stmts.len, [&sg, &stmts](uint32_t a, uint32_t b) {
            return cmp(sg.blockStmts[stmts[a]].id, sg.blockStmts[stmts[b]].id) < 0;
        })) {
        st.fail(Err::OutOfMemory);
        return;
    }
    for (uint32_t i = 0; i < sord.len; ++i)
        renderId(o, "statementId", sg.blockStmts[stmts[sord[i]]].id, st);

    renderBlockChildren(o, sg, bi, ord, st);

    renderBin(o, sg, "blockBin", b.name, b.count, b.slot, st);
    renderId(o, "blockId", b.id, st);
    renderUserAttrs(o, sg, b.slot, st);
    o.lit("</");
    o.write(tag);
    o.put('>');
}

inline void renderBlockChildren(Out& o, Stage& sg, uint32_t parent,
                                const Vec<uint32_t>& ord, Status& st) {
    for (uint32_t i = 0; i < ord.len; ++i) {
        uint32_t bi = ord[i];
        if (sg.blocks[bi].parent != parent) continue;
        // Nested blocks belong to their parent, not to a process directly.
        if (parent == kNone && sg.blocks[bi].process != kNone) continue;
        renderOneBlock(o, sg, bi, parent == kNone ? "block" : "hierarchicalBlock",
                       ord, st);
    }
}

inline void renderBlock(Out& o, Stage& sg, Status& st) {
    const bool hasStmts = sg.stmts.len != 0;
    const bool hasBlocks = sg.blocks.len != 0;
    const bool hasProcs = sg.procs.len != 0;
    if (!hasStmts && !hasBlocks && !hasProcs) return;

    // s.process() creates a process *and* the block inside it, so "there are
    // blocks and there are processes" is the normal case, not a mixture. What
    // decides the arm is whether the top-level blocks sit under a process; a
    // scope with some of each cannot be expressed by the xsd:choice.
    uint32_t topInProc = 0, topFree = 0;
    for (uint32_t i = 0; i < sg.blocks.len; ++i) {
        if (sg.blocks[i].parent != kNone) continue;
        if (sg.blocks[i].process != kNone) ++topInProc;
        else ++topFree;
    }

    if ((hasStmts && (hasBlocks || hasProcs)) || (topInProc && topFree)) {
        // Emitting one arm and silently dropping the rest would lose coverage
        // without saying so; this is a contract violation.
        Message& m = st.failWith(Err::MixedBlockForms);
        m.add("scope ").quoted(sg.text(sg.name))
         .add(" mixes statement, block and process coverage; blockCoverage is an "
              "xsd:choice, so use only one of s.line(), s.block() and s.process()");
        st.latched();
        return;
    }

    o.lit("<blockCoverage");
    renderMetricAttrs(o, sg, CovKind::Block, st);
    o.put('>');

    if (hasStmts) {
        Vec<uint32_t> ord;
        if (!sg.sortInto(ord, sg.stmts.len, [&sg](uint32_t a, uint32_t b) {
                int c = cmp(sg.stmts[a].loc, sg.stmts[b].loc);
                if (c) return c < 0;
                c = sg.arena.compare(sg.stmts[a].name, sg.stmts[b].name);
                if (c) return c < 0;
                return sg.stmts[a].count < sg.stmts[b].count;
            })) {
            st.fail(Err::OutOfMemory);
            return;
        }
        for (uint32_t i = 0; i < ord.len; ++i) {
            const RStmt& s = sg.stmts[ord[i]];
            o.lit("<statement");
            renderAttrGroup(o, sg, s.slot, /*isBin=*/false, st);
            o.put('>');
            renderId(o, "id", s.loc, st);
            renderBin(o, sg, "bin", s.name, s.count, s.slot, st);
            o.lit("</statement>");
        }
    } else {
        Vec<uint32_t> ord;
        if (!sg.sortInto(ord, sg.blocks.len, [&sg](uint32_t a, uint32_t b) {
                int c = cmp(sg.blocks[a].id, sg.blocks[b].id);
                if (c) return c < 0;
                return sg.arena.compare(sg.blocks[a].name, sg.blocks[b].name) < 0;
            })) {
            st.fail(Err::OutOfMemory);
            return;
        }

        if (topInProc) {
            Vec<uint32_t> pord;
            if (!sg.sortInto(pord, sg.procs.len, [&sg](uint32_t a, uint32_t b) {
                    return sg.arena.compare(sg.procs[a].type, sg.procs[b].type) < 0;
                })) {
                st.fail(Err::OutOfMemory);
                return;
            }
            for (uint32_t i = 0; i < pord.len; ++i) {
                uint32_t pi = pord[i];
                o.lit("<process");
                attr(o, "processType", sg.text(sg.procs[pi].type), st.sanitize);
                renderAttrGroup(o, sg, sg.procs[pi].slot, /*isBin=*/false, st);
                o.put('>');
                for (uint32_t j = 0; j < ord.len; ++j) {
                    uint32_t bi = ord[j];
                    if (sg.blocks[bi].process == pi && sg.blocks[bi].parent == kNone)
                        renderOneBlock(o, sg, bi, "block", ord, st);
                }
                renderUserAttrs(o, sg, sg.procs[pi].slot, st);
                o.lit("</process>");
            }
        } else {
            renderBlockChildren(o, sg, kNone, ord, st);
        }
    }

    o.lit("</blockCoverage>");
}

// ---------------------------------------------------------------------------
// Condition (I-2.3)
//
// EXPR requires @index and @width, which the caller never supplies: @index is
// the expression's ordinal within the scope (assigned after sorting, so it does
// not depend on emission order) and @width is its sub-expression count.
// ---------------------------------------------------------------------------
inline bool exprHasBins(const Stage& sg, uint32_t ei) {
    for (uint32_t k = 0; k < sg.exprBins.len; ++k)
        if (sg.exprBins[k].expr == ei) return true;
    return false;
}

// One EXPR, under `tag` -- "expr" at the top level and "hierarchicalExpr" when
// nested. Both are the same complex type, so one function serves both.
inline void renderExpr(Out& o, Stage& sg, uint32_t ei, Text tag,
                       const Vec<uint32_t>& ord, const Vec<uint32_t>& index,
                       Status& st, KeyGen& keys) {
    const RExpr& e = sg.exprs[ei];

    o.put('<');
    o.write(tag);
    attr(o, "name", sg.text(e.name), st.sanitize);
    o.lit(" key=\"");
    keys.writeKey(o, sg.text(e.name), st);
    o.put('"');
    attr(o, "exprString", sg.text(e.exprString), st.sanitize);
    attrU(o, "index", index[ei]);
    attrU(o, "width", e.subCount ? e.subCount : 1);
    if (!e.stmtType.empty())
        attr(o, "statementType", sg.text(e.stmtType), st.sanitize);
    renderAttrGroup(o, sg, e.slot, /*isBin=*/false, st);
    o.put('>');

    renderId(o, "id", e.loc, st);

    // subExpr has minOccurs="1". When the caller listed none, the expression
    // text itself is the only honest stand-in.
    if (e.subCount == 0) {
        o.lit("<subExpr>");
        writeEscaped(o, sg.text(e.exprString), st.sanitize);
        o.lit("</subExpr>");
    } else {
        // Emitted in the sequence the caller declared: sub-expression position
        // is what a bin's name refers to, so this is one of the lists whose
        // order is the information.
        for (uint32_t seq = 0; seq < e.subCount; ++seq)
            for (uint32_t i = 0; i < sg.subExprs.len; ++i) {
                if (sg.subExprs[i].expr != ei || sg.subExprs[i].seq != seq) continue;
                o.lit("<subExpr>");
                writeEscaped(o, sg.text(sg.subExprs[i].text), st.sanitize);
                o.lit("</subExpr>");
                break;
            }
    }

    // Bins are an unordered set of facts about the expression, so they get the
    // same content-derived order as everything else.
    Vec<uint32_t> bins;
    for (uint32_t i = 0; i < sg.exprBins.len; ++i)
        if (sg.exprBins[i].expr == ei) bins.push(i);
    Vec<uint32_t> bord;
    if (!sg.sortInto(bord, bins.len, [&sg, &bins](uint32_t a, uint32_t b) {
            int c = sg.arena.compare(sg.exprBins[bins[a]].name,
                                     sg.exprBins[bins[b]].name);
            if (c) return c < 0;
            return sg.exprBins[bins[a]].count < sg.exprBins[bins[b]].count;
        })) {
        st.fail(Err::OutOfMemory);
        return;
    }
    for (uint32_t i = 0; i < bord.len; ++i) {
        const RExprBin& b = sg.exprBins[bins[bord[i]]];
        renderBin(o, sg, "bin", b.name, b.count, b.slot, st);
    }

    for (uint32_t i = 0; i < ord.len; ++i) {
        uint32_t ci = ord[i];
        if (sg.exprs[ci].parent != ei) continue;
        if (!exprHasBins(sg, ci)) { st.warn(); continue; }
        renderExpr(o, sg, ci, "hierarchicalExpr", ord, index, st, keys);
    }

    renderUserAttrs(o, sg, e.slot, st);
    o.lit("</");
    o.write(tag);
    o.put('>');
}

inline void renderCondition(Out& o, Stage& sg, Status& st) {
    if (sg.exprs.len == 0) return;

    Vec<uint32_t> ord;
    if (!sg.sortInto(ord, sg.exprs.len, [&sg](uint32_t a, uint32_t b) {
            int c = cmp(sg.exprs[a].loc, sg.exprs[b].loc);
            if (c) return c < 0;
            c = sg.arena.compare(sg.exprs[a].name, sg.exprs[b].name);
            if (c) return c < 0;
            return sg.arena.compare(sg.exprs[a].exprString, sg.exprs[b].exprString) < 0;
        })) {
        st.fail(Err::OutOfMemory);
        return;
    }

    // @index is the position in sorted order, so it is stable across emission
    // orders in the way T-3 requires.
    Vec<uint32_t> index;
    if (!index.reserve(sg.exprs.len)) { st.fail(Err::OutOfMemory); return; }
    index.len = sg.exprs.len;
    for (uint32_t i = 0; i < ord.len; ++i) index[ord[i]] = i;

    // An EXPR with no bins cannot be emitted (bin has minOccurs="1"), and a
    // conditionCoverage element with nothing in it is not worth writing.
    uint32_t emitted = 0;
    for (uint32_t i = 0; i < ord.len; ++i)
        if (sg.exprs[ord[i]].parent == kNone && exprHasBins(sg, ord[i])) ++emitted;
    if (emitted == 0) {
        st.warn();
        return;
    }

    KeyGen keys;
    o.lit("<conditionCoverage");
    renderMetricAttrs(o, sg, CovKind::Condition, st);
    o.put('>');
    for (uint32_t i = 0; i < ord.len; ++i) {
        uint32_t ei = ord[i];
        if (sg.exprs[ei].parent != kNone) continue;
        if (!exprHasBins(sg, ei)) { st.warn(); continue; }
        renderExpr(o, sg, ei, "expr", ord, index, st, keys);
    }
    o.lit("</conditionCoverage>");
}

// ---------------------------------------------------------------------------
// Branch (I-2.4)
//
// Arms grouped on (file, line) into a BRANCH_STATEMENT; each arm is a BRANCH
// with its own id and branchBin, and branchBin follows any nested branches.
// ---------------------------------------------------------------------------
// One BRANCH_STATEMENT, under `tag` -- "statement" at the top of
// branchCoverage, "nestedBranch" inside an arm. Same complex type either way.
inline void renderBranchStatement(Out& o, Stage& sg, uint32_t si, Text tag,
                                  const Vec<uint32_t>& sord, Status& st) {
    const RBrStmt& s = sg.brStmts[si];

    o.put('<');
    o.write(tag);
    if (!s.expr.empty()) attr(o, "branchExpr", sg.text(s.expr), st.sanitize);
    attr(o, "statementType", sg.text(s.stmtType), st.sanitize);
    renderAttrGroup(o, sg, s.slot, /*isBin=*/false, st);
    o.put('>');
    renderId(o, "id", s.loc, st);

    // This statement's arms, ordered by name so they appear in a defined order
    // regardless of the order the caller called arm() in.
    Vec<uint32_t> mine;
    for (uint32_t i = 0; i < sg.brArms.len; ++i)
        if (sg.brArms[i].stmt == si) mine.push(i);

    if (mine.len) {
        Vec<uint32_t> aord;
        if (!sg.sortInto(aord, mine.len, [&sg, &mine](uint32_t a, uint32_t b) {
                int c = sg.arena.compare(sg.brArms[mine[a]].name,
                                         sg.brArms[mine[b]].name);
                if (c) return c < 0;
                return cmp(sg.brArms[mine[a]].loc, sg.brArms[mine[b]].loc) < 0;
            })) {
            st.fail(Err::OutOfMemory);
            return;
        }
        for (uint32_t i = 0; i < aord.len; ++i) {
            uint32_t ai = mine[aord[i]];
            const RBrArm& a = sg.brArms[ai];
            o.lit("<branch>");
            renderId(o, "id", a.loc, st);
            // BRANCH's sequence puts branchBin after nestedBranch*, so the
            // arm's own bin is held back until its children are written.
            for (uint32_t j = 0; j < sord.len; ++j)
                if (sg.brStmts[sord[j]].parentArm == ai)
                    renderBranchStatement(o, sg, sord[j], "nestedBranch", sord, st);
            renderBin(o, sg, "branchBin", a.name, a.count, a.slot, st);
            o.lit("</branch>");
        }
    }

    renderUserAttrs(o, sg, s.slot, st);
    o.lit("</");
    o.write(tag);
    o.put('>');
}

inline void renderBranch(Out& o, Stage& sg, Status& st) {
    if (sg.brStmts.len == 0) return;

    Vec<uint32_t> ord;
    if (!sg.sortInto(ord, sg.brStmts.len, [&sg](uint32_t a, uint32_t b) {
            int c = cmp(sg.brStmts[a].loc, sg.brStmts[b].loc);
            if (c) return c < 0;
            return sg.arena.compare(sg.brStmts[a].expr, sg.brStmts[b].expr) < 0;
        })) {
        st.fail(Err::OutOfMemory);
        return;
    }

    o.lit("<branchCoverage");
    renderMetricAttrs(o, sg, CovKind::Branch, st);
    o.put('>');
    for (uint32_t i = 0; i < ord.len; ++i) {
        if (sg.brStmts[ord[i]].parentArm != kNone) continue;  // rendered nested
        renderBranchStatement(o, sg, ord[i], "statement", ord, st);
    }
    o.lit("</branchCoverage>");
}

}  // namespace stage
}  // namespace UCIS_XML_NAMESPACE

// ==== ux_render_func.hpp ======================================================

// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// ux_render_func.hpp - the functional-coverage renderers: FSM, assertion,
// covergroup. Tasks I-3.1 through I-3.3.
//
// Three of the ordering rules design §4 lists as the writer's problem live
// here: FSM states before transitions, ASSERTION's eight optional bins in their
// one legal sequence, and CGINSTANCE's coverpoints before its crosses.


namespace UCIS_XML_NAMESPACE {
namespace stage {

// ---------------------------------------------------------------------------
// FSM (I-3.1)
// ---------------------------------------------------------------------------
inline void renderFsm(Out& o, Stage& sg, Status& st) {
    if (sg.fsms.len == 0) return;

    Vec<uint32_t> ord;
    if (!sg.sortInto(ord, sg.fsms.len, [&sg](uint32_t a, uint32_t b) {
            int c = sg.arena.compare(sg.fsms[a].name, sg.fsms[b].name);
            if (c) return c < 0;
            return sg.arena.compare(sg.fsms[a].type, sg.fsms[b].type) < 0;
        })) {
        st.fail(Err::OutOfMemory);
        return;
    }

    o.lit("<fsmCoverage");
    renderMetricAttrs(o, sg, CovKind::Fsm, st);
    o.put('>');

    for (uint32_t oi = 0; oi < ord.len; ++oi) {
        const uint32_t fi = ord[oi];
        const RFsm& f = sg.fsms[fi];

        o.lit("<fsm");
        if (!f.name.empty()) attr(o, "name", sg.text(f.name), st.sanitize);
        if (!f.type.empty()) attr(o, "type", sg.text(f.type), st.sanitize);
        if (f.width) attrU(o, "width", positive(f.width, st.sanitize));
        renderAttrGroup(o, sg, f.slot, /*isBin=*/false, st);
        o.put('>');

        // state* before stateTransition*, whichever order the caller used.
        Vec<uint32_t> mine;
        for (uint32_t i = 0; i < sg.fsmStates.len; ++i)
            if (sg.fsmStates[i].fsm == fi) mine.push(i);

        Vec<uint32_t> sord;
        if (!sg.sortInto(sord, mine.len, [&sg, &mine](uint32_t a, uint32_t b) {
                int c = sg.arena.compare(sg.fsmStates[mine[a]].name,
                                         sg.fsmStates[mine[b]].name);
                if (c) return c < 0;
                return sg.arena.compare(sg.fsmStates[mine[a]].value,
                                        sg.fsmStates[mine[b]].value) < 0;
            })) {
            st.fail(Err::OutOfMemory);
            return;
        }
        for (uint32_t i = 0; i < sord.len; ++i) {
            const RFsmState& s = sg.fsmStates[mine[sord[i]]];
            o.lit("<state");
            if (!s.name.empty()) attr(o, "stateName", sg.text(s.name), st.sanitize);
            if (!s.value.empty()) attr(o, "stateValue", sg.text(s.value), st.sanitize);
            o.put('>');
            renderBin(o, sg, "stateBin", Str(), s.count, s.slot, st);
            o.lit("</state>");
        }

        Vec<uint32_t> tmine;
        for (uint32_t i = 0; i < sg.fsmTrans.len; ++i)
            if (sg.fsmTrans[i].fsm == fi) tmine.push(i);

        Vec<uint32_t> tord;
        if (!sg.sortInto(tord, tmine.len, [&sg, &tmine](uint32_t a, uint32_t b) {
                const RFsmTrans& x = sg.fsmTrans[tmine[a]];
                const RFsmTrans& y = sg.fsmTrans[tmine[b]];
                uint32_t n = x.n < y.n ? x.n : y.n;
                for (uint32_t k = 0; k < n; ++k) {
                    int c = sg.arena.compare(sg.strPool[x.first + k],
                                             sg.strPool[y.first + k]);
                    if (c) return c < 0;
                }
                return x.n < y.n;
            })) {
            st.fail(Err::OutOfMemory);
            return;
        }
        for (uint32_t i = 0; i < tord.len; ++i) {
            const RFsmTrans& t = sg.fsmTrans[tmine[tord[i]]];
            // FSM_TRANSITION requires state with minOccurs="2"; a path shorter
            // than that is not expressible, so it is dropped rather than padded.
            if (t.n < 2) { st.warn(); continue; }

            for (uint32_t k = 0; k < t.n; ++k) {
                Text want = sg.text(sg.strPool[t.first + k]);
                bool known = false;
                for (uint32_t j = 0; j < mine.len && !known; ++j)
                    known = equal(sg.text(sg.fsmStates[mine[j]].name), want);
                if (known) continue;
                Message& m = st.failWith(Err::UnknownState);
                m.add("fsm ").quoted(sg.text(f.name))
                 .add(" has a transition through state ").quoted(want)
                 .add(", which was never declared with state()");
                st.latched();
                return;
            }

            o.lit("<stateTransition>");
            for (uint32_t k = 0; k < t.n; ++k) {
                o.lit("<state>");
                writeEscaped(o, sg.text(sg.strPool[t.first + k]), st.sanitize);
                o.lit("</state>");
            }
            renderBin(o, sg, "transitionBin", Str(), t.count, t.slot, st);
            o.lit("</stateTransition>");
        }

        renderUserAttrs(o, sg, f.slot, st);
        o.lit("</fsm>");
    }

    o.lit("</fsmCoverage>");
}

// ---------------------------------------------------------------------------
// Assertion (I-3.2)
// ---------------------------------------------------------------------------
inline void renderAssertion(Out& o, Stage& sg, Status& st) {
    if (sg.asserts.len == 0) return;

    Vec<uint32_t> ord;
    if (!sg.sortInto(ord, sg.asserts.len, [&sg](uint32_t a, uint32_t b) {
            int c = sg.arena.compare(sg.asserts[a].name, sg.asserts[b].name);
            if (c) return c < 0;
            return sg.arena.compare(sg.asserts[a].kind, sg.asserts[b].kind) < 0;
        })) {
        st.fail(Err::OutOfMemory);
        return;
    }

    // ASSERTION's sequence, in the schema's declared order. The caller calls
    // attempts()/passes()/... in any order; the array index is the schema order.
    static const char* const kTags[kAssertBinCount] = {
        "coverBin", "passBin", "failBin", "vacuousBin",
        "disabledBin", "attemptBin", "activeBin", "peakActiveBin",
    };
    o.lit("<assertionCoverage");
    renderMetricAttrs(o, sg, CovKind::Assertion, st);
    o.put('>');

    for (uint32_t oi = 0; oi < ord.len; ++oi) {
        const RAssert& a = sg.asserts[ord[oi]];
        o.lit("<assertion");
        attr(o, "name", sg.text(a.name), st.sanitize);
        attr(o, "assertionKind", sg.text(a.kind), st.sanitize);
        renderAttrGroup(o, sg, a.slot, /*isBin=*/false, st);
        o.put('>');
        for (uint32_t i = 0; i < kAssertBinCount; ++i) {
            if ((a.present & (1u << i)) == 0) continue;
            renderBin(o, sg, kTags[i], Str(), a.bins[i], a.binSlot[i], st);
        }
        renderUserAttrs(o, sg, a.slot, st);
        o.lit("</assertion>");
    }

    o.lit("</assertionCoverage>");
}

// ---------------------------------------------------------------------------
// Covergroup (I-3.3)
// ---------------------------------------------------------------------------

// COVERPOINT_BIN's contents live inside range/sequence rather than in a BIN, so
// unlike every other bin in the schema it carries neither binAttributes nor
// objAttributes -- only @alias. Exclusion, goal and weight are therefore carried
// as userAttr, which is both schema-valid and the convention already used
// elsewhere in this tree for information UCIS has no attribute for. The call
// does what it says and nothing is lost; the mapping is documented rather than
// warned about.
inline void renderCoverpointBinExtras(Out& o, const Stage& sg, const Extra& e,
                                      Status& st) {
    if (e.excluded) {
        o.lit("<userAttr key=\"excluded\" type=\"str\">true</userAttr>");
    }
    if (!e.excludedReason.empty()) {
        o.lit("<userAttr key=\"excludedReason\" type=\"str\">");
        writeEscaped(o, sg.text(e.excludedReason), st.sanitize);
        o.lit("</userAttr>");
    }
    if (e.goal != Extra::kNoGoal) {
        o.lit("<userAttr key=\"coverageCountGoal\" type=\"int64\">");
        writeUInt(o, e.goal);
        o.lit("</userAttr>");
    }
    if (e.weight != Extra::kNoWeight) {
        o.lit("<userAttr key=\"weight\" type=\"int64\">");
        writeUInt(o, e.weight);
        o.lit("</userAttr>");
    }
}

inline void renderCoverpointBin(Out& o, Stage& sg, const RCpBin& b, KeyGen& keys,
                                Status& st) {
    const Extra& e = sg.extraAt(b.slot);

    o.lit("<coverpointBin");
    attr(o, "name", sg.text(b.name), st.sanitize);
    o.lit(" key=\"");
    keys.writeKey(o, sg.text(b.name), st);
    o.put('"');
    attr(o, "type", Text(toString(b.type)), st.sanitize);
    if (!e.alias.empty()) attr(o, "alias", sg.text(e.alias), st.sanitize);
    o.put('>');

    if (b.seqCount) {
        o.lit("<sequence>");
        renderContents(o, sg, Str(), b.count, st);
        for (uint32_t i = 0; i < b.seqCount; ++i) {
            o.lit("<seqValue>");
            writeInt(o, sg.intPool[b.seqFirst + i]);
            o.lit("</seqValue>");
        }
        o.lit("</sequence>");
    } else {
        o.lit("<range");
        attrI(o, "from", b.from);
        attrI(o, "to", b.to);
        o.put('>');
        renderContents(o, sg, Str(), b.count, st);
        o.lit("</range>");
    }

    renderCoverpointBinExtras(o, sg, e, st);
    renderUserAttrs(o, sg, b.slot, st);
    o.lit("</coverpointBin>");
}

inline void renderCovergroup(Out& o, Stage& sg, Status& st) {
    if (sg.cgs.len == 0) return;

    Vec<uint32_t> ord;
    if (!sg.sortInto(ord, sg.cgs.len, [&sg](uint32_t a, uint32_t b) {
            int c = sg.arena.compare(sg.cgs[a].name, sg.cgs[b].name);
            if (c) return c < 0;
            return sg.arena.compare(sg.cgs[a].typeName, sg.cgs[b].typeName) < 0;
        })) {
        st.fail(Err::OutOfMemory);
        return;
    }

    KeyGen cgKeys;
    o.lit("<covergroupCoverage");
    renderMetricAttrs(o, sg, CovKind::Covergroup, st);
    o.put('>');

    for (uint32_t oi = 0; oi < ord.len; ++oi) {
        const uint32_t ci = ord[oi];
        const RCg& cg = sg.cgs[ci];

        o.lit("<cgInstance");
        attr(o, "name", sg.text(cg.name), st.sanitize);
        o.lit(" key=\"");
        cgKeys.writeKey(o, sg.text(cg.name), st);
        o.put('"');
        {
            const Extra& e = sg.extraAt(cg.slot);
            if (!e.alias.empty()) attr(o, "alias", sg.text(e.alias), st.sanitize);
            if (e.excluded) attrBool(o, "excluded", true);
            if (!e.excludedReason.empty())
                attr(o, "excludedReason", sg.text(e.excludedReason), st.sanitize);
        }
        o.put('>');

        renderOptions(o, sg, cg.opts, kOptsCgInstance, st);

        o.lit("<cgId");
        attr(o, "cgName", sg.text(cg.typeName.empty() ? cg.name : cg.typeName),
             st.sanitize);
        attr(o, "moduleName", sg.text(cg.moduleName), st.sanitize);
        o.put('>');
        // CG_ID's two ids are the covergroup *instance*'s declaration site and
        // its *type*'s. A caller that gave neither gets the enclosing scope's,
        // which is at least in the right file, rather than file 1 line 1.
        renderId(o, "cginstSourceId", cg.hasInstLoc ? cg.instLoc : sg.id, st);
        renderId(o, "cgSourceId", cg.hasTypeLoc ? cg.typeLoc : sg.id, st);
        o.lit("</cgId>");

        // cgParms is a caller-ordered list -- parameter position is meaningful --
        // so this sorts on the declaration sequence, not on the name.
        Vec<uint32_t> parms;
        for (uint32_t i = 0; i < sg.cgParms.len; ++i)
            if (sg.cgParms[i].cg == ci) parms.push(i);
        Vec<uint32_t> parmOrd;
        if (!sg.sortInto(parmOrd, parms.len, [&sg, &parms](uint32_t a, uint32_t b) {
                return sg.cgParms[parms[a]].seq < sg.cgParms[parms[b]].seq;
            })) {
            st.fail(Err::OutOfMemory);
            return;
        }
        for (uint32_t i = 0; i < parmOrd.len; ++i) {
            const RCgParm& p = sg.cgParms[parms[parmOrd[i]]];
            o.lit("<cgParms><name>");
            writeEscaped(o, sg.text(p.name), st.sanitize);
            o.lit("</name><value>");
            writeEscaped(o, sg.text(p.value), st.sanitize);
            o.lit("</value></cgParms>");
        }

        // coverpoint* then cross*, whichever order the caller declared them in.
        Vec<uint32_t> mine;
        for (uint32_t i = 0; i < sg.cps.len; ++i)
            if (sg.cps[i].cg == ci) mine.push(i);

        Vec<uint32_t> cpOrd;
        if (!sg.sortInto(cpOrd, mine.len, [&sg, &mine](uint32_t a, uint32_t b) {
                return sg.arena.compare(sg.cps[mine[a]].name,
                                        sg.cps[mine[b]].name) < 0;
            })) {
            st.fail(Err::OutOfMemory);
            return;
        }

        KeyGen cpKeys;
        for (uint32_t i = 0; i < cpOrd.len; ++i) {
            const uint32_t pi = mine[cpOrd[i]];
            const RCp& cp = sg.cps[pi];

            Vec<uint32_t> bins;
            for (uint32_t k = 0; k < sg.cpBins.len; ++k)
                if (sg.cpBins[k].cp == pi) bins.push(k);
            // coverpointBin has minOccurs="1".
            if (bins.len == 0) { st.warn(); continue; }

            o.lit("<coverpoint");
            attr(o, "name", sg.text(cp.name), st.sanitize);
            o.lit(" key=\"");
            cpKeys.writeKey(o, sg.text(cp.name), st);
            o.put('"');
            {
                const Extra& e = sg.extraAt(cp.slot);
                if (!e.alias.empty()) attr(o, "alias", sg.text(e.alias), st.sanitize);
            }
            if (!cp.exprString.empty())
                attr(o, "exprString", sg.text(cp.exprString), st.sanitize);
            o.put('>');

            renderOptions(o, sg, cp.opts, kOptsCoverpoint, st);

            Vec<uint32_t> binOrd;
            if (!sg.sortInto(binOrd, bins.len, [&sg, &bins](uint32_t a, uint32_t b) {
                    const RCpBin& x = sg.cpBins[bins[a]];
                    const RCpBin& y = sg.cpBins[bins[b]];
                    if (x.type != y.type) return x.type < y.type;
                    int c = sg.arena.compare(x.name, y.name);
                    if (c) return c < 0;
                    if (x.from != y.from) return x.from < y.from;
                    return x.to < y.to;
                })) {
                st.fail(Err::OutOfMemory);
                return;
            }

            KeyGen binKeys;
            for (uint32_t k = 0; k < binOrd.len; ++k)
                renderCoverpointBin(o, sg, sg.cpBins[bins[binOrd[k]]], binKeys, st);

            renderUserAttrs(o, sg, cp.slot, st);
            o.lit("</coverpoint>");
        }

        Vec<uint32_t> xmine;
        for (uint32_t i = 0; i < sg.crosses.len; ++i)
            if (sg.crosses[i].cg == ci) xmine.push(i);

        Vec<uint32_t> xOrd;
        if (!sg.sortInto(xOrd, xmine.len, [&sg, &xmine](uint32_t a, uint32_t b) {
                return sg.arena.compare(sg.crosses[xmine[a]].name,
                                        sg.crosses[xmine[b]].name) < 0;
            })) {
            st.fail(Err::OutOfMemory);
            return;
        }

        KeyGen xKeys;
        for (uint32_t i = 0; i < xOrd.len; ++i) {
            const uint32_t xi = xmine[xOrd[i]];
            const RCross& x = sg.crosses[xi];

            // crossExpr names coverpoints of this covergroup. Naming one that
            // does not exist produces a document that validates but describes
            // nothing, so it is caught here.
            for (uint32_t k = 0; k < x.exprCount; ++k) {
                Text want = sg.text(sg.strPool[x.exprFirst + k]);
                bool known = false;
                for (uint32_t j = 0; j < mine.len && !known; ++j)
                    known = equal(sg.text(sg.cps[mine[j]].name), want);
                if (known) continue;
                Message& m = st.failWith(Err::UnknownCoverpoint);
                m.add("cross ").quoted(sg.text(x.name)).add(" names coverpoint ")
                 .quoted(want).add(", which is not in covergroup ")
                 .quoted(sg.text(cg.name));
                st.latched();
                return;
            }

            o.lit("<cross");
            attr(o, "name", sg.text(x.name), st.sanitize);
            o.lit(" key=\"");
            xKeys.writeKey(o, sg.text(x.name), st);
            o.put('"');
            {
                const Extra& e = sg.extraAt(x.slot);
                if (!e.alias.empty()) attr(o, "alias", sg.text(e.alias), st.sanitize);
            }
            o.put('>');

            renderOptions(o, sg, x.opts, kOptsCross, st);

            // crossExpr order matches the index order of every crossBin tuple,
            // so it is the caller's order, not sorted.
            for (uint32_t k = 0; k < x.exprCount; ++k) {
                o.lit("<crossExpr>");
                writeEscaped(o, sg.text(sg.strPool[x.exprFirst + k]), st.sanitize);
                o.lit("</crossExpr>");
            }

            Vec<uint32_t> xb;
            for (uint32_t k = 0; k < sg.crossBins.len; ++k)
                if (sg.crossBins[k].cross == xi) xb.push(k);

            Vec<uint32_t> xbOrd;
            if (!sg.sortInto(xbOrd, xb.len, [&sg, &xb](uint32_t a, uint32_t b) {
                    const RCrossBin& p = sg.crossBins[xb[a]];
                    const RCrossBin& q = sg.crossBins[xb[b]];
                    uint32_t n = p.idxCount < q.idxCount ? p.idxCount : q.idxCount;
                    for (uint32_t k = 0; k < n; ++k) {
                        int64_t pv = sg.intPool[p.idxFirst + k];
                        int64_t qv = sg.intPool[q.idxFirst + k];
                        if (pv != qv) return pv < qv;
                    }
                    if (p.idxCount != q.idxCount) return p.idxCount < q.idxCount;
                    return sg.arena.compare(p.name, q.name) < 0;
                })) {
                st.fail(Err::OutOfMemory);
                return;
            }

            KeyGen xbKeys;
            for (uint32_t k = 0; k < xbOrd.len; ++k) {
                const RCrossBin& b = sg.crossBins[xb[xbOrd[k]]];
                // CROSS_BIN requires index with minOccurs="1".
                if (b.idxCount == 0) { st.warn(); continue; }
                o.lit("<crossBin");
                attr(o, "name", sg.text(b.name), st.sanitize);
                o.lit(" key=\"");
                xbKeys.writeKey(o, sg.text(b.name), st);
                o.put('"');
                if (!b.type.empty()) attr(o, "type", sg.text(b.type), st.sanitize);
                {
                    const Extra& e = sg.extraAt(b.slot);
                    if (!e.alias.empty()) attr(o, "alias", sg.text(e.alias), st.sanitize);
                }
                o.put('>');
                for (uint32_t j = 0; j < b.idxCount; ++j) {
                    o.lit("<index>");
                    writeInt(o, sg.intPool[b.idxFirst + j]);
                    o.lit("</index>");
                }
                renderContents(o, sg, Str(), b.count, st);
                renderUserAttrs(o, sg, b.slot, st);
                o.lit("</crossBin>");
            }

            renderUserAttrs(o, sg, x.slot, st);
            o.lit("</cross>");
        }

        renderUserAttrs(o, sg, cg.slot, st);
        o.lit("</cgInstance>");
    }

    o.lit("</covergroupCoverage>");
}

}  // namespace stage
}  // namespace UCIS_XML_NAMESPACE

// ==== ux_spool.hpp ============================================================

// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// ux_spool.hpp - the deferred-mode holding area for scope bodies.
//
// Task I-1.6. Under Sources::Deferred the file table is not known until the
// last scope has been seen, but the schema requires it first, so scope bodies
// are held here and replayed after the header at close().
//
// Memory-backed below `threshold` (8 MiB by default) and a temp file above it,
// so the common case never touches disk: OpenTitan's whole compressed body is
// 3.11 MiB (design §3.1).
//
// A note on the gzip-member passthrough described in design §3.1. Concatenated
// gzip members decompress transparently (RFC 1952 §2.2), so a *writer-owned*
// gzip stream can emit the header as one complete member and then copy an
// already-compressed spool through byte for byte, with no recompression at all.
// That requires the writer to own the compression -- with an opaque
// caller-supplied gzip sink there is only one stream, and the spool necessarily
// holds uncompressed bytes. See gzipSink() in ux_zlib.hpp; the cheap path is
// what CoverageWriter::toFile() with a .gz path selects.


namespace UCIS_XML_NAMESPACE {

class Spool {
public:
    explicit Spool(size_t threshold = 8u * 1024u * 1024u)
        : threshold_(threshold), size_(0), file_(nullptr), failed_(false) {}

    ~Spool() { reset(); }

    Spool(const Spool&) = delete;
    Spool& operator=(const Spool&) = delete;

    static int writeFn(void* ctx, const char* p, size_t n) {
        return static_cast<Spool*>(ctx)->append(p, n) ? 0 : 1;
    }

    Sink sink() { return Sink(this, &writeFn); }

    bool append(const char* p, size_t n) {
        if (failed_) return false;
        if (n == 0) return true;

        if (!file_ && size_ + n > threshold_ && !spill()) return false;

        if (file_) {
#if UCIS_XML_NO_STDIO
            failed_ = true;
            return false;
#else
            if (std::fwrite(p, 1, n, fp()) != n) { failed_ = true; return false; }
#endif
        } else {
            if (!mem_.reserve(mem_.len + static_cast<uint32_t>(n))) { failed_ = true; return false; }
            std::memcpy(mem_.ptr + mem_.len, p, n);
            mem_.len += static_cast<uint32_t>(n);
        }
        size_ += n;
        return true;
    }

    // Copies everything spooled to `out`, verbatim. Byte-for-byte matters: when
    // the spool holds compressed gzip members, reinterpreting the bytes in any
    // way would corrupt them.
    bool replay(Out& out) {
        if (failed_) return false;
        if (!file_) {
            out.write(mem_.ptr, mem_.len);
            return !out.failed;
        }
#if UCIS_XML_NO_STDIO
        return false;
#else
        if (std::fflush(fp()) != 0 || std::fseek(fp(), 0, SEEK_SET) != 0) return false;
        char buf[64 * 1024];
        for (;;) {
            size_t got = std::fread(buf, 1, sizeof(buf), fp());
            if (got == 0) break;
            out.write(buf, got);
            if (out.failed) return false;
        }
        return std::ferror(fp()) == 0;
#endif
    }

    void reset() {
#if !UCIS_XML_NO_STDIO
        if (file_) std::fclose(fp());
#endif
        file_ = nullptr;
        mem_.release();
        size_ = 0;
        failed_ = false;
    }

    // Only meaningful before anything has been spooled.
    void setThreshold(size_t v) { threshold_ = v; }

    uint64_t size() const { return size_; }
    bool spilled() const { return file_ != nullptr; }
    bool failed() const { return failed_; }

private:
#if !UCIS_XML_NO_STDIO
    std::FILE* fp() const { return static_cast<std::FILE*>(file_); }
#endif

    // Moves what is already buffered into a temp file and switches over.
    bool spill() {
#if UCIS_XML_NO_STDIO
        // Without stdio there is nowhere to spill to; keep growing in memory
        // rather than dropping coverage, and let the allocator be the limit.
        return true;
#else
        file_ = std::tmpfile();
        if (!file_) { failed_ = true; return false; }
        if (mem_.len && std::fwrite(mem_.ptr, 1, mem_.len, fp()) != mem_.len) {
            failed_ = true;
            return false;
        }
        mem_.release();
        return true;
#endif
    }

    Vec<char> mem_;
    size_t threshold_;
    uint64_t size_;
    // void* rather than std::FILE*: <cstdio> is not included under
    // UCIS_XML_NO_STDIO, and the member has to declare cleanly either way.
    void* file_;
    bool failed_;
};

}  // namespace UCIS_XML_NAMESPACE

// ==== ux_doc.hpp ==============================================================

// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// ux_doc.hpp - the document frame: the root element, the source-file table, the
// history node, and the scope stack.
//
// Task I-1.9. Everything the schema requires but the caller never mentions is
// supplied here (design §4): file ids, instance ids, the nine required
// HISTORY_NODE attributes, and the timestamp.


#ifndef UCIS_XML_NO_TIME
#define UCIS_XML_NO_TIME 0
#endif


#if !UCIS_XML_NO_TIME
#endif

namespace UCIS_XML_NAMESPACE {

/**
 * Describes the tool that produced the coverage. Required.
 *
 * .. code-block:: cpp
 *
 *    cov.tool(ux::Tool().name("verilator").version("5.040").vendorId("VLTR"));
 *
 * Chained setters rather than designated initializers, because the standard
 * floor is C++17. The members are public, so the C++20 aggregate form works
 * too; it is never required.
 *
 * Four of ``HISTORY_NODE``'s nine mandatory attributes come from here.
 */
struct Tool {
    Text name_, version_, vendorId_, category_;

    /** Constructs with ``category`` defaulted to ``"UCIS:simulator"``. */
    Tool() : category_("UCIS:simulator") {}

    /**
     * Sets the tool's name, emitted as ``@vendorTool`` and ``@writtenBy``.
     *
     * :param v: Tool name, e.g. ``"verilator"``.
     * :return: ``*this``, for chaining.
     */
    Tool& name(Text v) { name_ = v; return *this; }

    /**
     * Sets the tool's version, emitted as ``@vendorToolVersion``.
     *
     * :param v: Version string, e.g. ``"5.040"``.
     * :return: ``*this``, for chaining.
     */
    Tool& version(Text v) { version_ = v; return *this; }

    /**
     * Sets the vendor identifier, emitted as ``@vendorId``.
     *
     * :param v: Vendor id, e.g. ``"VLTR"``.
     * :return: ``*this``, for chaining.
     */
    Tool& vendorId(Text v) { vendorId_ = v; return *this; }

    /**
     * Sets what kind of tool this is, emitted as ``@toolCategory``.
     *
     * :param v: Category, e.g. ``"UCIS:simulator"`` or ``"UCIS:formal"``.
     *     Defaults to ``"UCIS:simulator"``.
     * :return: ``*this``, for chaining.
     */
    Tool& category(Text v) { category_ = v; return *this; }
};

/**
 * Describes the test run this document records. Required.
 *
 * .. code-block:: cpp
 *
 *    cov.test(ux::Test().name("run1").passed(true).seed("12345"));
 *
 * Only ``name`` and ``passed`` are required by the schema; everything else is
 * optional provenance that merge and triage tools use, and that costs nothing
 * to supply if you have it.
 */
struct Test {
    Text name_, seed_, cmd_, args_, cwd_, userName_, comment_, physicalName_, kind_;
    bool passed_;
    bool haveSimTime_, haveCpuTime_;
    double simTime_, cpuTime_;
    Text timeunit_;

    /** Constructs a test that is marked passed, with no optional fields set. */
    Test()
        : passed_(true), haveSimTime_(false), haveCpuTime_(false),
          simTime_(0), cpuTime_(0) {}

    /**
     * Sets the test's name, emitted as ``@logicalName``. Required.
     *
     * :param v: Test name, e.g. ``"smoke"``.
     * :return: ``*this``, for chaining.
     */
    Test& name(Text v) { name_ = v; return *this; }

    /**
     * Sets whether the test passed, emitted as ``@testStatus``.
     *
     * :param v: ``true`` if it passed. Defaults to ``true``.
     * :return: ``*this``, for chaining.
     */
    Test& passed(bool v) { passed_ = v; return *this; }

    /**
     * Sets the random seed, emitted as ``@seed``.
     *
     * :param v: The seed, as text -- seeds outgrow 64 bits.
     * :return: ``*this``, for chaining.
     */
    Test& seed(Text v) { seed_ = v; return *this; }

    /**
     * Sets the command that ran the test, emitted as ``@cmd``.
     *
     * :param v: The command name.
     * :return: ``*this``, for chaining.
     */
    Test& cmd(Text v) { cmd_ = v; return *this; }

    /**
     * Sets the command's arguments, emitted as ``@args``.
     *
     * :param v: The argument string.
     * :return: ``*this``, for chaining.
     */
    Test& args(Text v) { args_ = v; return *this; }

    /**
     * Sets the working directory, emitted as ``@runCwd``.
     *
     * :param v: Absolute path the test ran in.
     * :return: ``*this``, for chaining.
     */
    Test& cwd(Text v) { cwd_ = v; return *this; }

    /**
     * Sets who ran the test, emitted as ``@userName``.
     *
     * :param v: User name.
     * :return: ``*this``, for chaining.
     */
    Test& userName(Text v) { userName_ = v; return *this; }

    /**
     * Sets a free-form comment, emitted as ``@comment``.
     *
     * :param v: The comment.
     * :return: ``*this``, for chaining.
     */
    Test& comment(Text v) { comment_ = v; return *this; }

    /**
     * Sets the test's on-disk name, emitted as ``@physicalName``.
     *
     * Use it when the logical name is not what the results directory is
     * called.
     *
     * :param v: The physical name.
     * :return: ``*this``, for chaining.
     */
    Test& physicalName(Text v) { physicalName_ = v; return *this; }

    /**
     * Sets what kind of run this was, emitted as ``@kind``.
     *
     * :param v: Kind, e.g. ``"simulation"``.
     * :return: ``*this``, for chaining.
     */
    Test& kind(Text v) { kind_ = v; return *this; }

    /**
     * Sets how much simulated time the test covered.
     *
     * :param v: The time, emitted as ``@simtime``.
     * :param unit: Its unit, emitted as ``@timeunit``, e.g. ``"ns"``.
     * :return: ``*this``, for chaining.
     */
    Test& simTime(double v, Text unit) {
        simTime_ = v; timeunit_ = unit; haveSimTime_ = true; return *this;
    }

    /**
     * Sets how much CPU time the test consumed, emitted as ``@cpuTime``.
     *
     * :param v: Seconds.
     * :return: ``*this``, for chaining.
     */
    Test& cpuTime(double v) { cpuTime_ = v; haveCpuTime_ = true; return *this; }
};

/**
 * Document-level settings, passed to :cpp:func:`CoverageWriter::open`.
 *
 * Every default is the measured or safe choice; a caller that sets none of
 * these gets compact, correct output.
 */
struct WriterOptions {
    Sources sources_;
    bool pretty_;
    bool singleMember_;
    size_t stagingLimit_;
    size_t spoolThreshold_;
    Text writtenTime_;

    /** Constructs with every option at its default. */
    WriterOptions()
        : sources_(Sources::UpFront), pretty_(false), singleMember_(false),
          stagingLimit_(64u * 1024u * 1024u),
          spoolThreshold_(8u * 1024u * 1024u) {}

    /**
     * Chooses how the source-file table is built.
     *
     * :param v: The mode. Defaults to :cpp:enumerator:`Sources::UpFront`.
     * :return: ``*this``, for chaining.
     */
    WriterOptions& sources(Sources v) { sources_ = v; return *this; }

    /**
     * Turns on indented output.
     *
     * For golden files and human inspection only: it roughly doubles the byte
     * count, and at these sizes that is not free.
     *
     * :param v: ``true`` to indent. Defaults to ``false``.
     * :return: ``*this``, for chaining.
     */
    WriterOptions& pretty(bool v) { pretty_ = v; return *this; }

    /**
     * Forces a single gzip member instead of concatenated ones.
     *
     * Multi-member gzip is standard and handled by ``gzip``, ``zcat``, zlib's
     * ``gzread`` and Python's ``gzip``, but a consumer that calls raw
     * ``inflate()`` once sees only the first member. Set this if that risk is
     * unacceptable, at the cost of a recompression pass.
     *
     * :param v: ``true`` to force one member. Defaults to ``false``.
     * :return: ``*this``, for chaining.
     */
    WriterOptions& singleMember(bool v) { singleMember_ = v; return *this; }

    /**
     * Sets the staged-memory tripwire.
     *
     * A scope staging more than this counts a warning and reports through
     * :cpp:func:`CoverageWriter::stagingPeak`. Nothing is dropped and nothing
     * fails; it exists so unbounded growth is visible rather than silent.
     *
     * :param v: Bytes. Defaults to 64 MiB, about eleven times the largest
     *     scope measured on OpenTitan.
     * :return: ``*this``, for chaining.
     */
    WriterOptions& stagingLimit(size_t v) { stagingLimit_ = v; return *this; }

    /**
     * Sets when the deferred-mode spool moves from memory to a temp file.
     *
     * :param v: Bytes. Defaults to 8 MiB.
     * :return: ``*this``, for chaining.
     */
    WriterOptions& spoolThreshold(size_t v) { spoolThreshold_ = v; return *this; }

    /**
     * Overrides the document timestamp instead of reading the clock.
     *
     * This is how golden files stay stable across runs.
     *
     * :param v: An ``xsd:dateTime``, e.g. ``"2026-01-01T00:00:00"``. Defaults
     *     to empty, which uses the current UTC time.
     * :return: ``*this``, for chaining.
     */
    WriterOptions& writtenTime(Text v) { writtenTime_ = v; return *this; }
};

namespace stage {

// ---------------------------------------------------------------------------
// Doc: the document being written, and the stack of scopes staged for it.
//
// The scope stack exists for design §5.2's nested form. A nested child closes
// before its parent, so children are written to the output ahead of their
// parent -- which is legal, since INSTANCE_COVERAGE elements are a flat
// sequence linked by @parentInstanceId and the schema declares no key/keyref
// between them.
// ---------------------------------------------------------------------------
class Doc {
public:
    Doc()
        : spool_(8u * 1024u * 1024u), body_(nullptr),
          passed_(true), haveSimTime_(false), haveCpuTime_(false),
          simTime_(0), cpuTime_(0), nextInstanceId_(1),
          haveTool_(false), haveTest_(false), headerWritten_(false),
          closed_(false), scopeCount_(0), stagingPeak_(0), overLimit_(0) {}

    ~Doc() { clearStack(); }

    Doc(const Doc&) = delete;
    Doc& operator=(const Doc&) = delete;

    Status st;
    WriterOptions opt;

    bool open(Sink sink) {
        if (!out_.open(sink)) return st.fail(Err::SinkFailed);
        files_.setMode(opt.sources_);
        spool_.setThreshold(opt.spoolThreshold_);
        if (opt.sources_ == Sources::Deferred) {
            if (!spoolOut_.open(spool_.sink())) return st.fail(Err::OutOfMemory);
            body_ = &spoolOut_;
        } else {
            body_ = &out_;
        }
        return true;
    }

    void setTool(const Tool& t) {
        toolName_ = strings_.add(t.name_);
        toolVersion_ = strings_.add(t.version_);
        toolVendor_ = strings_.add(t.vendorId_);
        toolCategory_ = strings_.add(t.category_);
        haveTool_ = true;
    }

    void setTest(const Test& t) {
        testName_ = strings_.add(t.name_);
        testSeed_ = strings_.add(t.seed_);
        testCmd_ = strings_.add(t.cmd_);
        testArgs_ = strings_.add(t.args_);
        testCwd_ = strings_.add(t.cwd_);
        testUser_ = strings_.add(t.userName_);
        testComment_ = strings_.add(t.comment_);
        testPhysical_ = strings_.add(t.physicalName_);
        testKind_ = strings_.add(t.kind_);
        testUnit_ = strings_.add(t.timeunit_);
        // Only the scalars are kept: a copy of Test would hold Text views
        // into caller memory that need not outlive this call.
        passed_ = t.passed_;
        haveSimTime_ = t.haveSimTime_;
        simTime_ = t.simTime_;
        haveCpuTime_ = t.haveCpuTime_;
        cpuTime_ = t.cpuTime_;
        haveTest_ = true;
    }

    void addSource(Text path) {
        if (closed_) { st.fail(Err::ClosedWriter); return; }
        if (files_.frozen()) {
            Message& m = st.failWith(Err::LateSourceFile);
            m.add("source file ").quoted(path)
             .add(" registered after the first scope; call cov.sources() before "
                  "emitting coverage, or construct with Sources::Deferred");
            st.latched();
            return;
        }
        files_.declare(path, st);
    }

    uint32_t resolveFile(Text path) {
        return files_.resolve(path, top() ? top()->text(top()->name) : Text(), st);
    }

    Stage* top() { return stack_.len ? stack_[stack_.len - 1] : nullptr; }

    // Opens a scope. `parent` is the staged parent for the nested form, or
    // nullptr for the flat form.
    Stage* beginScope(Text name, Text moduleName, const Loc* loc, Stage* parent) {
        if (closed_ || !st.ok()) return nullptr;
        // The file list is complete once the first scope opens, so under
        // UpFront this is where the header can finally be written.
        files_.freeze();
        if (!headerWritten_ && opt.sources_ == Sources::UpFront) {
            if (!writeHeader(out_)) return nullptr;
        }

        // Rule 2 of the contract: scopes are contiguous. A name that has been
        // used before and closed means the caller came back to it, which does
        // not fail validation -- it just quietly splits one instance's coverage
        // across several instanceCoverages elements, which is worse, because
        // nothing downstream will tell them either.
        bool fresh = false;
        uint32_t seenAt = scopeNames_.getOrAdd(scopeArena_, name, scopeCount_, &fresh);
        if (seenAt != StrMap::kAbsent && !fresh) {
            Message& m = st.failWith(Err::ScopeReopened);
            m.add("scope ").quoted(name)
             .add(" was opened again after being closed; scopes must be "
                  "contiguous, so emit all of a scope's coverage before moving "
                  "on (ux::sortByScope() groups a flat record list for you)");
            st.latched();
            return nullptr;
        }

        Stage* s = new (std::nothrow) Stage();
        if (!s) { st.fail(Err::OutOfMemory); return nullptr; }
        s->files = &files_;
        s->status = &st;
        s->name = s->str(name);
        s->moduleName = s->str(moduleName);
        s->instanceId = nextInstanceId_++;
        s->parentInstanceId = parent ? parent->instanceId : kNone;
        if (loc) s->id = s->resolve(loc->file, loc->line, loc->inlineCount);
        if (stack_.push(s) == UINT32_MAX) {
            delete s;
            st.fail(Err::OutOfMemory);
            return nullptr;
        }
        return s;
    }

    // Closes the innermost scope. Rendering happens here: this is the point at
    // which the whole scope is known and can be sorted into schema order.
    void endScope(Stage* s) {
        if (!s) return;
        // Find it; the flat form always closes the innermost, but a caller
        // holding a parent handle can close out of order and we should say so
        // rather than corrupt the stack.
        if (stack_.len == 0 || stack_[stack_.len - 1] != s) {
            uint32_t at = kNone;
            for (uint32_t i = 0; i < stack_.len; ++i)
                if (stack_[i] == s) { at = i; break; }
            if (at == kNone) return;  // already closed
            Message& m = st.failWith(Err::ScopeReopened);
            m.add("scope ").quoted(s->text(s->name))
             .add(" closed while ").quoted(stack_[stack_.len - 1]->text(
                      stack_[stack_.len - 1]->name))
             .add(" is still open; finish one scope before opening the next");
            st.latched();
        }

        size_t used = s->bytes();
        if (used > stagingPeak_) stagingPeak_ = used;
        if (used > opt.stagingLimit_) { ++overLimit_; st.warn(); }
        if (s->oom()) st.fail(Err::OutOfMemory);

        if (st.ok()) renderScope(*body_, *s);
        ++scopeCount_;

        if (stack_.len && stack_[stack_.len - 1] == s) stack_.len--;
        else {
            for (uint32_t i = 0; i < stack_.len; ++i)
                if (stack_[i] == s) {
                    for (uint32_t j = i + 1; j < stack_.len; ++j) stack_[j - 1] = stack_[j];
                    stack_.len--;
                    break;
                }
        }
        delete s;
    }

    bool close() {
        if (closed_) return st.ok();
        closed_ = true;

        while (stack_.len) {
            Stage* s = stack_[stack_.len - 1];
            Message& m = st.failWith(Err::ScopeNotClosed);
            m.add("scope ").quoted(s->text(s->name)).add(" was never closed");
            st.latched();
            stack_.len--;
            delete s;
        }

        if (scopeCount_ == 0) st.fail(Err::EmptyDocument);
        if (!haveTool_ || !haveTest_) st.fail(Err::MissingHistory);

        if (opt.sources_ == Sources::Deferred) {
            spoolOut_.flush();
            if (st.ok() && !writeHeader(out_)) return false;
            if (st.ok() && !spool_.replay(out_)) st.fail(Err::SinkFailed);
        } else if (!headerWritten_) {
            // A document with no scopes never triggered the lazy header.
            writeHeader(out_);
        }

        out_.lit("</UCIS>");
        if (opt.pretty_) out_.put('\n');
        if (!out_.flush() && st.ok()) st.fail(Err::SinkFailed);
        return st.ok();
    }

    uint32_t scopeCount() const { return scopeCount_; }
    size_t stagingPeak() const { return stagingPeak_; }
    // Scopes whose staged size exceeded WriterOptions::stagingLimit.
    uint32_t scopesOverStagingLimit() const { return overLimit_; }
    uint64_t spoolBytes() const { return spool_.size(); }
    bool spoolSpilled() const { return spool_.spilled(); }

    Out& out() { return out_; }
    Spool& spool() { return spool_; }
    FileTable& files() { return files_; }

private:
    void clearStack() {
        for (uint32_t i = 0; i < stack_.len; ++i) delete stack_[i];
        stack_.len = 0;
    }

    // "YYYY-MM-DDThh:mm:ss", which is what xsd:dateTime wants.
    void writeTimestamp(Out& o) {
        if (!opt.writtenTime_.empty()) { o.write(opt.writtenTime_); return; }
#if UCIS_XML_NO_TIME
        o.lit("1970-01-01T00:00:00");
#else
        std::time_t t = std::time(nullptr);
        std::tm tmv;
#if defined(_WIN32)
        if (gmtime_s(&tmv, &t) != 0) { o.lit("1970-01-01T00:00:00"); return; }
#else
        if (!gmtime_r(&t, &tmv)) { o.lit("1970-01-01T00:00:00"); return; }
#endif
        char b[20];
        const int y = tmv.tm_year + 1900;
        b[0] = static_cast<char>('0' + (y / 1000) % 10);
        b[1] = static_cast<char>('0' + (y / 100) % 10);
        b[2] = static_cast<char>('0' + (y / 10) % 10);
        b[3] = static_cast<char>('0' + y % 10);
        b[4] = '-';
        two(b + 5, tmv.tm_mon + 1);
        b[7] = '-';
        two(b + 8, tmv.tm_mday);
        b[10] = 'T';
        two(b + 11, tmv.tm_hour);
        b[13] = ':';
        two(b + 14, tmv.tm_min);
        b[16] = ':';
        two(b + 17, tmv.tm_sec < 60 ? tmv.tm_sec : 59);
        o.write(b, 19);
#endif
    }

    static void two(char* p, int v) {
        p[0] = static_cast<char>('0' + (v / 10) % 10);
        p[1] = static_cast<char>('0' + v % 10);
    }

    bool writeHeader(Out& o) {
        if (headerWritten_) return true;
        headerWritten_ = true;

        o.lit("<?xml version=\"1.0\" encoding=\"utf-8\"?>");
        if (opt.pretty_) o.put('\n');
        o.lit("<UCIS ucisVersion=\"" UCIS_XML_UCIS_VERSION "\"");
        attr(o, "writtenBy", strings_.get(toolName_), st.sanitize);
        o.lit(" writtenTime=\"");
        writeTimestamp(o);
        o.lit("\">");

        for (uint32_t i = 0; i < files_.count(); ++i) {
            if (opt.pretty_) { o.put('\n'); o.lit("  "); }
            o.lit("<sourceFiles");
            attr(o, "fileName", files_.path(i), st.sanitize);
            attrU(o, "id", i + 1);
            o.lit("/>");
        }

        if (opt.pretty_) { o.put('\n'); o.lit("  "); }
        writeHistoryNode(o);
        return !o.failed;
    }

    void writeHistoryNode(Out& o) {
        o.lit("<historyNodes historyNodeId=\"0\"");
        attr(o, "logicalName", strings_.get(testName_), st.sanitize);
        attrBool(o, "testStatus", passed_);
        o.lit(" date=\"");
        writeTimestamp(o);
        o.put('"');
        attr(o, "toolCategory", strings_.get(toolCategory_), st.sanitize);
        o.lit(" ucisVersion=\"" UCIS_XML_UCIS_VERSION "\"");
        attr(o, "vendorId", strings_.get(toolVendor_), st.sanitize);
        attr(o, "vendorTool", strings_.get(toolName_), st.sanitize);
        attr(o, "vendorToolVersion", strings_.get(toolVersion_), st.sanitize);

        if (!testSeed_.empty()) attr(o, "seed", strings_.get(testSeed_), st.sanitize);
        if (!testCmd_.empty()) attr(o, "cmd", strings_.get(testCmd_), st.sanitize);
        if (!testArgs_.empty()) attr(o, "args", strings_.get(testArgs_), st.sanitize);
        if (!testCwd_.empty()) attr(o, "runCwd", strings_.get(testCwd_), st.sanitize);
        if (!testUser_.empty()) attr(o, "userName", strings_.get(testUser_), st.sanitize);
        if (!testComment_.empty())
            attr(o, "comment", strings_.get(testComment_), st.sanitize);
        if (!testPhysical_.empty())
            attr(o, "physicalName", strings_.get(testPhysical_), st.sanitize);
        if (!testKind_.empty()) attr(o, "kind", strings_.get(testKind_), st.sanitize);
        if (haveSimTime_) {
            writeDouble(o, " simtime=\"", simTime_);
            if (!testUnit_.empty()) attr(o, "timeunit", strings_.get(testUnit_), st.sanitize);
        }
        if (haveCpuTime_) writeDouble(o, " cpuTime=\"", cpuTime_);
        o.lit("/>");
    }

    // xsd:double, with enough precision to be useful and no dependency on
    // printf's locale. Simulation times are the only doubles in the format.
    void writeDouble(Out& o, const char* prefix, double v) {
        o.write(prefix, std::strlen(prefix));
        if (v < 0) { o.put('-'); v = -v; }
        uint64_t whole = static_cast<uint64_t>(v);
        writeUInt(o, whole);
        double frac = v - static_cast<double>(whole);
        if (frac > 0) {
            o.put('.');
            for (int i = 0; i < 6; ++i) {
                frac *= 10;
                int d = static_cast<int>(frac);
                if (d < 0) d = 0;
                if (d > 9) d = 9;
                o.put(static_cast<char>('0' + d));
                frac -= d;
            }
        }
        o.put('"');
    }

    void renderScope(Out& o, Stage& s) {
        if (opt.pretty_) o.put('\n');
        o.lit("<instanceCoverages");
        attr(o, "name", s.text(s.name), st.sanitize);
        o.lit(" key=\"");
        writeEscaped(o, s.text(s.name), st.sanitize);
        o.put('"');
        attrU(o, "instanceId", s.instanceId);
        if (!s.moduleName.empty())
            attr(o, "moduleName", s.text(s.moduleName), st.sanitize);
        if (s.parentInstanceId != kNone)
            attrU(o, "parentInstanceId", s.parentInstanceId);
        {
            const Extra& e = s.extraAt(s.slot);
            if (!e.alias.empty()) attr(o, "alias", s.text(e.alias), st.sanitize);
        }
        o.put('>');

        // INSTANCE_COVERAGE's nine-element sequence, in order. This is the sort
        // design §5 describes, with the kind key folded into the call order.
        // designParameter is a caller-ordered list, and params are appended in
        // call order, so insertion order is already the order to emit.
        for (uint32_t i = 0; i < s.params.len; ++i) {
            o.lit("<designParameter><name>");
            writeEscaped(o, s.text(s.params[i].name), st.sanitize);
            o.lit("</name><value>");
            writeEscaped(o, s.text(s.params[i].value), st.sanitize);
            o.lit("</value></designParameter>");
        }

        renderId(o, "id", s.id, st);
        renderToggle(o, s, st);
        renderBlock(o, s, st);
        renderCondition(o, s, st);
        renderBranch(o, s, st);
        renderFsm(o, s, st);
        renderAssertion(o, s, st);
        renderCovergroup(o, s, st);
        renderUserAttrs(o, s, s.slot, st);

        o.lit("</instanceCoverages>");
    }

    // Every scope name seen so far, for the rule-2 check above.
    StrArena scopeArena_;
    StrMap scopeNames_;

    Out out_;
    Out spoolOut_;
    Spool spool_;
    Out* body_;
    FileTable files_;
    Vec<Stage*> stack_;

    StrArena strings_;
    Str toolName_, toolVersion_, toolVendor_, toolCategory_;
    Str testName_, testSeed_, testCmd_, testArgs_, testCwd_, testUser_;
    Str testComment_, testPhysical_, testKind_, testUnit_;
    bool passed_, haveSimTime_, haveCpuTime_;
    double simTime_, cpuTime_;

    uint32_t nextInstanceId_;
    bool haveTool_, haveTest_, headerWritten_, closed_;
    uint32_t scopeCount_;
    size_t stagingPeak_;
    uint32_t overLimit_;
};

}  // namespace stage
}  // namespace UCIS_XML_NAMESPACE

// ==== ux_api.hpp ==============================================================

// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// ux_api.hpp - the caller-facing recording API: coverage vocabulary, no XML.
//
// Tasks I-2.5 and I-3.4. This is the whole point of the design. A producer
// describes the coverage it collected -- lines, toggles, branches, coverpoints
// -- and never sees a key, an inlineCount, an <options/>, an instanceId, a
// source-file id, or a schema ordering rule.
//
// DOC COMMENTS. Public API declarations in this file carry `/**` comments,
// which Hawkmoth extracts into docs/source/reference/cpp-api/. Internals use
// plain `//`, which is invisible to the extractor -- that is the mechanism
// keeping private members out of the published reference, so do not "tidy" a
// private member's comment into `/**`.
//
// Hawkmoth drops default arguments from the rendered signature, so every
// defaulted parameter states its default in prose. That is a fidelity
// workaround, but it reads better than a bare `= 1` anyway: it has room to say
// what the default means.


namespace UCIS_XML_NAMESPACE {

/**
 * A handle to a bin that was just created, for setting its uncommon attributes.
 *
 * Exists so that exclusion, aliases, goals and weights stay chainable without
 * appearing in the common-case signature:
 *
 * .. code-block:: cpp
 *
 *    cp.bin("small", 0, 3, 41).exclude("waived: unreachable in this config");
 *
 * Safe to hold for the life of the enclosing :cpp:class:`Scope`. It addresses a
 * slot index rather than a record, and slot indices survive the sorting the
 * renderers do when the scope closes.
 *
 * A default-constructed ``BinRef`` is inert: every setter is a no-op. That is
 * what calls return after an error has been latched, so a chain of setters
 * never has to be guarded.
 */
class BinRef {
public:
    /** Constructs an inert handle whose setters all do nothing. */
    BinRef() : s_(nullptr), slot_(0) {}

    // Internal: handles are produced by the recording calls, not by callers.
    BinRef(stage::Stage* s, uint32_t slot) : s_(s), slot_(slot) {}

    /**
     * Sets an alternative name for this bin.
     *
     * :param v: The alias, emitted as ``@alias``.
     * :return: ``*this``, for chaining.
     */
    BinRef& alias(Text v) {
        if (s_) s_->extraFor(slot_).alias = s_->str(v);
        return *this;
    }

    /**
     * Marks this bin excluded from coverage results, with an optional reason.
     *
     * :param reason: Why it is excluded, emitted as ``@excludedReason``.
     *     Defaults to empty, which sets ``@excluded`` alone.
     * :return: ``*this``, for chaining.
     */
    BinRef& exclude(Text reason = Text()) {
        if (s_) {
            stage::Extra& e = s_->extraFor(slot_);
            e.excluded = true;
            if (!reason.empty()) e.excludedReason = s_->str(reason);
        }
        return *this;
    }

    /**
     * Sets the hit count this bin is considered covered at.
     *
     * :param v: The goal, emitted as ``@coverageCountGoal``.
     * :return: ``*this``, for chaining.
     */
    BinRef& goal(uint64_t v) {
        if (s_) s_->extraFor(slot_).goal = v;
        return *this;
    }

    /**
     * Sets this bin's weight when coverage is aggregated.
     *
     * :param v: The weight. The schema's default is 1, and a weight of 1 is
     *     omitted from the output.
     * :return: ``*this``, for chaining.
     */
    BinRef& weight(uint64_t v) {
        if (s_) s_->extraFor(slot_).weight = v;
        return *this;
    }

    /**
     * Attaches a tool-specific attribute to this bin.
     *
     * The escape hatch for information UCIS has no field for -- a column
     * number, a waiver id, a source revision.
     *
     * :param key: Attribute name, emitted as ``@key``.
     * :param value: Attribute value, emitted as the element's text.
     * :param type: How the value should be interpreted. Defaults to
     *     :cpp:enumerator:`AttrType::Str`.
     * :return: ``*this``, for chaining.
     */
    BinRef& attr(Text key, Text value, AttrType type = AttrType::Str) {
        if (s_) s_->addUserAttr(slot_, key, value, type);
        return *this;
    }

private:
    stage::Stage* s_;
    uint32_t slot_;
};

// --- functional-coverage handles -------------------------------------------

/**
 * A finite state machine's coverage, from :cpp:func:`Scope::fsm`.
 *
 * States and transitions may be recorded in any order. The writer emits every
 * state before every transition, which is the order ``FSM`` requires.
 *
 * A transition through a state that was never declared with
 * :cpp:func:`state` is reported as :cpp:enumerator:`Err::UnknownState` rather
 * than written.
 */
class Fsm {
public:
    /** Constructs an inert handle whose recording calls all do nothing. */
    Fsm() : s_(nullptr), i_(0) {}

    // Internal: produced by Scope::fsm().
    Fsm(stage::Stage* s, uint32_t i) : s_(s), i_(i) {}

    /**
     * Records how often the machine was in one state.
     *
     * :param name: State name, e.g. ``"IDLE"``. Transitions refer to states by
     *     this name.
     * :param count: Times the machine was observed in this state.
     * :param value: The encoded state value, e.g. ``"0"``. Defaults to empty,
     *     which omits ``@stateValue``.
     * :return: A handle for the state's bin attributes.
     */
    BinRef state(Text name, uint64_t count, Text value = Text()) {
        if (!s_) return BinRef();
        stage::RFsmState r;
        r.fsm = i_;
        r.name = s_->str(name);
        r.value = s_->str(value);
        r.count = count;
        r.slot = s_->newSlot();
        s_->fsmStates.push(r);
        return BinRef(s_, r.slot);
    }

    /**
     * Records a transition along a path of two or more states.
     *
     * The path order *is* the transition, so it is preserved exactly as given
     * rather than sorted. A path shorter than two states cannot be expressed --
     * ``FSM_TRANSITION`` requires ``minOccurs="2"`` -- and is dropped with a
     * warning rather than padded.
     *
     * May be called before or after the :cpp:func:`state` calls that declare
     * the states it names.
     *
     * :param states: The state path, in order.
     * :param n: How many states ``states`` holds.
     * :param count: Times the transition was taken.
     * :return: A handle for the transition's bin attributes.
     */
    BinRef transition(const Text* states, uint32_t n, uint64_t count) {
        if (!s_) return BinRef();
        stage::RFsmTrans r;
        r.fsm = i_;
        r.first = s_->strPool.len;
        r.n = n;
        for (uint32_t k = 0; k < n; ++k) s_->strPool.push(s_->str(states[k]));
        r.count = count;
        r.slot = s_->newSlot();
        s_->fsmTrans.push(r);
        return BinRef(s_, r.slot);
    }

    /**
     * Records a simple two-state transition.
     *
     * :param from: State the machine left.
     * :param to: State it entered.
     * :param count: Times the transition was taken.
     * :return: A handle for the transition's bin attributes.
     */
    BinRef transition(Text from, Text to, uint64_t count) {
        Text pair[2] = {from, to};
        return transition(pair, 2, count);
    }

    /**
     * Attaches a tool-specific attribute to the FSM itself.
     *
     * :param key: Attribute name.
     * :param value: Attribute value.
     * :param t: Value type. Defaults to :cpp:enumerator:`AttrType::Str`.
     * :return: A handle addressing the FSM's attributes.
     */
    BinRef attr(Text key, Text value, AttrType t = AttrType::Str) {
        if (s_) s_->addUserAttr(s_->fsms[i_].slot, key, value, t);
        return BinRef(s_, s_ ? s_->fsms[i_].slot : 0);
    }

private:
    stage::Stage* s_;
    uint32_t i_;
};

/**
 * An assertion's coverage, from :cpp:func:`Scope::assertion`.
 *
 * ``ASSERTION`` declares eight optional bins in one exact sequence. Set
 * whichever ones your tool has, in whatever order it has them; the writer emits
 * the ones you set in the sequence the schema requires. Bins you never set are
 * omitted rather than written as zero, because "not measured" and "measured
 * zero" are different claims.
 */
class Assertion {
public:
    /** Constructs an inert handle whose recording calls all do nothing. */
    Assertion() : s_(nullptr), i_(0) {}

    // Internal: produced by Scope::assertion().
    Assertion(stage::Stage* s, uint32_t i) : s_(s), i_(i) {}

    /** Records cover-property hits. :param n: Count. :return: The bin's handle. */
    BinRef covers(uint64_t n) { return set(stage::kCover, n); }

    /** Records passes. :param n: Count. :return: The bin's handle. */
    BinRef passes(uint64_t n) { return set(stage::kPass, n); }

    /** Records failures. :param n: Count. :return: The bin's handle. */
    BinRef fails(uint64_t n) { return set(stage::kFail, n); }

    /** Records vacuous passes. :param n: Count. :return: The bin's handle. */
    BinRef vacuous(uint64_t n) { return set(stage::kVacuous, n); }

    /** Records attempts made while disabled. :param n: Count. :return: The bin's handle. */
    BinRef disabled(uint64_t n) { return set(stage::kDisabled, n); }

    /** Records attempts started. :param n: Count. :return: The bin's handle. */
    BinRef attempts(uint64_t n) { return set(stage::kAttempt, n); }

    /** Records attempts in flight. :param n: Count. :return: The bin's handle. */
    BinRef active(uint64_t n) { return set(stage::kActive, n); }

    /** Records the high-water mark of concurrent attempts. :param n: Count. :return: The bin's handle. */
    BinRef peakActive(uint64_t n) { return set(stage::kPeakActive, n); }

    /**
     * Attaches a tool-specific attribute to the assertion itself.
     *
     * :param key: Attribute name.
     * :param value: Attribute value.
     * :param t: Value type. Defaults to :cpp:enumerator:`AttrType::Str`.
     * :return: A handle addressing the assertion's attributes.
     */
    BinRef attr(Text key, Text value, AttrType t = AttrType::Str) {
        if (s_) s_->addUserAttr(s_->asserts[i_].slot, key, value, t);
        return BinRef(s_, s_ ? s_->asserts[i_].slot : 0);
    }

private:
    BinRef set(stage::AssertBin which, uint64_t n) {
        if (!s_) return BinRef();
        stage::RAssert& a = s_->asserts[i_];
        a.bins[which] = n;
        if ((a.present & (1u << which)) == 0) {
            a.present |= static_cast<uint8_t>(1u << which);
            a.binSlot[which] = s_->newSlot();
        }
        return BinRef(s_, a.binSlot[which]);
    }

    stage::Stage* s_;
    uint32_t i_;
};

/**
 * A coverpoint, from :cpp:func:`Covergroup::coverpoint`.
 *
 * A coverpoint must end up with at least one bin: ``COVERPOINT`` declares
 * ``coverpointBin`` with ``minOccurs="1"``, so one with no bins is dropped with
 * a warning rather than emitted empty.
 *
 * ``COVERPOINT_BIN`` is the one bin in the schema whose contents live inside
 * ``range``/``sequence`` rather than a ``BIN``, so it carries no
 * ``binAttributes``. Exclusion, goal and weight set through the returned
 * :cpp:class:`BinRef` are preserved as ``userAttr`` instead of being dropped.
 */
class Coverpoint {
public:
    /** Constructs an inert handle whose recording calls all do nothing. */
    Coverpoint() : s_(nullptr), i_(0) {}

    // Internal: produced by Covergroup::coverpoint().
    Coverpoint(stage::Stage* s, uint32_t i) : s_(s), i_(i) {}

    /**
     * Sets this coverpoint's sampling options.
     *
     * :param o: The options. Attributes equal to their schema default are
     *     omitted; see :cpp:class:`Options` for which apply to a coverpoint.
     * :return: ``*this``, for chaining.
     */
    Coverpoint& options(const Options& o) {
        if (s_) s_->cps[i_].opts = stage::toStageOpts(*s_, o);
        return *this;
    }

    /**
     * Sets the source expression this coverpoint samples.
     *
     * :param v: The expression text, e.g. ``"txn.len"``.
     * :return: ``*this``, for chaining.
     */
    Coverpoint& exprString(Text v) {
        if (s_) s_->cps[i_].exprString = s_->str(v);
        return *this;
    }

    /**
     * Adds a bin covering a range of values.
     *
     * :param name: Bin name; also its ``@key``, so it is what merges across runs.
     * :param from: First value in the range, inclusive.
     * :param to: Last value in the range, inclusive.
     * :param count: Times a sample landed in the range.
     * :return: A handle for the bin's attributes.
     */
    BinRef bin(Text name, int64_t from, int64_t to, uint64_t count) {
        return add(name, BinType::Bins, from, to, count);
    }

    /**
     * Adds a bin covering a single value.
     *
     * :param name: Bin name.
     * :param value: The value.
     * :param count: Times a sample matched it.
     * :return: A handle for the bin's attributes.
     */
    BinRef bin(Text name, int64_t value, uint64_t count) {
        return add(name, BinType::Bins, value, value, count);
    }

    /**
     * Adds an illegal bin: sampling into it is an error, not coverage.
     *
     * :param name: Bin name.
     * :param from: First value, inclusive.
     * :param to: Last value, inclusive.
     * :param count: Times a sample landed there, normally 0.
     * :return: A handle for the bin's attributes.
     */
    BinRef illegal(Text name, int64_t from, int64_t to, uint64_t count) {
        return add(name, BinType::Illegal, from, to, count);
    }

    /**
     * Adds an ignore bin: samples landing there count towards nothing.
     *
     * :param name: Bin name.
     * :param from: First value, inclusive.
     * :param to: Last value, inclusive.
     * :param count: Times a sample landed there.
     * :return: A handle for the bin's attributes.
     */
    BinRef ignore(Text name, int64_t from, int64_t to, uint64_t count) {
        return add(name, BinType::Ignore, from, to, count);
    }

    /**
     * Adds the default bin, which catches samples no other bin claims.
     *
     * :param name: Bin name.
     * :param count: Times a sample fell through to it.
     * :return: A handle for the bin's attributes.
     */
    BinRef defaultBin(Text name, uint64_t count) {
        return add(name, BinType::Default, 0, 0, count);
    }

    /**
     * Adds a transition bin: a sequence of values sampled in order.
     *
     * The value order *is* the information, so it is preserved exactly as
     * given rather than sorted.
     *
     * :param name: Bin name.
     * :param values: The value sequence, in order.
     * :param n: How many values ``values`` holds.
     * :param count: Times the sequence was observed.
     * :return: A handle for the bin's attributes.
     */
    BinRef sequenceBin(Text name, const int64_t* values, uint32_t n, uint64_t count) {
        if (!s_) return BinRef();
        stage::RCpBin b;
        b.cp = i_;
        b.name = s_->str(name);
        b.type = BinType::Bins;
        b.count = count;
        b.from = b.to = 0;
        b.seqFirst = s_->intPool.len;
        b.seqCount = n;
        for (uint32_t k = 0; k < n; ++k) s_->intPool.push(values[k]);
        b.slot = s_->newSlot();
        s_->cpBins.push(b);
        return BinRef(s_, b.slot);
    }

    /**
     * Attaches a tool-specific attribute to the coverpoint itself.
     *
     * :param key: Attribute name.
     * :param value: Attribute value.
     * :param t: Value type. Defaults to :cpp:enumerator:`AttrType::Str`.
     * :return: A handle addressing the coverpoint's attributes.
     */
    BinRef attr(Text key, Text value, AttrType t = AttrType::Str) {
        if (s_) s_->addUserAttr(s_->cps[i_].slot, key, value, t);
        return BinRef(s_, s_ ? s_->cps[i_].slot : 0);
    }

private:
    BinRef add(Text name, BinType type, int64_t from, int64_t to, uint64_t count) {
        if (!s_) return BinRef();
        stage::RCpBin b;
        b.cp = i_;
        b.name = s_->str(name);
        b.type = type;
        b.count = count;
        b.from = from;
        b.to = to;
        b.seqFirst = b.seqCount = 0;
        b.slot = s_->newSlot();
        s_->cpBins.push(b);
        return BinRef(s_, b.slot);
    }

    stage::Stage* s_;
    uint32_t i_;
};

/**
 * A cross of two or more coverpoints, from :cpp:func:`Covergroup::cross`.
 *
 * A cross naming a coverpoint that is not in its covergroup produces a document
 * that validates but describes nothing, so it is reported as
 * :cpp:enumerator:`Err::UnknownCoverpoint` instead.
 */
class Cross {
public:
    /** Constructs an inert handle whose recording calls all do nothing. */
    Cross() : s_(nullptr), i_(0) {}

    // Internal: produced by Covergroup::cross().
    Cross(stage::Stage* s, uint32_t i) : s_(s), i_(i) {}

    /**
     * Sets this cross's sampling options.
     *
     * :param o: The options. ``auto_bin_max`` and ``detect_overlap`` do not
     *     apply to a cross and are ignored.
     * :return: ``*this``, for chaining.
     */
    Cross& options(const Options& o) {
        if (s_) s_->crosses[i_].opts = stage::toStageOpts(*s_, o);
        return *this;
    }

    /**
     * Adds a cross bin, identified by one bin index per crossed coverpoint.
     *
     * The index tuple lines up positionally with the coverpoint list passed to
     * :cpp:func:`Covergroup::cross`, so its order is preserved. A bin with no
     * indices cannot be expressed -- ``CROSS_BIN`` requires ``minOccurs="1"``
     * -- and is dropped with a warning.
     *
     * :param name: Bin name, e.g. ``"<small,rd>"``.
     * :param indices: One index per crossed coverpoint, in the cross's order.
     * :param n: How many indices ``indices`` holds.
     * :param count: Times the combination was sampled.
     * :param type: Bin type, emitted as ``@type``. Defaults to empty, which
     *     leaves the schema's own default of ``"default"`` in force.
     * :return: A handle for the bin's attributes.
     */
    BinRef bin(Text name, const int64_t* indices, uint32_t n, uint64_t count,
               Text type = Text()) {
        if (!s_) return BinRef();
        stage::RCrossBin b;
        b.cross = i_;
        b.name = s_->str(name);
        b.type = s_->str(type);
        b.count = count;
        b.idxFirst = s_->intPool.len;
        b.idxCount = n;
        for (uint32_t k = 0; k < n; ++k) s_->intPool.push(indices[k]);
        b.slot = s_->newSlot();
        s_->crossBins.push(b);
        return BinRef(s_, b.slot);
    }

    /**
     * Attaches a tool-specific attribute to the cross itself.
     *
     * :param key: Attribute name.
     * :param value: Attribute value.
     * :param t: Value type. Defaults to :cpp:enumerator:`AttrType::Str`.
     * :return: A handle addressing the cross's attributes.
     */
    BinRef attr(Text key, Text value, AttrType t = AttrType::Str) {
        if (s_) s_->addUserAttr(s_->crosses[i_].slot, key, value, t);
        return BinRef(s_, s_ ? s_->crosses[i_].slot : 0);
    }

private:
    stage::Stage* s_;
    uint32_t i_;
};

/**
 * A covergroup instance, from :cpp:func:`Scope::covergroup`.
 *
 * Declare coverpoints and crosses in whichever order suits your tool: the
 * writer emits all coverpoints before all crosses, which is what ``CGINSTANCE``
 * requires.
 */
class Covergroup {
public:
    /** Constructs an inert handle whose recording calls all do nothing. */
    Covergroup() : s_(nullptr), i_(0) {}

    // Internal: produced by Scope::covergroup().
    Covergroup(stage::Stage* s, uint32_t i) : s_(s), i_(i) {}

    /**
     * Sets this instance's sampling options.
     *
     * :param o: The options. A covergroup instance is the only object for
     *     which ``per_instance`` and ``merge_instances`` apply.
     * :return: ``*this``, for chaining.
     */
    Covergroup& options(const Options& o) {
        if (s_) s_->cgs[i_].opts = stage::toStageOpts(*s_, o);
        return *this;
    }

    /**
     * Records where this covergroup *instance* is declared.
     *
     * Emitted as ``CG_ID/cginstSourceId``. Usually a different file from
     * :cpp:func:`typeAt`: the instance lives in the testbench that samples it,
     * the type in the package that declares it. Reporting tools link back to
     * source from here, so it is worth setting; unset, it falls back to the
     * enclosing scope's location.
     *
     * :param file: Source path. Must already be known under
     *     :cpp:enumerator:`Sources::UpFront`.
     * :param line: 1-based line number.
     * :param inlineCount: Which inlined copy this is. Defaults to 1.
     * :return: ``*this``, for chaining.
     */
    Covergroup& instanceAt(Text file, uint32_t line, uint32_t inlineCount = 1) {
        if (s_) {
            s_->cgs[i_].instLoc = s_->resolve(file, line, inlineCount);
            s_->cgs[i_].hasInstLoc = true;
        }
        return *this;
    }

    /**
     * Records where the covergroup *type* is declared.
     *
     * Emitted as ``CG_ID/cgSourceId``. See :cpp:func:`instanceAt` for why the
     * two are usually different files.
     *
     * :param file: Source path.
     * :param line: 1-based line number.
     * :param inlineCount: Which inlined copy this is. Defaults to 1.
     * :return: ``*this``, for chaining.
     */
    Covergroup& typeAt(Text file, uint32_t line, uint32_t inlineCount = 1) {
        if (s_) {
            s_->cgs[i_].typeLoc = s_->resolve(file, line, inlineCount);
            s_->cgs[i_].hasTypeLoc = true;
        }
        return *this;
    }

    /**
     * Records a covergroup parameter.
     *
     * Parameters are a caller-ordered list -- position is meaningful -- so they
     * are emitted in the order added rather than sorted by name.
     *
     * :param name: Parameter name.
     * :param value: Its value, as text.
     * :return: ``*this``, for chaining.
     */
    Covergroup& parameter(Text name, Text value) {
        if (!s_) return *this;
        stage::RCgParm p;
        p.cg = i_;
        p.seq = s_->cgParms.len;
        p.name = s_->str(name);
        p.value = s_->str(value);
        s_->cgParms.push(p);
        return *this;
    }

    /**
     * Adds a coverpoint to this covergroup.
     *
     * :param name: Coverpoint name; also its ``@key``, and the name a
     *     :cpp:func:`cross` refers to it by.
     * :return: A handle for adding bins and options.
     */
    Coverpoint coverpoint(Text name) {
        if (!s_) return Coverpoint();
        stage::RCp c;
        c.cg = i_;
        c.name = s_->str(name);
        c.slot = s_->newSlot();
        return Coverpoint(s_, s_->cps.push(c));
    }

    /**
     * Adds a cross of the named coverpoints.
     *
     * The coverpoints need not exist yet -- declare them in any order -- but
     * they must exist in this covergroup by the time the scope closes. The
     * order given here is the order every :cpp:func:`Cross::bin` index tuple
     * is interpreted in.
     *
     * :param name: Cross name.
     * :param coverpoints: Names of the coverpoints being crossed.
     * :param n: How many names ``coverpoints`` holds.
     * :return: A handle for adding cross bins and options.
     */
    Cross cross(Text name, const Text* coverpoints, uint32_t n) {
        if (!s_) return Cross();
        stage::RCross x;
        x.cg = i_;
        x.name = s_->str(name);
        x.exprFirst = s_->strPool.len;
        x.exprCount = n;
        for (uint32_t k = 0; k < n; ++k) s_->strPool.push(s_->str(coverpoints[k]));
        x.slot = s_->newSlot();
        return Cross(s_, s_->crosses.push(x));
    }

    /**
     * Adds a cross, taking the coverpoint names as a braced list.
     *
     * .. code-block:: cpp
     *
     *    ux::Cross x = cg.cross("x_len_kind", {"cp_len", "cp_kind"});
     *
     * :param name: Cross name.
     * :param coverpoints: Names of the coverpoints being crossed.
     * :return: A handle for adding cross bins and options.
     */
    Cross cross(Text name, std::initializer_list<Text> coverpoints) {
        return cross(name, coverpoints.begin(),
                     static_cast<uint32_t>(coverpoints.size()));
    }

    /**
     * Attaches a tool-specific attribute to the covergroup instance.
     *
     * :param key: Attribute name.
     * :param value: Attribute value.
     * :param t: Value type. Defaults to :cpp:enumerator:`AttrType::Str`.
     * :return: A handle addressing the instance's attributes.
     */
    BinRef attr(Text key, Text value, AttrType t = AttrType::Str) {
        if (s_) s_->addUserAttr(s_->cgs[i_].slot, key, value, t);
        return BinRef(s_, s_ ? s_->cgs[i_].slot : 0);
    }

private:
    stage::Stage* s_;
    uint32_t i_;
};

// --- code-coverage handles --------------------------------------------------

/**
 * A branch point, from :cpp:func:`Scope::branch`.
 *
 * Arms recorded at the same source location are grouped into one
 * ``BRANCH_STATEMENT`` for you; each arm becomes a ``BRANCH`` with its own id
 * and bin.
 */
class Branch {
public:
    /** Constructs an inert handle whose recording calls all do nothing. */
    Branch() : s_(nullptr), i_(0), lastArm_(stage::kNone) {}

    // Internal: produced by Scope::branch() and Scope::nestedBranch().
    Branch(stage::Stage* s, uint32_t i) : s_(s), i_(i), lastArm_(stage::kNone) {}

    /**
     * Records one arm of this branch.
     *
     * :param name: Arm name, e.g. ``"true"``, ``"false"``, or a case label.
     * :param count: Times this arm was taken.
     * :return: A handle for the arm's bin attributes.
     */
    BinRef arm(Text name, uint64_t count) {
        if (!s_) return BinRef();
        stage::RBrArm a;
        a.stmt = i_;
        a.loc = s_->brStmts[i_].loc;
        a.name = s_->str(name);
        a.count = count;
        a.slot = s_->newSlot();
        lastArm_ = s_->brArms.push(a);
        return BinRef(s_, a.slot);
    }

    /** :return: This branch's index within the scope. */
    uint32_t index() const { return i_; }

    /**
     * Identifies the arm added by the most recent :cpp:func:`arm` call, for
     * passing to :cpp:func:`Scope::nestedBranch`.
     *
     * :return: The arm's index, or an unset value if no arm has been added.
     */
    uint32_t lastArm() const { return lastArm_; }

private:
    stage::Stage* s_;
    uint32_t i_;
    uint32_t lastArm_;
};

/**
 * An expression's coverage, from :cpp:func:`Scope::condition`.
 *
 * An expression must end up with at least one bin: ``EXPR`` declares ``bin``
 * with ``minOccurs="1"``, so one with no bins is dropped with a warning rather
 * than emitted empty.
 *
 * ``EXPR``'s required ``@index`` and ``@width`` are supplied for you. The index
 * is the expression's ordinal within the scope, assigned after sorting so it
 * does not depend on the order you recorded things in; the width is the
 * sub-expression count.
 */
class Expr {
public:
    /** Constructs an inert handle whose recording calls all do nothing. */
    Expr() : s_(nullptr), i_(0) {}

    // Internal: produced by Scope::condition() and Scope::nestedCondition().
    Expr(stage::Stage* s, uint32_t i) : s_(s), i_(i) {}

    /**
     * Adds one operand of the expression, in evaluation order.
     *
     * The order *is* the meaning -- a bin's name refers to operands by position
     * -- so sub-expressions are emitted in the order added. ``subExpr`` has
     * ``minOccurs="1"``, so an expression with none listed gets its own
     * expression text as the single entry.
     *
     * :param text: The operand's source text, e.g. ``"req"``.
     * :return: ``*this``, for chaining.
     */
    Expr& subExpr(Text text) {
        if (!s_) return *this;
        stage::RSubExpr r;
        r.expr = i_;
        r.seq = s_->exprs[i_].subCount++;
        r.text = s_->str(text);
        s_->subExprs.push(r);
        return *this;
    }

    /**
     * Records how often one operand combination occurred.
     *
     * :param name: The combination, conventionally one character per operand,
     *     e.g. ``"10"``.
     * :param count: Times it occurred.
     * :return: A handle for the bin's attributes.
     */
    BinRef bin(Text name, uint64_t count) {
        if (!s_) return BinRef();
        stage::RExprBin b;
        b.expr = i_;
        b.name = s_->str(name);
        b.count = count;
        b.slot = s_->newSlot();
        s_->exprBins.push(b);
        return BinRef(s_, b.slot);
    }

    /** :return: This expression's index within the scope. */
    uint32_t index() const { return i_; }

private:
    stage::Stage* s_;
    uint32_t i_;
};

/**
 * A block, from :cpp:func:`Scope::block` or :cpp:func:`Scope::process`.
 *
 * Pass it to :cpp:func:`Scope::childBlock` to nest another block inside it, or
 * to :cpp:func:`Scope::blockStatement` to list the statements it contains.
 */
class Block {
public:
    /** Constructs an inert handle that no call will accept meaningfully. */
    Block() : s_(nullptr), i_(0) {}

    // Internal: produced by Scope::block(), ::process() and ::childBlock().
    Block(stage::Stage* s, uint32_t i) : s_(s), i_(i) {}

    /** :return: This block's index within the scope. */
    uint32_t index() const { return i_; }

private:
    friend class Scope;
    stage::Stage* s_;
    uint32_t i_;
};

/**
 * One design instance's worth of coverage -- the main recording surface.
 *
 * Move-only and RAII: the scope closes, and its whole ``instanceCoverages``
 * element is rendered, when the handle goes out of scope. C++17's guaranteed
 * copy elision is what lets ``ux::Scope s = cov.scope(...)`` work without a
 * copy constructor.
 *
 * .. code-block:: cpp
 *
 *    {
 *        ux::Scope s = cov.scope("top.u_alu", "alu");
 *        s.declaredAt("rtl/alu.sv", 12);
 *        s.line("rtl/alu.sv", 42, 17);
 *    }  // rendered here
 *
 * Within a scope you may record anything in any order. The writer stages every
 * fact, sorts on content when the scope closes, and emits the schema's required
 * sequence -- so the document depends on *what* you recorded, never on *when*.
 *
 * Scopes must be contiguous, though: finish one before opening the next, or
 * nest them with :cpp:func:`child`. Reopening a closed scope is reported as
 * :cpp:enumerator:`Err::ScopeReopened`, because it would otherwise split one
 * instance's coverage across several elements with nothing to say so.
 */
class Scope {
public:
    /** Constructs an inert handle whose recording calls all do nothing. */
    Scope() : d_(nullptr), s_(nullptr) {}

    // Internal: produced by CoverageWriter::scope() and Scope::child().
    Scope(stage::Doc* d, stage::Stage* s) : d_(d), s_(s) {}

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

    /** Move constructor; the source becomes inert. */
    Scope(Scope&& o) noexcept : d_(o.d_), s_(o.s_) { o.d_ = nullptr; o.s_ = nullptr; }

    /** Move assignment; closes this scope first, then takes over ``o``. */
    Scope& operator=(Scope&& o) noexcept {
        if (this != &o) { close(); d_ = o.d_; s_ = o.s_; o.d_ = nullptr; o.s_ = nullptr; }
        return *this;
    }

    /** Closes the scope, rendering it if it is still open. */
    ~Scope() { close(); }

    /**
     * Closes the scope early, rendering it and releasing its staged memory.
     *
     * Idempotent. Rarely needed -- the destructor does it -- but useful when
     * the handle outlives the region you want the scope to cover.
     */
    void close() {
        if (d_ && s_) d_->endScope(s_);
        d_ = nullptr;
        s_ = nullptr;
    }

    /** :return: ``true`` if this handle refers to an open scope. */
    bool valid() const { return s_ != nullptr; }

    /**
     * Opens a nested child scope.
     *
     * Builds the child's ``@parentInstanceId`` automatically. The parent stays
     * staged until it closes, so peak memory is depth times scope size; since
     * hierarchy is shallow and coverage concentrates in leaves, that is usually
     * cheaper than it sounds.
     *
     * :param name: The child's name, relative to this scope.
     * :param moduleName: The child's design-unit name. Defaults to empty,
     *     which omits ``@moduleName``.
     * :return: The child scope, which must close before this one.
     */
    Scope child(Text name, Text moduleName = Text()) {
        if (!d_) return Scope();
        return Scope(d_, d_->beginScope(name, moduleName, nullptr, s_));
    }

    // --- code coverage ---

    /**
     * Records a line or statement hit.
     *
     * :param file: Source path. Interned against the file table; under
     *     :cpp:enumerator:`Sources::UpFront` it must already be registered.
     * :param line: 1-based line number. Zero is clamped to 1 with a warning,
     *     since ``xsd:positiveInteger`` admits no zero.
     * :param count: Times the statement executed.
     * :param comment: A per-point description, emitted as the bin's
     *     ``@nameComponent`` -- UCIS has no ``@name`` on ``BIN`` itself.
     *     Defaults to empty.
     * :return: A handle for the bin's attributes.
     */
    BinRef line(Text file, uint32_t line, uint64_t count, Text comment = Text()) {
        if (!s_) return BinRef();
        stage::RStmt r;
        r.loc = loc(file, line, 1);
        r.name = s_->str(comment);
        r.count = count;
        r.slot = s_->newSlot();
        s_->stmts.push(r);
        return BinRef(s_, r.slot);
    }

    /**
     * Records toggle counts for one bit of one signal.
     *
     * Facts are grouped on ``(signal, bit)`` for you, which is what produces
     * the group-by-bit layout that measured 0.800x gzipped against the
     * alternative. It is not a mode; it is the only layout this structure can
     * produce.
     *
     * :param signal: Signal name.
     * :param bit: Bit name within the signal, e.g. ``"3"``.
     * :param edge: Which transition this count is for.
     * :param count: Times the transition occurred.
     * :return: A handle for the bin's attributes.
     */
    BinRef toggle(Text signal, Text bit, Edge edge, uint64_t count) {
        return toggle(signal, bit,
                      edge == Edge::Rise ? Text("0", 1) : Text("1", 1),
                      edge == Edge::Rise ? Text("1", 1) : Text("0", 1), count);
    }

    /**
     * Records toggle counts for a numbered bit, formatting the index for you.
     *
     * :param signal: Signal name.
     * :param bit: Bit index.
     * :param edge: Which transition this count is for.
     * :param count: Times the transition occurred.
     * :return: A handle for the bin's attributes.
     */
    BinRef toggle(Text signal, uint32_t bit, Edge edge, uint64_t count) {
        char b[11];
        return toggle(signal, Text(b, fmt(b, bit)), edge, count);
    }

    /**
     * Records a toggle with explicit transition endpoints.
     *
     * For anything beyond a two-state rise and fall -- four-state values, or a
     * tool with its own encoding.
     *
     * :param signal: Signal name.
     * :param bit: Bit name within the signal.
     * :param from: Value transitioned from, e.g. ``"x"``.
     * :param to: Value transitioned to.
     * :param count: Times the transition occurred.
     * :return: A handle for the bin's attributes.
     */
    BinRef toggle(Text signal, Text bit, Text from, Text to, uint64_t count) {
        if (!s_) return BinRef();
        stage::RToggle r;
        r.sig = s_->str(signal);
        r.bit = s_->str(bit);
        r.from = s_->str(from);
        r.to = s_->str(to);
        r.count = count;
        r.loc = s_->id;
        r.slot = s_->newSlot();
        s_->toggles.push(r);
        return BinRef(s_, r.slot);
    }

    /**
     * Records where this design instance is declared.
     *
     * Emitted as ``instanceCoverages/id``. Without it the scope points at the
     * synthetic ``(unknown)`` source file -- legal, but useless to a reporting
     * tool trying to link back to source.
     *
     * :param file: Source path.
     * :param line: 1-based line number.
     * :param inlineCount: Which inlined copy this is. Defaults to 1.
     * :return: ``*this``, for chaining.
     */
    Scope& declaredAt(Text file, uint32_t line, uint32_t inlineCount = 1) {
        if (s_) s_->id = loc(file, line, inlineCount);
        return *this;
    }

    /**
     * Records where a signal is declared.
     *
     * Emitted as ``toggleObject/id``. A toggle fact is about a *signal* rather
     * than a line, so there is no per-fact location to fall back on: this is
     * the only source of a toggle object's location. Without it, every toggle
     * object in the scope points at the enclosing scope's id.
     *
     * Call once per signal; later duplicates are ignored.
     *
     * :param name: Signal name, matching the one passed to :cpp:func:`toggle`.
     * :param file: Source path.
     * :param line: 1-based line number.
     */
    void signal(Text name, Text file, uint32_t line) {
        signalDetail(name, &file, line, 0, 0, true, false);
    }

    /**
     * Records a signal's declared bit range, emitted as ``DIMENSION``.
     *
     * :param name: Signal name.
     * :param left: Left bound of the range.
     * :param right: Right bound of the range.
     * :param downto: ``true`` if the range descends, as in ``[7:0]``.
     *     Defaults to ``true``.
     */
    void signal(Text name, int64_t left, int64_t right, bool downto = true) {
        signalDetail(name, nullptr, 0, left, right, downto, true);
    }

    /**
     * Records a signal's declaration site and bit range together, which is what
     * an elaborated design knows.
     *
     * :param name: Signal name.
     * :param file: Source path.
     * :param line: 1-based line number.
     * :param left: Left bound of the range.
     * :param right: Right bound of the range.
     * :param downto: ``true`` if the range descends. Defaults to ``true``.
     */
    void signal(Text name, Text file, uint32_t line, int64_t left, int64_t right,
                bool downto = true) {
        signalDetail(name, &file, line, left, right, downto, true);
    }

    /**
     * Opens a branch point; record its arms with :cpp:func:`Branch::arm`.
     *
     * :param file: Source path.
     * :param line: 1-based line number.
     * :param statementType: What kind of branch this is, e.g. ``"if"`` or
     *     ``"case"``. Required by the schema, so it defaults to ``"if"``.
     * :param branchExpr: The controlling expression's text. Defaults to empty,
     *     which omits ``@branchExpr``.
     * :return: A handle for recording arms.
     */
    Branch branch(Text file, uint32_t line, Text statementType = Text("if", 2),
                  Text branchExpr = Text()) {
        if (!s_) return Branch();
        stage::RBrStmt r;
        r.loc = loc(file, line, 1);
        r.expr = s_->str(branchExpr);
        r.stmtType = s_->str(statementType);
        r.parentArm = stage::kNone;
        r.slot = s_->newSlot();
        return Branch(s_, s_->brStmts.push(r));
    }

    /**
     * Opens a branch nested inside another branch's arm.
     *
     * The writer holds the parent arm's bin back until after its children, as
     * ``BRANCH``'s sequence requires.
     *
     * :param parentArm: The enclosing arm, from :cpp:func:`Branch::lastArm`
     *     called after that arm was added.
     * :param file: Source path.
     * :param line: 1-based line number.
     * :param statementType: What kind of branch this is. Defaults to ``"if"``.
     * :param branchExpr: The controlling expression's text. Defaults to empty.
     * :return: A handle for recording arms.
     */
    Branch nestedBranch(uint32_t parentArm, Text file, uint32_t line,
                        Text statementType = Text("if", 2),
                        Text branchExpr = Text()) {
        if (!s_) return Branch();
        stage::RBrStmt r;
        r.loc = loc(file, line, 1);
        r.expr = s_->str(branchExpr);
        r.stmtType = s_->str(statementType);
        r.parentArm = parentArm;
        r.slot = s_->newSlot();
        return Branch(s_, s_->brStmts.push(r));
    }

    /**
     * Opens an expression for condition coverage.
     *
     * :param file: Source path.
     * :param line: 1-based line number.
     * :param name: A name for the expression, e.g. ``"c_grant"``. Also its
     *     ``@key``.
     * :param exprString: The expression's source text, e.g. ``"req && !busy"``.
     * :param statementType: The enclosing statement kind. Defaults to empty,
     *     which omits ``@statementType``.
     * :return: A handle for adding operands and bins.
     */
    Expr condition(Text file, uint32_t line, Text name, Text exprString,
                   Text statementType = Text()) {
        if (!s_) return Expr();
        stage::RExpr r;
        r.parent = stage::kNone;
        r.loc = loc(file, line, 1);
        r.name = s_->str(name);
        r.exprString = s_->str(exprString);
        r.stmtType = s_->str(statementType);
        r.subCount = 0;
        r.slot = s_->newSlot();
        return Expr(s_, s_->exprs.push(r));
    }

    /**
     * Opens a sub-expression nested inside another, emitted as
     * ``hierarchicalExpr``.
     *
     * :param parent: The enclosing expression.
     * :param file: Source path.
     * :param line: 1-based line number.
     * :param name: A name for the sub-expression.
     * :param exprString: Its source text.
     * :return: A handle for adding operands and bins.
     */
    Expr nestedCondition(const Expr& parent, Text file, uint32_t line, Text name,
                         Text exprString) {
        if (!s_) return Expr();
        stage::RExpr r;
        r.parent = parent.index();
        r.loc = loc(file, line, 1);
        r.name = s_->str(name);
        r.exprString = s_->str(exprString);
        r.stmtType = Str();
        r.subCount = 0;
        r.slot = s_->newSlot();
        return Expr(s_, s_->exprs.push(r));
    }

    /**
     * Records a basic block, for tools with real block coverage.
     *
     * ``BLOCK_COVERAGE`` is an ``xsd:choice``, so a scope uses either
     * :cpp:func:`Scope::line` or :cpp:func:`Scope::block`/:cpp:func:`Scope::process` -- never both.
     * Mixing them is reported as :cpp:enumerator:`Err::MixedBlockForms`.
     *
     * :param file: Source path.
     * :param line: 1-based line number.
     * :param count: Times the block executed.
     * :param name: A description, emitted as the bin's ``@nameComponent``.
     *     Defaults to empty.
     * :return: A handle for nesting blocks and listing statements.
     */
    Block block(Text file, uint32_t line, uint64_t count, Text name = Text()) {
        return blockIn(stage::kNone, stage::kNone, file, line, count, name);
    }

    /**
     * Records a process and the block inside it.
     *
     * :param processType: The process kind, e.g. ``"always_ff"``. Required by
     *     the schema.
     * :param file: Source path.
     * :param line: 1-based line number.
     * :param count: Times the process body executed.
     * :param name: A description for the block's bin. Defaults to empty.
     * :return: A handle to the block inside the process.
     */
    Block process(Text processType, Text file, uint32_t line, uint64_t count,
                  Text name = Text()) {
        if (!s_) return Block();
        stage::RProcess p;
        p.type = s_->str(processType);
        p.slot = s_->newSlot();
        uint32_t pi = s_->procs.push(p);
        return blockIn(stage::kNone, pi, file, line, count, name);
    }

    /**
     * Lists a statement contained in a block, emitted as ``statementId``.
     *
     * :param b: The containing block.
     * :param file: Source path.
     * :param line: 1-based line number.
     * :param inlineCount: Which inlined copy this is. Defaults to 1.
     * :return: ``*this``, for chaining.
     */
    Scope& blockStatement(const Block& b, Text file, uint32_t line,
                          uint32_t inlineCount = 1) {
        if (!s_) return *this;
        stage::RBlockStmt r;
        r.block = b.index();
        r.id = loc(file, line, inlineCount);
        s_->blockStmts.push(r);
        return *this;
    }

    /**
     * Records a block nested inside another, emitted as ``hierarchicalBlock``.
     *
     * :param parent: The containing block.
     * :param file: Source path.
     * :param line: 1-based line number.
     * :param count: Times the block executed.
     * :param name: A description for its bin. Defaults to empty.
     * :return: A handle to the nested block.
     */
    Block childBlock(const Block& parent, Text file, uint32_t line, uint64_t count,
                     Text name = Text()) {
        return blockIn(parent.index(), stage::kNone, file, line, count, name);
    }

    // --- functional coverage ---

    /**
     * Opens a finite state machine.
     *
     * :param name: The state variable's name, e.g. ``"state_q"``.
     * :param type: The state type's name, e.g. ``"ctrl_state_e"``. Defaults to
     *     empty, which omits ``@type``.
     * :param width: The state encoding's width in bits. Defaults to 0, which
     *     omits ``@width``; a supplied 0 is clamped to 1, since the attribute
     *     is a ``positiveInteger``.
     * :return: A handle for recording states and transitions.
     */
    Fsm fsm(Text name, Text type = Text(), uint32_t width = 0) {
        if (!s_) return Fsm();
        stage::RFsm f;
        f.name = s_->str(name);
        f.type = s_->str(type);
        f.width = width;
        f.slot = s_->newSlot();
        return Fsm(s_, s_->fsms.push(f));
    }

    /**
     * Opens an assertion.
     *
     * :param name: The assertion's name.
     * :param kind: What kind it is, e.g. ``"assert"`` or ``"cover"``. Required
     *     by the schema, so it defaults to ``"assert"``.
     * :return: A handle for recording its bins.
     */
    Assertion assertion(Text name, Text kind = Text("assert", 6)) {
        if (!s_) return Assertion();
        stage::RAssert a;
        a.name = s_->str(name);
        a.kind = s_->str(kind);
        a.present = 0;
        for (uint32_t i = 0; i < stage::kAssertBinCount; ++i) {
            a.bins[i] = 0;
            a.binSlot[i] = 0;
        }
        a.slot = s_->newSlot();
        return Assertion(s_, s_->asserts.push(a));
    }

    /**
     * Opens a covergroup instance.
     *
     * :param name: The instance's name.
     * :param typeName: The covergroup type's name, emitted as ``@cgName``.
     *     Defaults to empty, in which case ``name`` is used.
     * :param moduleName: The declaring package or module, e.g.
     *     ``"work.fifo_pkg"``. Defaults to empty.
     * :return: A handle for recording coverpoints and crosses.
     */
    Covergroup covergroup(Text name, Text typeName = Text(),
                          Text moduleName = Text()) {
        if (!s_) return Covergroup();
        stage::RCg c;
        c.name = s_->str(name);
        c.typeName = s_->str(typeName);
        c.moduleName = s_->str(moduleName);
        c.hasInstLoc = false;
        c.hasTypeLoc = false;
        c.slot = s_->newSlot();
        return Covergroup(s_, s_->cgs.push(c));
    }

    // --- scope-level detail ---

    /**
     * Records a design parameter, emitted as ``designParameter``.
     *
     * Parameters are a caller-ordered list, so they are emitted in the order
     * added rather than sorted.
     *
     * :param name: Parameter name, e.g. ``"WIDTH"``.
     * :param value: Its value, as text.
     */
    void parameter(Text name, Text value) {
        if (!s_) return;
        stage::RParam p;
        p.name = s_->str(name);
        p.value = s_->str(value);
        p.seq = s_->params.len;
        s_->params.push(p);
    }

    /**
     * Attaches a tool-specific attribute to the scope itself.
     *
     * :param key: Attribute name.
     * :param value: Attribute value.
     * :param t: Value type. Defaults to :cpp:enumerator:`AttrType::Str`.
     * :return: A handle addressing the scope's attributes.
     */
    BinRef attr(Text key, Text value, AttrType t = AttrType::Str) {
        if (!s_) return BinRef();
        s_->addUserAttr(s_->slot, key, value, t);
        return BinRef(s_, s_->slot);
    }

    /**
     * Sets the metric mode of one coverage kind's container.
     *
     * :param kind: Which coverage container to set it on.
     * :param mode: The metric mode, e.g. ``"UCIS:toggle_enum"``.
     */
    void metricMode(CovKind kind, Text mode) {
        if (s_) s_->metricMode[static_cast<uint32_t>(kind)] = s_->str(mode);
    }

    /**
     * Sets the weight of one coverage kind's container.
     *
     * :param kind: Which coverage container to set it on.
     * :param w: The weight. The schema's default is 1, and 1 is omitted from
     *     the output.
     */
    void weight(CovKind kind, uint64_t w) {
        if (s_) s_->kindWeight[static_cast<uint32_t>(kind)] = w;
    }

    // Internal: the staging arena, for tests that inspect what was recorded.
    stage::Stage* raw() { return s_; }

private:
    void signalDetail(Text name, const Text* file, uint32_t line, int64_t left,
                      int64_t right, bool downto, bool hasDim) {
        if (!s_) return;
        stage::RSignal d;
        d.sig = s_->str(name);
        d.hasLoc = file != nullptr;
        if (file) d.loc = loc(*file, line, 1);
        d.left = left;
        d.right = right;
        d.downto = downto;
        d.hasDim = hasDim;
        s_->signals.push(d);
    }

    stage::SLoc loc(Text file, uint32_t line, uint32_t inlineCount) {
        return s_ ? s_->resolve(file, line, inlineCount) : stage::SLoc();
    }

    Block blockIn(uint32_t parent, uint32_t process, Text file, uint32_t line,
                  uint64_t count, Text name) {
        if (!s_) return Block();
        stage::RBlock b;
        b.parent = parent;
        b.process = process;
        b.name = s_->str(name);
        b.id = loc(file, line, 1);
        b.count = count;
        b.slot = s_->newSlot();
        return Block(s_, s_->blocks.push(b));
    }

    static uint32_t fmt(char* out, uint32_t v) {
        char tmp[11];
        uint32_t n = 0;
        do { tmp[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
        for (uint32_t i = 0; i < n; ++i) out[i] = tmp[n - 1 - i];
        return n;
    }

    stage::Doc* d_;
    stage::Stage* s_;
};

/**
 * The document being written -- create one, describe the run, record scopes,
 * close it.
 *
 * .. code-block:: cpp
 *
 *    ux::CoverageWriter cov;
 *    cov.openFile("coverage.xml");
 *    cov.tool(ux::Tool().name("mytool").version("1.0").vendorId("MINE"));
 *    cov.test(ux::Test().name("run1").passed(true));
 *    cov.sources({"rtl/alu.sv"});
 *    {
 *        ux::Scope s = cov.scope("top.u_alu", "alu");
 *        s.line("rtl/alu.sv", 42, 17);
 *    }
 *    if (!cov.close()) fprintf(stderr, "%s\n", cov.error());
 *
 * Neither copyable nor movable, deliberately: :cpp:class:`Scope` and every
 * handle below it hold a pointer into the writer, so a move would leave them
 * dangling. Construct it in place and open it.
 *
 * Errors are never thrown and never abort. The first error is latched, every
 * later call becomes a no-op, and :cpp:func:`close` reports -- a coverage write
 * must not take down a simulation that has been running for hours.
 */
class CoverageWriter {
public:
    /** Constructs an unopened writer; call :cpp:func:`open` or :cpp:func:`openFile`. */
    CoverageWriter() {}

    /**
     * Constructs and opens in one step.
     *
     * :param sink: Where bytes go.
     * :param o: Writer options. Defaults to a default-constructed
     *     :cpp:class:`WriterOptions`.
     */
    explicit CoverageWriter(Sink sink, const WriterOptions& o = WriterOptions()) {
        open(sink, o);
    }

    CoverageWriter(const CoverageWriter&) = delete;
    CoverageWriter& operator=(const CoverageWriter&) = delete;
    CoverageWriter(CoverageWriter&&) = delete;
    CoverageWriter& operator=(CoverageWriter&&) = delete;

    /**
     * Opens the writer against a sink.
     *
     * :param sink: Where bytes go. See :cpp:struct:`Sink` for wrapping a
     *     compressor or socket.
     * :param o: Writer options. Defaults to a default-constructed
     *     :cpp:class:`WriterOptions`.
     * :return: ``true`` on success.
     */
    bool open(Sink sink, const WriterOptions& o = WriterOptions()) {
        doc_.opt = o;
        return doc_.open(sink);
    }

#if !UCIS_XML_NO_STDIO
    /**
     * Opens the writer against a file.
     *
     * A ``.gz`` path without ``UCIS_XML_ENABLE_ZLIB`` writes plain XML under a
     * misleading name, so it counts a warning: silently producing 500 MB
     * because an extension was ignored is a bad default.
     *
     * Unavailable when ``UCIS_XML_NO_STDIO`` is set.
     *
     * :param path: File to create or truncate.
     * :param o: Writer options. Defaults to a default-constructed
     *     :cpp:class:`WriterOptions`.
     * :return: ``true`` on success.
     */
    bool openFile(const char* path, const WriterOptions& o = WriterOptions()) {
        doc_.opt = o;
        if (!file_.open(path)) return doc_.st.fail(Err::SinkFailed);
        size_t n = std::strlen(path);
        if (n > 3 && std::memcmp(path + n - 3, ".gz", 3) == 0 &&
            !UCIS_XML_ENABLE_ZLIB)
            doc_.st.warn();
        return doc_.open(file_.sink());
    }
#endif

    /**
     * Describes the producing tool. Required.
     *
     * :param t: The tool descriptor; see :cpp:struct:`Tool`.
     */
    void tool(const Tool& t) { doc_.setTool(t); }

    /**
     * Describes the test this document records. Required.
     *
     * :param t: The test descriptor; see :cpp:struct:`Test`.
     */
    void test(const Test& t) { doc_.setTest(t); }

    /**
     * Registers source files, from an array.
     *
     * A superset is fine -- unreferenced ``sourceFiles`` entries are
     * schema-valid -- so this is the compile file list you already have, not a
     * pre-scan of your coverage data. Must be called before the first scope
     * under :cpp:enumerator:`Sources::UpFront`.
     *
     * :param paths: The file paths.
     * :param n: How many paths ``paths`` holds.
     */
    void sources(const Text* paths, uint32_t n) {
        for (uint32_t i = 0; i < n; ++i) doc_.addSource(paths[i]);
    }

    /**
     * Registers one source file.
     *
     * :param path: The file path. Duplicates are folded.
     */
    void source(Text path) { doc_.addSource(path); }

    /**
     * Registers source files from a braced list.
     *
     * :param paths: The file paths.
     */
    void sources(std::initializer_list<Text> paths) {
        sources(paths.begin(), static_cast<uint32_t>(paths.size()));
    }

    /**
     * Opens a scope for one design instance.
     *
     * Scopes must be contiguous: finish one before opening the next, or nest
     * them with :cpp:func:`Scope::child`. Give the scope its declaration site
     * with :cpp:func:`Scope::declaredAt`.
     *
     * :param hierarchicalName: The instance's full dotted path, e.g.
     *     ``"top.u_alu"``.
     * :param moduleName: Its design-unit name. Defaults to empty, which omits
     *     ``@moduleName``.
     * :return: The scope, which renders when it goes out of scope.
     */
    Scope scope(Text hierarchicalName, Text moduleName = Text()) {
        return Scope(&doc_, doc_.beginScope(hierarchicalName, moduleName, nullptr,
                                            nullptr));
    }

    /**
     * Finishes the document and flushes it.
     *
     * :return: ``true`` if nothing failed at any point. On ``false``, read
     *     :cpp:func:`error` for what went wrong.
     */
    bool close() { return doc_.close(); }

    /** :return: ``true`` if no error has been latched. */
    bool ok() const { return doc_.st.ok(); }

    /**
     * :return: The first error's message, naming the fix where there is one.
     *     Empty while nothing has failed.
     */
    const char* error() const { return doc_.st.error(); }

    /** :return: The first error as an :cpp:enum:`Err`, for reacting in code. */
    Err errorCode() const { return doc_.st.code(); }

    /**
     * Counts everything the writer had to repair or complain about: sanitized
     * characters, clamped integers, key collisions, dropped constructs and
     * configuration warnings.
     *
     * Counts repairs *performed*, not distinct inputs repaired -- a name
     * appearing in two attributes is escaped twice. It is zero exactly when
     * nothing needed fixing, which is the standard to hold output to.
     *
     * :return: The total.
     */
    uint64_t warnings() const { return doc_.st.warnings(); }

    /**
     * Counts names that collided inside one container and had to be
     * distinguished with a ``#2``, ``#3`` suffix.
     *
     * ``@key`` is merge identity downstream, so a collision usually means a
     * name is less unique than intended.
     *
     * :return: The count, included in :cpp:func:`warnings`.
     */
    uint64_t keyCollisions() const { return doc_.st.keyCollisions(); }

    /**
     * The high-water mark of staged memory, in bytes.
     *
     * Peak memory is bounded by the largest single scope rather than by the
     * design; this is how that claim is checked rather than asserted. Measured
     * at 5.8 MB against a 122 MB document on OpenTitan.
     *
     * :return: Bytes.
     */
    size_t stagingPeak() const { return doc_.stagingPeak(); }

    /**
     * :return: Bytes written to the deferred-mode spool. Always 0 under
     *     :cpp:enumerator:`Sources::UpFront`.
     */
    uint64_t spoolBytes() const { return doc_.spoolBytes(); }

    /** :return: ``true`` if the spool outgrew memory and spilled to a temp file. */
    bool spoolSpilled() const { return doc_.spoolSpilled(); }

    // Internal: the document state, for tests.
    stage::Doc& doc() { return doc_; }

private:
    stage::Doc doc_;
#if !UCIS_XML_NO_STDIO
    FileSink file_;
#endif
};

}  // namespace UCIS_XML_NAMESPACE

// ==== ux_points.hpp ===========================================================

// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// ux_points.hpp - a flat sink for tools that have a flat list.
//
// Task I-2.6. Verilator's coverage.dat is a flat list of records, and so is
// every format converter's input. Those callers should not have to reconstruct
// a design hierarchy first just to satisfy an API, so PointSink opens and
// closes scopes as the `scope` field changes (design §2.1).
//
// The one thing it asks is that records for one scope are contiguous -- which
// is rule 2 of the contract, and a sort the caller has almost always already
// done. sortByScope() is here for those who have not.


namespace UCIS_XML_NAMESPACE {

/** Which kind of coverage a :cpp:struct:`Point` carries. */
enum class Kind : uint8_t {
    /** A statement or line hit; uses ``file``, ``line``, ``name``. */
    Line,
    /** A bit transition; uses ``signal``, ``bit``, ``edge``. */
    Toggle,
    /** One arm of a branch; uses ``file``, ``line``, ``name``. */
    Branch,
};

/**
 * One coverage record, for the flat recording path.
 *
 * .. code-block:: cpp
 *
 *    ux::Point p;
 *    p.kind(ux::Kind::Line).scope("top.u_a").file("a.sv").line(3).count(7);
 *    sink.add(p);
 *
 * Which fields matter depends on :cpp:func:`kind`; the rest are ignored.
 *
 * Two source locations, deliberately kept apart. ``file``/``line`` is where
 * *this record's* coverage item is -- and, on a toggle record, where the signal
 * is declared, which is the only source of a toggle object's location.
 * :cpp:func:`declaredAt` is where the enclosing *design instance* is declared,
 * which a converter reading a flat record list usually does not know; leave it
 * unset rather than guess, and the scope points at the synthetic ``(unknown)``
 * file.
 */
struct Point {
    Kind kind_;
    Text scope_, module_, file_, signal_, bit_, name_;
    uint32_t line_;
    Edge edge_;
    uint64_t count_;

    // Where the enclosing design instance is declared, if known. Separate from
    // file_/line_, which is where *this record's* coverage item is: a converter
    // reading a flat record list usually knows the second and not the first,
    // and guessing the instance's declaration site from the first record inside
    // it would put confidently wrong data in the document. Left unset, the
    // scope points at the synthetic "(unknown)" file, which is what that entry
    // is for.
    Text scopeFile_;
    uint32_t scopeLine_;

    /** Constructs a line-kind record at line 1 with a count of 0. */
    Point()
        : kind_(Kind::Line), line_(1), edge_(Edge::Rise), count_(0),
          scopeLine_(0) {}

    /**
     * Sets which kind of coverage this record carries.
     *
     * :param v: The kind. Defaults to :cpp:enumerator:`Kind::Line`.
     * :return: ``*this``, for chaining.
     */
    Point& kind(Kind v) { kind_ = v; return *this; }

    /**
     * Sets the design instance this record belongs to.
     *
     * :cpp:class:`PointSink` opens a new scope whenever this changes, so
     * records for one scope must be contiguous.
     *
     * :param v: Full dotted instance path.
     * :return: ``*this``, for chaining.
     */
    Point& scope(Text v) { scope_ = v; return *this; }

    /**
     * Sets the scope's design-unit name, used when the scope is opened.
     *
     * :param v: Module or design-unit name.
     * :return: ``*this``, for chaining.
     */
    Point& module(Text v) { module_ = v; return *this; }

    /**
     * Sets this record's source path.
     *
     * On a toggle record this is where the *signal* is declared.
     *
     * :param v: Source path.
     * :return: ``*this``, for chaining.
     */
    Point& file(Text v) { file_ = v; return *this; }

    /**
     * Sets this record's line number.
     *
     * :param v: 1-based line. Defaults to 1; zero is clamped with a warning.
     * :return: ``*this``, for chaining.
     */
    Point& line(uint32_t v) { line_ = v; return *this; }

    /**
     * Sets the signal name. Toggle records only.
     *
     * :param v: Signal name.
     * :return: ``*this``, for chaining.
     */
    Point& signal(Text v) { signal_ = v; return *this; }

    /**
     * Sets the bit within the signal. Toggle records only.
     *
     * :param v: Bit name, e.g. ``"3"``.
     * :return: ``*this``, for chaining.
     */
    Point& bit(Text v) { bit_ = v; return *this; }

    /**
     * Sets the record's description: a statement comment, or a branch arm name.
     *
     * :param v: The text.
     * :return: ``*this``, for chaining.
     */
    Point& name(Text v) { name_ = v; return *this; }

    /**
     * Sets which transition this is. Toggle records only.
     *
     * :param v: The edge. Defaults to :cpp:enumerator:`Edge::Rise`.
     * :return: ``*this``, for chaining.
     */
    Point& edge(Edge v) { edge_ = v; return *this; }

    /**
     * Sets the hit count.
     *
     * :param v: Times the item was covered. Defaults to 0.
     * :return: ``*this``, for chaining.
     */
    Point& count(uint64_t v) { count_ = v; return *this; }

    /**
     * Sets where the enclosing design *instance* is declared.
     *
     * Distinct from :cpp:func:`Point::file`/:cpp:func:`Point::line`, which locate this
     * record's item. Leave it unset if you do not know it: guessing the
     * instance's declaration site from the first record inside it puts
     * confidently wrong data in the document.
     *
     * :param file: Source path.
     * :param line: 1-based line number.
     * :return: ``*this``, for chaining.
     */
    Point& declaredAt(Text file, uint32_t line) {
        scopeFile_ = file; scopeLine_ = line; return *this;
    }
};

/**
 * Records a flat list of :cpp:struct:`Point` records, opening and closing
 * scopes as the ``scope`` field changes.
 *
 * Verilator's ``coverage.dat`` is a flat list of records, and so is every
 * converter's input. Those callers should not have to reconstruct a design
 * hierarchy first.
 *
 * .. code-block:: cpp
 *
 *    ux::sortByScope(pts, n);
 *    {
 *        ux::PointSink sink(cov);
 *        for (const ux::Point& p : pts) sink.add(p);
 *    }
 *
 * The one thing it asks is that records for a scope are **contiguous** --
 * reopening a closed scope is an error, because it would otherwise split one
 * instance's coverage across several elements. :cpp:func:`sortByScope` groups a
 * list that is not already grouped.
 */
class PointSink {
public:
    /**
     * Constructs a sink recording into ``w``.
     *
     * :param w: The writer, which must outlive this sink.
     */
    explicit PointSink(CoverageWriter& w) : w_(&w), open_(false) {}

    /** Closes any open scope. */
    ~PointSink() { flush(); }

    PointSink(const PointSink&) = delete;
    PointSink& operator=(const PointSink&) = delete;

    /**
     * Records one point, opening a new scope if ``p.scope`` changed.
     *
     * On a toggle record carrying a path, the signal's declaration site is
     * registered once per signal rather than once per record, so the cost is
     * bounded by the signal count and not the point count.
     *
     * :param p: The record.
     * :return: A handle for the resulting bin's attributes.
     */
    BinRef add(const Point& p) {
        if (!open_ || !equal(p.scope_, currentName())) {
            scope_.close();
            scope_ = w_->scope(p.scope_, p.module_);
            // The scope name has to be *copied*, not aliased. A converter
            // parsing records in place reuses one line buffer, so a Text kept
            // from the previous call points at bytes that have since been
            // overwritten -- and comparing against those silently opens a new
            // scope for every record. That turned OpenTitan's 1,520 scopes into
            // 155,917 before this was fixed.
            current_.clear();
            if (current_.reserve(static_cast<uint32_t>(p.scope_.size))) {
                std::memcpy(current_.ptr, p.scope_.data, p.scope_.size);
                current_.len = static_cast<uint32_t>(p.scope_.size);
            }
            if (p.scopeLine_) scope_.declaredAt(p.scopeFile_, p.scopeLine_);
            // The signal table is per scope, so it resets with the scope.
            seenSignals_.release();
            signalNames_.release();
            open_ = true;
        }
        switch (p.kind_) {
        case Kind::Toggle:
            // A toggle record carries the signal's location, and toggleObject/id
            // has nowhere else to get one -- but declaring it per record would
            // stage a signal entry per point. Declare it once per signal
            // instead: bounded by the signal count, not the point count.
            if (!p.file_.empty()) {
                bool fresh = false;
                seenSignals_.getOrAdd(signalNames_, p.signal_, 0, &fresh);
                if (fresh) scope_.signal(p.signal_, p.file_, p.line_);
            }
            return scope_.toggle(p.signal_, p.bit_, p.edge_, p.count_);
        case Kind::Branch: {
            Branch b = scope_.branch(p.file_, p.line_);
            return b.arm(p.name_, p.count_);
        }
        default:
            return scope_.line(p.file_, p.line_, p.count_, p.name_);
        }
    }

    /**
     * Closes the open scope, rendering it.
     *
     * The destructor does this; call it early only when the sink outlives the
     * records you want in the final scope.
     */
    void flush() {
        scope_.close();
        seenSignals_.release();
        signalNames_.release();
        open_ = false;
    }

private:
    Text currentName() const { return Text(current_.ptr, current_.len); }

    CoverageWriter* w_;
    Scope scope_;
    Vec<char> current_;
    StrArena signalNames_;
    StrMap seenSignals_;
    bool open_;
};

/**
 * Groups a flat record list by scope, in place.
 *
 * :cpp:class:`PointSink` requires records for one scope to be contiguous; this
 * is how a list that is not already grouped becomes so. The sort is stable, so
 * the relative order of records within a scope -- and any ordering the caller
 * does care about -- is preserved.
 *
 * :param pts: The records, reordered in place.
 * :param n: How many records ``pts`` holds.
 * :return: ``false`` if scratch space could not be allocated, in which case
 *     the list is left untouched.
 */
inline bool sortByScope(Point* pts, uint32_t n) {
    if (n < 2) return true;
    Vec<uint32_t> idx, scratch;
    Vec<Point> tmp;
    if (!idx.reserve(n) || !scratch.reserve(n) || !tmp.reserve(n)) return false;
    idx.len = scratch.len = tmp.len = n;
    for (uint32_t i = 0; i < n; ++i) idx[i] = i;
    mergeSortIndex(idx.ptr, scratch.ptr, n, [pts](uint32_t a, uint32_t b) {
        return compare(pts[a].scope_, pts[b].scope_) < 0;
    });
    for (uint32_t i = 0; i < n; ++i) tmp[i] = pts[idx[i]];
    for (uint32_t i = 0; i < n; ++i) pts[i] = tmp[i];
    return true;
}

}  // namespace UCIS_XML_NAMESPACE
