#pragma once
// ── Xul gate: topology mutation permission ──────────────────────────────────
// XCFE answers "is this mutation authorized?". The Xul gate answers "may this
// authorized mutation physically enter B now?". B is authoritative topology.
//
// A phase *string* cannot be the gate: "Xul" is caller-supplied data, so
// accepting it as proof of a commit would only turn a convention into a password.
// Instead the mutation entry point requires a CommitAuthority, and the only way to
// obtain one is XulGate::authorize_commit(). A writer cannot manufacture it — the
// constructor is private and friended to the gate alone — so possession is
// evidence of authorization rather than an assertion about it.
//
//   Valid(A_commit) <=> proposal verified (Ch'en)
//                    && mutation authorized (XCFE)
//                    && scope permitted
//                    && commit phase == Xul
//   Write(B, dB, nullopt) => DENY
//
// Invariant this exists to make real: B_{t+1} != B_t  =>  Xul && Authorized(dB).
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "phase_engine.h"

namespace khanary::kuhul {

// Proof that ONE specific proposed mutation was verified, authorized and scoped.
// Not default-constructible, not constructible outside the gate, and not
// copyable: move-only construction prevents duplication, and the moved-from
// authority carries no usable scope/identity so reuse fails. A one-shot
// certificate by construction — not a revocation system, and it does not need to
// be one for the property tested here.
//
// Contributor rule: headers under src/kuhul/ must open `namespace khanary::kuhul`.
// A fresh global `namespace kuhul` declares a different type —
// ::kuhul::XulGate != ::khanary::kuhul::XulGate — and `using namespace khanary`
// in a test can mask that until something fails to compile.
class CommitAuthority {
public:
    CommitAuthority(CommitAuthority&&) = default;
    CommitAuthority& operator=(CommitAuthority&&) = default;
    CommitAuthority(const CommitAuthority&) = delete;
    CommitAuthority& operator=(const CommitAuthority&) = delete;
    ~CommitAuthority() = default;

    // The scope this authority was granted for. A commit may not widen it.
    const std::string& scope() const { return scope_; }
    // Identity of the proposed mutation this authority was minted for: a token
    // for proposal A must not commit proposal B.
    const std::string& proposal_identity() const { return proposal_identity_; }

private:
    CommitAuthority(std::string scope, std::string proposal_identity)
        : scope_(std::move(scope)), proposal_identity_(std::move(proposal_identity)) {}
    std::string scope_;
    std::string proposal_identity_;
    friend class XulGate;
};

// What must be true before a commit authority can exist.
struct CommitDecision {
    bool  proposal_verified = false;    // Ch'en verified the proposal
    bool  mutation_authorized = false;  // XCFE authorized the mutation
    bool  scope_permitted = false;      // the scope is permitted
    Phase phase = Phase::Pop;           // must be Xul
};

class XulGate {
public:
    // Same shape as the existing XCFE phase-transition authorization: a host
    // installs the callback. An unbound gate denies, never grants.
    using Authorizer = std::function<bool(Phase, const std::string& scope)>;
    void set_authorizer(Authorizer fn) { authorizer_ = std::move(fn); }

    // The only mint of a CommitAuthority. Returns nullopt unless every condition
    // holds, so an unauthorized commit has nothing to present. The identity binds
    // the authority to one proposed mutation.
    std::optional<CommitAuthority> authorize_commit(const CommitDecision& decision,
                                                    const std::string& scope,
                                                    const std::string& proposal_identity) const {
        if (decision.phase != Phase::Xul) return std::nullopt;
        if (!decision.proposal_verified) return std::nullopt;
        if (!decision.mutation_authorized) return std::nullopt;
        if (!decision.scope_permitted) return std::nullopt;
        if (scope.empty() || proposal_identity.empty()) return std::nullopt;
        if (!authorizer_) return std::nullopt;
        if (!authorizer_(decision.phase, scope)) return std::nullopt;
        return CommitAuthority{scope, proposal_identity};
    }

    // Mutation entry point. No authority, no write; a token cannot be replayed
    // against another scope or another proposal; and taking the token by value
    // consumes it, so one authority authorizes one commit transaction.
    bool write(std::optional<CommitAuthority> authority,
               const std::string& scope,
               const std::string& proposal_identity,
               const std::function<void()>& mutation) const {
        if (!authority) return false;
        if (authority->scope() != scope) return false;
        if (authority->proposal_identity() != proposal_identity) return false;
        mutation();
        return true;
    }

private:
    Authorizer authorizer_;
};

} // namespace khanary::kuhul
