#include "control_flow.h"

#include "../kuhul/phase_engine.h"
#include "../kuhul/xcfe_node_class.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <unordered_map>

namespace khanary::micronaut::control {
namespace {

// Lowerings the executor implements natively and deterministically. An operator
// that lowers to one of these needs NO specialist Micronaut: requires is empty.
bool is_native_lowering(const std::string& xcfe_op) {
    return xcfe_op == "@if" || xcfe_op == "@then" || xcfe_op == "@else" ||
           xcfe_op == "@for" || xcfe_op == "@from" || xcfe_op == "@as";
}

// requires(op): the capability ids execution requires. Native lowerings require
// nothing; otherwise the operator authority itself is the required capability
// (a track declaring VERIFY is genuinely capable of verification work). This is
// a *declaration* in the capability namespace — deliberately not the descriptive
// `role` label, which lives in a different namespace.
std::vector<std::string> requires_for(const ControlOperator& op) {
    if (is_native_lowering(op.xcfe_op)) return {};
    return {op.op_name};
}

// XCFE Node ISA classification for an operator authority: the node class, and
// the canonical phase where that class's semantics are governed.
void classify_operator(const std::string& op_name,
                       std::string* node_class,
                       std::string* authority_phase) {
    kuhul::XcfeNodeClass cls{};
    if (!kuhul::xcfe_class_for_operator(op_name, cls)) return;
    if (node_class) *node_class = kuhul::xcfe_node_class_name(cls);
    kuhul::Phase authority = kuhul::Phase::Pop;
    if (authority_phase && kuhul::canonical_phase_for_xcfe_class(cls, authority))
        *authority_phase = kuhul::phase_name(authority);
}

std::string lower_ascii(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}

std::string trim_copy(const std::string& s) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string join_range(const std::vector<std::string>& toks, size_t begin, size_t end) {
    std::string out;
    for (size_t i = begin; i < end && i < toks.size(); ++i) {
        if (!out.empty()) out.push_back(' ');
        out += toks[i];
    }
    return out;
}

// Comparison operators the XCFE condition evaluator understands.
const char* const kCompareOps[] = {"==", "!=", "<=", ">=", "<", ">"};

// Comparison *words* the key grammar accepts as sugar for "==".
const char* const kCompareWords[] = {" equals ", " equal ", " is ", " are ", " = "};

bool has_compare_op(const std::string& text) {
    for (const char* op : kCompareOps)
        if (text.find(op) != std::string::npos) return true;
    return false;
}

// Build an XCFE condition ("lhs op rhs", or a bare context variable) from the
// operand a conditional operator bound.
std::string condition_from(const ControlStep& step) {
    // arity >= 2 comparison operator: the span before supplies the lhs.
    if (step.arity >= 2 && !step.operand_lhs.empty() && !step.operand.empty())
        return step.operand_lhs + " == " + step.operand;

    if (has_compare_op(step.operand)) return step.operand;

    // Sugar comparison words inside the operand ("x equals y") lower to "==".
    for (const char* word : kCompareWords) {
        const auto pos = step.operand.find(word);
        if (pos == std::string::npos) continue;
        const std::string lhs = trim_copy(step.operand.substr(0, pos));
        const std::string rhs = trim_copy(step.operand.substr(pos + std::strlen(word)));
        if (lhs.empty() || rhs.empty()) continue;
        return lhs + " == " + rhs;
    }

    // Bare boolean / context variable.
    return trim_copy(step.operand);
}

const std::unordered_map<std::string, size_t>& control_index() {
    static const std::unordered_map<std::string, size_t> kIndex = [] {
        std::unordered_map<std::string, size_t> index;
        const auto& ops = control_operators();
        for (size_t i = 0; i < ops.size(); ++i) index.emplace(ops[i].word, i);
        return index;
    }();
    return kIndex;
}

const ControlStep* find_step(const std::vector<ControlStep>& steps, const std::string& word) {
    for (const auto& s : steps)
        if (s.word == word) return &s;
    return nullptr;
}

// One step as its executable XCFE node.
util::JsonPtr step_node(const ControlStep& step) {
    if (step.xcfe_op == "@for") {
        // "each <item> in <collection>" — otherwise the operand is the collection.
        std::string item = "item";
        std::string collection = step.operand;
        const auto in_pos = step.operand.find(" in ");
        if (step.operand.rfind("each ", 0) == 0 && in_pos != std::string::npos) {
            item = trim_copy(step.operand.substr(5, in_pos - 5));
            collection = trim_copy(step.operand.substr(in_pos + 4));
        }
        if (collection.empty()) return util::json_null();
        return util::json_object({{"@for", util::json_string(collection)},
                                  {"@as", util::json_string(item)}});
    }
    if (step.xcfe_op == "@from")
        return util::json_object({{"@from", util::json_string(step.operand)}});
    if (step.xcfe_op == "@as")
        return util::json_object({{"@as", util::json_string(step.operand)}});
    if (step.xcfe_op == "@node")
        return util::json_object({{"@node", util::json_string(step.operand)}});
    if (step.xcfe_op == "@cause")
        return util::json_object({{"@cause", util::json_string(step.operand)}});
    if (step.xcfe_op == "@direct")
        return util::json_object({{"@direct", util::json_string(step.operand)}});
    return util::json_object({
        {"@op", util::json_string(step.xcfe_op)},
        {"@args", util::json_object({{"word", util::json_string(step.word)},
                                     {"operator", util::json_string(step.op_name)},
                                     {"phase", util::json_string(step.phase)},
                                     {"operand", util::json_string(step.operand)}})}});
}

}  // namespace

// ── The canonical control-flow operator table ───────────────────────────────
// Eleven control-flow words carry the sugar forms; the remaining rows are the
// control words the runtime already bound, kept here so there is one authority
// for control semantics (role, book, phase, XCFE lowering).

const std::vector<ControlOperator>& control_operators() {
    static const std::vector<ControlOperator> kOps = {
        // ── control-flow sugar ──────────────────────────────────────────────
        {"if", "DEPENDENCY", "conditional-branch", "@if", "Pop", "open",
         "if <condition> then <consequence> [else <alternative>]", 1},
        {"else", "ALTERNATIVE", "branch-alternative", "@else", "Yax", "mid",
         "else <alternative>", 1},
        {"then", "SEQUENCE", "ordered-transition", "@then", "Wo", "mid",
         "then <consequence>", 1},
        {"when", "SEQUENCE", "temporal-condition", "@if", "Pop", "open",
         "when <trigger> then <consequence>", 1},
        {"how", "DIRECT", "method-execution", "@direct", "Wo", "none",
         "how <method>", 1},
        {"who", "RELATION", "entity-binding", "@node", "Pop", "none",
         "who <actor>", 1},
        {"why", "CAUSE", "causal-link", "@cause", "Yax", "none",
         "why <reason>", 1},
        {"for", "SEQUENCE", "iteration-or-scope", "@for", "Sek", "open",
         "for each <item> in <collection>", 1},
        {"from", "BOUNDARY", "origin-extraction", "@from", "Pop", "none",
         "from <source>", 1},
        {"as", "SUBSTITUTE", "role-cast-alias", "@as", "Sek", "none",
         "as <role>", 1},
        {"equals", "SUBSTITUTE", "comparison-equivalence", "@if", "Ch'en", "none",
         "<lhs> equals <rhs>", 2},
        // ── other control words the runtime already bound ───────────────────
        {"where", "RELATION", "location-binding", "@where", "Wo", "none",
         "where <location>", 1},
        {"is", "STATE", "state-assertion", "@if", "Ch'en", "none",
         "<subject> is <value>", 2},
        {"are", "STATE", "state-assertion", "@if", "Ch'en", "none",
         "<subject> are <value>", 2},
        {"equal", "SUBSTITUTE", "comparison-equivalence", "@if", "Ch'en", "none",
         "<lhs> equal <rhs>", 2},
        {"and", "RELATION", "conjunctive-join", "@and", "Wo", "none",
         "<a> and <b>", 2},
        {"or", "ALTERNATIVE", "disjunctive-branch", "@or", "Yax", "none",
         "<a> or <b>", 2},
        {"not", "CONFLICT", "negation-constraint", "@not", "Sek", "none",
         "not <expression>", 1},
        {"because", "CAUSE", "causal-link", "@cause", "Yax", "none",
         "because <reason>", 1},
        {"requires", "DEPENDENCY", "precondition-binding", "@if", "Pop", "open",
         "requires <precondition>", 1},
        {"depends", "DEPENDENCY", "precondition-binding", "@if", "Pop", "open",
         "depends on <precondition>", 1},
        {"evidence", "EVIDENCE", "evidence-binding", "@op", "Ch'en", "none",
         "evidence <support>", 1},
        {"shows", "EVIDENCE", "evidence-binding", "@op", "Ch'en", "none",
         "shows <support>", 1},
        {"proves", "EVIDENCE", "evidence-binding", "@op", "Ch'en", "none",
         "proves <claim>", 1},
        {"verify", "VERIFY", "verification-gate", "@op", "Ch'en", "mid",
         "verify <claim>", 1},
        {"check", "TEST", "test-gate", "@op", "Ch'en", "mid",
         "check <claim>", 1},
        {"assert", "TEST", "test-gate", "@op", "Ch'en", "mid",
         "assert <claim>", 1}
    };
    return kOps;
}

const ControlOperator* find_control(const std::string& word) {
    const auto& index = control_index();
    const auto it = index.find(lower_ascii(trim_copy(word)));
    if (it == index.end()) return nullptr;
    return &control_operators()[it->second];
}

bool is_control_word(const std::string& word) { return find_control(word) != nullptr; }

std::string phase_after(const std::string& phase) {
    static const char* const kCycle[6] = {"Pop", "Wo", "Yax", "Sek", "Ch'en", "Xul"};
    for (size_t i = 0; i < 6; ++i)
        if (phase == kCycle[i]) return kCycle[(i + 1) % 6];
    return phase;
}

// ── Planning ────────────────────────────────────────────────────────────────

ControlPlan plan_control_flow(const std::vector<std::string>& tokens) {
    ControlPlan plan;

    std::vector<std::string> toks;
    toks.reserve(tokens.size());
    for (const auto& raw : tokens) {
        const auto lower = lower_ascii(trim_copy(raw));
        if (!lower.empty()) toks.push_back(lower);
    }
    if (toks.empty()) {
        plan.form = "none";
        return plan;
    }

    std::vector<size_t> indices;
    for (size_t i = 0; i < toks.size(); ++i)
        if (is_control_word(toks[i])) indices.push_back(i);

    // Each operator binds the span that follows it; for comparisons the span
    // since the previous operator supplies the left-hand side.
    for (size_t k = 0; k < indices.size(); ++k) {
        const size_t start = indices[k];
        const size_t end = (k + 1 < indices.size()) ? indices[k + 1] : toks.size();
        const ControlOperator* op = find_control(toks[start]);
        if (!op) continue;

        ControlStep step;
        step.word = op->word;
        step.op_name = op->op_name;
        step.semantic_role = op->semantic_role;
        step.xcfe_op = op->xcfe_op;
        step.phase = op->phase;
        step.emit_phase = phase_after(op->phase);
        step.requires_caps = requires_for(*op);
        classify_operator(op->op_name, &step.node_class, &step.authority_phase);
        step.block_role = op->block_role;
        step.position = static_cast<uint32_t>(start);
        step.arity = op->arity;
        step.operand = join_range(toks, start + 1, end);
        if (op->arity >= 2) {
            const size_t clause_start = (k == 0) ? 0 : indices[k - 1] + 1;
            step.operand_lhs = join_range(toks, clause_start, start);
        }
        plan.steps.push_back(std::move(step));
    }

    if (plan.steps.empty()) {
        plan.form = "none";
        return plan;
    }

    const ControlStep* if_step = find_step(plan.steps, "if");
    const ControlStep* when_step = find_step(plan.steps, "when");
    const ControlStep* then_step = find_step(plan.steps, "then");
    const ControlStep* else_step = find_step(plan.steps, "else");
    const ControlStep* cond_step = if_step ? if_step : when_step;

    if (cond_step && then_step) {
        // A real flow: the branches are delimited by the branch words, not by
        // the next control word, so a nested comparison ("if mode equals debug
        // then ...") stays part of the condition instead of truncating it.
        for (auto& step : plan.steps) {
            if (step.word == cond_step->word) {
                step.operand = join_range(toks, step.position + 1, then_step->position);
            } else if (step.word == "then") {
                const size_t end = (else_step && else_step->position > step.position)
                                       ? else_step->position
                                       : toks.size();
                step.operand = join_range(toks, step.position + 1, end);
            } else if (step.word == "else") {
                step.operand = join_range(toks, step.position + 1, toks.size());
            }
        }
        plan.structured = true;
        const char* head = if_step ? "if" : "when";
        plan.form = std::string(head) + "/then" + (else_step ? "/else" : "");
        plan.condition = condition_from(*cond_step);
        return plan;
    }

    // No consequence branch: either a comparison expression or a flat sequence
    // of control bindings.
    bool comparison = false;
    for (const auto& s : plan.steps)
        if (s.arity >= 2 || s.semantic_role == "comparison-equivalence") comparison = true;
    plan.form = comparison ? "compare" : "flat";
    if (comparison) {
        for (const auto& s : plan.steps) {
            if (s.arity >= 2 || s.semantic_role == "comparison-equivalence") {
                plan.condition = condition_from(s);
                break;
            }
        }
    }
    return plan;
}

util::JsonPtr lower_to_xcfe(const ControlPlan& plan) {
    if (plan.structured) {
        const ControlStep* then_step = find_step(plan.steps, "then");
        const ControlStep* else_step = find_step(plan.steps, "else");
        const ControlStep* method_step = find_step(plan.steps, "how");

        util::JsonObject node;
        node["@if"] = util::json_string(plan.condition);

        if (then_step && !then_step->operand.empty()) {
            node["@then"] = util::json_string(then_step->operand);
        } else if (method_step && !method_step->operand.empty()) {
            // "if <condition> then how <method>" — the branch is the method op.
            node["@then"] = util::json_object(
                {{"@direct", util::json_string(method_step->operand)}});
        } else {
            node["@then"] = util::json_null();
        }

        node["@else"] = (else_step && !else_step->operand.empty())
                            ? util::json_string(else_step->operand)
                            : util::json_null();

        return util::json_object(std::move(node));
    }

    // A comparison clause is a predicate: it evaluates to true/false.
    if (plan.form == "compare" && !plan.steps.empty()) {
        return util::json_object({{"@if", util::json_string(plan.condition)},
                                  {"@then", util::json_bool(true)},
                                  {"@else", util::json_bool(false)}});
    }

    // Otherwise an ordered sequence of phase-bound control nodes. A "from X as Y"
    // pair folds into one origin node, since @as is the origin's binding.
    util::JsonArray seq;
    for (size_t i = 0; i < plan.steps.size(); ++i) {
        const auto& step = plan.steps[i];
        if (step.xcfe_op == "@from") {
            const ControlStep* alias = nullptr;
            for (size_t j = i + 1; j < plan.steps.size(); ++j) {
                if (plan.steps[j].xcfe_op == "@as") { alias = &plan.steps[j]; break; }
            }
            util::JsonObject from_node{{"@from", util::json_string(step.operand)}};
            if (alias && !alias->operand.empty())
                from_node["@as"] = util::json_string(alias->operand);
            seq.push_back(util::json_object(std::move(from_node)));
            continue;
        }
        if (step.xcfe_op == "@as") {
            bool bound_by_from = false;
            for (size_t j = 0; j < i; ++j)
                if (plan.steps[j].xcfe_op == "@from") bound_by_from = true;
            if (bound_by_from) continue;  // already folded into the @from node
        }
        seq.push_back(step_node(step));
    }
    // A single control clause lowers to its own node so it is directly
    // executable; only a multi-step flow needs the ordered @seq wrapper.
    if (seq.size() == 1) return seq.front();
    return util::json_object({{"@seq", util::json_array(std::move(seq))}});
}

// ── Key-grammar rows ────────────────────────────────────────────────────────

std::vector<util::JsonPtr> operator_rows() {
    std::vector<util::JsonPtr> rows;
    for (const auto& op : control_operators()) {
        std::string node_class;
        std::string authority_phase;
        classify_operator(op.op_name, &node_class, &authority_phase);
        util::JsonArray requires_json;
        for (const auto& capability : requires_for(op))
            requires_json.push_back(util::json_string(capability));
        rows.push_back(util::json_object({
            {"word", util::json_string(op.word)},
            {"key", util::json_string("control:" + op.word)},
            {"operator", util::json_string(op.op_name)},
            {"book", util::json_string("ELIZA-1:" + op.op_name)},
            {"role", util::json_string(op.semantic_role)},
            {"class", util::json_string(node_class)},
            {"requires", util::json_array(std::move(requires_json))},
            {"flow_phase", util::json_string(op.phase)},
            {"authority_phase", util::json_string(authority_phase)},
            {"xcfe_op", util::json_string(op.xcfe_op)},
            {"phase", util::json_string(op.phase)},
            {"emit_phase", util::json_string(phase_after(op.phase))},
            {"block_role", util::json_string(op.block_role)},
            {"arity", util::json_number(static_cast<double>(op.arity))},
            {"sugar", util::json_string(op.sugar)},
            {"lexicon_source", util::json_string("src/micronaut/control_flow.cpp")}}));
    }
    return rows;
}

util::JsonPtr plan_row(const ControlPlan& plan) {
    util::JsonArray steps;
    for (const auto& s : plan.steps) {
        util::JsonArray requires_json;
        for (const auto& capability : s.requires_caps)
            requires_json.push_back(util::json_string(capability));
        steps.push_back(util::json_object({
            {"word", util::json_string(s.word)},
            {"operator", util::json_string(s.op_name)},
            {"xcfe_op", util::json_string(s.xcfe_op)},
            {"role", util::json_string(s.semantic_role)},
            {"class", util::json_string(s.node_class)},
            {"requires", util::json_array(std::move(requires_json))},
            {"phase", util::json_string(s.phase)},
            {"authority_phase", util::json_string(s.authority_phase)},
            {"emit_phase", util::json_string(s.emit_phase)},
            {"block_role", util::json_string(s.block_role)},
            {"position", util::json_number(static_cast<double>(s.position))},
            {"arity", util::json_number(static_cast<double>(s.arity))},
            {"operand", util::json_string(s.operand)},
            {"operand_lhs", util::json_string(s.operand_lhs)}}));
    }
    return util::json_object({
        {"schema", util::json_string("key.grammar.control_flow_plan.v1")},
        {"form", util::json_string(plan.form)},
        {"structured", util::json_bool(plan.structured)},
        {"condition", util::json_string(plan.condition)},
        {"steps", util::json_array(std::move(steps))}});
}

}  // namespace khanary::micronaut::control
