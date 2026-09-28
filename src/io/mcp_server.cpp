#include "mcp_server.h"
#include "code_parser.h"
#include "../kuhul/xcfe_executor.h"
#include "../micronaut/relation_isa.h"
#include "../gpu/svg_compute_runtime.h"
#include "../util/json.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace khanary::io {

using namespace khanary::util;

namespace {

// XCFE evaluation authority is host-owned (docs/CONFORMANCE.md): a caller
// context may carry data, but never authority. These keys are dropped rather
// than trusted, so an RPC payload can never widen a transition gate or switch
// the fold mode.
bool xcfe_authority_key(const std::string& key) {
    if (key.rfind("xcfe", 0) == 0) return true;  // xcfe.mode, xcfe_authorized, ...
    return key == "semantic_folding_only";
}

struct LocalClusteringMetrics {
    size_t degree = 0;
    size_t triangle_count = 0;
    size_t triple_count = 0;
    double local_clustering = 0.0;
    double local_clustering_directed = 0.0;
};

std::string rg_float6(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6f", v);
    return std::string(buf);
}

std::unordered_map<std::string, LocalClusteringMetrics>
compute_local_clustering_metrics(const micronaut::MicronautRegistry& mu_reg) {
    using NeighborSet = std::unordered_set<std::string>;
    std::unordered_map<std::string, NeighborSet> neighbors_undirected;
    std::unordered_map<std::string, NeighborSet> neighbors_directed_out;
    std::unordered_map<std::string, LocalClusteringMetrics> metrics;

    const auto ids = mu_reg.ids();
    for (const auto& id : ids) {
        neighbors_undirected[id];
        neighbors_directed_out[id];
    }

    auto add_edge = [&](const std::string& from,
                        const std::string& to,
                        bool directed) {
        if (from.empty() || to.empty() || from == to) return;
        neighbors_undirected[from].insert(to);
        neighbors_undirected[to].insert(from);
        neighbors_directed_out[from].insert(to);
        if (!directed) neighbors_directed_out[to].insert(from);
    };

    for (const auto& id : ids) {
        const auto* e = mu_reg.get(id);
        if (!e) continue;

        if (!e->relations.empty()) {
            for (const auto& rel : e->relations) {
                if (!mu_reg.has(rel.target_id)) continue;
                add_edge(id, rel.target_id, rel.directed);
            }
        } else {
            // Legacy fallback: plain sub-micronaut delegation.
            for (const auto& sm : e->sub_micronauts) {
                if (!mu_reg.has(sm)) continue;
                add_edge(id, sm, true);
            }
        }
    }

    auto has_undirected = [&](const std::string& a, const std::string& b) {
        auto it = neighbors_undirected.find(a);
        if (it == neighbors_undirected.end()) return false;
        return it->second.count(b) > 0;
    };
    auto has_directed = [&](const std::string& a, const std::string& b) {
        auto it = neighbors_directed_out.find(a);
        if (it == neighbors_directed_out.end()) return false;
        return it->second.count(b) > 0;
    };

    for (const auto& id : ids) {
        LocalClusteringMetrics m;
        const auto& nbrs = neighbors_undirected[id];
        m.degree = nbrs.size();

        if (m.degree >= 2) {
            const std::vector<std::string> nv(nbrs.begin(), nbrs.end());
            size_t realized_undirected = 0;
            size_t realized_directed = 0;
            for (size_t i = 0; i < nv.size(); ++i) {
                for (size_t j = i + 1; j < nv.size(); ++j) {
                    const auto& a = nv[i];
                    const auto& b = nv[j];
                    if (has_undirected(a, b)) ++realized_undirected;
                    if (has_directed(a, b)) ++realized_directed;
                    if (has_directed(b, a)) ++realized_directed;
                }
            }
            m.triangle_count = realized_undirected;
            m.triple_count = (m.degree * (m.degree - 1)) / 2;
            if (m.triple_count > 0)
                m.local_clustering = static_cast<double>(realized_undirected) /
                                     static_cast<double>(m.triple_count);
            const size_t possible_directed = m.degree * (m.degree - 1);
            if (possible_directed > 0)
                m.local_clustering_directed = static_cast<double>(realized_directed) /
                                              static_cast<double>(possible_directed);
        }

        metrics[id] = m;
    }

    return metrics;
}

struct IncidenceColumn {
    std::string id;
    std::string kind;    // relation | legacy_delegate | phase_fold
    std::string source;  // relation source (if pair-edge)
    std::string target;  // relation target (if pair-edge)
    std::string verb;    // original label
    std::string relation_type;   // canonical relation token
    std::string relation_family; // semantic|causal|epistemic|dependency|context|control|runtime|unknown
    std::string phase;   // phase_fold only
    bool directed = true;
    double weight = 1.0; // pattern-strength channel W_E
    std::vector<size_t> members; // participating node indices
};

struct IncidenceModel {
    std::vector<std::string> node_ids;
    std::unordered_map<std::string, size_t> node_index;
    std::vector<IncidenceColumn> columns;
};

IncidenceModel build_incidence_model(
    const micronaut::MicronautRegistry& mu_reg,
    bool include_phase_folds)
{
    IncidenceModel model;
    model.node_ids = mu_reg.ids();
    std::sort(model.node_ids.begin(), model.node_ids.end());
    for (size_t i = 0; i < model.node_ids.size(); ++i)
        model.node_index[model.node_ids[i]] = i;

    std::array<std::vector<size_t>, 6> phase_members;

    for (const auto& src : model.node_ids) {
        const auto* e = mu_reg.get(src);
        if (!e) continue;

        phase_members[static_cast<size_t>(e->primary_phase)].push_back(model.node_index[src]);

        if (!e->relations.empty()) {
            for (const auto& rel : e->relations) {
                auto it_t = model.node_index.find(rel.target_id);
                if (it_t == model.node_index.end()) continue;
                if (src == rel.target_id) continue;

                IncidenceColumn col;
                col.id = "e" + std::to_string(model.columns.size());
                col.kind = "relation";
                col.source = src;
                col.target = rel.target_id;
                col.verb = rel.verb;
                col.relation_type = micronaut::canonical_relation(rel.verb);
                col.relation_family = micronaut::relation_family_name(
                    micronaut::relation_family_of(rel.verb));
                col.directed = rel.directed;
                col.weight = 1.0;
                col.members.push_back(model.node_index[src]);
                col.members.push_back(it_t->second);
                model.columns.push_back(std::move(col));
            }
        } else {
            for (const auto& sm : e->sub_micronauts) {
                auto it_t = model.node_index.find(sm);
                if (it_t == model.node_index.end()) continue;
                if (src == sm) continue;

                IncidenceColumn col;
                col.id = "e" + std::to_string(model.columns.size());
                col.kind = "legacy_delegate";
                col.source = src;
                col.target = sm;
                col.verb = "@delegates_to";
                col.relation_type = micronaut::canonical_relation(col.verb);
                col.relation_family = micronaut::relation_family_name(
                    micronaut::relation_family_of(col.verb));
                col.directed = true;
                col.weight = 1.0;
                col.members.push_back(model.node_index[src]);
                col.members.push_back(it_t->second);
                model.columns.push_back(std::move(col));
            }
        }
    }

    if (include_phase_folds) {
        for (size_t pi = 0; pi < phase_members.size(); ++pi) {
            if (phase_members[pi].empty()) continue;
            const auto phase = static_cast<kuhul::Phase>(pi);
            IncidenceColumn col;
            col.id = std::string("fold_phase_") + kuhul::phase_name(phase);
            col.kind = "phase_fold";
            col.phase = kuhul::phase_name(phase);
            col.target = std::string("@phase:") + col.phase;
            col.verb = "@phase_fold";
            col.relation_type = "@phase_fold";
            col.relation_family = "phase";
            col.directed = false;
            col.weight = 1.0;
            col.members = phase_members[pi];
            model.columns.push_back(std::move(col));
        }
    }

    return model;
}

using IMatrix = std::vector<std::vector<int>>;
using DMatrix = std::vector<std::vector<double>>;

DMatrix compute_b_w_bt(const IMatrix& b, const std::vector<double>* weights = nullptr) {
    const size_t n = b.size();
    const size_t m = (n == 0) ? 0 : b[0].size();
    DMatrix out(n, std::vector<double>(n, 0.0));
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            double sum = 0.0;
            for (size_t k = 0; k < m; ++k) {
                const double wk = weights ? (*weights)[k] : 1.0;
                sum += static_cast<double>(b[i][k]) * wk * static_cast<double>(b[j][k]);
            }
            out[i][j] = sum;
        }
    }
    return out;
}

DMatrix compute_b_t_b(const IMatrix& b) {
    const size_t n = b.size();
    const size_t m = (n == 0) ? 0 : b[0].size();
    DMatrix out(m, std::vector<double>(m, 0.0));
    for (size_t i = 0; i < m; ++i) {
        for (size_t j = 0; j < m; ++j) {
            double sum = 0.0;
            for (size_t r = 0; r < n; ++r)
                sum += static_cast<double>(b[r][i]) * static_cast<double>(b[r][j]);
            out[i][j] = sum;
        }
    }
    return out;
}

JsonPtr json_int_matrix(const IMatrix& mat) {
    JsonArray rows;
    for (const auto& r : mat) {
        JsonArray row;
        for (int v : r) row.push_back(json_number(static_cast<double>(v)));
        rows.push_back(json_array(std::move(row)));
    }
    return json_array(std::move(rows));
}

JsonPtr json_double_matrix(const DMatrix& mat) {
    JsonArray rows;
    for (const auto& r : mat) {
        JsonArray row;
        for (double v : r) row.push_back(json_number(v));
        rows.push_back(json_array(std::move(row)));
    }
    return json_array(std::move(rows));
}

} // namespace

MCPServer::MCPServer(
    uint16_t port,
    manifest::ManifestRegistry& reg,
    kuhul::KuhulVM& vm,
    micronaut::MicronautRegistry& mu_reg,
    micronaut::Factory& factory,
    micronaut::SandboxInspector& sandbox,
    memory::ConfidenceStore& confidence,
    world::WorldState& world,
    JROM& jrom,
    gpu::SvgComputeRuntime* svg_runtime)
    : rpc_(port), manifest_(reg), vm_(vm), mu_reg_(mu_reg),
      factory_(factory), sandbox_(sandbox), confidence_(confidence),
      world_(world), jrom_(jrom), svg_runtime_(svg_runtime)
{
    register_tools();
}

bool MCPServer::start()    { return rpc_.start(); }
void MCPServer::stop()     { rpc_.stop(); }
bool MCPServer::is_running() const { return rpc_.is_running(); }

// Auto-log every tool call: tool:log:{name}, tool:log:last, tool:count:{name}.
// handler is already a full call expression e.g. tool_kuhul_forge(p); evaluated as-is.
#define REG_TOOL(name, handler) \
    rpc_.register_method(name, [this](const JsonPtr& p) -> JsonPtr { \
        auto result = (handler); \
        auto& gs = vm_.gram_store(); \
        gs.gramWrite("tool:log:" name, name, "Xul", "tool_call_log", "TOOL-\xc2\xb5"); \
        gs.gramWrite("tool:log:last",  name, "Xul", "tool_call_log", "TOOL-\xc2\xb5"); \
        auto prev = gs.gramRead("tool:count:" name); \
        int cnt = (prev && !prev->value.empty()) ? std::stoi(prev->value) : 0; \
        gs.gramWrite("tool:count:" name, std::to_string(cnt + 1), "Xul", "tool_call_log", "TOOL-\xc2\xb5"); \
        return result; \
    })

void MCPServer::register_tools() {
    REG_TOOL("kuhul_forge",        tool_kuhul_forge(p));
    REG_TOOL("manifest_resolve",   tool_manifest_resolve(p));
    REG_TOOL("fold_execute",       tool_fold_execute(p));
    REG_TOOL("micronaut_run",      tool_micronaut_run(p));
    REG_TOOL("grammar_check",      tool_grammar_check(p));
    REG_TOOL("phase_query",        tool_phase_query(p));
    REG_TOOL("gram_read",          tool_gram_read(p));
    REG_TOOL("gram_write",         tool_gram_write(p));
    REG_TOOL("confidence_get",     tool_confidence_get(p));
    REG_TOOL("jrom_replay",        tool_jrom_replay(p));
    REG_TOOL("sandbox_query",      tool_sandbox_query(p));
    REG_TOOL("sandbox_list",       tool_sandbox_list(p));
    REG_TOOL("sandbox_violations", tool_sandbox_violations(p));
    REG_TOOL("world_state",        tool_world_state(p));
    REG_TOOL("field_dag",          tool_field_dag(p));
    REG_TOOL("field_dag_svg",      tool_field_dag_svg(p));
    REG_TOOL("relational_graph",   tool_relational_graph(p));
    REG_TOOL("incidence_matrix",   tool_incidence_matrix(p));
    REG_TOOL("semantic_fold",      tool_semantic_fold(p));
    REG_TOOL("code_parse",         tool_code_parse(p));
    REG_TOOL("xcfe_eval",          tool_xcfe_eval(p));
    REG_TOOL("memory_save",        tool_memory_save(p));
    REG_TOOL("memory_recall",      tool_memory_recall(p));
    REG_TOOL("tool_card",          tool_tool_card(p));
}

#undef REG_TOOL

JsonPtr MCPServer::tool_kuhul_forge(const JsonPtr& params) {
    if (!params || !params->is_object()) return json_string("error: params required");
    auto prog = params->get_string("program");
    if (!prog) return json_string("error: program required");
    kuhul::XCFEContext ctx;
    auto result = vm_.run(*prog, ctx);
    JsonObject obj;
    obj["ok"]    = json_bool(result.ok);
    obj["error"] = json_string(result.error);
    return json_object(std::move(obj));
}

JsonPtr MCPServer::tool_manifest_resolve(const JsonPtr& params) {
    if (!params || !params->is_object()) return json_null();
    auto key = params->get_string("key");
    if (!key) return json_null();
    auto expanded = manifest_.resolve(*key);
    JsonArray arr;
    for (const auto& s : expanded) arr.push_back(json_string(s));
    return json_array(std::move(arr));
}

JsonPtr MCPServer::tool_fold_execute(const JsonPtr& params) {
    if (!params || !params->is_object()) return json_null();
    auto prog = params->get_string("program");
    if (!prog) return json_null();
    auto result = vm_.run(*prog);
    JsonObject obj;
    obj["ok"] = json_bool(result.ok);
    obj["error"] = json_string(result.error);
    return json_object(std::move(obj));
}

// Evaluate an XCFE control node (`@if`, `@for`, `@from`, `@as`, `@cause`,
// `@direct`, `@op`) against a caller-supplied, data-only context. This is the
// surface that makes the key grammar's control flow reachable from a tool call
// rather than only from C++.
JsonPtr MCPServer::tool_xcfe_eval(const JsonPtr& params) {
    if (!params || !params->is_object()) return json_string("error: params required");
    const JsonValue* node = params->get("node");
    if (!node) return json_string("error: node required");

    kuhul::XCFEContext ctx;
    size_t dropped = 0;
    if (const JsonValue* context = params->get("context"); context && context->is_object()) {
        for (const auto& [key, value] : context->as_object()) {
            if (xcfe_authority_key(key)) {
                ++dropped;
                continue;
            }
            if (!value) continue;
            ctx.emplace(key, value->is_string() ? value->as_string() : stringify(value));
        }
    }

    kuhul::XCFEExecutor exec;  // constructor registers the full op library
    JsonObject obj;
    obj["ok"] = json_bool(true);
    obj["result"] = exec.eval(JsonPtr(new JsonValue(*node)), ctx);
    obj["context_keys"] = json_number(static_cast<double>(ctx.size()));
    obj["authority_keys_dropped"] = json_number(static_cast<double>(dropped));
    return json_object(std::move(obj));
}

JsonPtr MCPServer::tool_micronaut_run(const JsonPtr& params) {
    if (!params || !params->is_object()) return json_null();
    auto id = params->get_string("id");
    if (!id) return json_null();
    const auto* e = mu_reg_.get(*id);
    if (!e) return json_string("not found");
    JsonObject obj;
    obj["id"]    = json_string(e->id);
    obj["phase"] = json_string(kuhul::phase_name(e->primary_phase));
    obj["active"]= json_bool(e->active);
    JsonArray caps;
    for (const auto& c : e->capabilities) caps.push_back(json_string(c));
    obj["capabilities"] = json_array(std::move(caps));
    return json_object(std::move(obj));
}

JsonPtr MCPServer::tool_grammar_check(const JsonPtr& params) {
    if (!params || !params->is_object()) return json_null();
    auto prog = params->get_string("program");
    if (!prog) return json_null();
    auto instructions = kuhul::KuhulVM::parse_program(*prog);
    JsonObject obj;
    obj["valid"]     = json_bool(!instructions.empty());
    obj["instr_count"] = json_number((double)instructions.size());
    return json_object(std::move(obj));
}

JsonPtr MCPServer::tool_phase_query(const JsonPtr& params) {
    if (!params || !params->is_object()) return json_null();
    auto phase_s = params->get_string("phase");
    if (!phase_s) return json_null();
    kuhul::Phase p;
    if (!kuhul::phase_from_name(*phase_s, p)) return json_string("unknown phase");
    JsonObject obj;
    obj["name"]   = json_string(kuhul::phase_name(p));
    obj["angle"]  = json_number(kuhul::phase_angle(p));
    obj["next"]   = json_string(kuhul::phase_name(kuhul::next_phase(p)));
    return json_object(std::move(obj));
}

JsonPtr MCPServer::tool_gram_read(const JsonPtr& params) {
    if (!params || !params->is_object()) return json_null();
    auto key = params->get_string("key");
    if (!key) return json_null();
    auto g = vm_.gram_store().gramRead(*key);
    if (!g) return json_null();
    JsonObject obj;
    obj["key"]   = json_string(g->key);
    obj["value"] = json_string(g->value);
    obj["phase"] = json_string(g->phase);
    return json_object(std::move(obj));
}

JsonPtr MCPServer::tool_gram_write(const JsonPtr& params) {
    if (!params || !params->is_object()) return json_null();
    auto key = params->get_string("key");
    auto val = params->get_string("value");
    if (!key || !val) return json_bool(false);
    vm_.gram_store().gramWrite(*key, *val, "Xul");
    jrom_.log("gramWrite", "{\"key\":\"" + *key + "\"}");
    return json_bool(true);
}

// ── MEMORY-µ ──────────────────────────────────────────────────────────────
//
// Gram key schema:  mem:{category}:{key}
// Categories:       preference | session | micronaut_data | note
// UI card @kind:    memory_card.v1
//
// Denied key prefixes (sandbox): xcfe. fold. ngram: ctrl:
// Allowed prefix:   mem:

namespace {

bool memory_key_denied(const std::string& key) {
    static const std::vector<std::string> deny = {"xcfe.", "fold.", "ngram:", "ctrl:"};
    for (const auto& d : deny)
        if (key.size() >= d.size() && key.substr(0, d.size()) == d) return true;
    return false;
}

std::string memory_channel(const std::string& category) {
    if (category == "preference")    return "user_preference";
    if (category == "session")       return "session_context";
    if (category == "micronaut_data")return "micronaut_data";
    return "session_context";
}

JsonObject memory_card_from_gram(const kuhul::Gram& g) {
    // Parse "mem:{category}:{key}"
    std::string category, user_key;
    std::string s = g.key.size() > 4 ? g.key.substr(4) : g.key; // strip "mem:"
    auto sep = s.find(':');
    if (sep != std::string::npos) {
        category = s.substr(0, sep);
        user_key = s.substr(sep + 1);
    } else {
        user_key = s;
    }
    JsonObject card;
    card["@kind"]         = json_string("memory_card.v1");
    card["id"]            = json_string(g.key);
    card["category"]      = json_string(category);
    card["key"]           = json_string(user_key);
    card["value"]         = json_string(g.value);
    card["phase_written"] = json_string(g.phase);
    card["channel"]       = json_string(g.channel);
    return card;
}

} // namespace

JsonPtr MCPServer::tool_memory_save(const JsonPtr& params) {
    if (!params || !params->is_object()) return json_null();
    auto category_p = params->get_string("category");
    auto key_p      = params->get_string("key");
    auto value_p    = params->get_string("value");
    if (!category_p || !key_p || !value_p)
        return json_string("error: category, key, and value required");

    const std::string gram_key = "mem:" + *category_p + ":" + *key_p;
    if (memory_key_denied(gram_key))
        return json_string("error: key prefix denied by MEMORY-µ sandbox");

    const std::string channel = memory_channel(*category_p);
    vm_.gram_store().gramWrite(gram_key, *value_p, "Xul", channel, "MEMORY-µ");
    jrom_.log("memory_save",
        "{\"key\":\"" + gram_key + "\",\"category\":\"" + *category_p + "\"}");

    JsonObject card = memory_card_from_gram(
        kuhul::Gram{gram_key, *value_p, 0, "Xul", channel, "MEMORY-µ"});
    if (auto t = params->get_string("card_title")) card["card_title"] = json_string(*t);
    if (auto h = params->get_string("card_hint"))  card["card_hint"]  = json_string(*h);
    card["saved"] = json_bool(true);
    return json_object(std::move(card));
}

JsonPtr MCPServer::tool_memory_recall(const JsonPtr& params) {
    if (!params || !params->is_object()) return json_null();
    auto key_p      = params->get_string("key");
    auto category_p = params->get_string("category");

    const auto& gs = vm_.gram_store();
    JsonArray cards;

    if (key_p) {
        // Exact lookup: try given category or all categories
        const std::vector<std::string> cats = category_p
            ? std::vector<std::string>{*category_p}
            : std::vector<std::string>{"preference","session","micronaut_data","note"};
        for (const auto& cat : cats) {
            if (auto g = gs.gramRead("mem:" + cat + ":" + *key_p)) {
                cards.push_back(json_object(memory_card_from_gram(*g)));
                break;
            }
        }
    } else {
        // List: all mem: keys, optionally filtered by category
        const std::string prefix = category_p
            ? "mem:" + *category_p + ":"
            : "mem:";
        for (const auto& k : gs.keys()) {
            if (k.substr(0, prefix.size()) != prefix) continue;
            if (auto g = gs.gramRead(k))
                cards.push_back(json_object(memory_card_from_gram(*g)));
        }
    }

    JsonObject result;
    result["@kind"] = json_string("memory_recall.v1");
    result["count"] = json_number(static_cast<double>(cards.size()));
    result["cards"] = json_array(std::move(cards));
    return json_object(std::move(result));
}

// ── TOOL-µ ────────────────────────────────────────────────────────────────

JsonPtr MCPServer::tool_tool_card(const JsonPtr& params) {
    if (!params || !params->is_object()) return json_null();
    auto name_p = params->get_string("tool_name");
    if (!name_p) return json_string("error: tool_name required");
    const std::string& name = *name_p;

    // Static manifest — mirrors TOOL-u.semantic-tracks.v1.json#tool_manifest
    struct ToolMeta { const char* desc; const char* param_list; const char* ret; const char* micronaut; const char* phase; };
    static const std::unordered_map<std::string, ToolMeta> manifest = {
        {"kuhul_forge",        {"Run a K'UHUL XCFE program",                      "program",                         "{ok,error}",          "KUHUL",       "Xul"  }},
        {"manifest_resolve",   {"Resolve manifest key to values",                  "key",                             "[string]",            "Coordinator", "Pop"  }},
        {"fold_execute",       {"Execute a fold program",                           "program",                         "{ok,error}",          "KUHUL",       "Xul"  }},
        {"micronaut_run",      {"Run a named micronaut",                            "id,input",                        "string",              "Coordinator", "Pop"  }},
        {"grammar_check",      {"Check grammar conformance",                        "text",                            "{valid,errors}",      "CONTROL-µ",   "Ch'en"}},
        {"phase_query",        {"Query phase geometry",                             "phase",                           "{name,angle,next}",   "KUHUL",       "Pop"  }},
        {"gram_read",          {"Read a gram by key",                               "key",                             "{key,value,phase}",   "Mx2LM",       "Pop"  }},
        {"gram_write",         {"Write a gram by key",                              "key,value",                       "bool",                "Mx2LM",       "Xul"  }},
        {"confidence_get",     {"Get confidence score for an id",                   "id",                              "{id,score}",          "KNOWLED-1",   "Sek"  }},
        {"jrom_replay",        {"Replay JROM log",                                  "file",                            "{replayed}",          "Coordinator", "Pop"  }},
        {"sandbox_query",      {"Query sandbox state",                              "id",                              "object",              "CONTROL-µ",   "Ch'en"}},
        {"sandbox_list",       {"List sandbox entries",                             "",                                "[id]",                "CONTROL-µ",   "Pop"  }},
        {"sandbox_violations", {"List sandbox violations",                          "",                                "[violation]",         "CONTROL-µ",   "Ch'en"}},
        {"world_state",        {"Read K-CUBE world state",                          "",                                "object",              "Coordinator", "Pop"  }},
        {"field_dag",          {"Semantic field DAG as XML",                        "gml?",                            "xml",                 "Coordinator", "Sek"  }},
        {"field_dag_svg",      {"Field DAG + SVG tensor render",                    "gml?",                            "svg",                 "Coordinator", "Sek"  }},
        {"relational_graph",   {"Relational micronaut graph",                       "",                                "xml",                 "Mx2LM",       "Sek"  }},
        {"incidence_matrix",   {"Incidence algebra B/W_E surface",                  "head?,tau?",                      "object",              "Mx2LM",       "Sek"  }},
        {"semantic_fold",      {"Sparse fold fingerprints",                         "head,tau?",                       "object",              "KUHUL",       "Yax"  }},
        {"code_parse",         {"Grammar-driven code parser graph",                 "grammar_file,source",             "object",              "Coordinator", "Pop"  }},
        {"xcfe_eval",          {"Evaluate XCFE control node",                       "node,context?",                   "{ok,result}",         "CONTROL-µ",   "Xul"  }},
        {"memory_save",        {"Save user preference / session / micronaut data",  "category,key,value,card_title?,card_hint?", "memory_card.v1", "MEMORY-µ", "Xul"}},
        {"memory_recall",      {"Recall memory cards by key or category",           "key?,category?",                  "memory_recall.v1",    "MEMORY-µ",    "Pop"  }},
        {"tool_card",          {"Get display card for a registered tool",           "tool_name",                       "tool_card.v1",        "TOOL-µ",      "Pop"  }},
    };

    auto it = manifest.find(name);
    const auto& gs = vm_.gram_store();

    JsonObject card;
    card["@kind"]     = json_string("tool_card.v1");
    card["tool_name"] = json_string(name);

    if (it != manifest.end()) {
        const auto& m = it->second;
        card["description"] = json_string(m.desc);
        card["params"]      = json_string(m.param_list);
        card["returns"]     = json_string(m.ret);
        card["micronaut"]   = json_string(m.micronaut);
        card["phase"]       = json_string(m.phase);
        card["found"]       = json_bool(true);
    } else {
        card["found"]       = json_bool(false);
        card["description"] = json_string("unknown tool");
    }

    // Attach call log from gram store
    if (auto cnt = gs.gramRead("tool:count:" + name))
        card["call_count"] = json_string(cnt->value);
    else
        card["call_count"] = json_string("0");

    if (auto last = gs.gramRead("tool:log:" + name))
        card["last_called"] = json_string(last->phase);

    return json_object(std::move(card));
}

JsonPtr MCPServer::tool_confidence_get(const JsonPtr& params) {
    if (!params || !params->is_object()) return json_null();
    auto id = params->get_string("id");
    if (!id) return json_null();
    return json_number(confidence_.get(*id));
}

JsonPtr MCPServer::tool_jrom_replay(const JsonPtr& params) {
    uint64_t from_seq = 0;
    if (params && params->is_object()) {
        if (auto n = params->get_number("from_seq")) from_seq = (uint64_t)*n;
    }
    JsonArray events;
    jrom_.replay(from_seq, [&](const JROMEvent& e) {
        JsonObject obj;
        obj["seq"]  = json_number((double)e.seq);
        obj["type"] = json_string(e.type);
        events.push_back(json_object(std::move(obj)));
    });
    return json_array(std::move(events));
}

// ── Sandbox inspection tools ──────────────────────────────────────────────────

JsonPtr MCPServer::tool_sandbox_query(const JsonPtr& params) {
    if (!params || !params->is_object()) return json_null();
    auto id = params->get_string("id");
    if (!id) return json_string("error: id required");
    auto result = sandbox_.sandbox_json(*id);
    if (!result || result->is_null()) return json_string("not found");
    return result;
}

JsonPtr MCPServer::tool_sandbox_list(const JsonPtr& /*params*/) {
    return sandbox_.all_json();
}

JsonPtr MCPServer::tool_sandbox_violations(const JsonPtr& params) {
    size_t n = 0;
    if (params && params->is_object())
        if (auto num = params->get_number("last_n")) n = (size_t)*num;
    return sandbox_.violations_json(n);
}

// ── K-CUBE world state ──────────────────────────────────────────────────────────

JsonPtr MCPServer::tool_world_state(const JsonPtr& /*params*/) {
    world_.rebuild();
    return world_.to_json();
}

void MCPServer::add_gml(const std::string& path) {
    gml_paths_.push_back(path);
}

JsonPtr MCPServer::tool_field_dag(const JsonPtr& /*params*/) {
    world_.rebuild();
    world::FieldDag dag(world_, mu_reg_);
    dag.build();
    for (const auto& gp : gml_paths_) dag.load_gml(gp);
    return json_string(dag.to_xml());
}

JsonPtr MCPServer::tool_field_dag_svg(const JsonPtr& /*params*/) {
    world_.rebuild();
    world::FieldDag dag(world_, mu_reg_);
    dag.build();
    for (const auto& gp : gml_paths_) dag.load_gml(gp);
    world::FieldSvgProjection projection;
    if (svg_runtime_ && svg_runtime_->project(dag.nodes(), dag.edges(), projection))
        return json_string(dag.to_svg(&projection));
    return json_string(dag.to_svg(nullptr));
}

static std::string rg_xml_esc(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if      (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else               out += c;
    }
    return out;
}

JsonPtr MCPServer::tool_relational_graph(const JsonPtr& /*params*/) {
    const auto metrics = compute_local_clustering_metrics(mu_reg_);

    std::string xml;
    xml.reserve(4096);
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml += "<graph xmlns:k=\"https://kuhul.io/relational\" edgedefault=\"directed\">\n";

    // XCFE phase anchor nodes — @phase: typed, one per C6 face
    static constexpr const char* PHASE_RAD_STR[6] =
        { "0.000", "1.047", "2.094", "3.142", "4.189", "5.236" };
    for (const auto& face : world::KCUBE_FACES) {
        kuhul::Phase next_p = kuhul::next_phase(face.phase);
        xml += "  <node id=\"@phase:";
        xml += rg_xml_esc(std::string(kuhul::phase_name(face.phase)));
        xml += "\" type=\"xcfe_phase\" face=\"";
        xml += rg_xml_esc(std::string(face.geometry));
        xml += "\" angle_deg=\"";
        xml += std::to_string(static_cast<int>(face.angle_deg));
        xml += "\" angle_rad=\"";
        xml += PHASE_RAD_STR[static_cast<int>(face.phase)];
        xml += "\" next=\"@phase:";
        xml += rg_xml_esc(std::string(kuhul::phase_name(next_p)));
        xml += "\"/>\n";
    }

    for (const auto& id : mu_reg_.ids()) {
        const auto* e = mu_reg_.get(id);
        if (!e) continue;
        std::string phase_s(kuhul::phase_name(e->primary_phase));

        xml += "  <node id=\"@node:";
        xml += rg_xml_esc(id);
        xml += "\" category=\"";
        xml += rg_xml_esc(e->category);
        xml += "\" phase=\"";
        xml += rg_xml_esc(phase_s);
        xml += "\"";
        if (const auto it = metrics.find(id); it != metrics.end()) {
            xml += " degree=\"";
            xml += std::to_string(it->second.degree);
            xml += "\" local_clustering=\"";
            xml += rg_float6(it->second.local_clustering);
            xml += "\" local_clustering_directed=\"";
            xml += rg_float6(it->second.local_clustering_directed);
            xml += "\" triangles=\"";
            xml += std::to_string(it->second.triangle_count);
            xml += "\" triples=\"";
            xml += std::to_string(it->second.triple_count);
            xml += "\"";
        }

        // Coordinator with sub-micronauts: emit nested graph
        if (!e->sub_micronauts.empty() && e->sandbox.coordinator) {
            xml += ">\n    <graph>\n";
            for (const auto& sm : e->sub_micronauts) {
                xml += "      <node id=\"@node:";
                xml += rg_xml_esc(sm);
                xml += "\"/>\n";
            }
            xml += "    </graph>\n  </node>\n";
        } else {
            xml += "/>\n";
        }

        // Typed relational verb edges
        for (const auto& rel : e->relations) {
            const auto rel_type = micronaut::canonical_relation(rel.verb);
            const auto rel_family = micronaut::relation_family_name(
                micronaut::relation_family_of(rel.verb));
            xml += "  <edge source=\"@node:";
            xml += rg_xml_esc(id);
            xml += "\" target=\"@node:";
            xml += rg_xml_esc(rel.target_id);
            xml += "\" verb=\"";
            xml += rg_xml_esc(rel.verb);
            xml += "\" relation_type=\"";
            xml += rg_xml_esc(rel_type);
            xml += "\" relation_family=\"";
            xml += rg_xml_esc(rel_family);
            xml += "\"";
            if (!rel.directed) xml += " directed=\"false\"";
            xml += "/>\n";
        }
        // Legacy sub_micronaut fallback (canonicalized to @delegates_to)
        if (e->relations.empty()) {
            for (const auto& sm : e->sub_micronauts) {
                xml += "  <edge source=\"@node:";
                xml += rg_xml_esc(id);
                xml += "\" target=\"@node:";
                xml += rg_xml_esc(sm);
                xml += "\" verb=\"@delegates_to\" relation_type=\"@delegates_to\" relation_family=\"runtime\"/>\n";
            }
        }
        // Phase anchor edge: @node:X → @phase:P
        xml += "  <edge source=\"@node:";
        xml += rg_xml_esc(id);
        xml += "\" target=\"@phase:";
        xml += rg_xml_esc(phase_s);
        xml += "\" verb=\"@phase\" arc_weight=\"1.000\"/>\n";
    }

    xml += "</graph>\n";
    return json_string(xml);
}

JsonPtr MCPServer::tool_incidence_matrix(const JsonPtr& params) {
    bool include_phase_folds = true;
    if (params && params->is_object()) {
        if (auto b = params->get_bool("include_phase_folds"))
            include_phase_folds = *b;
    }

    const auto model = build_incidence_model(mu_reg_, include_phase_folds);
    const size_t n = model.node_ids.size();
    const size_t m = model.columns.size();

    IMatrix b_binary(n, std::vector<int>(m, 0));
    IMatrix b_oriented(n, std::vector<int>(m, 0));
    std::vector<double> w_e(m, 1.0);

    for (size_t ci = 0; ci < model.columns.size(); ++ci) {
        const auto& c = model.columns[ci];
        w_e[ci] = std::clamp(c.weight, 0.0, 1.0);

        for (size_t ni : c.members)
            if (ni < n) b_binary[ni][ci] = 1;

        if (c.kind == "phase_fold") {
            for (size_t ni : c.members)
                if (ni < n) b_oriented[ni][ci] = 1;
            continue;
        }

        if (c.directed) {
            auto it_s = model.node_index.find(c.source);
            auto it_t = model.node_index.find(c.target);
            if (it_s != model.node_index.end() && it_t != model.node_index.end()) {
                b_oriented[it_s->second][ci] = -1;
                b_oriented[it_t->second][ci] = 1;
            }
        } else {
            for (size_t ni : c.members)
                if (ni < n) b_oriented[ni][ci] = 1;
        }
    }

    DMatrix b_weighted(n, std::vector<double>(m, 0.0));
    for (size_t r = 0; r < n; ++r)
        for (size_t c = 0; c < m; ++c)
            b_weighted[r][c] = static_cast<double>(b_oriented[r][c]) * w_e[c];

    const auto bbt = compute_b_w_bt(b_oriented, nullptr); // B B^T
    const auto bwbt = compute_b_w_bt(b_oriented, &w_e);   // B W_E B^T
    const auto btb_binary = compute_b_t_b(b_binary);      // B^T B (membership overlap)
    const auto btb_oriented = compute_b_t_b(b_oriented);  // oriented relation overlap

    JsonObject out;
    out["kind"] = json_string("incidence_algebra");
    out["node_count"] = json_number(static_cast<double>(n));
    out["relation_count"] = json_number(static_cast<double>(m));

    JsonObject convention;
    convention["directed"] = json_string("-1 leaves source, +1 enters target");
    convention["undirected"] = json_string("+1 for both incident nodes");
    convention["hyperedge"] = json_string("+1 for participating member nodes");
    convention["weight_channel"] = json_string("W_E is separate from topology B");
    out["convention"] = json_object(std::move(convention));

    JsonObject relation_isa;
    JsonArray relation_types;
    for (const auto& rel : micronaut::relation_isa())
        relation_types.push_back(json_string(rel));
    relation_isa["relation_types"] = json_array(std::move(relation_types));
    JsonArray compat_aliases;
    for (const auto& alias : micronaut::relation_isa_compat_aliases())
        compat_aliases.push_back(json_string(alias));
    relation_isa["compat_aliases"] = json_array(std::move(compat_aliases));
    out["relation_isa"] = json_object(std::move(relation_isa));

    JsonArray nodes;
    for (size_t i = 0; i < model.node_ids.size(); ++i) {
        JsonObject node;
        node["index"] = json_number(static_cast<double>(i));
        node["id"] = json_string(model.node_ids[i]);
        nodes.push_back(json_object(std::move(node)));
    }
    out["nodes"] = json_array(std::move(nodes));

    JsonArray relations;
    for (size_t i = 0; i < model.columns.size(); ++i) {
        const auto& c = model.columns[i];
        JsonObject rel;
        rel["index"] = json_number(static_cast<double>(i));
        rel["id"] = json_string(c.id);
        rel["kind"] = json_string(c.kind);
        rel["verb"] = json_string(c.verb);
        rel["relation_type"] = json_string(c.relation_type.empty() ? c.verb : c.relation_type);
        rel["relation_family"] = json_string(c.relation_family.empty() ? "unknown" : c.relation_family);
        rel["directed"] = json_bool(c.directed);
        rel["weight"] = json_number(c.weight);
        if (!c.source.empty()) rel["source"] = json_string(c.source);
        if (!c.target.empty()) rel["target"] = json_string(c.target);
        if (!c.phase.empty()) rel["phase"] = json_string(c.phase);

        JsonArray members;
        for (size_t ni : c.members) {
            if (ni >= model.node_ids.size()) continue;
            JsonObject mobj;
            mobj["index"] = json_number(static_cast<double>(ni));
            mobj["id"] = json_string(model.node_ids[ni]);
            members.push_back(json_object(std::move(mobj)));
        }
        rel["members"] = json_array(std::move(members));
        relations.push_back(json_object(std::move(rel)));
    }
    out["relations"] = json_array(std::move(relations));

    JsonArray weights;
    for (double w : w_e) weights.push_back(json_number(w));
    out["weights_w_e"] = json_array(std::move(weights));

    out["incidence_binary"] = json_int_matrix(b_binary);
    out["incidence_oriented"] = json_int_matrix(b_oriented);
    out["incidence_weighted"] = json_double_matrix(b_weighted);

    out["node_laplacian_bbt"] = json_double_matrix(bbt);
    out["node_laplacian_bwbt"] = json_double_matrix(bwbt);
    out["relation_overlap_btb_binary"] = json_double_matrix(btb_binary);
    out["relation_overlap_btb_oriented"] = json_double_matrix(btb_oriented);

    return json_object(std::move(out));
}

JsonPtr MCPServer::tool_semantic_fold(const JsonPtr& params) {
    bool include_phase_folds = true;
    std::string node;
    std::string node_a;
    std::string node_b;
    std::string edge_type;   // "" = default canonical set; "all"; or column kind name
    std::string relation_type_filter;
    std::string relation_family_filter;
    size_t top_k = 5;

    if (params && params->is_object()) {
        if (auto b = params->get_bool("include_phase_folds"))
            include_phase_folds = *b;
        if (auto s = params->get_string("node")) node = *s;
        if (auto s = params->get_string("node_a")) node_a = *s;
        if (auto s = params->get_string("node_b")) node_b = *s;
        if (auto s = params->get_string("edge_type")) edge_type = *s;
        if (auto s = params->get_string("relation")) relation_type_filter = *s;
        if (auto s = params->get_string("relation_type")) relation_type_filter = *s;
        if (auto s = params->get_string("verb")) relation_type_filter = *s;
        if (auto s = params->get_string("relation_family")) relation_family_filter = *s;
        if (auto n = params->get_number("top_k")) {
            if (*n > 0.0) top_k = static_cast<size_t>(*n);
        }
    }

    const auto model = build_incidence_model(mu_reg_, include_phase_folds);
    const size_t n = model.node_ids.size();
    const size_t m = model.columns.size();

    // B_τ = B M_τ  — typed projection mask over column kinds.
    // Default ("") = canonical semantic-fold set: phase_fold + relation.
    // legacy_delegate excluded by default (bookkeeping, not semantic participation).
    std::vector<bool> col_mask(m, true);
    const bool relation_filter_active = !relation_type_filter.empty();
    const auto relation_filter_canon = micronaut::canonical_relation(relation_type_filter);
    const auto relation_family_enum = micronaut::relation_family_from_name(relation_family_filter);
    const bool relation_family_filter_active =
        !relation_family_filter.empty() && relation_family_enum != micronaut::RelationFamily::Unknown;
    if (edge_type == "all") {
        // explicit all: no filtering
    } else {
        const bool use_default = edge_type.empty() || edge_type == "default";
        for (size_t ci = 0; ci < m; ++ci) {
            const auto& kind = model.columns[ci].kind;
            if (relation_filter_active && use_default) {
                col_mask[ci] = (kind == "relation" || kind == "legacy_delegate");
            } else {
                col_mask[ci] = use_default
                    ? (kind == "phase_fold" || kind == "relation")
                    : (kind == edge_type);
            }
        }
    }

    for (size_t ci = 0; ci < m; ++ci) {
        if (!col_mask[ci]) continue;
        const auto& col = model.columns[ci];

        if (relation_filter_active) {
            const bool has_match = micronaut::relation_matches(
                col.relation_type.empty() ? col.verb : col.relation_type,
                relation_filter_canon);
            if (!has_match) {
                col_mask[ci] = false;
                continue;
            }
        }

        if (relation_family_filter_active) {
            const auto fam = micronaut::relation_family_of(
                col.relation_type.empty() ? col.verb : col.relation_type);
            if (fam != relation_family_enum) {
                col_mask[ci] = false;
                continue;
            }
        }
    }
    const size_t m_masked = static_cast<size_t>(
        std::count(col_mask.begin(), col_mask.end(), true));

    IMatrix b_binary(n, std::vector<int>(m, 0));
    for (size_t ci = 0; ci < model.columns.size(); ++ci) {
        if (!col_mask[ci]) continue;
        for (size_t ni : model.columns[ci].members) {
            if (ni < n) b_binary[ni][ci] = 1;
        }
    }

    auto build_context_row = [&](size_t node_idx) {
        std::vector<size_t> active;
        if (node_idx >= n) return active;
        for (size_t ci = 0; ci < m; ++ci) {
            if (b_binary[node_idx][ci] != 0) active.push_back(ci);
        }
        return active;
    };

    auto context_obj = [&](size_t ci) -> JsonPtr {
        if (ci >= model.columns.size()) return json_null();
        const auto& c = model.columns[ci];
        JsonObject obj;
        obj["index"] = json_number(static_cast<double>(ci));
        obj["id"] = json_string(c.id);
        obj["kind"] = json_string(c.kind);
        obj["verb"] = json_string(c.verb);
        obj["relation_type"] = json_string(c.relation_type.empty() ? c.verb : c.relation_type);
        obj["relation_family"] = json_string(c.relation_family.empty() ? "unknown" : c.relation_family);
        obj["directed"] = json_bool(c.directed);
        obj["weight"] = json_number(c.weight);
        if (!c.source.empty()) obj["source"] = json_string(c.source);
        if (!c.target.empty()) obj["target"] = json_string(c.target);
        if (!c.phase.empty()) obj["phase"] = json_string(c.phase);
        return json_object(std::move(obj));
    };

    auto fold_state_obj = [&](const std::string& id) -> JsonPtr {
        const auto* e = mu_reg_.get(id);
        if (!e) return json_null();
        JsonObject st;
        st["phase"] = json_string(kuhul::phase_name(e->primary_phase));
        st["confidence"] = json_number(e->confidence);
        st["static_weight"] = json_number(e->static_weight);
        st["health"] = json_number(e->health);
        st["active"] = json_bool(e->active);
        return json_object(std::move(st));
    };

    auto sparse_fingerprint = [&](size_t node_idx) -> JsonPtr {
        if (node_idx >= n) return json_null();
        const auto active = build_context_row(node_idx);
        const std::string& id = model.node_ids[node_idx];
        JsonObject obj;
        obj["node"] = json_string(id);
        obj["node_index"] = json_number(static_cast<double>(node_idx));
        obj["context_count"] = json_number(static_cast<double>(m_masked));
        obj["active_count"] = json_number(static_cast<double>(active.size()));
        obj["density"] = json_number(m_masked == 0 ? 0.0 :
            static_cast<double>(active.size()) / static_cast<double>(m_masked));
        obj["state"] = fold_state_obj(id);

        JsonArray active_indices;
        JsonArray active_contexts;
        for (size_t ci : active) {
            active_indices.push_back(json_number(static_cast<double>(ci)));
            active_contexts.push_back(context_obj(ci));
        }
        obj["active_indices"] = json_array(std::move(active_indices));
        obj["active_contexts"] = json_array(std::move(active_contexts));
        return json_object(std::move(obj));
    };

    auto pair_metrics = [&](size_t ia, size_t ib) -> JsonPtr {
        JsonObject obj;
        obj["node_a"] = json_string(model.node_ids[ia]);
        obj["node_b"] = json_string(model.node_ids[ib]);
        obj["node_a_index"] = json_number(static_cast<double>(ia));
        obj["node_b_index"] = json_number(static_cast<double>(ib));

        size_t a_count = 0;
        size_t b_count = 0;
        size_t and_count = 0;
        size_t or_count = 0;
        size_t xor_count = 0;
        size_t sub_ab_count = 0;
        size_t sub_ba_count = 0;

        std::vector<size_t> and_idx;
        std::vector<size_t> or_idx;
        std::vector<size_t> xor_idx;
        std::vector<size_t> sub_ab_idx;
        std::vector<size_t> sub_ba_idx;

        for (size_t ci = 0; ci < m; ++ci) {
            const bool a = (b_binary[ia][ci] != 0);
            const bool b = (b_binary[ib][ci] != 0);
            if (a) ++a_count;
            if (b) ++b_count;
            if (a && b) { ++and_count; and_idx.push_back(ci); }
            if (a || b) { ++or_count; or_idx.push_back(ci); }
            if (a != b) { ++xor_count; xor_idx.push_back(ci); }
            if (a && !b) { ++sub_ab_count; sub_ab_idx.push_back(ci); }
            if (b && !a) { ++sub_ba_count; sub_ba_idx.push_back(ci); }
        }

        const double jaccard = (or_count == 0) ? 1.0 :
            static_cast<double>(and_count) / static_cast<double>(or_count);
        const double cosine = (a_count == 0 || b_count == 0) ? 0.0 :
            static_cast<double>(and_count) /
            std::sqrt(static_cast<double>(a_count) * static_cast<double>(b_count));
        const double overlap = (std::min(a_count, b_count) == 0) ? 0.0 :
            static_cast<double>(and_count) /
            static_cast<double>(std::min(a_count, b_count));

        obj["a_active_count"] = json_number(static_cast<double>(a_count));
        obj["b_active_count"] = json_number(static_cast<double>(b_count));
        obj["and_count"] = json_number(static_cast<double>(and_count));
        obj["or_count"] = json_number(static_cast<double>(or_count));
        obj["xor_count"] = json_number(static_cast<double>(xor_count));
        obj["sub_a_minus_b_count"] = json_number(static_cast<double>(sub_ab_count));
        obj["sub_b_minus_a_count"] = json_number(static_cast<double>(sub_ba_count));
        obj["hamming_distance"] = json_number(static_cast<double>(xor_count));
        obj["jaccard_similarity"] = json_number(jaccard);
        obj["cosine_similarity"] = json_number(cosine);
        obj["overlap_similarity"] = json_number(overlap);

        auto append_contexts = [&](const std::vector<size_t>& idx) {
            JsonArray arr;
            for (size_t ci : idx) arr.push_back(context_obj(ci));
            return json_array(std::move(arr));
        };
        obj["and_contexts"] = append_contexts(and_idx);
        obj["or_contexts"] = append_contexts(or_idx);
        obj["xor_contexts"] = append_contexts(xor_idx);
        obj["sub_a_minus_b_contexts"] = append_contexts(sub_ab_idx);
        obj["sub_b_minus_a_contexts"] = append_contexts(sub_ba_idx);

        return json_object(std::move(obj));
    };

    JsonObject out;
    out["kind"] = json_string("semantic_fold");
    out["node_count"] = json_number(static_cast<double>(n));
    out["context_count_total"] = json_number(static_cast<double>(m));
    out["context_count"] = json_number(static_cast<double>(m_masked));
    out["edge_type"] = json_string(edge_type.empty() ? "default" : edge_type);
    out["relation_type_filter"] = json_string(
        relation_filter_active ? relation_filter_canon : std::string{});
    out["relation_family_filter"] = json_string(
        relation_family_filter_active ? micronaut::relation_family_name(relation_family_enum) : std::string{});
    out["include_phase_folds"] = json_bool(include_phase_folds);
    out["fold_definition"] = json_string(
        "Fold(v)=bounded distributed semantic context around v over incidence contexts");
    out["phase_relation"] = json_string(
        "Fold is structure; K-UHUL phases are operations/states over that structure");

    JsonObject formal_model;
    formal_model["fold_equation"] = json_string(
        "FOLD = semantic_context + superordinate_identity + incidence_participation + computational_boundary");
    formal_model["shf_signature"] = json_string(
        "SHF(h,tau)=<h,B_tau[h,*],N_tau(h),W_tau,State(h)>");
    formal_model["fold_operator"] = json_string(
        "Fold:{contexts,relations,nodes}->h");
    formal_model["unfold_operator"] = json_string(
        "Unfold(h,tau)={e | B_tau[h,e] != 0}");
    formal_model["traverse_operator"] = json_string(
        "TRAVERSE(start, relation_type, direction, depth, limit, predicate)");
    formal_model["xcfe_ops"] = json_string(
        "if,for,from,as,cause,direct,identity");
    formal_model["xcfe_op_forms"] = json_string(
        "native key {\"@<op>\":...} | explicit {\"@op\":\"<op>\",\"@args\":...} | token \"@op:<op>\"; "
        "@then/@else are branch labels selected by @if, not independently invocable ops");
    formal_model["xcfe_node_classes"] = json_string(
        "CONDITION,ROUTING,CAUSAL,STRUCTURE,SELECTION,EXECUTION,CAPABILITY,TRANSFORM,"
        "VERIFICATION,CONSEQUENCE,COMMIT,MEMORY,NEGATION,KUHUL_RUNTIME");
    formal_model["xcfe_class_invariant"] = json_string(
        "authority_phase = CanonicalPhase(class); flow_phase = authority_phase is legal but "
        "not required; class is not the Relation ISA and there is no Policy class");
    formal_model["xcfe_context_authority"] = json_string(
        "caller context is data-only; xcfe.* and semantic_folding_only are host-owned and dropped");

    JsonArray representations;
    for (const char* r : {"Book", "Graph", "B_matrix", "Fingerprint", "SVG_3D", "SCXQ2"})
        representations.push_back(json_string(r));
    formal_model["representations"] = json_array(std::move(representations));
    out["formal_model"] = json_object(std::move(formal_model));

    JsonObject relation_isa;
    JsonArray relation_types;
    for (const auto& rel : micronaut::relation_isa())
        relation_types.push_back(json_string(rel));
    relation_isa["relation_types"] = json_array(std::move(relation_types));
    JsonArray compat_aliases;
    for (const auto& alias : micronaut::relation_isa_compat_aliases())
        compat_aliases.push_back(json_string(alias));
    relation_isa["compat_aliases"] = json_array(std::move(compat_aliases));
    out["relation_isa"] = json_object(std::move(relation_isa));

    if (!node_a.empty() && !node_b.empty()) {
        const auto it_a = model.node_index.find(node_a);
        const auto it_b = model.node_index.find(node_b);
        if (it_a == model.node_index.end())
            return json_string("error: node_a not found: " + node_a);
        if (it_b == model.node_index.end())
            return json_string("error: node_b not found: " + node_b);
        out["pair"] = pair_metrics(it_a->second, it_b->second);
        out["fingerprint_a"] = sparse_fingerprint(it_a->second);
        out["fingerprint_b"] = sparse_fingerprint(it_b->second);
        out["state_a"] = fold_state_obj(node_a);
        out["state_b"] = fold_state_obj(node_b);
    } else {
        const std::string focus = !node.empty() ? node :
            (model.node_ids.empty() ? "" : model.node_ids.front());
        if (!focus.empty()) {
            const auto it = model.node_index.find(focus);
            if (it == model.node_index.end())
                return json_string("error: node not found: " + focus);

            const size_t i_focus = it->second;
            out["node"] = sparse_fingerprint(i_focus);

            struct SimRow {
                std::string id;
                size_t index = 0;
                double cosine = 0.0;
                double jaccard = 0.0;
                double overlap = 0.0;
            };
            std::vector<SimRow> sims;
            sims.reserve(n > 0 ? n - 1 : 0);
            for (size_t j = 0; j < n; ++j) {
                if (j == i_focus) continue;
                auto pair = pair_metrics(i_focus, j);
                if (!pair || !pair->is_object()) continue;
                SimRow row;
                row.id = model.node_ids[j];
                row.index = j;
                row.cosine = pair->get_number("cosine_similarity").value_or(0.0);
                row.jaccard = pair->get_number("jaccard_similarity").value_or(0.0);
                row.overlap = pair->get_number("overlap_similarity").value_or(0.0);
                sims.push_back(row);
            }
            std::sort(sims.begin(), sims.end(),
                      [](const SimRow& a, const SimRow& b) {
                          if (std::fabs(a.cosine - b.cosine) > 1e-9) return a.cosine > b.cosine;
                          if (std::fabs(a.jaccard - b.jaccard) > 1e-9) return a.jaccard > b.jaccard;
                          return a.id < b.id;
                      });
            if (sims.size() > top_k) sims.resize(top_k);

            JsonArray neighbors;
            for (const auto& s : sims) {
                JsonObject row;
                row["id"] = json_string(s.id);
                row["index"] = json_number(static_cast<double>(s.index));
                row["cosine_similarity"] = json_number(s.cosine);
                row["jaccard_similarity"] = json_number(s.jaccard);
                row["overlap_similarity"] = json_number(s.overlap);
                neighbors.push_back(json_object(std::move(row)));
            }
            out["nearest_neighbors"] = json_array(std::move(neighbors));
        }
    }

    return json_object(std::move(out));
}

JsonPtr MCPServer::tool_code_parse(const JsonPtr& params) {
    std::string source;
    std::string grammar_file;
    bool include_fold = true;

    if (params && params->is_object()) {
        if (auto s = params->get_string("source"))       source       = *s;
        if (auto s = params->get_string("grammar_file")) grammar_file = *s;
        if (auto b = params->get_bool("include_fold"))   include_fold = *b;
    }

    if (source.empty()) {
        JsonObject err;
        err["error"] = json_string("source required");
        return json_object(std::move(err));
    }
    if (grammar_file.empty()) {
        JsonObject err;
        err["error"] = json_string("grammar_file required (e.g. \"klsl.code-grammar.json\")");
        return json_object(std::move(err));
    }

    // Try path as-is, then relative to data/grammar/
    JsonPtr grammar_json;
    for (const std::string& try_path : { grammar_file, "data/grammar/" + grammar_file }) {
        std::ifstream f(try_path);
        if (!f) continue;
        std::string buf((std::istreambuf_iterator<char>(f)), {});
        grammar_json = util::parse(buf);
        if (grammar_json) break;
    }

    if (!grammar_json) {
        JsonObject err;
        err["error"] = json_string("grammar_file not found or invalid JSON: " + grammar_file);
        return json_object(std::move(err));
    }

    const CodeGraph graph = parse_with_grammar(grammar_json, source);

    JsonObject result;
    result["language"]   = json_string(graph.language);
    result["node_count"] = json_number(static_cast<double>(graph.nodes.size()));
    result["edge_count"] = json_number(static_cast<double>(graph.edges.size()));

    JsonArray nodes_arr;
    for (const auto& nd : graph.nodes) {
        JsonObject no;
        no["id"]   = json_string(nd.id);
        no["type"] = json_string(nd.type);
        no["text"] = json_string(nd.text);
        no["line"] = json_number(static_cast<double>(nd.line));
        no["col"]  = json_number(static_cast<double>(nd.col));
        if (!nd.captures.empty()) {
            JsonObject caps;
            for (const auto& [k, v] : nd.captures)
                caps[k] = json_string(v);
            no["captures"] = json_object(std::move(caps));
        }
        nodes_arr.push_back(json_object(std::move(no)));
    }
    result["nodes"] = json_array(std::move(nodes_arr));

    JsonArray edges_arr;
    for (const auto& ed : graph.edges) {
        JsonObject eo;
        eo["from"]   = json_string(ed.from);
        eo["to"]     = json_string(ed.to);
        eo["kind"]   = json_string(ed.kind);
        eo["weight"] = json_number(static_cast<double>(ed.weight));
        edges_arr.push_back(json_object(std::move(eo)));
    }
    result["edges"] = json_array(std::move(edges_arr));

    // Build fold fingerprints from the code graph's own incidence model
    if (include_fold && !graph.nodes.empty()) {
        const size_t N = graph.nodes.size();
        std::unordered_map<std::string, size_t> node_idx;
        for (size_t i = 0; i < N; ++i)
            node_idx[graph.nodes[i].id] = i;

        struct LocalColumn {
            std::string kind;
            std::string from;
            std::string to;
            std::vector<size_t> members;
        };
        std::vector<LocalColumn> columns;
        std::unordered_map<std::string, size_t> col_idx;

        for (const auto& ed : graph.edges) {
            const std::string key = ed.kind + ":" + ed.from + ":" + ed.to;
            if (col_idx.find(key) == col_idx.end()) {
                col_idx[key] = columns.size();
                columns.push_back({ ed.kind, ed.from, ed.to, {} });
            }
            auto add = [&](const std::string& nid) {
                auto it = node_idx.find(nid);
                if (it != node_idx.end())
                    columns[col_idx.at(key)].members.push_back(it->second);
            };
            add(ed.from);
            add(ed.to);
        }

        const size_t M = columns.size();
        std::vector<std::vector<int>> B(N, std::vector<int>(M, 0));
        for (size_t ci = 0; ci < M; ++ci)
            for (size_t ni : columns[ci].members)
                if (ni < N) B[ni][ci] = 1;

        JsonArray fingerprints;
        for (size_t i = 0; i < N; ++i) {
            JsonObject fp;
            fp["node"] = json_string(graph.nodes[i].id);
            fp["type"] = json_string(graph.nodes[i].type);
            fp["text"] = json_string(graph.nodes[i].text);
            size_t active = 0;
            JsonArray ctxs;
            for (size_t ci = 0; ci < M; ++ci) {
                if (B[i][ci]) {
                    ++active;
                    ctxs.push_back(json_string(columns[ci].kind + ":" + columns[ci].to));
                }
            }
            fp["active"]  = json_number(static_cast<double>(active));
            fp["density"] = json_number(M == 0 ? 0.0 :
                static_cast<double>(active) / static_cast<double>(M));
            fp["contexts"] = json_array(std::move(ctxs));
            fingerprints.push_back(json_object(std::move(fp)));
        }
        result["fold_fingerprints"] = json_array(std::move(fingerprints));
    }

    return json_object(std::move(result));
}

} // namespace khanary::io
