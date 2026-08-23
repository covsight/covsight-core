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
#pragma once

#include "ux_doc.hpp"

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
