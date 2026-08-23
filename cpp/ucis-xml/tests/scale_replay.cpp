// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// T-11: replay a real Verilator coverage.dat through the writer.
//
// Design §5 makes a claim -- peak memory is bounded by the largest single
// scope, not by the design -- and a claim in a document is not a property of
// the code. This measures it: peak RSS, staged peak, throughput, output size,
// and whether the spool spilled, on OpenTitan's 1,241,667 points.
//
// Not part of the default suite: it needs a 241 MB input that is not in the
// repository. Run it directly, or through tests/ucis_xml/test_scale.py.
//
//   scale_replay <coverage.dat> <out.xml> [--deferred]
//
// Record format (Verilator SystemC::Coverage-3), the same one
// bench/coverage/scope_distribution.py parses:
//   C '<\x01key\x02value>...' <count>
// with f=file, l=line, n=column, t=type, page, o=comment, h=hierarchy.
#include "ux_points.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace ux = ucisxml;

namespace {

// Peak resident set size in KiB, from /proc. Zero where it is unavailable,
// which just means that one assertion is skipped rather than the whole test.
long peakRssKiB() {
    std::FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return 0;
    char line[256];
    long kb = 0;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, "VmHWM:", 6) == 0) {
            kb = std::strtol(line + 6, nullptr, 10);
            break;
        }
    }
    std::fclose(f);
    return kb;
}

struct Record {
    ux::Text hier, file, comment, signal, bit, kind;
    unsigned long line;
    unsigned long long count;
    bool rise;
};

// Splits one record body in place. The buffer stays alive for the whole
// replay, so the Text views into it are valid until the writer has copied
// them -- which it does before every call returns.
bool parse(char* body, size_t n, char* tail, Record& r) {
    r = Record();
    r.line = 1;
    r.count = std::strtoull(tail, nullptr, 10);

    size_t i = 0;
    while (i < n) {
        if (static_cast<unsigned char>(body[i]) != 0x01) { ++i; continue; }
        size_t keyStart = ++i;
        while (i < n && static_cast<unsigned char>(body[i]) != 0x02) ++i;
        if (i >= n) break;
        size_t keyEnd = i++;
        size_t valStart = i;
        while (i < n && static_cast<unsigned char>(body[i]) != 0x01) ++i;
        size_t valEnd = i;

        ux::Text val(body + valStart, valEnd - valStart);
        size_t klen = keyEnd - keyStart;
        const char* k = body + keyStart;

        if (klen == 1 && k[0] == 'h') r.hier = val;
        else if (klen == 1 && k[0] == 'f') r.file = val;
        else if (klen == 1 && k[0] == 'o') r.comment = val;
        else if (klen == 1 && k[0] == 't') r.kind = val;
        else if (klen == 1 && k[0] == 'l')
            r.line = std::strtoul(body + valStart, nullptr, 10);
    }
    return !r.hier.empty();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: scale_replay <coverage.dat> <out.xml> [--deferred]\n");
        return 2;
    }
    const bool deferred = argc > 3 && std::strcmp(argv[3], "--deferred") == 0;

    std::FILE* in = std::fopen(argv[1], "rb");
    if (!in) { std::perror(argv[1]); return 2; }

    ux::CoverageWriter cov;
    ux::WriterOptions o;
    o.writtenTime("2026-01-01T00:00:00");
    if (deferred) o.sources(ux::Sources::Deferred);
    if (!cov.openFile(argv[2], o)) {
        std::fprintf(stderr, "open failed: %s\n", cov.error());
        return 2;
    }
    cov.tool(ux::Tool().name("verilator").version("5.041").vendorId("VLTR"));
    cov.test(ux::Test().name("replay").passed(true));

    // Under UpFront the file list has to exist before the first scope, and a
    // converter reading a flat record file does not have one -- so it makes
    // one, with a pre-scan. That pass is the honest cost of UpFront for this
    // shape of producer, and it is why Deferred exists; measuring both is the
    // point of running this tool twice.
    ux::Vec<char> paths;
    ux::Vec<uint32_t> pathOffsets;
    if (!deferred) {
        char scan[1u << 20];
        while (std::fgets(scan, sizeof(scan), in)) {
            size_t len = std::strlen(scan);
            char* s = static_cast<char*>(std::memchr(scan, 0x01, len));
            while (s) {
                if (s[1] == 'f' && static_cast<unsigned char>(s[2]) == 0x02) {
                    char* v = s + 3;
                    char* e = v;
                    while (*e && static_cast<unsigned char>(*e) != 0x01 &&
                           *e != '\'') ++e;
                    uint32_t off = paths.len;
                    paths.reserve(paths.len + static_cast<uint32_t>(e - v) + 1);
                    std::memcpy(paths.ptr + off, v, static_cast<size_t>(e - v));
                    paths.len = off + static_cast<uint32_t>(e - v);
                    paths.push('\0');
                    pathOffsets.push(off);
                    break;
                }
                s = static_cast<char*>(std::memchr(
                        s + 1, 0x01, len - static_cast<size_t>(s + 1 - scan)));
            }
        }
        std::rewind(in);
        // Duplicates are folded by the file table, so the list can be handed
        // over verbatim -- superset and all.
        for (uint32_t i = 0; i < pathOffsets.len; ++i)
            cov.source(ux::Text(paths.ptr + pathOffsets[i]));
        std::printf("prescanPaths    %u\n", pathOffsets.len);
    }

    // coverage.dat is ordered by source file, not by hierarchy, so one scope's
    // records are scattered through it -- 1,520 scopes appear as about 30,000
    // runs. Rule 2 of the contract asks for contiguity, so the records have to
    // be grouped first. That is what ux::sortByScope() is for when the caller
    // has the records in memory; here they are 241 MB, so this builds an index
    // instead: intern each scope name once, keep (scopeId, file offset) per
    // record, sort by scope, and re-read in that order.
    //
    // The index is the tool's cost, not the writer's, and is reported
    // separately: 12 bytes per record against a writer whose peak is one scope.
    ux::StrArena scopeArena;
    ux::StrMap scopeIds;
    ux::Vec<uint32_t> recScope;
    ux::Vec<int64_t> recOffset;

    size_t cap = 1u << 20;
    char* buf = static_cast<char*>(std::malloc(cap));
    unsigned long long points = 0, skipped = 0;

    for (;;) {
        long off = std::ftell(in);
        if (!std::fgets(buf, static_cast<int>(cap), in)) break;
        size_t len = std::strlen(buf);
        if (len < 2 || buf[0] != 'C' || buf[1] != ' ') continue;
        char* start = static_cast<char*>(std::memchr(buf, '\'', len));
        char* end = nullptr;
        if (!start) continue;
        for (char* q = buf + len; q > start; --q)
            if (*(q - 1) == '\'') { end = q - 1; break; }
        if (!end || end <= start) continue;
        *end = '\0';

        Record r;
        if (!parse(start + 1, static_cast<size_t>(end - start - 1), end + 1, r)) {
            ++skipped;
            continue;
        }
        bool fresh = false;
        uint32_t id = scopeIds.getOrAdd(scopeArena, r.hier, scopeIds.count(), &fresh);
        recScope.push(id);
        recOffset.push(off);
    }

    ux::Vec<uint32_t> order, scratch;
    order.reserve(recScope.len);
    scratch.reserve(recScope.len);
    order.len = scratch.len = recScope.len;
    for (uint32_t i = 0; i < recScope.len; ++i) order[i] = i;
    // Stable, so records keep their file order within a scope.
    ux::mergeSortIndex(order.ptr, scratch.ptr, order.len,
                       [&recScope](uint32_t a, uint32_t b) {
                           return recScope[a] < recScope[b];
                       });

    std::printf("distinctScopes  %u\n", scopeIds.count());
    std::printf("indexBytes      %llu\n",
                static_cast<unsigned long long>(recScope.bytes() + recOffset.bytes() +
                                                order.bytes() + scratch.bytes()));

    std::clock_t t0 = std::clock();
    {
        ux::PointSink sink(cov);
        for (uint32_t oi = 0; oi < order.len; ++oi) {
            if (std::fseek(in, static_cast<long>(recOffset[order[oi]]), SEEK_SET) != 0)
                break;
            if (!std::fgets(buf, static_cast<int>(cap), in)) break;
            size_t len = std::strlen(buf);
            char* start = static_cast<char*>(std::memchr(buf, '\'', len));
            if (!start) continue;
            char* end = nullptr;
            for (char* q = buf + len; q > start; --q)
                if (*(q - 1) == '\'') { end = q - 1; break; }
            if (!end || end <= start) continue;
            *end = '\0';

            Record r;
            if (!parse(start + 1, static_cast<size_t>(end - start - 1), end + 1, r))
                continue;

            ux::Point p;
            p.scope(r.hier).count(r.count);
            // Verilator's toggle comment is "sig[3]:0->1"; anything else is a
            // line-shaped point. Reconstructing the toggle structure is the
            // converter's job, not the writer's, so it is done here.
            bool isToggle = r.kind.size == 6 &&
                            std::memcmp(r.kind.data, "toggle", 6) == 0;
            if (isToggle && r.comment.size) {
                const char* c = r.comment.data;
                size_t n = r.comment.size;
                size_t colon = n;
                for (size_t i = n; i-- > 0;) if (c[i] == ':') { colon = i; break; }
                size_t base = colon < n ? colon : n;
                size_t open = base, close = base;
                if (base && c[base - 1] == ']')
                    for (size_t i = base - 1; i-- > 0;)
                        if (c[i] == '[') { open = i; close = base - 1; break; }
                ux::Text sig(c, open);
                ux::Text bit(open < base ? c + open + 1 : c,
                             open < base ? close - open - 1 : 0);
                bool rise = colon >= n || c[n - 1] == '1';
                // The record's file/line is the signal's declaration site, and
                // toggleObject/id has no other source. PointSink declares it
                // once per signal rather than once per record.
                p.kind(ux::Kind::Toggle).signal(sig).bit(bit)
                 .file(r.file).line(static_cast<uint32_t>(r.line))
                 .edge(rise ? ux::Edge::Rise : ux::Edge::Fall);
            } else {
                p.kind(ux::Kind::Line).file(r.file)
                 .line(static_cast<uint32_t>(r.line)).name(r.comment);
            }
            sink.add(p);
            ++points;
        }
    }

    std::free(buf);
    std::fclose(in);

    bool ok = cov.close();
    double secs = static_cast<double>(std::clock() - t0) / CLOCKS_PER_SEC;

    std::printf("points          %llu\n", points);
    std::printf("skipped         %llu\n", skipped);
    std::printf("mode            %s\n", deferred ? "Deferred" : "UpFront");
    std::printf("ok              %d\n", ok ? 1 : 0);
    std::printf("error           %s\n", cov.error());
    std::printf("warnings        %llu\n",
                static_cast<unsigned long long>(cov.warnings()));
    std::printf("stagingPeak     %llu\n",
                static_cast<unsigned long long>(cov.stagingPeak()));
    std::printf("spoolBytes      %llu\n",
                static_cast<unsigned long long>(cov.spoolBytes()));
    std::printf("spoolSpilled    %d\n", cov.spoolSpilled() ? 1 : 0);
    std::printf("peakRssKiB      %ld\n", peakRssKiB());
    std::printf("seconds         %.3f\n", secs);
    if (secs > 0)
        std::printf("pointsPerSec    %.0f\n", static_cast<double>(points) / secs);

    return ok ? 0 : 1;
}
