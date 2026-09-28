#pragma once
//
// Key-grammar control flow — the sugar layer.
//
// The key grammar treats control words as first-class *flow* operators, not as
// a bag of operator tokens: each word declares the deterministic operator
// authority it binds to (ELIZA-1 book), the XCFE op it lowers to, the K'UHUL
// phase the construct runs in, its block role and its arity. A token sequence
// such as "if mode equals debug then verbose else quiet" plans into an ordered,
// phase-bound control plan that lowers to an executable XCFE node
// (@if/@then/@else), so control flow in the key grammar is as direct as syntax
// sugar while staying anchored in XCFE/K'UHUL operators.
//
#include "../util/json.h"

#include <cstdint>
#include <string>
#include <vector>

namespace khanary::micronaut::control {

// How a control word participates in a flow.
enum class BlockRole { None, Open, Mid, Close };

// One first-class control-flow operator.
struct ControlOperator {
    std::string word;           // lexical trigger ("if", "then", "equals", ...)
    std::string op_name;        // operator authority -> book key ELIZA-1:<op_name>
    std::string semantic_role;  // descriptive/control-semantic role (NOT a capability id)
    std::string xcfe_op;        // lowering target ("@if", "@then", "@else", ...)
    std::string phase;          // K'UHUL phase binding
    std::string block_role;     // "open" | "mid" | "close" | "none"
    std::string sugar;          // human-readable sugar form
    uint32_t arity = 1;         // operands the operator consumes
    // Capability ids execution requires. Named requires_caps because `requires`
    // is a C++20 keyword; the contract/JSON field is "requires".
    std::vector<std::string> requires_caps;
};

// The canonical control-flow operator table.
const std::vector<ControlOperator>& control_operators();

// Look up a control operator (case-insensitive); nullptr when not a control word.
const ControlOperator* find_control(const std::string& word);

bool is_control_word(const std::string& word);

// C6 successor phase (Pop -> Wo -> Yax -> Sek -> Ch'en -> Xul -> Pop).
std::string phase_after(const std::string& phase);

// ── Planning ────────────────────────────────────────────────────────────────

// One planned control step: an operator with its bound operand, in token order.
struct ControlStep {
    std::string word;
    std::string op_name;
    std::string semantic_role;
    std::string xcfe_op;
    std::string phase;           // flow phase: WHERE IT APPEARS
    std::string emit_phase;      // C6 successor of the flow phase
    std::string node_class;      // XCFE Node ISA class (from the operator authority)
    std::string authority_phase; // CanonicalPhase(node_class): WHERE ITS SEMANTICS ARE GOVERNED
    std::string block_role;      // "open" | "mid" | "close" | "none"
    std::string operand;         // bound operand text (may be empty)
    std::string operand_lhs;     // comparison lhs when arity >= 2
    std::vector<std::string> requires_caps;  // capability ids (contract field: "requires")
    uint32_t position = 0;       // token index of the operator
    uint32_t arity = 1;
};

struct ControlPlan {
    bool structured = false;   // a real flow (if/when ... then [else]) was recognised
    std::string form;          // "if/then/else" | "when/then" | "compare" | "flat" | "none"
    std::string condition;     // XCFE condition expression when structured
    std::vector<ControlStep> steps;
};

// Deterministic token scan: operators in order, operands bound to the span that
// follows each operator (the span before it supplies the lhs for comparisons).
ControlPlan plan_control_flow(const std::vector<std::string>& tokens);

// Lower a plan to an executable XCFE node: {"@if":..,"@then":..,"@else":..} for a
// structured flow, or an ordered {"@seq":[..]} of operator steps otherwise.
util::JsonPtr lower_to_xcfe(const ControlPlan& plan);

// Key-grammar rows: one control_flow_row per operator (the checked-in artifact
// form), and one control_flow_plan row describing a planned flow.
std::vector<util::JsonPtr> operator_rows();
util::JsonPtr plan_row(const ControlPlan& plan);

}  // namespace khanary::micronaut::control
