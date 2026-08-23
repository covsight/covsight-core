// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// ux_render_func.hpp - the functional-coverage renderers: FSM, assertion,
// covergroup. Tasks I-3.1 through I-3.3.
//
// Three of the ordering rules design §4 lists as the writer's problem live
// here: FSM states before transitions, ASSERTION's eight optional bins in their
// one legal sequence, and CGINSTANCE's coverpoints before its crosses.
#pragma once

#include "ux_render.hpp"

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
