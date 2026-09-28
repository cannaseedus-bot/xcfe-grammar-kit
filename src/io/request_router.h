#pragma once
#include "../micronaut/micronaut_registry.h"
#include "../memory/scoring.h"
#include "../kuhul/phase_engine.h"
#include "../util/json.h"
#include <string>
#include <functional>
#include <array>
#include <cmath>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <cctype>
#include <mutex>
#include <random>
#include "../util/sha256.h"

namespace khanary::io {

inline constexpr const char* ROUTE_SCORE_POLICY =
    "mesh_static_pattern_fold_phase_driver_confidence_health_distributional_lexical_nsm_function_wsd_compositional_coarse_ngram_tool_gram_vector_gate_prior";

// ── Entropy measurement layer (diagnostic only) ─────────────────────────────
// Order is fixed: Requirements -> Eligibility -> WCR -> EntropyMeasure ->
// Decision/Throttle. Never Entropy -> Eligibility.
//   Entropy may recommend; entropy may never authorize.
// Nothing in RoutingEntropy may be reachable from evaluate_eligibility() or
// from any capability-authorization path.

// Advisory threshold on the normalized routing entropy: at or above this the
// eligible candidates are near-indistinguishable, so omega-throttle may
// recommend gathering evidence rather than collapsing.
inline constexpr double kOmegaEntropyThreshold = 0.90;

struct RoutingEntropy {
    size_t eligible_count = 0;
    double bits = 0.0;        // H = -Σ p_i log2 p_i with p_i = S_i / Σ S_j, S = W·C·R
    double normalized = 0.0;  // H / log2(|Eligible|); 0 when |Eligible| <= 1
    bool   omega_throttled = false;
};

// Normalized W·C·R routing entropy over the already-eligible candidate set.
inline RoutingEntropy routing_entropy(const std::vector<double>& wcr_scores,
                                      double threshold = kOmegaEntropyThreshold) {
    RoutingEntropy out;
    out.eligible_count = wcr_scores.size();
    if (wcr_scores.size() <= 1) return out;
    double sum = 0.0;
    for (double score : wcr_scores) sum += (score > 0.0 ? score : 0.0);
    if (sum <= 0.0) return out;
    double h = 0.0;
    for (double score : wcr_scores) {
        const double p = (score > 0.0 ? score : 0.0) / sum;
        if (p > 0.0) h -= p * std::log2(p);
    }
    out.bits = h;
    const double h_max = std::log2(static_cast<double>(wcr_scores.size()));
    if (h_max > 0.0) out.normalized = h / h_max;
    out.omega_throttled = out.normalized >= threshold;
    return out;
}

// ── Ω gate: the blocking observation ────────────────────────────────────────
// |Eligible| = 0                -> MISS_capability  -> Factory
// |Eligible| > 1 and H > H_Ω    -> MISS_ambiguity   -> research/unfold -> requery
// Entropy still authorizes nothing: it can only refuse premature collapse.
// Exhaustion stays explicitly unresolved — the gate NEVER falls through to Allow.
inline constexpr uint32_t kAmbiguityMaxAttempts = 2;
// Cap on the number of tracked query fingerprints, so an unbounded query stream
// cannot grow the budget map without limit.
inline constexpr size_t kAmbiguityAttemptCap = 256;

enum class AmbiguityGate { Allow, BlockAmbiguity, ExhaustedAmbiguity };

inline AmbiguityGate ambiguity_gate(const RoutingEntropy& entropy,
                                    uint32_t prior_attempts,
                                    uint32_t max_attempts = kAmbiguityMaxAttempts) {
    if (entropy.eligible_count <= 1) return AmbiguityGate::Allow;
    if (!entropy.omega_throttled) return AmbiguityGate::Allow;
    return prior_attempts >= max_attempts ? AmbiguityGate::ExhaustedAmbiguity
                                         : AmbiguityGate::BlockAmbiguity;
}

// ── Calibration instrumentation (provisional defaults, measured boundary) ───
// H_Ω = 0.90 and A_max = 2 are provisional, not derived. Every routing event
// emits C_Q = <|Eligible|, Ĥ_route, S_1, S_2, S_1-S_2, attempt, outcome> so the
// operating point can be measured instead of guessed. The top-two margin is a
// separate signal from entropy: two fields can share an entropy while differing
// substantially in how separated the winner is locally.
struct WcrFieldStats {
    double top1 = 0.0;    // S_1
    double top2 = 0.0;    // S_2
    double margin = 0.0;  // S_1 - S_2
};

inline WcrFieldStats wcr_field_stats(const std::vector<double>& scores) {
    WcrFieldStats out;
    for (double score : scores) {
        const double s = score > 0.0 ? score : 0.0;
        if (s > out.top1) {
            out.top2 = out.top1;
            out.top1 = s;
        } else if (s > out.top2) {
            out.top2 = s;
        }
    }
    out.margin = out.top1 - out.top2;
    return out;
}

// The observable outcome bucket at decision time. Resolution buckets ("resolved
// after research / after attempt N") are joins over later events, not decided here.
inline const char* ambiguity_outcome_label(const std::string& block_reason,
                                           size_t eligible_count) {
    if (block_reason == "MISS_ambiguity_exhausted") return "omega_exhausted";
    if (block_reason == "MISS_ambiguity") return "omega_blocked";
    if (block_reason == "MISS_capability") return "miss_capability";
    if (block_reason == "XCFE_DENIED") return "xcfe_denied";
    if (block_reason == "NO_SELECTED_RESOLVER") return "no_selected_resolver";
    if (eligible_count == 1) return "single_eligible";
    if (eligible_count == 0) return "no_eligible";
    return "allowed";
}

// ── Episode identity (pseudonymous join key) ────────────────────────────────
// The episode is the ambiguity episode, not an attempt: every attempt at the same
// routing question shares one id, which is what makes the retry sequence joinable.
// Truncation alone is pseudonymization, not anonymization — a short predictable
// prompt would be dictionary-guessable — so the id is a keyed digest of a
// runtime-scoped nonce plus the canonical query. The nonce is stable within the
// process (attempts join) and different across runs (no cross-run dictionary).
inline constexpr size_t kEpisodeIdChars = 16;

// Calibration epoch: bumped whenever the observation contract changes, so rows
// from different decision conditions are never silently pooled. Rows lacking the
// field predate the epoch marker (and the ambiguity_attempt fix) and must be
// excluded from retry-transition analysis.
inline constexpr const char* kCalibrationEpoch = "episode-v2-attempt-index";

// Canonical form: trimmed, lowercased, internal whitespace collapsed.
inline std::string canonical_routing_query(const std::string& query) {
    std::string out;
    bool pending_space = false;
    for (unsigned char ch : query) {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            pending_space = !out.empty();
            continue;
        }
        if (pending_space) {
            out.push_back(' ');
            pending_space = false;
        }
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

// Process-scoped nonce. Regenerated per run so episode ids do not join across runs.
inline const std::string& routing_episode_nonce() {
    static const std::string nonce = [] {
        std::random_device device;
        std::string material = std::to_string(device()) + "-" +
                               std::to_string(device()) + "-" +
                               std::to_string(static_cast<unsigned long long>(
                                   std::chrono::steady_clock::now().time_since_epoch().count()));
        return util::sha256_hex(material).substr(0, 32);
    }();
    return nonce;
}

inline std::string episode_id_for(const std::string& canonical_query,
                                  const std::string& nonce = routing_episode_nonce()) {
    const std::string digest = util::sha256_hex(nonce + "\x1f" + canonical_query);
    return digest.substr(0, kEpisodeIdChars);
}

struct RouteRequest {
    std::string              query;
    std::vector<std::string> tokens;
    kuhul::Phase             phase = kuhul::Phase::Pop;
    util::JsonPtr            payload;
    // Route origin / population label, set by the call site. Queries that never
    // construct a RouteRequest (e.g. the key_store lane) are outside the sampled
    // population, so observations are P(GOOD | Ĥ, lane) — never plain P(GOOD | Ĥ).
    std::string              lane;
};

struct RouteResult {
    std::string   micronaut_id;  // primary bone (highest-weight track)
    std::string   handler_id;    // track whose handler actually fired (may differ from micronaut_id)
    kuhul::Phase  dispatched_phase;
    std::string   selected_task_op; // inferred/declared SGSFG node op for resolver eligibility
    std::string   resolver_policy;  // resolver policy descriptor (e.g. matrix_buffer.required)
    std::string   required_primitive; // required execution primitive (if any)
    std::vector<std::string> required_capabilities; // hard capability requirements from task/policy
    std::vector<std::string> eligible_resolvers;    // policy-admitted candidates before W-C-R ranking
    std::vector<std::string> ineligible_resolvers;  // policy-rejected candidates
    bool          policy_filter_applied = false; // Yax policy gate applied to candidate set
    bool          capability_miss = false;       // EligibleResolvers(v) = ∅
    bool          selected_capable = false;      // Sek revalidation: selected resolver still capable
    bool          policy_satisfied = true;       // Sek revalidation: policy still satisfied
    bool          xcfe_authorized = false;       // Host policy decision, never granted by payload
    bool          dispatch_allowed = false;      // Sek dispatch gate outcome
    std::string   dispatch_block_reason;         // MISS_capability | XCFE_DENIED | NO_SELECTED_RESOLVER | ...
    // Entropy measurement of the eligible W·C·R distribution. Diagnostic: it can
    // recommend throttling, and can never authorize eligibility.
    size_t        eligible_count = 0;               // policy-admitted candidates ranked
    double        routing_entropy_bits = 0.0;       // H = -Σ p_i log2 p_i
    double        routing_entropy_normalized = 0.0; // H / log2(eligible_count)
    bool          omega_throttled = false;          // near-indistinguishable candidates
    bool          omega_blocked = false;            // Ω refused premature collapse
    uint32_t      ambiguity_attempt = 0;            // bounded retry/evidence budget
    // Calibration tuple C_Q (measured, not guessed): S_1, S_2, S_1-S_2 and the
    // decision-time outcome bucket.
    double        wcr_top1 = 0.0;
    double        wcr_top2 = 0.0;
    double        wcr_margin = 0.0;
    std::string   ambiguity_outcome;                // see ambiguity_outcome_label()
    std::string   episode_id;                       // pseudonymous ambiguity-episode join key
    float         score;          // composite score = static * pattern * fold_weight * driver_weight * confidence * health * relevance * vector_gate
    float         fold_weight;    // track-declared phase fold coefficient [0,1]
    float         phase_driver_weight; // runtime phase-driver trust coefficient [0,1]
    float         semantic_score; // secondary score from MicronautScorer
    float         static_weight;  // deterministic prior [0,1]
    float         pattern_strength; // association/pattern channel [0,1]
    float         confidence_score; // earned reliability [0,1]
    float         health_score;     // liveness/availability [0,1]
    float         relevance_score;  // merged lexical+semantic relevance [0,1]
    float         bow_score;        // bag-of-words lexical relevance channel [0,1]
    float         distributional_score; // distributional-context similarity channel [0,1]
    float         lexical_semantic_score; // lexical-normalized relation similarity [0,1]
    float         nsm_prime_score; // NSM semantic-prime overlap channel [0,1]
    float         function_word_score; // closed-class/function-word structural channel [0,1]
    float         content_word_ratio; // query lexical composition: content-word ratio [0,1]
    float         function_word_ratio; // query lexical composition: function-word ratio [0,1]
    float         content_function_binding_ratio; // function words adjacent to content words [0,1]
    float         word_sense_score; // context disambiguated polysemy channel [0,1]
    float         compositional_phrase_score; // multiword compositional phrase channel [0,1]
    float         coarse_ngram_score; // coarse n-gram structural prior [0,1]
    float         tool_gram_score; // tool-call intent n-gram channel [0,1]
    float         vector_relevance_score; // geometric relevance gate: cos(Q,K) normalized to [0,1]
    float         wcr_attention_score; // W*C*R with R = vector_relevance_score
    float         query_vector_x; // Q.x
    float         query_vector_y; // Q.y
    float         query_vector_z; // Q.z
    float         key_vector_x;   // selected K.x
    float         key_vector_y;   // selected K.y
    float         key_vector_z;   // selected K.z
    // LBS bone weights: top-k tracks with normalized weights (Σw_k = 1.0).
    // blend[0] is the primary bone; blend[1..] are secondary influences.
    std::vector<std::pair<std::string, float>> blend;
    util::JsonPtr response;
};

using ResponseFn = std::function<util::JsonPtr(
    const std::string& micronaut_id,
    const RouteRequest& req)>;

class RequestRouter {
public:
    RequestRouter(
        micronaut::MicronautRegistry& mu_reg,
        memory::MicronautScorer& scorer,
        const std::string& data_dir = "data");

    // Route a request through fold-first gravity scoring → semantic tiebreak → response
    RouteResult route(const RouteRequest& req);

    // Register a response handler for a specific micronaut
    void register_handler(const std::string& micronaut_id, ResponseFn fn);

    // Default handler (for unmatched or factory-bound requests)
    void set_default_handler(ResponseFn fn);

    // Host-only policy binding. An unbound router denies dispatch. Payload flags
    // may request denial, but cannot grant authority or replace this callback.
    using Authorizer = std::function<bool(const RouteRequest&, const std::string&)>;
    void set_authorizer(Authorizer fn) { authorizer_ = std::move(fn); }

    // Episode observation sink (JSONL, append-only). Off by default so tests and
    // embedders write nothing unless they opt in; failures never affect routing.
    void enable_calibration(const std::string& path) { calibration_path_ = path; }

private:
    micronaut::MicronautRegistry& mu_reg_;
    memory::MicronautScorer&      scorer_;
    std::unordered_map<std::string, ResponseFn> handlers_;
    ResponseFn default_handler_;
    Authorizer authorizer_;
    std::array<float, 6> phase_driver_weights_{};
    std::array<float, 6> phase_wsd_weights_{};
    std::array<float, 6> phase_function_channel_weights_{};
    std::array<float, 6> phase_tool_channel_weights_{};
    // Bounded ambiguity budget: query fingerprint -> Ω blocks so far. Bounded in
    // size so an unbounded query stream cannot grow it without limit.
    std::unordered_map<std::string, uint32_t> ambiguity_attempts_;
    std::string calibration_path_;   // empty = observation sink disabled
    std::mutex  calibration_mutex_;

    std::vector<std::string> tokenize_query(const std::string& query) const;
    float phase_driver_weight(kuhul::Phase phase) const;
    float phase_wsd_weight(kuhul::Phase phase) const;
    float phase_function_channel_weight(kuhul::Phase phase) const;
    float phase_tool_channel_weight(kuhul::Phase phase) const;
    void load_phase_driver_weights(const std::string& data_dir);
};

} // namespace khanary::io
