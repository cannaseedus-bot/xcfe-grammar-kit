#include "xcfe_executor.h"
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace khanary::kuhul {

using namespace khanary::util;

namespace {

static std::string trim_copy(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static bool parse_bool_text(const std::string& raw, bool& out) {
    const std::string v = lower_copy(trim_copy(raw));
    if (v == "1" || v == "true" || v == "yes" || v == "on") {
        out = true;
        return true;
    }
    if (v == "0" || v == "false" || v == "no" || v == "off") {
        out = false;
        return true;
    }
    return false;
}

static bool parse_number_text(const std::string& raw, double& out) {
    const std::string v = trim_copy(raw);
    if (v.empty()) return false;
    char* end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    if (!end || *end != '\0') return false;
    out = d;
    return true;
}

// ── Control-op argument helpers ─────────────────────────────────────────────
// A native control op receives the whole control object as args, so a field may
// be spelled several ways (@for/each/collection, ...). These pick the first
// present, non-empty form.

static std::string resolve_value(const std::string& expr, const XCFEContext& ctx) {
    const std::string trimmed = trim_copy(expr);
    if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"')
        return trimmed.substr(1, trimmed.size() - 2);
    const std::string v = XCFEExecutor::lookup(trimmed, ctx);
    return v.empty() ? trimmed : trim_copy(v);
}

static std::string first_string(const JsonPtr& args,
                                std::initializer_list<const char*> keys) {
    if (!args || !args->is_object()) return {};
    for (const char* key : keys) {
        const JsonValue* v = args->get(key);
        if (!v) continue;
        if (v->is_string()) {
            if (!v->as_string().empty()) return v->as_string();
        } else if (v->is_bool() || v->is_number()) {
            return stringify(JsonPtr(new JsonValue(*v)));
        }
    }
    return {};
}

// Child *node* (object/array/string) for op bodies such as @do.
static JsonPtr child_node(const JsonPtr& args, std::initializer_list<const char*> keys) {
    if (!args || !args->is_object()) return nullptr;
    for (const char* key : keys) {
        const JsonValue* v = args->get(key);
        if (v && !v->is_null()) return JsonPtr(new JsonValue(*v));
    }
    return nullptr;
}

// Collection elements: a JSON array first, then comma/semicolon/pipe/space
// separated tokens.
static std::vector<std::string> split_collection(const std::string& raw) {
    std::vector<std::string> items;
    const std::string value = trim_copy(raw);
    if (value.empty()) return items;
    if (value.front() == '[') {
        if (auto parsed = parse(value); parsed && parsed->is_array()) {
            for (const auto& element : parsed->as_array()) {
                if (!element) continue;
                items.push_back(element->is_string() ? element->as_string()
                                                     : stringify(element));
            }
            return items;
        }
    }
    std::string current;
    for (char c : value) {
        if (c == ',' || c == ';' || c == '|' || std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                items.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) items.push_back(current);
    return items;
}

// Where a value came from: URI origin, first namespace segment, or first
// element of a delimited value.
static std::string extract_origin(const std::string& value) {
    if (value.empty()) return value;
    const size_t scheme = value.find("://");
    if (scheme != std::string::npos) {
        const size_t slash = value.find('/', scheme + 3);
        return slash == std::string::npos ? value : value.substr(0, slash);
    }
    for (char separator : {'.', '/', '\\'}) {
        const size_t pos = value.find(separator);
        if (pos != std::string::npos && pos > 0) return value.substr(0, pos);
    }
    for (char separator : {',', ';', '|'}) {
        const size_t pos = value.find(separator);
        if (pos != std::string::npos && pos > 0) return value.substr(0, pos);
    }
    return value;
}

static std::string apply_cast(const std::string& value, const std::string& cast) {
    if (cast.empty() || cast == "string") return value;
    if (cast == "upper" || cast == "lower" || cast == "key") {
        std::string out;
        out.reserve(value.size());
        for (unsigned char c : value) {
            if (cast == "upper") out.push_back(static_cast<char>(std::toupper(c)));
            else if (cast == "lower") out.push_back(static_cast<char>(std::tolower(c)));
            else out.push_back(std::isalnum(c) ? static_cast<char>(std::toupper(c)) : '_');
        }
        return out;
    }
    if (cast == "number") {
        double d = 0.0;
        return parse_number_text(value, d) ? std::to_string(d) : value;
    }
    if (cast == "bool") {
        bool b = false;
        return parse_bool_text(value, b) ? (b ? "true" : "false") : value;
    }
    return value;
}

// Collection source for @for: an explicit literal, a context binding, or —
// when the name is not bound — nothing. An unbound collection is empty rather
// than the variable's own name read as a one-element list.
static std::string collection_source(const std::string& expr, const XCFEContext& ctx) {
    const std::string text = trim_copy(expr);
    if (text.empty()) return {};
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
        return text.substr(1, text.size() - 2);
    const std::string bound = XCFEExecutor::lookup(text, ctx);
    if (!bound.empty()) return bound;
    const bool looks_literal = text.front() == '[' || text.find(',') != std::string::npos ||
                               text.find(';') != std::string::npos ||
                               text.find('|') != std::string::npos;
    return looks_literal ? text : std::string{};
}

// Method extraction for @direct: "how do i deploy" names the method "deploy".
static std::string method_from_operand(const std::string& operand) {
    static const char* const kStop[] = {"do",    "does", "did",   "can",   "could",
                                        "should", "would", "shall", "may",  "might",
                                        "must",  "i",    "you",   "we",    "they",
                                        "he",    "she",  "it",    "to",    "a",
                                        "an",    "the",  "how"};
    std::vector<std::string> tokens;
    std::string current;
    for (char c : operand) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) tokens.push_back(current);

    size_t start = 0;
    while (start < tokens.size()) {
        bool stop = false;
        for (const char* word : kStop)
            if (tokens[start] == word) stop = true;
        if (!stop) break;
        ++start;
    }
    std::string out;
    for (size_t i = start; i < tokens.size(); ++i) {
        if (!out.empty()) out.push_back(' ');
        out += tokens[i];
    }
    return out.empty() ? trim_copy(operand) : out;
}

} // namespace

XCFEExecutor::XCFEExecutor() {
    // Built-in: @op identity
    register_op("identity",
                [](const JsonPtr& args, const XCFEContext&, const EvalFn&) { return args; });
    register_control_ops();
}

// The control-flow op library. Each op returns a deterministic value and reads
// only the host-created context — XCFE never performs side effects on its own.
void XCFEExecutor::register_control_ops() {
    // @for — iterate a context collection, evaluating @do per element.
    register_op("for",
                [](const JsonPtr& args, const XCFEContext& ctx, const EvalFn& eval) -> JsonPtr {
        const std::string expr = first_string(args, {"@for", "each", "collection"});
        const auto items = split_collection(collection_source(expr, ctx));
        const std::string var = first_string(args, {"@as", "as", "item"});
        const std::string index_var = first_string(args, {"@index", "index"});
        const JsonPtr body = child_node(args, {"@do", "do", "@then"});
        JsonArray out;
        const size_t count = items.size() < 256 ? items.size() : 256;
        for (size_t i = 0; i < count; ++i) {
            if (!body) {
                out.push_back(json_string(items[i]));
                continue;
            }
            XCFEContext child = ctx;
            if (!var.empty()) child[var] = items[i];
            if (!index_var.empty()) child[index_var] = std::to_string(i);
            out.push_back(eval(body, child));
        }
        return json_array(std::move(out));
    });

    // @from — origin extraction; with @as + @do the origin is bound for the body.
    register_op("from",
                [](const JsonPtr& args, const XCFEContext& ctx, const EvalFn& eval) -> JsonPtr {
        const std::string origin =
            extract_origin(resolve_value(first_string(args, {"@from", "from", "source"}), ctx));
        const std::string alias = first_string(args, {"@as", "as"});
        const JsonPtr body = child_node(args, {"@do", "do", "@then"});
        if (!alias.empty() && body) {
            XCFEContext child = ctx;
            child[alias] = origin;
            return eval(body, child);
        }
        return json_string(origin);
    });

    // @as — alias/cast; with @value + @do the value is bound for the body.
    register_op("as",
                [](const JsonPtr& args, const XCFEContext& ctx, const EvalFn& eval) -> JsonPtr {
        const std::string name = first_string(args, {"@as", "as", "name", "role"});
        const std::string value_expr = first_string(args, {"@value", "value"});
        const std::string value =
            apply_cast(value_expr.empty() ? name : resolve_value(value_expr, ctx),
                       first_string(args, {"@cast", "cast"}));
        const JsonPtr body = child_node(args, {"@do", "do", "@then"});
        if (!name.empty() && !value_expr.empty() && body) {
            XCFEContext child = ctx;
            child[name] = value;
            return eval(body, child);
        }
        return json_string(value);
    });

    // @cause (ELIZA-1:CAUSE) — the causal link, not just the word.
    register_op("cause",
                [](const JsonPtr& args, const XCFEContext& ctx, const EvalFn&) -> JsonPtr {
        const std::string effect =
            resolve_value(first_string(args, {"@cause", "cause", "@why", "why", "effect"}), ctx);
        std::string reason =
            resolve_value(first_string(args, {"@because", "because", "reason"}), ctx);
        if (reason.empty() || reason == effect) reason = lookup("cause", ctx);
        return json_object({{"@relation", json_string("@causes")},
                            {"operator", json_string("CAUSE")},
                            {"book", json_string("ELIZA-1:CAUSE")},
                            {"phase", json_string("Yax")},
                            {"effect", json_string(effect)},
                            {"cause", json_string(reason)}});
    });

    // @direct (ELIZA-1:DIRECT) — resolve a method to the host-provided action.
    register_op("direct",
                [](const JsonPtr& args, const XCFEContext& ctx, const EvalFn&) -> JsonPtr {
        const std::string method = method_from_operand(
            resolve_value(first_string(args, {"@direct", "direct", "@how", "how", "method"}), ctx));
        const std::string action = lookup("op." + method, ctx);
        return json_object({{"operator", json_string("DIRECT")},
                            {"book", json_string("ELIZA-1:DIRECT")},
                            {"phase", json_string("Wo")},
                            {"method", json_string(method)},
                            {"resolved", json_bool(!action.empty())},
                            {"action", json_string(action)}});
    });
}

void XCFEExecutor::register_op(const std::string& name, OpFn fn) {
    ops_[name] = std::move(fn);
}

std::string XCFEExecutor::lookup(const std::string& path, const XCFEContext& ctx) {
    const std::string key = trim_copy(path);
    if (key.empty()) return "";

    auto it = ctx.find(key);
    if (it != ctx.end()) return it->second;

    // Dotted path fallback: try suffixes ("a.b.c" -> "b.c" -> "c")
    size_t dot = key.find('.');
    while (dot != std::string::npos && dot + 1 < key.size()) {
        const std::string suffix = key.substr(dot + 1);
        auto sit = ctx.find(suffix);
        if (sit != ctx.end()) return sit->second;
        dot = key.find('.', dot + 1);
    }
    return "";
}

bool XCFEExecutor::eval_condition(const std::string& condition, const XCFEContext& ctx) {
    // Explicit condition evaluation for XCFE control flow.
    // Semantic-fold similarity booleans belong to data lanes, not here.
    auto eval_side = [&](const std::string& s) -> std::string {
        const std::string trimmed = trim_copy(s);
        // If wrapped in quotes → literal
        if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"')
            return trimmed.substr(1, trimmed.size() - 2);
        std::string v = lookup(trimmed, ctx);
        return v.empty() ? trimmed : trim_copy(v);
    };

    for (const auto& op : std::vector<std::string>{"==", "!=", "<=", ">=", "<", ">"}) {
        auto pos = condition.find(op);
        if (pos != std::string::npos) {
            std::string lhs = eval_side(condition.substr(0, pos));
            std::string rhs = eval_side(condition.substr(pos + op.size()));

            double ld = 0.0;
            double rd = 0.0;
            const bool lhs_num = parse_number_text(lhs, ld);
            const bool rhs_num = parse_number_text(rhs, rd);
            if (op == "==")  return lhs == rhs;
            if (op == "!=")  return lhs != rhs;
            if (op == "<=")  return (lhs_num && rhs_num) ? (ld <= rd) : (lhs <= rhs);
            if (op == ">=")  return (lhs_num && rhs_num) ? (ld >= rd) : (lhs >= rhs);
            if (op == "<")   return (lhs_num && rhs_num) ? (ld < rd)  : (lhs < rhs);
            if (op == ">")   return (lhs_num && rhs_num) ? (ld > rd)  : (lhs > rhs);
        }
    }

    // Plain boolean literal or context variable
    std::string v = eval_side(condition);
    bool out = false;
    if (parse_bool_text(v, out)) return out;
    return false;
}

JsonPtr XCFEExecutor::eval(const JsonPtr& node, const XCFEContext& ctx) {
    if (!node) return json_null();

    if (node->is_string()) {
        return eval_token(node->as_string(), ctx);
    }

    if (node->is_object()) {
        const auto& obj = node->as_object();

        // @if / @then / @else
        auto if_it = obj.find("@if");
        if (if_it != obj.end()) {
            bool cond = false;
            if (if_it->second->is_string())
                cond = eval_condition(if_it->second->as_string(), ctx);
            else if (if_it->second->is_bool())
                cond = if_it->second->as_bool();

            const char* branch = cond ? "@then" : "@else";
            auto b_it = obj.find(branch);
            if (b_it != obj.end()) return eval(b_it->second, ctx);
            return json_null();
        }

        // Native control ops: the first key present in precedence order
        // dispatches to the registered op of the same name, with this whole
        // object as args (so @as/@do/@index read as op arguments).
        for (const char* control_key : {"@for", "@from", "@as", "@cause", "@direct"}) {
            if (obj.find(control_key) == obj.end()) continue;
            auto control_it = ops_.find(std::string(control_key).substr(1));
            if (control_it == ops_.end()) continue;
            const EvalFn eval_fn = [this](const JsonPtr& n, const XCFEContext& c) {
                return this->eval(n, c);
            };
            return control_it->second(node, ctx, eval_fn);
        }

        // @op / @fn / @args
        auto op_it = obj.find("@op");
        if (op_it == obj.end()) op_it = obj.find("@fn");
        if (op_it != obj.end()) {
            std::string name = op_it->second->is_string()
                                   ? op_it->second->as_string()
                                   : stringify(op_it->second);
            if (!name.empty() && name.front() == '@') name.erase(0, 1);
            JsonPtr args = json_null();
            auto args_it = obj.find("@args");
            if (args_it != obj.end()) args = args_it->second;

            auto fn_it = ops_.find(name);
            if (fn_it != ops_.end()) {
                const EvalFn eval_fn = [this](const JsonPtr& n, const XCFEContext& c) {
                    return this->eval(n, c);
                };
                return fn_it->second(args, ctx, eval_fn);
            }
            return json_null();
        }

        // @node:Name in object context
        auto node_it = obj.find("@node");
        if (node_it != obj.end() && node_it->second->is_string()) {
            return eval_token("@node:" + node_it->second->as_string(), ctx);
        }

        return node; // pass through unknown objects
    }

    if (node->is_array()) {
        JsonArray result;
        for (const auto& elem : node->as_array()) {
            auto r = eval(elem, ctx);
            if (r && !r->is_null()) result.push_back(r);
        }
        return json_array(std::move(result));
    }

    return node;
}

JsonPtr XCFEExecutor::eval_token(const std::string& token, const XCFEContext& ctx) {
    if (token.rfind("@node:", 0) == 0) {
        // Referenced by name — caller resolves via ManifestRegistry
        return json_string(token); // pass the ref through
    }
    if (token.rfind("@op:", 0) == 0) {
        std::string name = token.substr(4);
        if (!name.empty() && name.front() == '@') name.erase(0, 1);
        auto it = ops_.find(name);
        if (it == ops_.end()) return json_null();
        const EvalFn eval_fn = [this](const JsonPtr& n, const XCFEContext& c) {
            return this->eval(n, c);
        };
        return it->second(json_null(), ctx, eval_fn);
    }
    // Other @ tokens — pass through as operative strings
    return json_string(token);
}

bool XCFEExecutor::authorize_phase_transition(Phase from, Phase to, const XCFEContext& ctx) {
    if (PhaseEngine::is_horizontal(from, to)) return true; // always authorized

    // Explicit semantic-fold mode: similarity lanes cannot authorize control transitions.
    if (auto it = ctx.find("semantic_folding_only"); it != ctx.end()) {
        bool b = false;
        if (parse_bool_text(it->second, b) && b) return false;
    }
    if (auto it = ctx.find("xcfe.mode"); it != ctx.end()) {
        const std::string mode = lower_copy(trim_copy(it->second));
        if (mode == "similarity_only" || mode == "semantic_folding_only") return false;
    }

    // Explicit policy toggles.
    if (auto it = ctx.find("xcfe.block_vertical"); it != ctx.end()) {
        bool b = false;
        if (parse_bool_text(it->second, b) && b) return false;
    }
    if (auto it = ctx.find("xcfe.allow_vertical"); it != ctx.end()) {
        bool b = false;
        if (parse_bool_text(it->second, b)) return b;
    }

    // Default policy: allow vertical folds unless explicitly denied.
    return true;
}

} // namespace khanary::kuhul
