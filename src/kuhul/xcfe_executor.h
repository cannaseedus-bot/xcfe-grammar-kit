#pragma once
#include "phase_engine.h"
#include "../util/json.h"
#include <string>
#include <unordered_map>
#include <functional>
#include <any>

namespace khanary::kuhul {

using XCFEContext = std::unordered_map<std::string, std::string>;

// Recursive evaluator handed to registered ops, so an op can evaluate a body
// node. That is what makes @for/@from/@as real control flow rather than names.
using EvalFn = std::function<util::JsonPtr(const util::JsonPtr& node, const XCFEContext& ctx)>;
using OpFn = std::function<util::JsonPtr(const util::JsonPtr& args, const XCFEContext& ctx,
                                         const EvalFn& eval)>;

class XCFEExecutor {
public:
    XCFEExecutor();

    // Register a named @op handler
    void register_op(const std::string& name, OpFn fn);

    // Evaluate a JSON node from a manifest array element.
    // Handles: @if/@then/@else, @for, @from, @as, @cause, @direct, @op/@fn/@args,
    // @node: references.
    // Control flow is explicit XCFE structure; lexical similarity tokens are data, not flow control.
    // A native control key dispatches to the registered op of the same name
    // (leading '@' optional) with the whole object as args, so @as/@do/@index
    // read naturally as op arguments. Precedence (first key present wins):
    // @if, @for, @from, @as, @cause, @direct.
    // Returns the result value, or null for no-op.
    util::JsonPtr eval(const util::JsonPtr& node, const XCFEContext& ctx);

    // Evaluate a string token: @-operator dispatch or raw pass-through
    util::JsonPtr eval_token(const std::string& token, const XCFEContext& ctx);

    // Phase-gate check: does executing this transition require XCFE authorization?
    // Returns true if authorized (or if it's horizontal — always authorized)
    bool authorize_phase_transition(Phase from, Phase to, const XCFEContext& ctx);

    // Evaluate a context-variable lookup expression "a.b.c"
    static std::string lookup(const std::string& path, const XCFEContext& ctx);

    // Evaluate a simple condition: "lhs op rhs" where op is ==, !=, <, >, <=, >=
    static bool eval_condition(const std::string& condition, const XCFEContext& ctx);

private:
    // @for, @from, @as, @cause, @direct — the control-flow op library.
    void register_control_ops();

    std::unordered_map<std::string, OpFn> ops_;
};

} // namespace khanary::kuhul
