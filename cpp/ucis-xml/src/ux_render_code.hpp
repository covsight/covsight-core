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
#pragma once

#include "ux_render.hpp"

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
