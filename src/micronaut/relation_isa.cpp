#include "relation_isa.h"
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace khanary::micronaut {

namespace {

using Family = RelationFamily;

const std::unordered_map<std::string, Family>& canonical_relation_families() {
    static const std::unordered_map<std::string, Family> kMap = {
        // SEMANTIC
        {"@is_a", Family::Semantic},
        {"@part_of", Family::Semantic},
        {"@has_a", Family::Semantic},
        {"@has_part", Family::Semantic},
        {"@has_property", Family::Semantic},
        {"@value", Family::Semantic},
        {"@possesses", Family::Semantic},
        {"@related_to", Family::Semantic},
        {"@contains", Family::Semantic},
        {"@declares", Family::Semantic},
        {"@defines", Family::Semantic},
        // CAUSAL
        {"@causes", Family::Causal},
        {"@prevents", Family::Causal},
        {"@enables", Family::Causal},
        // EPISTEMIC
        {"@supports", Family::Epistemic},
        {"@contradicts", Family::Epistemic},
        {"@evidence_for", Family::Epistemic},
        // DEPENDENCY
        {"@requires", Family::Dependency},
        {"@produces", Family::Dependency},
        {"@depends_on", Family::Dependency},
        {"@imports", Family::Dependency},
        {"@uses", Family::Dependency},
        {"@accesses", Family::Dependency},
        {"@reads", Family::Dependency},
        {"@samples", Family::Dependency},
        {"@returns", Family::Dependency},
        // CONTEXT
        {"@context", Family::Context},
        {"@previous", Family::Context},
        {"@next", Family::Context},
        {"@annotates", Family::Context},
        {"@via", Family::Context},
        // CONTROL
        {"@governs", Family::Control},
        {"@redirect", Family::Control},
        // RUNTIME
        {"@calls", Family::Runtime},
        {"@delegates_to", Family::Runtime},
        {"@return_to", Family::Runtime},
        {"@resumes", Family::Runtime}
    };
    return kMap;
}

const std::unordered_map<std::string, std::string>& relation_aliases() {
    static const std::unordered_map<std::string, std::string> kAliases = {
        // Compatibility aliases from existing track corpus.
        {"@relate", "@related_to"},
        {"@delegate", "@delegates_to"},
        {"@call", "@delegates_to"},
        {"@invoke", "@delegates_to"},
        {"@is_type", "@is_a"},
        {"@instance_of", "@is_a"},
        {"@has_component", "@has_part"},
        {"@has_feature", "@has_property"},
        {"@property_of", "@has_property"},
        {"@owns", "@possesses"},
        {"@own", "@possesses"},
        {"@possess", "@possesses"},
        {"@owner_of", "@possesses"},
        {"@contain", "@contains"},
        {"@declare", "@declares"},
        {"@define", "@defines"},
        {"@import", "@imports"},
        {"@use", "@uses"},
        {"@access", "@accesses"},
        {"@read", "@reads"},
        {"@sample", "@samples"},
        {"@return", "@returns"},
        {"@annotate", "@annotates"},
        {"@through", "@via"},
        {"@invokes", "@calls"},
        {"@compose", "@part_of"},
        {"@verify", "@supports"},
        {"@research", "@requires"},
        {"@aggregate", "@produces"},
        {"@eval", "@governs"},
        {"@mx2lm", "@redirect"},
        {"@app", "@context"}
    };
    return kAliases;
}

std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

const std::unordered_map<std::string, std::string>& lexical_relation_cues() {
    static const std::unordered_map<std::string, std::string> kLexical = {
        {"because", "@causes"},
        {"cause", "@causes"},
        {"reason", "@causes"},
        {"results", "@causes"},
        {"therefore", "@causes"},
        {"supports", "@supports"},
        {"supported", "@supports"},
        {"evidence", "@supports"},
        {"verify", "@supports"},
        {"validate", "@supports"},
        {"check", "@supports"},
        {"confirm", "@supports"},
        {"contradict", "@contradicts"},
        {"conflict", "@contradicts"},
        {"oppose", "@contradicts"},
        {"requires", "@requires"},
        {"need", "@requires"},
        {"depends", "@depends_on"},
        {"depends_on", "@depends_on"},
        {"dependency", "@depends_on"},
        {"has_part", "@has_part"},
        {"component", "@has_part"},
        {"part", "@part_of"},
        {"property", "@has_property"},
        {"has_property", "@has_property"},
        {"color", "@has_property"},
        {"colour", "@has_property"},
        {"value", "@value"},
        {"black", "@value"},
        {"own", "@possesses"},
        {"owns", "@possesses"},
        {"possess", "@possesses"},
        {"possesses", "@possesses"},
        {"contains", "@contains"},
        {"contain", "@contains"},
        {"declare", "@declares"},
        {"declares", "@declares"},
        {"define", "@defines"},
        {"defines", "@defines"},
        {"import", "@imports"},
        {"imports", "@imports"},
        {"use", "@uses"},
        {"uses", "@uses"},
        {"access", "@accesses"},
        {"accesses", "@accesses"},
        {"read", "@reads"},
        {"reads", "@reads"},
        {"sample", "@samples"},
        {"samples", "@samples"},
        {"returns", "@returns"},
        {"annotate", "@annotates"},
        {"annotates", "@annotates"},
        {"via", "@via"},
        {"calls", "@calls"},
        {"context", "@context"},
        {"previous", "@previous"},
        {"next", "@next"},
        {"route", "@redirect"},
        {"delegate", "@delegates_to"},
        {"call", "@calls"},
        {"invoke", "@delegates_to"},
        {"resume", "@resumes"},
        {"return", "@return_to"},
        {"govern", "@governs"},
        {"control", "@governs"},
        {"related", "@related_to"},
        {"similar", "@related_to"}
    };
    return kLexical;
}

} // namespace

std::string normalize_relation_token(const std::string& relation) {
    std::string out;
    out.reserve(relation.size() + 1);

    bool prev_sep = false;
    for (unsigned char uc : relation) {
        const char c = static_cast<char>(std::tolower(uc));
        if (std::isalnum(uc)) {
            out.push_back(c);
            prev_sep = false;
        } else if (c == '@' && out.empty()) {
            out.push_back('@');
            prev_sep = false;
        } else if (c == '_' || c == '-' || std::isspace(uc)) {
            if (!out.empty() && !prev_sep && out.back() != '@') {
                out.push_back('_');
                prev_sep = true;
            }
        }
    }

    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.empty()) return out;
    if (out.front() != '@') out.insert(out.begin(), '@');
    return out;
}

std::string canonical_relation(const std::string& relation) {
    const auto normalized = normalize_relation_token(relation);
    if (normalized.empty()) return normalized;

    const auto& aliases = relation_aliases();
    if (auto it = aliases.find(normalized); it != aliases.end())
        return it->second;
    return normalized;
}

bool is_supported_relation(const std::string& relation) {
    const auto canon = canonical_relation(relation);
    if (canon.empty()) return false;
    const auto& fam = canonical_relation_families();
    return fam.find(canon) != fam.end();
}

bool relation_matches(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    return canonical_relation(a) == canonical_relation(b);
}

bool is_weak_relation(const std::string& relation) {
    return canonical_relation(relation) == "@related_to";
}

RelationFamily relation_family_of(const std::string& relation) {
    const auto canon = canonical_relation(relation);
    const auto& fam = canonical_relation_families();
    if (auto it = fam.find(canon); it != fam.end()) return it->second;
    return RelationFamily::Unknown;
}

RelationFamily relation_family_from_name(const std::string& name) {
    std::string lowered;
    lowered.reserve(name.size());
    for (unsigned char uc : name)
        lowered.push_back(static_cast<char>(std::tolower(uc)));

    if (lowered == "semantic") return RelationFamily::Semantic;
    if (lowered == "causal") return RelationFamily::Causal;
    if (lowered == "epistemic" || lowered == "evidence") return RelationFamily::Epistemic;
    if (lowered == "dependency" || lowered == "process") return RelationFamily::Dependency;
    if (lowered == "context") return RelationFamily::Context;
    if (lowered == "control") return RelationFamily::Control;
    if (lowered == "runtime" || lowered == "task") return RelationFamily::Runtime;
    return RelationFamily::Unknown;
}

std::string relation_family_name(RelationFamily family) {
    switch (family) {
        case RelationFamily::Semantic:   return "semantic";
        case RelationFamily::Causal:     return "causal";
        case RelationFamily::Epistemic:  return "epistemic";
        case RelationFamily::Dependency: return "dependency";
        case RelationFamily::Context:    return "context";
        case RelationFamily::Control:    return "control";
        case RelationFamily::Runtime:    return "runtime";
        default:                         return "unknown";
    }
}

std::string infer_relation_from_lexical(const std::string& lexical_hint) {
    if (lexical_hint.empty()) return "@related_to";

    // First pass: canonical/alias relation token handling.
    const auto as_relation = canonical_relation(lexical_hint);
    if (is_supported_relation(as_relation)) return as_relation;

    // Second pass: lexical cue lookup.
    std::string lowered = lower_copy(lexical_hint);
    std::replace(lowered.begin(), lowered.end(), '-', ' ');
    std::replace(lowered.begin(), lowered.end(), '_', ' ');

    const auto& cues = lexical_relation_cues();
    auto one = [&](const std::string& token) -> std::string {
        auto it = cues.find(token);
        if (it != cues.end()) return it->second;
        return {};
    };

    if (auto direct = one(lowered); !direct.empty())
        return canonical_relation(direct);

    std::string token;
    for (char c : lowered) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            token.push_back(c);
            continue;
        }
        if (!token.empty()) {
            if (auto mapped = one(token); !mapped.empty())
                return canonical_relation(mapped);
            token.clear();
        }
    }
    if (!token.empty()) {
        if (auto mapped = one(token); !mapped.empty())
            return canonical_relation(mapped);
    }
    return "@related_to";
}

std::string relation_phase_hint(const std::string& relation) {
    switch (relation_family_of(relation)) {
        case RelationFamily::Semantic:   return "Pop";
        case RelationFamily::Context:    return "Pop";
        case RelationFamily::Causal:     return "Wo";
        case RelationFamily::Dependency: return "Wo";
        case RelationFamily::Control:    return "Wo";
        case RelationFamily::Runtime:    return "Sek";
        case RelationFamily::Epistemic:  return "Ch'en";
        default:                         return "";
    }
}

std::vector<std::string> relation_isa() {
    std::vector<std::string> out;
    const auto& fam = canonical_relation_families();
    out.reserve(fam.size());
    for (const auto& [rel, _] : fam) out.push_back(rel);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> relation_isa_compat_aliases() {
    std::vector<std::string> out;
    const auto& aliases = relation_aliases();
    out.reserve(aliases.size());
    for (const auto& [alias, canon] : aliases) {
        if (alias == canon) continue;
        out.push_back(alias);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

} // namespace khanary::micronaut
