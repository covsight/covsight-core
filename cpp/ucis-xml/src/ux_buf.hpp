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
#pragma once

#include "ux_util.hpp"

#if !UCIS_XML_NO_STDIO
#include <cstdio>
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
