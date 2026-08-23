// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// ux_doc.hpp - the document frame: the root element, the source-file table, the
// history node, and the scope stack.
//
// Task I-1.9. Everything the schema requires but the caller never mentions is
// supplied here (design §4): file ids, instance ids, the nine required
// HISTORY_NODE attributes, and the timestamp.
#pragma once

#include "ux_render_code.hpp"
#include "ux_render_func.hpp"
#include "ux_intern.hpp"
#include "ux_spool.hpp"

#ifndef UCIS_XML_NO_TIME
#define UCIS_XML_NO_TIME 0
#endif

#include <new>

#if !UCIS_XML_NO_TIME
#include <ctime>
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
