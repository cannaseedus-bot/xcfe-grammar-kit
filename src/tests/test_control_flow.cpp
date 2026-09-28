#include "conformance.h"
#include "../kuhul/xcfe_executor.h"
#include "../kuhul/xcfe_node_class.h"
#include "../micronaut/control_flow.h"
#include "../micronaut/eliza_operators.h"
#include "../micronaut/mx2lm-ngram.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace khanary;
using namespace khanary::micronaut;

namespace {

// Evaluate a lowered control node through the real XCFE executor.
std::string eval_node(const util::JsonPtr& node, const kuhul::XCFEContext& ctx) {
    kuhul::XCFEExecutor exec;
    auto out = exec.eval(node, ctx);
    if (!out || !out->is_string()) return "<non-string>";
    return out->as_string();
}

// Same, but for structural results (arrays/objects/booleans) — JSON text.
std::string eval_json(const util::JsonPtr& node, const kuhul::XCFEContext& ctx) {
    kuhul::XCFEExecutor exec;
    auto out = exec.eval(node, ctx);
    if (!out) return "<null-ptr>";
    return util::stringify(out);
}

const control::ControlStep* step(const control::ControlPlan& plan, const std::string& word) {
    for (const auto& s : plan.steps)
        if (s.word == word) return &s;
    return nullptr;
}

bool in_cycle(const std::string& phase) {
    static const std::vector<std::string> kCycle = {"Pop", "Wo", "Yax", "Sek", "Ch'en", "Xul"};
    return std::find(kCycle.begin(), kCycle.end(), phase) != kCycle.end();
}

}  // namespace

int main() {
    return test::run("control-flow", [](test::Suite& suite) {
        // ── the eleven control-flow words are first-class operators ─────────
        const std::vector<std::string> goal = {"if",   "else", "then", "when", "how", "who",
                                              "why",  "for",  "from", "as",   "equals"};
        for (const auto& word : goal) {
            const auto* op = control::find_control(word);
            suite.check("control_word_" + word, op != nullptr);
            if (!op) continue;
            suite.check("role_" + word, !op->semantic_role.empty());
            suite.check("xcfe_" + word, !op->xcfe_op.empty() && op->xcfe_op[0] == '@');
            suite.check("sugar_" + word, !op->sugar.empty());
            suite.check("arity_" + word, op->arity >= 1 && op->arity <= 2);
            suite.check("block_role_" + word,
                        op->block_role == "open" || op->block_role == "mid" ||
                            op->block_role == "close" || op->block_role == "none");
        }

        // ── every operator binds a real authority and a C6 phase ────────────
        const ElizaOperatorBank bank;
        bool authorities_ok = true;
        bool phases_ok = true;
        for (const auto& op : control::control_operators()) {
            if (bank.find(op.op_name) == nullptr) authorities_ok = false;
            if (!in_cycle(op.phase)) phases_ok = false;
        }
        suite.check("authorities_in_eliza_bank", authorities_ok);
        suite.check("phases_in_c6_cycle", phases_ok);

        // ── C6 successor ────────────────────────────────────────────────────
        suite.check("phase_after_pop", control::phase_after("Pop") == "Wo");
        suite.check("phase_after_wo", control::phase_after("Wo") == "Yax");
        suite.check("phase_after_yax", control::phase_after("Yax") == "Sek");
        suite.check("phase_after_sek", control::phase_after("Sek") == "Ch'en");
        suite.check("phase_after_chen", control::phase_after("Ch'en") == "Xul");
        suite.check("phase_after_xul_wraps", control::phase_after("Xul") == "Pop");
        suite.check("phase_after_unknown", control::phase_after("NoSuchPhase") == "NoSuchPhase");

        // ── sugar: if <condition> then <consequence> else <alternative> ─────
        const auto plan = control::plan_control_flow(
            {"if", "mode", "equals", "debug", "then", "verbose", "else", "quiet"});
        suite.check("if_structured", plan.structured);
        suite.check("if_form", plan.form == "if/then/else");
        suite.check("if_condition", plan.condition == "mode == debug");
        suite.check("if_step_count", plan.steps.size() == 4);
        const auto* then_step = step(plan, "then");
        const auto* else_step = step(plan, "else");
        suite.check("if_then_operand", then_step && then_step->operand == "verbose");
        suite.check("if_else_operand", else_step && else_step->operand == "quiet");
        suite.check("then_phase_wo", then_step && then_step->phase == "Wo");
        suite.check("else_phase_yax", else_step && else_step->phase == "Yax");
        const auto* cond = step(plan, "if");
        suite.check("if_emit_phase_wo", cond && cond->emit_phase == "Wo");

        // lowering + execution through the real XCFE executor
        const auto node = control::lower_to_xcfe(plan);
        suite.check("lowered_if_condition",
                    node->get("@if") && node->get("@if")->is_string() &&
                        node->get("@if")->as_string() == "mode == debug");
        const kuhul::XCFEContext debug_ctx{{"mode", "debug"}};
        const kuhul::XCFEContext release_ctx{{"mode", "release"}};
        suite.check("xcfe_then_branch", eval_node(node, debug_ctx) == "verbose");
        suite.check("xcfe_else_branch", eval_node(node, release_ctx) == "quiet");

        // ── sugar: when <trigger> then <consequence> ────────────────────────
        const auto when_plan = control::plan_control_flow({"when", "ready", "then", "go"});
        suite.check("when_structured", when_plan.structured);
        suite.check("when_form", when_plan.form == "when/then");
        suite.check("when_condition", when_plan.condition == "ready");
        const auto when_node = control::lower_to_xcfe(when_plan);
        suite.check("when_then_branch",
                    eval_node(when_node, kuhul::XCFEContext{{"ready", "true"}}) == "go");
        suite.check("when_else_inactive",
                    eval_node(when_node, kuhul::XCFEContext{{"ready", "false"}}) != "go");

        // ── comparison without a branch ─────────────────────────────────────
        const auto cmp = control::plan_control_flow({"name", "equals", "khanary"});
        suite.check("compare_form", cmp.form == "compare");
        suite.check("compare_not_structured", !cmp.structured);
        const auto* eq = step(cmp, "equals");
        suite.check("compare_phase_chen", eq && eq->phase == "Ch'en");
        suite.check("compare_xcfe_if", eq && eq->xcfe_op == "@if");
        suite.check("compare_arity2", eq && eq->arity == 2);
        suite.check("compare_lhs", eq && eq->operand_lhs == "name");
        suite.check("compare_rhs", eq && eq->operand == "khanary");

        // ── query operators + origin/cast (flat phase-bound bindings) ───────
        const auto who = control::plan_control_flow({"who", "wrote", "this"});
        suite.check("who_flat", who.form == "flat" && !who.structured);
        suite.check("who_phase_pop", step(who, "who") && step(who, "who")->phase == "Pop");
        suite.check("who_xcfe_node", step(who, "who") && step(who, "who")->xcfe_op == "@node");

        const auto why = control::plan_control_flow({"why", "did", "it", "fail"});
        suite.check("why_operator_cause", step(why, "why") && step(why, "why")->op_name == "CAUSE");
        suite.check("why_phase_yax", step(why, "why") && step(why, "why")->phase == "Yax");

        const auto how = control::plan_control_flow({"how", "do", "i", "deploy"});
        suite.check("how_operator_direct",
                    step(how, "how") && step(how, "how")->op_name == "DIRECT");
        suite.check("how_xcfe_direct", step(how, "how") && step(how, "how")->xcfe_op == "@direct");

        const auto iter = control::plan_control_flow({"for", "each", "item", "in", "list"});
        suite.check("for_phase_sek", step(iter, "for") && step(iter, "for")->phase == "Sek");
        suite.check("for_xcfe", step(iter, "for") && step(iter, "for")->xcfe_op == "@for");
        suite.check("for_block_open", step(iter, "for") && step(iter, "for")->block_role == "open");

        const auto origin = control::plan_control_flow({"from", "archive", "as", "source"});
        suite.check("from_phase_pop", step(origin, "from") && step(origin, "from")->phase == "Pop");
        suite.check("as_phase_sek", step(origin, "as") && step(origin, "as")->phase == "Sek");
        suite.check("as_role_cast",
                    step(origin, "as") && step(origin, "as")->semantic_role == "role-cast-alias");
        suite.check("from_before_as", step(origin, "from") && step(origin, "as") &&
                                          step(origin, "from")->position < step(origin, "as")->position);
        const auto origin_node = control::lower_to_xcfe(origin);
        // A single clause lowers to its own node; a multi-step flow to an @seq.
        suite.check("single_step_clause_is_unwrapped",
                    origin_node->get("@from") && !origin_node->get("@seq"));
        const auto multi = control::plan_control_flow({"who", "wrote", "this", "why", "it", "failed"});
        const auto multi_node = control::lower_to_xcfe(multi);
        const auto* multi_seq = multi_node->get("@seq");
        suite.check("multi_step_flow_lowers_to_seq",
                    multi_seq && multi_seq->is_array() && multi_seq->as_array().size() == 2);

        // ── edge cases ──────────────────────────────────────────────────────
        suite.check("empty_plan_none", control::plan_control_flow({}).form == "none");
        suite.check("no_control_none",
                    control::plan_control_flow({"hello", "world"}).form == "none");
        suite.check("unknown_not_control", control::find_control("banana") == nullptr);
        suite.check("case_insensitive", control::find_control("IF") != nullptr);
        suite.check("if_without_then_not_structured",
                    !control::plan_control_flow({"if", "a", "b"}).structured);

        // ── runtime integration: phase-bound typed control bindings ─────────
        const auto bindings =
            Mx2LmNGram::resolve_control_bindings({"if", "mode", "equals", "debug"});
        suite.check("binding_count", bindings.size() == 2);
        if (bindings.size() == 2) {
            suite.check("binding_if_authority", bindings[0].operator_name == "DEPENDENCY");
            suite.check("binding_if_phase", bindings[0].detect_phase == "Pop");
            suite.check("binding_if_emit", bindings[0].emit_phase == "Wo");
            suite.check("binding_if_xcfe", bindings[0].xcfe_op == "@if");
            suite.check("binding_book", bindings[0].book_key == "ELIZA-1:DEPENDENCY");
            suite.check("binding_equals_authority", bindings[1].operator_name == "SUBSTITUTE");
            suite.check("binding_equals_phase", bindings[1].detect_phase == "Ch'en");
        }
        const auto taxonomy = Mx2LmNGram::classify_gram_taxonomy(
            Mx2LmNGram::tokenize("if who why from as equals"));
        suite.check("control_tokens_present_in_coarse", !taxonomy.coarse_grams.empty());
        const auto projection =
            Mx2LmNGram::compose(Mx2LmNGram::tokenize("if mode equals debug then go"));
        suite.check("compose_binds_controls", projection.control_bindings.size() == 3);

        // ── executing every sugar form through the real executor ─────────────
        // @for — native iteration over a context collection.
        const auto iter_node = control::lower_to_xcfe(iter);
        suite.check("for_lowers_to_at_for",
                    iter_node->get("@for") && iter_node->get("@for")->as_string() == "list");
        suite.check("for_binds_each_name",
                    iter_node->get("@as") && iter_node->get("@as")->as_string() == "item");
        const kuhul::XCFEContext list_ctx{{"list", "[\"alpha\",\"beta\",\"gamma\"]"}};
        suite.check("for_executes_json_array",
                    eval_json(iter_node, list_ctx) == "[\"alpha\",\"beta\",\"gamma\"]");
        suite.check("for_executes_delimited_collection",
                    eval_json(iter_node, kuhul::XCFEContext{{"list", "one, two three"}}) ==
                        "[\"one\",\"two\",\"three\"]");
        suite.check("for_empty_collection_is_empty_array",
                    eval_json(iter_node, kuhul::XCFEContext{}) == "[]");
        const auto for_body = util::json_object({
            {"@for", util::json_string("list")},
            {"@as", util::json_string("item")},
            {"@do", util::json_object({{"@as", util::json_string("out")},
                                       {"@value", util::json_string("item")},
                                       {"@cast", util::json_string("upper")}})}});
        suite.check("for_binds_the_element_per_iteration",
                    eval_json(for_body, list_ctx) == "[\"ALPHA\",\"BETA\",\"GAMMA\"]");
        const auto for_index = util::json_object({
            {"@for", util::json_string("list")},
            {"@as", util::json_string("item")},
            {"@index", util::json_string("i")},
            {"@do", util::json_object({{"@as", util::json_string("out")},
                                       {"@value", util::json_string("i")}})}});
        suite.check("for_binds_the_index",
                    eval_json(for_index, list_ctx) == "[\"0\",\"1\",\"2\"]");

        // @from — origin extraction (and binding the origin for a body).
        const auto from_plan = control::plan_control_flow({"from", "archive.deep", "as", "source"});
        const auto from_node = control::lower_to_xcfe(from_plan);
        suite.check("from_folds_into_one_node", !from_node->get("@seq") && from_node->get("@from"));
        suite.check("from_lowers_to_at_from",
                    from_node->get("@from") &&
                        from_node->get("@from")->as_string() == "archive.deep");
        suite.check("from_folds_the_alias",
                    from_node->get("@as") && from_node->get("@as")->as_string() == "source");
        suite.check("from_executes_dotted_origin",
                    eval_node(from_node, kuhul::XCFEContext{}) == "archive");
        suite.check("from_executes_uri_origin",
                    eval_node(util::json_object({{"@from", util::json_string("https://khanary.dev/x/y")}}),
                              kuhul::XCFEContext{}) == "https://khanary.dev");
        suite.check("from_executes_delimited_origin",
                    eval_node(util::json_object({{"@from", util::json_string("a,b,c")}}),
                              kuhul::XCFEContext{}) == "a");
        const auto from_body = util::json_object({
            {"@from", util::json_string("archive.deep")},
            {"@as", util::json_string("src")},
            {"@do", util::json_object({{"@as", util::json_string("out")},
                                       {"@value", util::json_string("src")},
                                       {"@cast", util::json_string("upper")}})}});
        suite.check("from_binds_the_origin_for_the_body",
                    eval_node(from_body, kuhul::XCFEContext{}) == "ARCHIVE");

        // @as — alias / cast.
        suite.check("as_casts_to_upper",
                    eval_node(util::json_object({{"@as", util::json_string("label")},
                                                 {"@value", util::json_string("ready")},
                                                 {"@cast", util::json_string("upper")}}),
                              kuhul::XCFEContext{}) == "READY");
        suite.check("as_casts_to_key",
                    eval_node(util::json_object({{"@as", util::json_string("k")},
                                                 {"@value", util::json_string("a b-c")},
                                                 {"@cast", util::json_string("key")}}),
                              kuhul::XCFEContext{}) == "A_B_C");
        suite.check("as_resolves_a_context_value",
                    eval_node(util::json_object({{"@as", util::json_string("k")},
                                                 {"@value", util::json_string("mode")}}),
                              kuhul::XCFEContext{{"mode", "debug"}}) == "debug");
        const auto as_body = util::json_object({
            {"@as", util::json_string("alias")},
            {"@value", util::json_string("mode")},
            {"@do", util::json_object({{"@if", util::json_string("alias == debug")},
                                       {"@then", util::json_string("verbose")},
                                       {"@else", util::json_string("quiet")}})}});
        suite.check("as_binds_the_alias_for_the_body",
                    eval_node(as_body, kuhul::XCFEContext{{"mode", "debug"}}) == "verbose");

        // @cause — the causal link, not just the word.
        const auto cause_node = control::lower_to_xcfe(why);
        suite.check("why_lowers_to_at_cause", cause_node->get("@cause") && !cause_node->get("@seq"));
        const std::string cause_out = eval_json(cause_node, kuhul::XCFEContext{});
        suite.check("cause_executes_relation", cause_out.find("@causes") != std::string::npos);
        suite.check("cause_executes_operator", cause_out.find("CAUSE") != std::string::npos);
        suite.check("cause_executes_effect", cause_out.find("did it fail") != std::string::npos);
        suite.check("cause_takes_reason_from_context",
                    eval_json(util::json_object({{"@cause", util::json_string("it failed")}}),
                              kuhul::XCFEContext{{"cause", "disk full"}})
                            .find("disk full") != std::string::npos);

        // @direct — DIRECT resolves a method to the host-provided action.
        const auto direct_node = control::lower_to_xcfe(how);
        suite.check("how_lowers_to_at_direct",
                    direct_node->get("@direct") && !direct_node->get("@seq"));
        const std::string direct_unresolved = eval_json(direct_node, kuhul::XCFEContext{});
        suite.check("direct_reports_unresolved",
                    direct_unresolved.find("\"resolved\":false") != std::string::npos);
        suite.check("direct_reports_operator",
                    direct_unresolved.find("DIRECT") != std::string::npos);
        suite.check("direct_resolves_host_action",
                    eval_json(direct_node, kuhul::XCFEContext{{"op.deploy", "ship-it"}})
                            .find("ship-it") != std::string::npos);

        // A comparison clause executes as a predicate.
        const auto cmp_node = control::lower_to_xcfe(cmp);
        suite.check("compare_executes_true",
                    eval_json(cmp_node, kuhul::XCFEContext{{"name", "khanary"}}) == "true");
        suite.check("compare_executes_false",
                    eval_json(cmp_node, kuhul::XCFEContext{{"name", "other"}}) == "false");

        // The @op form addresses the same ops (leading '@' normalised).
        suite.check("op_form_addresses_a_control_op",
                    eval_json(util::json_object({{"@op", util::json_string("@for")},
                                                 {"@args", util::json_object(
                                                     {{"@for", util::json_string("list")}})}}),
                              list_ctx) == "[\"alpha\",\"beta\",\"gamma\"]");
        suite.check("unknown_op_is_null",
                    eval_json(util::json_object({{"@op", util::json_string("nope")}}),
                              kuhul::XCFEContext{}) == "null");

        // ── XCFE Node ISA: class, flow phase, authority phase ───────────────
        bool every_operator_has_class = true;
        for (const auto& op : control::control_operators()) {
            kuhul::XcfeNodeClass cls{};
            if (!kuhul::xcfe_class_for_operator(op.op_name, cls)) every_operator_has_class = false;
        }
        suite.check("every_operator_has_a_class", every_operator_has_class);
        bool every_class_has_phase = true;
        for (kuhul::XcfeNodeClass cls : kuhul::xcfe_node_classes()) {
            kuhul::Phase phase = kuhul::Phase::Pop;
            if (!kuhul::canonical_phase_for_xcfe_class(cls, phase)) every_class_has_phase = false;
        }
        suite.check("every_class_has_a_canonical_phase", every_class_has_phase);
        suite.check("isa_is_fourteen_classes", kuhul::xcfe_node_classes().size() == 14);

        // the three former holes are closed
        kuhul::XcfeNodeClass hole{};
        suite.check("substitute_is_transform",
                    kuhul::xcfe_class_for_operator("SUBSTITUTE", hole) &&
                        hole == kuhul::XcfeNodeClass::Transform);
        suite.check("boundary_is_structure",
                    kuhul::xcfe_class_for_operator("BOUNDARY", hole) &&
                        hole == kuhul::XcfeNodeClass::Structure);
        suite.check("relation_is_structure",
                    kuhul::xcfe_class_for_operator("RELATION", hole) &&
                        hole == kuhul::XcfeNodeClass::Structure);

        // the two phase coordinates, and the fact that they may legally differ
        const auto isa_plan = control::plan_control_flow({"why", "did", "it", "fail"});
        const auto* why_step = step(isa_plan, "why");
        suite.check("why_class_causal", why_step && why_step->node_class == "CAUSAL");
        suite.check("why_flow_phase_yax", why_step && why_step->phase == "Yax");
        suite.check("why_authority_phase_wo", why_step && why_step->authority_phase == "Wo");
        suite.check("flow_phase_may_differ_from_authority",
                    why_step && why_step->phase != why_step->authority_phase);
        const auto cond_plan = control::plan_control_flow({"if", "a", "then", "b"});
        suite.check("if_class_condition",
                    step(cond_plan, "if") && step(cond_plan, "if")->node_class == "CONDITION");
        suite.check("if_authority_wo",
                    step(cond_plan, "if") && step(cond_plan, "if")->authority_phase == "Wo");
        suite.check("then_class_consequence",
                    step(cond_plan, "then") &&
                        step(cond_plan, "then")->node_class == "CONSEQUENCE");
        const auto as_only = control::plan_control_flow({"as", "role"});
        suite.check("as_class_transform",
                    step(as_only, "as") && step(as_only, "as")->node_class == "TRANSFORM");
        const auto from_only = control::plan_control_flow({"from", "src"});
        suite.check("from_class_structure",
                    step(from_only, "from") && step(from_only, "from")->node_class == "STRUCTURE");
        suite.check("else_class_selection",
                    step(control::plan_control_flow({"else", "x"}), "else") &&
                        step(control::plan_control_flow({"else", "x"}), "else")->node_class ==
                            "SELECTION");

        // artifact rows carry all three coordinates
        bool rows_carry_isa = true;
        for (const auto& row : control::operator_rows()) {
            if (!row->get("class") || !row->get("authority_phase") || !row->get("flow_phase"))
                rows_carry_isa = false;
        }
        suite.check("rows_carry_class_coordinates", rows_carry_isa);

        // ── key-grammar artifact rows ───────────────────────────────────────
        const auto rows = control::operator_rows();
        suite.check("operator_rows_count", rows.size() == control::control_operators().size());
        bool rows_complete = true;
        for (const auto& row : rows) {
            for (const char* field : {"word", "key", "operator", "book", "role", "xcfe_op",
                                      "phase", "emit_phase", "block_role", "arity", "sugar"}) {
                if (!row->get(field)) rows_complete = false;
            }
            if (!row->get("key") || row->get("key")->as_string().rfind("control:", 0) != 0)
                rows_complete = false;
            if (!row->get("book") || row->get("book")->as_string().rfind("ELIZA-1:", 0) != 0)
                rows_complete = false;
        }
        suite.check("operator_rows_complete", rows_complete);
        const auto plan_row = control::plan_row(plan);
        suite.check("plan_row_schema",
                    plan_row->get("schema") &&
                        plan_row->get("schema")->as_string() ==
                            "key.grammar.control_flow_plan.v1");
        suite.check("plan_row_form",
                    plan_row->get("form") && plan_row->get("form")->as_string() == "if/then/else");
        suite.check("plan_row_steps",
                    plan_row->get("steps") && plan_row->get("steps")->is_array() &&
                        plan_row->get("steps")->as_array().size() == 4);
    });
}
