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
#pragma once

#include "ux_key.hpp"

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
