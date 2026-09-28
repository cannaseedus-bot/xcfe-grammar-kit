#pragma once
//
// XCFE Node ISA — the node *class* coordinate.
//
// Class, operator, flow phase and authority phase are independent coordinates:
//
//   class           what kind of control responsibility the node has
//   operator        what control operation it requests
//   flow_phase      WHERE IT APPEARS (where the construct was encountered)
//   authority_phase WHERE ITS SEMANTICS ARE GOVERNED
//
// CanonicalPhase(class) is fixed by this header. `authority_phase` must always
// equal CanonicalPhase(class); `flow_phase == authority_phase` is legal but is
// never required, so the two coordinates may legitimately differ.
//
// XcfeNodeClass is deliberately NOT RelationFamily (src/micronaut/relation_isa.h)
// — relations have their own ISA. There is deliberately no `Policy` class:
// operation authority is XCFE's own mechanism
// (XCFEExecutor::authorize_phase_transition + the `xcfe_required` edge flag),
// so making it a node class would double-encode it.
//
// This file is header-only (no link dependency) so any target may classify a
// node without pulling in the phase engine.
//
#include "phase_engine.h"

#include <cctype>
#include <string>
#include <vector>

namespace khanary::kuhul {

// The closed 14-class set.
enum class XcfeNodeClass {
    Condition,
    Routing,
    Causal,
    Structure,
    Selection,
    Execution,
    Capability,
    Transform,
    Verification,
    Consequence,
    Commit,
    Memory,
    Negation,
    KuhulRuntime,
};

inline const std::vector<XcfeNodeClass>& xcfe_node_classes() {
    static const std::vector<XcfeNodeClass> kClasses = {
        XcfeNodeClass::Condition,   XcfeNodeClass::Routing,     XcfeNodeClass::Causal,
        XcfeNodeClass::Structure,   XcfeNodeClass::Selection,   XcfeNodeClass::Execution,
        XcfeNodeClass::Capability,  XcfeNodeClass::Transform,   XcfeNodeClass::Verification,
        XcfeNodeClass::Consequence, XcfeNodeClass::Commit,      XcfeNodeClass::Memory,
        XcfeNodeClass::Negation,    XcfeNodeClass::KuhulRuntime};
    return kClasses;
}

inline const char* xcfe_node_class_name(XcfeNodeClass node_class) {
    switch (node_class) {
        case XcfeNodeClass::Condition:    return "CONDITION";
        case XcfeNodeClass::Routing:      return "ROUTING";
        case XcfeNodeClass::Causal:       return "CAUSAL";
        case XcfeNodeClass::Structure:    return "STRUCTURE";
        case XcfeNodeClass::Selection:    return "SELECTION";
        case XcfeNodeClass::Execution:    return "EXECUTION";
        case XcfeNodeClass::Capability:   return "CAPABILITY";
        case XcfeNodeClass::Transform:    return "TRANSFORM";
        case XcfeNodeClass::Verification: return "VERIFICATION";
        case XcfeNodeClass::Consequence:  return "CONSEQUENCE";
        case XcfeNodeClass::Commit:       return "COMMIT";
        case XcfeNodeClass::Memory:       return "MEMORY";
        case XcfeNodeClass::Negation:     return "NEGATION";
        case XcfeNodeClass::KuhulRuntime: return "KUHUL_RUNTIME";
    }
    return "";
}

// CanonicalPhase(class) — where the class's semantics are governed. Never
// depends on where a construct was encountered.
inline bool canonical_phase_for_xcfe_class(XcfeNodeClass node_class, Phase& out) {
    switch (node_class) {
        case XcfeNodeClass::Condition:
        case XcfeNodeClass::Routing:
        case XcfeNodeClass::Causal:
        case XcfeNodeClass::Structure:
            out = Phase::Wo;
            return true;
        case XcfeNodeClass::Selection:
            out = Phase::Yax;
            return true;
        case XcfeNodeClass::Execution:
        case XcfeNodeClass::Capability:
        case XcfeNodeClass::Transform:
            out = Phase::Sek;
            return true;
        case XcfeNodeClass::Verification:
        case XcfeNodeClass::Consequence:
            out = Phase::Chen;
            return true;
        case XcfeNodeClass::Commit:
            out = Phase::Xul;
            return true;
        case XcfeNodeClass::Memory:
        case XcfeNodeClass::Negation:
        case XcfeNodeClass::KuhulRuntime:
            out = Phase::Pop;
            return true;
    }
    return false;
}

inline bool xcfe_node_class_from_name(const std::string& name, XcfeNodeClass& out) {
    std::string upper;
    upper.reserve(name.size());
    for (unsigned char ch : name) upper.push_back(static_cast<char>(std::toupper(ch)));
    for (XcfeNodeClass node_class : xcfe_node_classes()) {
        if (upper == xcfe_node_class_name(node_class)) {
            out = node_class;
            return true;
        }
    }
    return false;
}

// operator -> class. Operator names are the ELIZA-1 vocabulary; SUBSTITUTE ->
// TRANSFORM and BOUNDARY / RELATION -> STRUCTURE close the three holes that
// previously left those operators unclassified.
inline bool xcfe_class_for_operator(const std::string& op_name, XcfeNodeClass& out) {
    std::string upper;
    upper.reserve(op_name.size());
    for (unsigned char ch : op_name) upper.push_back(static_cast<char>(std::toupper(ch)));
    if (upper == "DEPENDENCY" || upper == "CONSTRAINT") {
        out = XcfeNodeClass::Condition;
        return true;
    }
    if (upper == "ROUTING") {
        out = XcfeNodeClass::Routing;
        return true;
    }
    if (upper == "CAUSE") {
        out = XcfeNodeClass::Causal;
        return true;
    }
    if (upper == "BOUNDARY" || upper == "RELATION") {
        out = XcfeNodeClass::Structure;
        return true;
    }
    if (upper == "ALTERNATIVE") {
        out = XcfeNodeClass::Selection;
        return true;
    }
    if (upper == "DIRECT") {
        out = XcfeNodeClass::Execution;
        return true;
    }
    if (upper == "CAPABILITY") {
        out = XcfeNodeClass::Capability;
        return true;
    }
    if (upper == "SUBSTITUTE") {
        out = XcfeNodeClass::Transform;
        return true;
    }
    if (upper == "EVIDENCE" || upper == "TEST" || upper == "VERIFY") {
        out = XcfeNodeClass::Verification;
        return true;
    }
    if (upper == "SEQUENCE") {
        out = XcfeNodeClass::Consequence;
        return true;
    }
    if (upper == "COMMIT") {
        out = XcfeNodeClass::Commit;
        return true;
    }
    if (upper == "INVARIANT" || upper == "STATE" || upper == "MEMORY") {
        out = XcfeNodeClass::Memory;
        return true;
    }
    if (upper == "CONFLICT" || upper == "CONTRAST") {
        out = XcfeNodeClass::Negation;
        return true;
    }
    if (upper == "KUHUL_RUNTIME" || upper == "IDENTITY") {
        out = XcfeNodeClass::KuhulRuntime;
        return true;
    }
    return false;
}

}  // namespace khanary::kuhul
