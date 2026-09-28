#pragma once
#include <string>
#include <vector>

namespace khanary::micronaut {

enum class RelationFamily {
    Semantic,
    Causal,
    Epistemic,
    Dependency,
    Context,
    Control,
    Runtime,
    Unknown
};

// Normalize relation token for comparison: lowercase, canonical separators,
// and ensure an '@' prefix.
std::string normalize_relation_token(const std::string& relation);

// Canonical relation mapping with compatibility aliases.
// Example: @relate -> @related_to, @delegate -> @delegates_to
std::string canonical_relation(const std::string& relation);

// Closed relation vocabulary check (canonical + compatibility aliases).
bool is_supported_relation(const std::string& relation);

// Canonical equality check across aliases.
bool relation_matches(const std::string& a, const std::string& b);

// @related_to is the weak fallback relation.
bool is_weak_relation(const std::string& relation);

RelationFamily relation_family_of(const std::string& relation);
RelationFamily relation_family_from_name(const std::string& name);
std::string relation_family_name(RelationFamily family);

// Infer a canonical relation token from lexical language cues.
// Examples: "because" -> "@causes", "verify" -> "@supports".
std::string infer_relation_from_lexical(const std::string& lexical_hint);

// Canonical phase hint for relation family.
// Returns one of Pop/Wo/Yax/Sek/Ch'en/Xul or empty when unknown.
std::string relation_phase_hint(const std::string& relation);

// Canonical relation ISA list (sorted).
std::vector<std::string> relation_isa();

// Accepted compatibility aliases (sorted), excluding canonical tokens.
std::vector<std::string> relation_isa_compat_aliases();

} // namespace khanary::micronaut
