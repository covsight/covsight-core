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
#pragma once

#include "ux_api.hpp"

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
