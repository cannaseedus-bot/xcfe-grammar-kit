# Semantic Hypernym Folds

## Revelation — Hypernym is a subtype, not the whole fold

The stronger runtime definition is now:

```
Fold(v) = bounded distributed semantic context associated with v
```

So:

- `Hypernym Fold ⊂ Semantic Folds`
- token = address, `Book(token)` = unfoldable context neighborhood
- incidence `B` models participation; `W_E` remains pattern-strength only
- fold comparison/logic is operational (`AND/OR/XOR/SUB`, cosine/jaccard/hamming)

## Convergent definition

```
FOLD =
  semantic context
  + superordinate identity
  + incidence participation
  + computational boundary
```

| K'UHUL concept | Formal counterpart |
|---|---|
| Fold | bounded semantic/contextual structure |
| Semantic Hypernym Fold | superordinate + participating semantic neighborhood |
| Book | addressable neighborhood/context declaration |
| Unfold | resolve participating contexts/nodes |
| Node | semantic identity |
| Incidence `B` | node ↔ context/fold participation |
| Fingerprint `B[v,*]` | distributed context representation |
| Horizontal fold | same phase (Δθ=0), different track: `(φ, T_A) → (φ, T_B)` |
| Vertical fold | same track, next phase (Δθ=π/3): `(φ, T) → (φ+π/3, T)` |
| Fold similarity | similarity between incidence fingerprints |
| Fold interaction | `B W_E B^T` |
| Fold topology | incidence/hypergraph structure |

### Structure vs phase

```
Fold is the structure.
Phase is operation/state over that structure.
```

Equivalent formal signature:

```
SHF(h,tau) = < h, B_tau[h,*], N_tau(h), W_tau, State(h) >
```

## `kuhul.xsd` grammar contract

The concrete grammar is now declared at:

```
data/schema/kuhul.xsd
```

Its purpose is to codify structure and boundaries:

- **Fold ADT shape** (`fold_adt`) with identity, incidence operand, `W_E` channel, runtime state.
- **Phase wheel declaration** (`phase_wheel`) as discrete transitions over fold state.
- **Ch'en certificate envelope** (`chen_certificate`) with assertion set and `PASS|FAIL|UNKNOWN`.
- **Xul commit envelope** (`xul_commit`) for compare-and-commit using authorization + freshness checks.

The schema explains *what must exist*; runtime/XCFE still decides *when commit is legal*.

## Core definition

A **Semantic Hypernym Fold (SHF)** is a superordinate semantic container whose unfolding
exposes its participating semantic neighborhood:

```
SHF(H) = ⟨ H, V_H, E_H, B_H, W_H ⟩
```

| Symbol | Meaning |
|--------|---------|
| H      | semantic identity / superordinate (the "word for book") |
| V_H    | participating semantic nodes |
| E_H    | typed relations among them |
| B_H    | incidence matrix — which nodes participate in which contexts |
| W_H    | association / pattern-strength channel |

Membership in the fold is not restricted to `IS-A`:

```
x IS-A H
x PARTICIPATES-IN H
x SUPPORTS H
... (typed per the Book)
```

## Three levels

```
H          — semantic identity / superordinate
Book(H)    — declared neighborhood  ≡  N(H)  ≡  SHF(H)  (semantic level)
SHF(H)     — folded computational semantic unit (runtime level)
```

The **Book** is the addressable representation of the fold. `Book(H) ≡ N(H) ≡ SHF(H)` at
the semantic level; their physical encodings may differ.

Compression and restoration:

```
{ v₁, v₂, … vₙ, E_H }   ──FOLD──▶   ⌈ H ⌉
         ⌈ H ⌉           ──UNFOLD──▶  Book(H)
```

So "word for book" is almost literal:

```
Hypernym  →  compressed semantic address  →  Book/Fold  →  semantic neighborhood
```

## Fold ≠ just superordinate

| Term      | Role |
|-----------|------|
| Fold      | mechanism (general) |
| Semantic  | domain |
| Hypernym  | superordinate identity |

A `BIRD` fold is a classical hypernym fold.  
An `EVIDENCE` fold contains observations, operators, verification concepts, and confidence
signals — not all of which are hyponyms of *evidence*. Membership is typed participation,
not strictly taxonomy.

```
                ⟁EVIDENCE⟁
         Semantic Hypernym Fold
                     │
       ┌─────────────┼─────────────┐
       ▼             ▼             ▼
  observation      proof       measurement
       │             │             │
       ├── support ──┼── verify ───┤
       │             │             │
       ▼             ▼             ▼
   assertion     confidence    contradiction
```

## Algebraic grounding via the incidence matrix

The common substrate is:

```
B = build_incidence_model(V, E)
```

where every edge carries its type. A **projection mask** M_τ selects a column family:

```
B_τ = B M_τ
```

### Typed fingerprints

| Mode | Formula | Question answered |
|------|---------|-------------------|
| execution | F_exec(v) = B_phase_fold[v, *] | Which execution contexts activate this track? |
| field | F_field(v) = B_all[v, *] | What semantic/operational contexts does this track participate in overall? |
| typed | F(v, τ) = B_τ[v, *] | Fingerprint restricted to edge family τ |

Similarity becomes explicitly typed:

```
Sim_τ(u, v) = sim( B_τ[u,*], B_τ[v,*] )
```

### The two tools are operators over the same representation

```
build_incidence_model()
        │
        │  full typed B
        ▼
┌────────────────────────────────────┐
│           INCIDENCE MODEL          │
│  B + edge types + W_E + node meta  │
└─────────────┬──────────────────────┘
              │
      ┌───────┴────────┐
      ▼                ▼
incidence_matrix   semantic_fold
      │                │
      ▼                ▼
 B / BᵀB / BW_EBᵀ   row B_τ[v,*]
 Laplacian           fingerprint
 projections         similarity
```

`incidence_matrix` = global matrix view of B.  
`semantic_fold` = row/profile query over a typed projection of B.

The weighted node projection and the fold fingerprint share the same incidence facts:

```
A_τ^(w) = B_τ W_τ B_τᵀ          (node similarity matrix)
f_τ(v)  = B_τ[v, *]             (per-node fingerprint / fold SDR)
```

A `REASON-µ ↔ INFER-µ` similarity result and their value in BW_EBᵀ are derived from the
same incidence facts through different projections.

## Canonical semantic-fold type set

Don't use `all edges` as the default fingerprint — bookkeeping, diagnostics, and
serialization wiring pollute the semantic signal.

The canonical default for `semantic_fold` should be declared in a **Semantic Hypernym Fold
contract / Book**, not hardcoded. Recommended starting set:

```
B_semantic-fold = B_{ phase_fold, semantic, governs, delegates, context }
```

This allows the contract to evolve as track types evolve without touching the algorithm.

## semantic_fold tool API (typed mode)

```json
{
  "node": "REASON-µ",
  "edge_type": "phase_fold"        // governs | delegates | semantic | context | all
}
```

```json
{
  "node_a": "REASON-µ",
  "node_b": "INFER-µ",
  "edge_type": "governs",
  "top_k": 5
}
```

Calling modes:
- `node` only → fingerprint + nearest-k neighbors (sorted by cosine)
- `node_a` + `node_b` → pair metrics (cosine, jaccard, overlap, hamming, AND/OR/XOR/SUB)
- `edge_type` omitted → uses the contract default type set

SEMFOLD command surface (src/main.cpp):

```
SEMFOLD: REASON-µ vs INFER-µ
SEMFOLD: REASON-µ | INFER-µ
SEMFOLD: REASON-µ, INFER-µ
@semfold REASON-µ
```

## Unfold operation

Resolving a fold algebraically:

```
Unfold(v, τ) = { e | B_τ[v, e] ≠ 0 }
```

This is the set of contexts/edges in which `v` participates under projection τ — the
explicit algebraic form of "what the Book contains."

## Runtime lifecycle

```
SHF(H)
  │
  ├─ Pop      activate fold into the field
  ├─ Wo       establish execution state
  ├─ Yax      select relevant substructure from neighborhood
  ├─ Sek      operate on selected nodes/relations
  ├─ Ch'en    observe / verify results
  └─ Xul      collapse / commit fold outcome
```

Each phase step is an event logged to JROM; authorized events feed back into the
incidence model through the projection ABI (`⟁FIELD`, `⟁EVIDENCE`, `⟁VERIFY`, `⟁XUL`).

## Relationship to Semantic Folding (SDR)

Semantic Folding (Numenta / Webber) represents a word as a sparse binary vector over a
2D topographic semantic map — a 16,000-bit SDR where active bits indicate the contexts
in which the word appears.

KHANARY's SHF is the same idea expressed in a typed graph:

| Semantic Folding | KHANARY SHF |
|-----------------|-------------|
| 16,000-bit binary SDR | row B_τ[v, *] |
| 2D topographic map | field.xml + SVG-3D manifold |
| boolean AND/OR/XOR/SUB | semantic_fold pair ops |
| Hamming / Jaccard / cosine | semantic_fold similarity metrics |
| context occurrence indicator | incidence column |
| pure binary fingerprint | typed fingerprint with W_E pattern strength |

The difference: KHANARY uses typed semantic nodes and pattern-strength weights (W_E)
rather than pure binary indicators. The result is a **typed semantic fold** rather than a
binary SDR — strictly more expressive, at the cost of requiring a declared type contract.

---

## Fold as abstract data type

A fold doesn't have to mean one physical data structure. The same abstract object can be
serialized into any of these representations:

| Representation | Form |
|---------------|------|
| JSON Book | manifest / neighborhood declaration |
| GraphML | topology |
| B-matrix | incidence participation |
| Fingerprint B_τ[v,*] | distributed semantic-context SDR |
| SVG-3D tensor | geometric / runtime-state projection |
| SCXQ2 | compressed execution encoding |

```
Fold_ADT  ──▶  Book        (manifest)
          ──▶  Graph       (topology)
          ──▶  B-matrix    (incidence)
          ──▶  Fingerprint (context SDR)
          ──▶  Tensor      (runtime state)
          ──▶  SCXQ2       (execution)
```

**The projection is not the object.** Each representation is a view; the fold algebra is
defined on the abstract object, not on any particular encoding.

## Unified correspondence table

| K'UHUL concept | Formal counterpart |
|---------------|-------------------|
| Fold | bounded semantic/contextual structure |
| Semantic Hypernym Fold | superordinate + participating semantic neighborhood |
| Book | addressable neighborhood/context declaration |
| Unfold | resolve participating contexts/nodes |
| Node | semantic identity |
| Incidence B | node ↔ context/fold participation |
| Fingerprint B[v,*] | distributed context representation |
| Horizontal fold | same phase (Δθ=0), different track: `(φ, T_A) → (φ, T_B)` |
| Vertical fold | same track, next phase (Δθ=π/3): `(φ, T) → (φ+π/3, T)` |
| Fold similarity | similarity between incidence fingerprints |
| Fold interaction | B W_E Bᵀ |
| Fold topology | incidence/hypergraph structure |

## Semantic Navigation State — Ω = (φ, T, s)

The SHF algebraic model defines what a fold *is*. The navigation state defines how the
machine *moves* through fold space during execution.

```
Ω = (φ, T, s)
```

| Coordinate | Range | Answers |
|---|---|---|
| `φ` | C6 wheel: Pop/Wo/Yax/Sek/Ch'en/Xul | WHEN / WHERE in the phase cycle |
| `T` | active track / semantic space | WHO / WHICH Book is being traversed |
| `s` | `0` = folded/bounded, `1` = unfolded/expanded | is the working context expanded into F_Q? |

The three coordinates are **independent**. A fold/unfold step changes `s` (and optionally `T`)
without advancing `φ`. A vertical phase transition changes `φ` without touching `s` or `T`.

Movement kinds:

```
Horizontal: (φ, T_A) → (φ, T_B)     Δθ = 0   — same phase, different track
Vertical:   (φ, T)   → (φ+π/3, T)   Δθ = π/3 — same track, next phase
```

**Fold ≠ Xul:**

- `fold(T)` compresses `N_τ(h, T)` from F_Q into a bounded intermediate `C_T`. Changes `s → 0`.
  Fold does not commit topology.
- `unfold(T)` expands `N_τ(h, T)` into F_Q. Changes `s → 1`.
- `Xul` is the unique phase that may commit ΔB → B. It owns `F_h → F_h'`.

**Canonical traversal primitive:**

```
Handle → unfold(T) → Neighborhood → Attention → fold(T) → C_T
```

Multi-track form — same phase φ, multiple tracks:

```
F_Q^(0) → unfold(T₁) → fold(T₁) → C_T₁ → unfold(T₂) → fold(T₂) → C_T₂ → …
```

Micronaut composition follows the same law: `F_Advisor ↔ F_Research ↔ F_Memory` are
horizontal field couplings at the same phase, not an imperative call stack.

---

## Complete formal definition

```
SHF(h, τ) = ⟨ h, B_τ[h,*], N_τ(h), W_τ, State(h) ⟩
```

| Component | Meaning |
|-----------|---------|
| h | semantic superordinate / identity |
| B_τ[h,*] | typed incidence fingerprint |
| N_τ(h) | neighborhood obtained by unfolding those incidences |
| W_τ | pattern strengths over the participating relations |
| State(h) | runtime tensor state |

Fold and Unfold have exact operations:

```
Fold   : { contexts, relations, nodes } → h
Unfold : (h, τ) → { e | B_τ[h, e] ≠ 0 }
```

This is the **word-for-book** principle expressed algebraically:

```
Hypernym  →  h  →  B_τ[h,*]  →  Unfold  →  { participating contexts }
```

## Fold is the structure; phase is the operation

The six K'UHUL phases don't define what a fold is. They define what happens to a fold
over an execution cycle:

```
Fold  ──Pop──▶  Fold  ──Wo──▶  Fold  ──Yax──▶  Fold
              ──Sek──▶  Fold  ──Ch'en──▶  Fold  ──Xul──▶  Fold'
```

The semantic identity h remains stable. Activation, admitted neighborhood, computation,
observation, confidence, and collapsed state change across phases.

> **Fold = structure.   Phase = operation / state over that structure.**

A phase may itself define an operational fold boundary, but the phases are not the folds.
More precisely: **K'UHUL phases operate over folded semantic structures.**

## The two tools, precisely stated

```
semantic_fold(v)    ≡  B_τ[v, *]    — one fold: row query over the space
incidence_matrix()  ≡  B_τ          — the space in which all folds are defined
```

`semantic_fold` asks about a fold.  
`incidence_matrix` exposes the space in which all of those folds are defined.

They are two mathematically related observations of the same underlying algebra — not
two representations, but two operators over the same incidence facts.

---

## Three spaces and the complete machine state

K'UHUL execution operates over a product of three distinct spaces:

```
H   — Fold/hypergraph space       (structural, mostly read-only)
C_6 — discrete phase/control space
X   — runtime tensor/state space  (mutable per step)
```

The complete machine state at time t:

```
Ω_t = ( F_h, φ_t, X_t )
```

One K'UHUL step under ordinary execution:

```
Ω_{t+1} = ( F_h, next(φ_t), T_φ_t(F_h, X_t, Q) )
```

F_h is unchanged. Only an authorized topology-evolving operation produces F_h → F_h'.

### Phase-wheel algebra

The phase pointer is a one-hot vector in C_6. The transition matrix P is a cyclic shift:

```
    ┌ 0 0 0 0 0 1 ┐
    │ 1 0 0 0 0 0 │
P = │ 0 1 0 0 0 0 │       P^6 = I
    │ 0 0 1 0 0 0 │
    │ 0 0 0 1 0 0 │
    └ 0 0 0 0 1 0 ┘
```

P moves the phase pointer only. It does not perform the semantic operation.

### Phase operators

Each phase owns a typed operator T_φ:

```
T_Pop   : X → X + observation
T_Wo    : X → X + state / constraints
T_Yax   : X → X + admitted neighborhood
T_Sek   : X → X + execution result
T_Ch'en : X → X + verified observation / certificate
T_Xul   : X → X'   (and optionally B_t → B_{t+1})
```

The full K'UHUL cycle is their composition:

```
K = T_Xul ∘ T_Ch'en ∘ T_Sek ∘ T_Yax ∘ T_Wo ∘ T_Pop

X_{n+1} = K(X_n)
```

### B_τ is a read-only structural operand

During Pop through Ch'en, B_τ is never written:

```
∀ φ ≠ Xul :   T_φ(B) = B
```

Yax constructs an ephemeral admission mask over a read of B_τ — it never writes back:

```
m_h        = Gate_Q( B_τ[h,*], W_E )
B_h_active = B_τ[h,*] ⊙ m_h          (local, not persisted)
```

Sek operates on that admitted structure:

```
A_h_active = B_h_active  W_E  B_h_active^T
```

**Phase changes State. Ordinary phase execution does not silently change Topology.**

---

## Projection typology

The six physical representations project from the same Fold ADT — but they are not all
equivalent. Call the mapping Π_k : F_h → R_k.

### Lossless (ker Π_k = {0})

These retain full structural information and are invertible:

| Representation | Notes |
|---------------|-------|
| B-matrix | exact incidence indices, weights, node identity |
| GraphML | topology + all metadata |
| SCXQ2 bytecode | zero-copy binary serialization of B_τ; O(1) slice to B_τ[h,*] |

### Lossy (ker Π_k ≠ {0})

These discard information deliberately:

| Representation | What is discarded |
|---------------|-------------------|
| SDR fingerprint | structural node identities → fixed-width binary; enables O(1) overlap |
| SVG-3D tensor | non-visual metadata; X_h → spatial coordinates in R^3 |
| JSON Book | implementation wiring; retains declared semantic neighborhood only |

Claim isomorphism only where the mapping is lossless and invertible. An SDR that
deliberately drops node identity is not isomorphic to the GraphML that retains it.

### Continuous phase as downstream projection

Execution authority lives in the discrete C_6 algebra. The continuous circle S^1 is a
derived projection, not a driver:

```
φ_t ∈ C_6  ──Π_θ──▶  θ(t) = (π/3)·φ_t ∈ S^1  ──Π_shader──▶  HLSL / SVG-3D
```

Arc weight in shader space: w_ij^phase = cos(θ_j − θ_i)

This gives all the phase geometry without making geometry authoritative over execution.

---

## Ch'en: assertion-producing verifier

**Ch'en determines whether a proposed delta satisfies declared invariants. It does not
authorize. Xul owns the commit decision.**

```
T_Ch'en : (X_candidate, ΔB, Q, A) → (X_verified, V)
```

Where A is the assertion contract and V is the verification certificate.

### Assertion contract A

```
A = A_struct ∧ A_semantic ∧ A_state ∧ A_authority
```

| Class | Examples |
|-------|---------|
| STRUCTURAL | ΔB is well-formed; referenced nodes/edges exist; incidence types legal; immutable topology untouched |
| SEMANTIC | proposed relation has declared type; evidence supports it; W_E is pattern-strength only, not truth |
| STATE | tensor contract satisfied; values in bounds; no NaN; claimed observations present in Ch'en inputs |
| AUTHORITY | evolution explicitly requested; operator has evolve capability; provenance exists; scope permitted |

### Assertion result

Each assertion returns a typed tuple, not a boolean:

```
a_i → ( status, evidence, expected, observed, reason )

status ∈ { PASS, FAIL, UNKNOWN }
```

`UNKNOWN` must never become `PASS`. Missing evidence is not a pass.

Verification succeeds iff all required assertions pass:

```
Verified(V) ⟺ ∀ a_i ∈ Required(A), status(a_i) = PASS
```

### Resolution classes (answer-path)

Ch'en can classify query/answer resolution separately from topology rewrite verification:

```
VERIFIED | CONTRADICTED | MISS | AMBIGUOUS | INSUFFICIENT_CONTEXT | INVALID
```

Core invariant:

```
MISS != WRONG
```

`CONTRADICTED` means a wrong assertion against evidence. `MISS` means the currently unfolded
semantic field is insufficient, so the next action is unfold/acquire rather than confidence
punishment.

Deficit formalization:

```
MISS(Q) = RequiredContext(Q) - ResolvedContext(F_Q)
```

Research pressure:

```
rho_research = M * I * U
```

Where `M` is missing-context magnitude, `I` is conclusion importance, and `U` is uncertainty.
For high `rho_research`, route to research acquisition rather than guess/collapse.

Miss-kind routing:

- semantic miss → unfold another Book/fold
- topology miss → graph/incidence query
- memory miss → recall
- factual miss → RAG/external research
- mathematical miss → deterministic math executor
- capability miss → delegate Micronaut
- evidence miss → acquire/verify source provenance

In runtime terms, this is the µN-ary recursive routing layer (Mx2LM): MISS emits a structured
research plan, returns as new perception/evidence, then re-enters Pop→Wo→Yax→Sek→Ch'en.

### Certificate V

```json
{
  "@kind": "chen.verification",
  "subject": "fold://REASON-µ",
  "topology_version": 41,
  "candidate_delta": "ΔB:8f23...",
  "assertions": [
    { "id": "incidence.type.valid",      "status": "PASS" },
    { "id": "evidence.supports.edge",    "status": "PASS" },
    { "id": "evolve.scope.authorized",   "status": "PASS" }
  ],
  "verified": true,
  "rewrite_authorized": false
}
```

`verified ≠ committed`. Ch'en never sets `rewrite_authorized = true`. It supplies the
evidence. XCFE/Xul makes the authorization determination.

The certificate carries topology_version so that a stale certificate can be detected:

```
hash(B_current) must equal V.topology_hash before Xul applies ΔB
```

If topology changed after Ch'en verified it, the certificate is stale and the delta must
be re-verified.

---

## Xul: compare-and-commit

```
T_Xul(B_t, ΔB, V) =
  B_t ⊕ ΔB   if Verified(V) ∧ Authorized(V) ∧ Fresh(V)
  B_t         otherwise
```

### Transaction model

```
Sek
 │ proposes ΔB
 ▼
Ch'en
 │ ASSERT ΔB → certificate V
 ├── FAIL → reject (B_t unchanged)
 └── PASS ─▶ XCFE (authority check)
              │
              ▼
             Xul
        compare-and-commit
         ├── reject → B_t
         └── commit → B_t ⊕ ΔB
```

Phase responsibilities:

```
Sek    = propose
Ch'en  = prove / assert
XCFE   = authorize
Xul    = commit / collapse
```

### Topological invariant (strongest form)

```
B_{t+1} ≠ B_t  ⟹  ∃V : Verified(V) ∧ Authorized(V) ∧ Fresh(V) ∧ Commit_Xul(V)
```

In plain language: **no Fold topology may change unless Ch'en can prove the proposed
delta, XCFE can authorize it, and Xul commits that exact verified delta against the
topology version that was verified.**

### Application to semantic evolution (KNOWLED-1)

The existing evolution formulation ΔZ = ΔG_semantic ⊕ ΔC_earned produces two separately
asserted deltas:

```
V_G = Ch'en(ΔG_semantic)
V_C = Ch'en(ΔC_earned)
```

A bad answer can legitimately produce ΔC ≠ 0, ΔG = 0 — negative feedback does not
force a topology rewrite. Only when verified evidence supports a new semantic
relationship does ΔG ≠ 0.

---

## Summary: Ch'en and Xul

```
Ch'en = observation elevated into an assertion certificate
Xul   = the only point at which verified possibility collapses into persistent reality
```
