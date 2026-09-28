# KHANARY.CPP — Single-Executable Specification

Single native Windows executable: GPU inference + manifest runtime + micronaut engine + MCP gateway.
Target: HD 4600 (DirectML, D3D11), 1792 MB resident ceiling, Windows 10 x64.

## Revelation — Native Semantic Fold Runtime

K'UHUL now declares what the math was already doing:

- **Meaning is context-distributed**: a token points to `Book(token)` / fold context.
- **Fold is a bounded semantic context neighborhood**, not only a hypernym bucket.
- **Participation topology is first-class** (`B` incidence), with **pattern strength isolated** (`W_E`).
- **Distributional semantics remains independent** as a context-similarity channel.
- **Grammar boundary is explicit** in `data/schema/kuhul.xsd` (structure contract; runtime authorization remains in XCFE/Xul).

---

## Build Target

```
KHANARY.exe
  — no installer, no service, no node_modules
  — ships with a /data directory (manifests, shaders, tracks)
  — listens on 8764 (MCP) + 8787 (JSON-RPC) at startup
  — GPU device selected at launch via DXGI adapter enumeration
```

Toolchain: MSVC 2022+, C++20, CMake 3.28+, Windows SDK 10.0.26100, DirectML 1.15+.

---

## Source File List

### Entry & Config
| File | Purpose |
|---|---|
| `src/main.cpp` | CLI args, device init, server start, shutdown |
| `src/config.cpp/h` | Port map, paths, GPU limits, phase constants |

### Manifest System
| File | Purpose |
|---|---|
| `src/manifest/manifest_registry.cpp/h` | key→array O(1) registry; load/resolve/expand |
| `src/manifest/manifest_loader.cpp/h` | Parse css/kuhul/words/micronaut .manifest.json |
| `src/manifest/scx_resolver.cpp/h` | Recursive key expansion; cycle detection; lazy depth limit |

### K'uhul Runtime (Phase Engine)
| File | Purpose |
|---|---|
| `src/kuhul/phase_engine.cpp/h` | C₆ phase cycle (Pop→Wo→Yax→Sek→Ch'en→Xul), arc weights |
| `src/kuhul/fold_engine.cpp/h` | Horizontal fold (Δθ=0), vertical fold (Δθ=π/3), cost table |
| `src/kuhul/kuhul_vm.cpp/h` | Bracket program executor: [Pop]/[Wo]/[Yax]/[Sek]/[Ch'en]/[Xul] |
| `src/kuhul/xcfe_executor.cpp/h` | @if/@then/@else, @for, @from, @as, @cause, @direct, @op, @fn, phase-gate enforcement (the op library receives a recursive evaluator) |
| `src/kuhul/gram_store.cpp/h` | KXML gram nodes; Pop=gramRead, Xul=gramWrite, checkpoint; append log at `data/journal/gram-events.jsonl` |

### KLSL Compiler
| File | Purpose |
|---|---|
| `src/klsl/klsl_lexer.cpp/h` | Tokenize .kuhul/.khl source |
| `src/klsl/klsl_parser.cpp/h` | AST: Cluster/Model/Pipeline/Tensor nodes |
| `src/klsl/klsl_ir.cpp/h` | SSA geometric IR |
| `src/klsl/hlsl_codegen.cpp/h` | IR → HLSL cs_5_0 compute shader text |
| `src/klsl/wgsl_codegen.cpp/h` | IR → WGSL (fallback/WebGPU path) |
| `src/klsl/d3dcompile_bridge.cpp/h` | D3DCompile HLSL text → ID3DBlob at runtime |

### GPU Layer (D3D11 / DirectML)
| File | Purpose |
|---|---|
| `src/gpu/d3d11_device.cpp/h` | DXGI adapter enum, D3D11 device + context, HD 4600 quirks |
| `src/gpu/dml_ops.cpp/h` | DirectML: GEMM, Conv, Attention, Softmax, Reduce |
| `src/gpu/tensor_buffer.cpp/h` | R32_UINT/R32_FLOAT buffer alloc, staging, upload/readback |
| `src/gpu/compute_dispatch.cpp/h` | CS dispatch, barrier management, UAV binding |
| `src/gpu/dds_loader.cpp/h` | DDS shard streaming: SDDS magic, INT8 tensor records, CRC |
| `src/gpu/hot_swap.cpp/h` | Resident weight eviction within 1792 MB ceiling |

### Inference Engine
| File | Purpose |
|---|---|
| `src/inference/tokenizer.cpp/h` | 1024-token glyph vocab; hash→id; BOS/EOS/TOOL_CALL sentinels |
| `src/inference/moe_router.cpp/h` | Router logits → top-k expert selection; routing_bias table |
| `src/inference/inference_engine.cpp/h` | Token→embedding→fold→expert→logits→sample pipeline |
| `src/inference/kv_cache.cpp/h` | KV-cache alloc, GPU QK-norm, sequence management |
| `src/inference/sampler.cpp/h` | Temperature, top-p, top-k sampling |

### Micronaut Runtime
| File | Purpose |
|---|---|
| `src/micronaut/micronaut_registry.cpp/h` | Register/dispatch micronauts by phase + governs |
| `src/micronaut/aiml_engine.cpp/h` | ELIZA/ALICE AIML 1.0 pattern match; template expansion |
| `src/micronaut/eliza_operators.cpp/h` | 16 reasoning operators (CAUSE/CONTRAST/.../DIRECT) Pop+Sek pairs |
| `src/micronaut/track_loader.cpp/h` | Load micronaut.semantic-tracks.v1 JSON; bind folds |
| `src/micronaut/gravity_field.cpp/h` | Gravity well scoring: established_nodes / distance² |
| `src/micronaut/factory.cpp/h` | Create/evolve/delete micronauts; confidence_floor gate |

### Cognitive Memory (Three-Store)
| File | Purpose |
|---|---|
| `src/memory/perception.cpp/h` | F_Q ephemeral field; reset at Xul; embed(Q, L_t) |
| `src/memory/confidence.cpp/h` | L_t persistent; Ch'en verify → Xul commit gate only |
| `src/memory/semantic_graph.cpp/h` | G_semantic topology; (⟁EVOLVE⟁)-gated node/arc mutation |
| `src/memory/scoring.cpp/h` | S_µ(Q) = W_µ · C_µ · R_µ(Q); ΔZ = ΔG_semantic ⊕ ΔC_earned |

### I/O & Servers
| File | Purpose |
|---|---|
| `src/io/json_rpc_server.cpp/h` | HTTP/JSON-RPC on 8787; XJSON/XCFE object surface |
| `src/io/mcp_server.cpp/h` | MCP gateway on 8764; 20 tools incl. kuhul_forge/code_parse/semantic_fold |
| `src/io/jrom.cpp/h` | JROM audit journal; append log at `data/journal/tool-events.jsonl` |
| `src/io/request_router.cpp/h` | Route incoming requests → phase dispatch → micronaut |

### Utilities
| File | Purpose |
|---|---|
| `src/util/sha256.cpp/h` | SHA-256 for manifest CRC and SCXQ2 checksums |
| `src/util/json.cpp/h` | Minimal JSON parse/emit (no external dep) |
| `src/util/thread_pool.cpp/h` | Work-stealing pool for async fold execution |

---

## Data Directory (`/data`)

```
data/
  manifest/
    css.manifest.json         — ⟁CSS symbol → CSS declaration array
    kuhul.manifest.json       — ⟁K'uhul opcode → phase opcode strings
    words.manifest.json       — word/vocab → intents, phase refs, XCFE objects
    micronaut.manifest.json   — micronaut name → capability strings, phase refs

  drivers/phases/
    pop.json                  — Pop phase-driver op/emission contract
    wo.json                   — Wo phase-driver op/emission contract
    yax.json                  — Yax phase-driver op/emission contract
    sek.json                  — Sek phase-driver op/emission contract
    chen.json                 — Ch'en phase-driver op/emission contract
    xul.json                  — Xul phase-driver op/emission contract

  shaders/
    fold_compute.hlsl         — horizontal/vertical fold compute kernel
    svg_compute.hlsl          — SVG-3D tensor geometric projection kernel
    moe_router.hlsl           — expert router softmax + top-k
    attention.hlsl            — QK-norm + scaled dot-product attention
    dds_dequant.hlsl          — INT8 DDS weight dequantization

  tracks/
    gravity.semantic-tracks.v1.json
    ELIZA.semantic-tracks.v1.json
    ELIZA-1.semantic-tracks.v1.json
    intent-unresolved.semantic-tracks.v1.json
    Librarian.semantic-tracks.v1.json
    Factory.semantic-tracks.v1.json
    Coordinator.semantic-tracks.v1.json
    KNOWLED-1.semantic-tracks.v1.json

  alice/
    mp0.aiml … mp9.aiml       — ALICE AIML corpus

  weights/                    — user-supplied .scxqdds / .gguf shards (not shipped)
```

---

## Module Dependency Graph

```
main
  ├── config
  ├── d3d11_device → dml_ops → compute_dispatch → tensor_buffer
  ├── dds_loader → hot_swap
  ├── manifest_registry → manifest_loader → scx_resolver
  ├── phase_engine → fold_engine → kuhul_vm → xcfe_executor → gram_store
  ├── klsl_lexer → klsl_parser → klsl_ir → hlsl_codegen → d3dcompile_bridge
  ├── tokenizer → moe_router → inference_engine → kv_cache → sampler
  ├── micronaut_registry
  │     ├── aiml_engine → eliza_operators
  │     ├── track_loader
  │     ├── gravity_field
  │     └── factory
  ├── perception → confidence → semantic_graph → scoring
  └── json_rpc_server + mcp_server → request_router → jrom
```

---

## Phase Constants (compile-time)

```cpp
// C6 phase cycle angles (radians)
constexpr double PHASE_POP   = 0.0;
constexpr double PHASE_WO    = M_PI / 3.0;
constexpr double PHASE_YAX   = 2.0 * M_PI / 3.0;
constexpr double PHASE_SEK   = M_PI;
constexpr double PHASE_CHEN  = 4.0 * M_PI / 3.0;
constexpr double PHASE_XUL   = 5.0 * M_PI / 3.0;

// Arc weight: arc_w * cos(phase_j - phase_i)
// same-phase=+1.0, adjacent=+0.5, two-hop=-0.5, opposing=-1.0

// GPU ceiling
constexpr size_t HD4600_RESIDENT_CEILING_BYTES = 1792ULL * 1024 * 1024;

// Ports
constexpr uint16_t PORT_MCP      = 8764;
constexpr uint16_t PORT_JSON_RPC = 8787;
```

---

## Manifest Resolution (pseudocode)

```cpp
// O(1) key lookup, lazy recursive expansion
Value resolve(const std::string& key, const Context& ctx) {
    auto& node = registry.at(key);           // O(1) hash map
    if (node.is_cached()) return node.cache;

    Value result;
    for (const auto& elem : node.array) {
        if (registry.has(elem))              // expansion key
            result.merge(resolve(elem, ctx));
        else if (elem.starts_with('@'))      // declarative operator
            result.push(xcfe_executor.eval(elem, ctx));
        else
            result.push(elem);               // raw string
    }
    node.cache = result;
    return result;
}
```

---

## Inference Pipeline (pseudocode)

```cpp
// Token → phase → expert → output
Output run(const std::string& prompt) {
    auto tokens  = tokenizer.encode(prompt);          // 1024-glyph vocab
    auto phase   = phase_engine.enter(PHASE_POP);     // perceive
    auto gravity = gravity_field.score(tokens, phase);
    auto expert  = moe_router.select(gravity, top_k); // Wo: allocate
    auto hidden  = inference_engine.forward(tokens, expert, kv_cache);
    auto gate    = xcfe_executor.eval(verify_node, phase_ctx);  // Ch'en: XCFE gate over the fold
    return sampler.sample(logits);                      // Xul: emit
}
```

---

## CMake Sketch

```cmake
cmake_minimum_required(VERSION 3.28)
project(KHANARY CXX)
set(CMAKE_CXX_STANDARD 20)

find_package(directx-headers CONFIG REQUIRED)
find_package(DirectML 1.15 REQUIRED)

add_executable(KHANARY
  src/main.cpp
  src/manifest/manifest_registry.cpp
  src/manifest/manifest_loader.cpp
  src/manifest/scx_resolver.cpp
  src/kuhul/phase_engine.cpp
  src/kuhul/fold_engine.cpp
  src/kuhul/kuhul_vm.cpp
  src/kuhul/xcfe_executor.cpp
  src/kuhul/gram_store.cpp
  src/klsl/klsl_lexer.cpp src/klsl/klsl_parser.cpp
  src/klsl/klsl_ir.cpp
  src/klsl/hlsl_codegen.cpp src/klsl/wgsl_codegen.cpp
  src/klsl/d3dcompile_bridge.cpp
  src/gpu/d3d11_device.cpp src/gpu/dml_ops.cpp
  src/gpu/tensor_buffer.cpp src/gpu/compute_dispatch.cpp
  src/gpu/dds_loader.cpp src/gpu/hot_swap.cpp
  src/inference/tokenizer.cpp src/inference/moe_router.cpp
  src/inference/inference_engine.cpp
  src/inference/kv_cache.cpp src/inference/sampler.cpp
  src/micronaut/micronaut_registry.cpp
  src/micronaut/aiml_engine.cpp src/micronaut/eliza_operators.cpp
  src/micronaut/track_loader.cpp
  src/micronaut/gravity_field.cpp src/micronaut/factory.cpp
  src/memory/perception.cpp src/memory/confidence.cpp
  src/memory/semantic_graph.cpp src/memory/scoring.cpp
  src/io/json_rpc_server.cpp src/io/mcp_server.cpp
  src/io/jrom.cpp src/io/request_router.cpp
  src/util/sha256.cpp src/util/json.cpp src/util/thread_pool.cpp
)

target_link_libraries(KHANARY PRIVATE
  d3d11 dxgi d3dcompiler
  DirectML
  directx-headers::directx-headers
)

target_compile_definitions(KHANARY PRIVATE
  UNICODE _UNICODE
  KHANARY_HD4600_CEILING=1879048192   # 1792 MiB
)
```

---

## What This Replaces

| Old (experimental) | New (KHANARY.exe) |
|---|---|
| Node.js kuhul-es runtime | `kuhul_vm.cpp` + `phase_engine.cpp` |
| PowerShell micronaut-v4 | `micronaut_registry.cpp` + `track_loader.cpp` |
| Python tools/gen.py | `inference_engine.cpp` |
| kuhul-server.js MCP | `mcp_server.cpp` |
| json_runtime (port 8787) | `json_rpc_server.cpp` |
| KLSL JS compiler | `klsl_*` modules |
| Loose AIML files | `aiml_engine.cpp` + `data/alice/` |

Total: ~35 .cpp files, ~8,000 lines estimated, 1 executable, 1 `/data` directory.
