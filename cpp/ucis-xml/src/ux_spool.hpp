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
#pragma once

#include "ux_buf.hpp"

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
