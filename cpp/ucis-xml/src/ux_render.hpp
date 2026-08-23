// SPDX-License-Identifier: Apache-2.0 OR MIT
// Copyright the covsight contributors.
//
// ux_render.hpp - the element fragments every coverage kind shares: source-id
// elements, BIN, the attribute groups, USER_ATTR, and the options children.
//
// Two rules from design §8 are implemented here rather than in each renderer,
// because they are what make XML viable at these sizes at all:
//   * attributes equal to their schema default are omitted (§8.2);
//   * COVERPOINT, CROSS and CGINSTANCE each *require* an options child, so the
//     required child costs ten bytes, "<options/>", and must not be skipped.
#pragma once

#include "ux_stage.hpp"

namespace UCIS_XML_NAMESPACE {
namespace stage {

// STATEMENT_ID and LINE_ID, under whichever element name the context requires
// (`id`, `blockId`, `statementId`, `cginstSourceId`, `cgSourceId`).
inline void renderId(Out& o, Text tag, const SLoc& loc, Status& st) {
    o.put('<');
    o.write(tag);
    attrU(o, "file", positive(loc.file, st.sanitize));
    attrU(o, "line", positive(loc.line, st.sanitize));
    attrU(o, "inlineCount", positive(loc.inlineCount, st.sanitize));
    o.lit("/>");
}

// binAttributes: alias, coverageCountGoal, excluded, excludedReason, weight.
// objAttributes is the same minus coverageCountGoal, so one function with a
// flag covers both.
inline void renderAttrGroup(Out& o, const Stage& sg, uint32_t slot, bool isBin,
                            Status& st) {
    const Extra& e = sg.extraAt(slot);
    if (!e.alias.empty()) attr(o, "alias", sg.text(e.alias), st.sanitize);
    if (isBin && e.goal != Extra::kNoGoal) attrU(o, "coverageCountGoal", e.goal);
    if (e.excluded) attrBool(o, "excluded", true);
    if (!e.excludedReason.empty())
        attr(o, "excludedReason", sg.text(e.excludedReason), st.sanitize);
    if (e.weight != Extra::kNoWeight) attrU(o, "weight", e.weight);
}

inline void renderUserAttrs(Out& o, const Stage& sg, uint32_t slot, Status& st) {
    uint32_t i = sg.extraAt(slot).attrHead;
    while (i != kNone) {
        const UserAttr& a = sg.userAttrs[i];
        o.lit("<userAttr");
        attr(o, "key", sg.text(a.key), st.sanitize);
        attr(o, "type", Text(toString(a.type)), st.sanitize);
        o.put('>');
        writeEscaped(o, sg.text(a.value), st.sanitize);
        o.lit("</userAttr>");
        i = a.next;
    }
}

// BIN_CONTENTS. `name` becomes @nameComponent, which is where a tool's
// per-point comment belongs -- UCIS has no @name on BIN itself.
inline void renderContents(Out& o, const Stage& sg, Str name, uint64_t count,
                           Status& st) {
    o.lit("<contents");
    attrU(o, "coverageCount", count);
    if (!name.empty()) attr(o, "nameComponent", sg.text(name), st.sanitize);
    o.lit("/>");
}

// A complete BIN under the element name the context requires: `bin`,
// `blockBin`, `branchBin`, `stateBin`, `transitionBin`, or one of ASSERTION's
// eight.
inline void renderBin(Out& o, const Stage& sg, Text tag, Str name, uint64_t count,
                      uint32_t slot, Status& st) {
    o.put('<');
    o.write(tag);
    renderAttrGroup(o, sg, slot, /*isBin=*/true, st);
    o.put('>');
    renderContents(o, sg, name, count, st);
    renderUserAttrs(o, sg, slot, st);
    o.lit("</");
    o.write(tag);
    o.put('>');
}

// Which of the shared options attributes a given element actually declares.
enum OptsKind : uint8_t { kOptsCoverpoint, kOptsCross, kOptsCgInstance };

// The required <options/> child. Everything defaulted collapses to ten bytes.
inline void renderOptions(Out& o, const Stage& sg, const SOpts& op, OptsKind kind,
                          Status& st) {
    o.lit("<options");
    if (op.weight != 1) attrU(o, "weight", op.weight);
    if (op.goal != 100) attrU(o, "goal", op.goal);
    if (!op.comment.empty()) attr(o, "comment", sg.text(op.comment), st.sanitize);
    if (op.atLeast != 1) attrU(o, "at_least", op.atLeast);
    if (kind != kOptsCross) {
        if (op.detectOverlap) attrBool(o, "detect_overlap", true);
        if (op.autoBinMax != 64) attrU(o, "auto_bin_max", op.autoBinMax);
    }
    if (kind != kOptsCoverpoint && op.crossNumPrintMissing != 0)
        attrU(o, "cross_num_print_missing", op.crossNumPrintMissing);
    if (kind == kOptsCgInstance) {
        if (op.perInstance) attrBool(o, "per_instance", true);
        if (op.mergeInstances) attrBool(o, "merge_instances", true);
    }
    o.lit("/>");
}

// metricAttributes, carried by every per-kind coverage container.
inline void renderMetricAttrs(Out& o, const Stage& sg, CovKind kind, Status& st) {
    uint32_t k = static_cast<uint32_t>(kind);
    if (!sg.metricMode[k].empty())
        attr(o, "metricMode", sg.text(sg.metricMode[k]), st.sanitize);
    if (sg.kindWeight[k] != Extra::kNoWeight) attrU(o, "weight", sg.kindWeight[k]);
}

}  // namespace stage
}  // namespace UCIS_XML_NAMESPACE
