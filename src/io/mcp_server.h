#pragma once
#include "json_rpc_server.h"
#include "../manifest/manifest_registry.h"
#include "../kuhul/kuhul_vm.h"
#include "../micronaut/micronaut_registry.h"
#include "../micronaut/factory.h"
#include "../micronaut/sandbox_inspector.h"
#include "../memory/confidence.h"
#include "../world/world_state.h"
#include "../world/field_dag.h"
#include "jrom.h"
#include <string>
#include <memory>
#include <vector>

namespace khanary::gpu {
class SvgComputeRuntime;
}

namespace khanary::io {

// MCP gateway on port 8764
// Exposes 24 tools: kuhul_forge, manifest_resolve, fold_execute, micronaut_run,
//   grammar_check, phase_query, gram_read, gram_write, confidence_get, jrom_replay,
//   sandbox_query, sandbox_list, sandbox_violations, world_state,
//   field_dag, field_dag_svg, relational_graph, incidence_matrix, semantic_fold,
//   code_parse, xcfe_eval,
//   memory_save, memory_recall   ← MEMORY-µ tools
//   tool_card                    ← TOOL-µ tool card lookup
// Every call auto-logs to gram store: tool:log:{name}, tool:count:{name}
class MCPServer {
public:
    MCPServer(
        uint16_t port,
        manifest::ManifestRegistry& reg,
        kuhul::KuhulVM& vm,
        micronaut::MicronautRegistry& mu_reg,
        micronaut::Factory& factory,
        micronaut::SandboxInspector& sandbox,
        memory::ConfidenceStore& confidence,
        world::WorldState& world,
        JROM& jrom,
        gpu::SvgComputeRuntime* svg_runtime = nullptr);

    bool start();
    void stop();
    bool is_running() const;

    // Register a GML file path to be overlaid on every field_dag build.
    void add_gml(const std::string& path);

private:
    std::vector<std::string> gml_paths_;
    JsonRpcServer rpc_;

    manifest::ManifestRegistry&     manifest_;
    kuhul::KuhulVM&                  vm_;
    micronaut::MicronautRegistry&    mu_reg_;
    micronaut::Factory&              factory_;
    micronaut::SandboxInspector&     sandbox_;
    memory::ConfidenceStore&         confidence_;
    world::WorldState&               world_;
    JROM&                            jrom_;
    gpu::SvgComputeRuntime*          svg_runtime_ = nullptr;

    void register_tools();

    // Tool implementations
    util::JsonPtr tool_kuhul_forge(const util::JsonPtr& params);
    util::JsonPtr tool_manifest_resolve(const util::JsonPtr& params);
    util::JsonPtr tool_fold_execute(const util::JsonPtr& params);
    util::JsonPtr tool_micronaut_run(const util::JsonPtr& params);
    util::JsonPtr tool_grammar_check(const util::JsonPtr& params);
    util::JsonPtr tool_phase_query(const util::JsonPtr& params);
    util::JsonPtr tool_gram_read(const util::JsonPtr& params);
    util::JsonPtr tool_gram_write(const util::JsonPtr& params);
    util::JsonPtr tool_confidence_get(const util::JsonPtr& params);
    util::JsonPtr tool_jrom_replay(const util::JsonPtr& params);
    // Sandbox inspection
    util::JsonPtr tool_sandbox_query(const util::JsonPtr& params);
    util::JsonPtr tool_sandbox_list(const util::JsonPtr& params);
    util::JsonPtr tool_sandbox_violations(const util::JsonPtr& params);
    // K-CUBE 3D world state
    util::JsonPtr tool_world_state(const util::JsonPtr& params);
    // 2D semantic field DAG → XML (topology + tensor weights)
    util::JsonPtr tool_field_dag(const util::JsonPtr& params);
    // 2D field graph + SVG-3D isometric tensor blocks → SVG string
    util::JsonPtr tool_field_dag_svg(const util::JsonPtr& params);
    // Relational micronaut graph → GraphML-style XML with @verb edges
    util::JsonPtr tool_relational_graph(const util::JsonPtr& params);
    // Incidence algebra surface: B, W_E, BWB^T and B^TB projections
    util::JsonPtr tool_incidence_matrix(const util::JsonPtr& params);
    // Semantic fold surface: sparse context fingerprints + boolean/similarity ops
    util::JsonPtr tool_semantic_fold(const util::JsonPtr& params);
    // Semantic Code Parser Graph: grammar + source → fold-native node/edge graph
    util::JsonPtr tool_code_parse(const util::JsonPtr& params);
    util::JsonPtr tool_xcfe_eval(const util::JsonPtr& params);
    // MEMORY-µ: save/recall user preferences, session context, micronaut data
    // Returns UI cards: { @kind:"memory_card.v1", id, category, key, value, ... }
    util::JsonPtr tool_memory_save(const util::JsonPtr& params);
    util::JsonPtr tool_memory_recall(const util::JsonPtr& params);
    // TOOL-µ: display card for any registered tool; also auto-logged on every call
    // Returns: { @kind:"tool_card.v1", tool_name, description, params, phase, micronaut, call_count }
    util::JsonPtr tool_tool_card(const util::JsonPtr& params);
};

} // namespace khanary::io
