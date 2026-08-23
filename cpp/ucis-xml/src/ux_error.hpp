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
#pragma once

#include "ux_text.hpp"

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
