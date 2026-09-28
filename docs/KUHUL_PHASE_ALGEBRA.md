# K'UHUL Phase Algebra and Fold Transaction Model

Guide companion to `SEMANTIC_FOLDS.md`. This document covers the algebraic mechanics
of K'UHUL execution: three-space state, phase-wheel P matrix, typed T_φ operators,
topological invariance, and the Ch'en/Xul commit contract.

---

## Three Spaces and Complete Machine State

K'UHUL execution operates over a product of three distinct spaces:

```
H   — Fold / hypergraph space       (structural, mostly read-only)
C_6 — discrete phase / control space (6 states: Pop Wo Yax Sek Ch'en Xul)
X   — runtime tensor / state space  (mutable per step)
```

Complete machine state at time t:

```
Ω_t = ( F_h, φ_t, X_t )
```

One K'UHUL step:

```
Ω_{t+1} = ( F_h, next(φ_t), T_φ_t(F_h, X_t, Q) )
```

F_h is unchanged under all ordinary phases. Only Xul can produce F_h → F_h'.

---

## Semantic Navigation State — Ω = (φ, T, s)

The execution-level `(F_h, φ_t, X_t)` describes the formal algebraic machine. For semantic
traversal across tracks and fold boundaries, a three-axis **navigation state** is defined:

```
Ω = (φ, T, s)
```

| Coordinate | Range | Answers |
|---|---|---|
| `φ` | C6 wheel: Pop/Wo/Yax/Sek/Ch'en/Xul | WHEN / WHERE in the phase cycle |
| `T` | active track (micronaut, semantic space) | WHO / WHICH semantic space |
| `s` | `0` = folded/bounded, `1` = unfolded/expanded | is the working context bounded or expanded? |

The three coordinates are **genuinely independent**. Changing any one leaves the other two
unchanged unless explicitly transitioned.

### Two movement kinds

```
Horizontal: (φ, T_A) → (φ, T_B)     Δθ = 0   — same phase, different track
Vertical:   (φ, T)   → (φ+π/3, T)   Δθ = π/3 — same track, next phase
```

A diagonal move `(φ, T_A) → (φ+π/3, T_B)` decomposes into an ordered pair of primitive steps.
The decomposition order is architecturally meaningful.

### Fold ≠ Xul

```
Fold  — compress working context of track T into bounded intermediate C_T.
        Changes s: 1 → 0.  Does not touch B.
Xul   — compare-and-commit ΔB.
        The only operation that may produce F_h → F_h'.
```

A fold may occur at any phase. Xul is one phase in the cycle. They are orthogonal operations.

### Fold/unfold binary oscillation

```
s = 0   folded / bounded   C_T exists; working context is compressed
s = 1   unfolded / expanded  N_τ(h, T) is in F_Q; working context is active
```

`s` oscillates independently of the C6 wheel.

### Canonical traversal primitive

```
Handle → unfold(T) → Neighborhood → Attention → fold(T) → C_T
```

Where:

- `unfold(T)` brings `N_τ(h, T)` into the active semantic field `F_Q`; state: `s → 1`
- `fold(T)` compresses what was learned into the bounded intermediate `C_T`; state: `s → 0`

Multi-track iterated form:

```
F_Q^(0) → unfold(T₁) → fold(T₁) → C_T₁ → unfold(T₂) → fold(T₂) → C_T₂ → …
```

### Micronaut composition as field coupling

Micronauts compose as horizontal field coupling at the same phase — not as an imperative call
stack. `F_Advisor ↔ F_Research ↔ F_Memory` are three tracks at the same φ, coupled by unfold/fold
pairs that share a common `F_Q` rather than invoking each other sequentially.

---

## Phase-Wheel Algebra

### Transition matrix P (cyclic C_6)

The phase pointer is a one-hot vector in C_6. P is a cyclic left-shift:

```
    ┌ 0 0 0 0 0 1 ┐
    │ 1 0 0 0 0 0 │
P = │ 0 1 0 0 0 0 │       P^6 = I
    │ 0 0 1 0 0 0 │
    │ 0 0 0 1 0 0 │
    └ 0 0 0 0 1 0 ┘
```

Encoding: `Pop=0, Wo=1, Yax=2, Sek=3, Ch'en=4, Xul=5`

P moves the phase pointer. It does not perform the semantic operation.

### Phase operators T_φ

Each phase owns a typed operator:

| Phase | Operator | Effect on X |
|-------|----------|-------------|
| Pop | T_Pop | X + observation (perceive input) |
| Wo | T_Wo | X + state / constraints (allocate, declare intent) |
| Yax | T_Yax | X + admitted neighborhood (filter B_τ[h,*] by Gate_Q) |
| Sek | T_Sek | X + execution result (compute A_h_active) |
| Ch'en | T_Ch'en | X + verified observation / certificate V |
| Xul | T_Xul | X' and optionally B_t → B_{t+1} |

Full K'UHUL cycle composition:

```
K = T_Xul ∘ T_Ch'en ∘ T_Sek ∘ T_Yax ∘ T_Wo ∘ T_Pop

X_{n+1} = K(X_n)
```

---

## Topological Invariance

During Pop through Ch'en, the incidence topology B_τ is never written:

```
∀ φ ≠ Xul :   T_φ(B) = B
```

Yax constructs an ephemeral admission mask over a read of B_τ. It never persists back:

```
m_h        = Gate_Q( B_τ[h,*], W_E )          (query-gated attention mask)
B_h_active = B_τ[h,*] ⊙ m_h                  (local scratch; not persisted)
```

Sek operates on that admitted structure:

```
A_h_active = B_h_active  W_E  B_h_active^T    (semantic activation matrix)
```

**Phase changes State. Ordinary phase execution does not silently change Topology.**

---

## Phase DLL Driver Lock (Audit / Enforce)

Runtime can declare per-phase driver bindings:

```
Pop.dll  Wo.dll  Yax.dll  Sek.dll  Chen.dll  Xul.dll
```

Contract location:

- Driver policy: `data/schema/fold.runtime.json#phase_driver_lock`
- Driver hash anchors: `data/schema/kuhul.fold-runtime.json#phase_driver_sha256`

Lock modes:

- `audit` (default): report missing/pending/mismatched drivers, continue execution.
- `enforce`: deny phase execution when required DLL/hash verification fails.

This preserves authority order: K'UHUL phase algebra remains the semantic authority, while
DLLs are bounded runtime adapters whose integrity is checked before they can gate execution.

---

## Projection: Continuous Phase as Downstream View

Execution authority lives in discrete C_6. The continuous S^1 circle is a derived view:

```
φ_t ∈ C_6  ──Π_θ──▶  θ(t) = (π/3)·φ_t ∈ S^1  ──Π_shader──▶  HLSL / SVG-3D
```

Arc weight in shader space: `w_ij^phase = cos(θ_j − θ_i)`

K'UHUL executes in the discrete algebra. SVG-3D / HLSL projects that execution into the
continuous render field.

---

## Ch'en: Assertion-Producing Verifier

Ch'en determines whether a proposed topology delta satisfies declared invariants.
**It does not authorize. Xul owns the commit decision.**

```
T_Ch'en : (X_candidate, ΔB, Q, A) → (X_verified, V)
```

### Assertion contract

```
A = A_struct ∧ A_semantic ∧ A_state ∧ A_authority
```

| Class | Checks |
|-------|--------|
| STRUCTURAL | ΔB well-formed; referenced nodes/edges exist; types legal; immutable topology untouched |
| SEMANTIC | proposed relation has declared type; evidence supports it; W_E is pattern-strength only, not truth |
| STATE | tensor contract satisfied; values in bounds; no NaN; claimed observations present in Ch'en inputs |
| AUTHORITY | evolution explicitly requested; operator has evolve capability; provenance exists; scope permitted |

Each assertion returns a typed tuple:

```
a_i → ( status, evidence, expected, observed, reason )

status ∈ { PASS, FAIL, UNKNOWN }
```

`UNKNOWN` must never become `PASS`. Missing evidence is not a pass.

```
Verified(V) ⟺ ∀ a_i ∈ Required(A), status(a_i) = PASS
```

### Resolution classification (MISS is not WRONG)

For answer/evidence outcomes, Ch'en should classify resolution state before commit routing:

```
VERIFIED
CONTRADICTED
MISS
AMBIGUOUS
INSUFFICIENT_CONTEXT
INVALID
```

Key rule:

```
MISS != WRONG
```

Meaning:

- `CONTRADICTED` means the system asserted A despite evidence supporting ¬A.
- `MISS` means required context was not sufficiently unfolded to justify collapse.

Recommended transitions:

- VERIFIED → Xul
- CONTRADICTED → Revise
- MISS / INSUFFICIENT_CONTEXT → Acquire/Unfold then re-run Yax→Sek→Ch'en
- AMBIGUOUS → Branch/Disambiguate
- INVALID → Reject

Formal deficit model:

```
MISS(Q) = RequiredContext(Q) - ResolvedContext(F_Q)
```

Research pressure:

```
rho_research = M * I * U
```

Where `M` is missing-context magnitude, `I` is importance to the requested conclusion,
and `U` is uncertainty. High `rho_research` means acquire evidence before collapse.

Miss-kind routing examples:

- semantic miss → unfold additional Book/fold
- topology miss → graph/incidence query
- memory miss → recall trace
- factual miss → RAG/external research
- mathematical miss → deterministic MathML/executor path
- capability miss → delegate Micronaut
- evidence miss → source acquisition/provenance verification

### Verification certificate V

```json
{
  "@kind": "chen.verification",
  "subject": "fold://REASON-µ",
  "topology_version": 41,
  "topology_hash": "sha256:...",
  "candidate_delta": "ΔB:8f23...",
  "assertions": [
    { "id": "incidence.type.valid",    "status": "PASS" },
    { "id": "evidence.supports.edge",  "status": "PASS" },
    { "id": "evolve.scope.authorized", "status": "PASS" }
  ],
  "verified": true,
  "rewrite_authorized": false
}
```

`verified ≠ committed`. Ch'en never sets `rewrite_authorized = true`. That is XCFE's decision.

The certificate carries `topology_hash` so Xul can detect stale certificates:

```
hash(B_current) must equal V.topology_hash before Xul applies ΔB
```

---

## Xul: Compare-and-Commit

```
T_Xul(B_t, ΔB, V) =
  B_t ⊕ ΔB   if Verified(V) ∧ Authorized(V) ∧ Fresh(V)
  B_t         otherwise
```

### Transaction flow

```
Sek
 │ proposes ΔB
 ▼
Ch'en
 │ ASSERT ΔB → certificate V
 ├── FAIL   → reject (B_t unchanged, X rolled back)
 └── PASS ──▶ XCFE (authority check)
               │
               ├── deny  → reject → B_t
               └── grant ──▶ Xul
                              │
                              ├── stale certificate → reject → B_t
                              └── Fresh(V) → B_t ⊕ ΔB = B_{t+1}
```

Phase responsibilities:

```
Sek    = propose
Ch'en  = prove / assert
XCFE   = authorize
Xul    = commit / collapse
```

### Topological invariant — strongest form

```
B_{t+1} ≠ B_t  ⟹  ∃V : Verified(V) ∧ Authorized(V) ∧ Fresh(V) ∧ Commit_Xul(V)
```

No Fold topology may change unless:
- Ch'en has proven the proposed delta (all Required assertions PASS)
- XCFE has authorized it
- Xul commits that exact delta against the topology version that was verified

### Application: KNOWLED-1 semantic evolution

The ΔZ = ΔG_semantic ⊕ ΔC_earned formulation produces two separately asserted deltas:

```
V_G = Ch'en(ΔG_semantic)
V_C = Ch'en(ΔC_earned)
```

A bad answer can legitimately produce `ΔC ≠ 0, ΔG = 0` — negative feedback does not
force a topology rewrite. Only when verified evidence supports a new semantic relationship
does `ΔG ≠ 0`.

---

## Summary

```
Ch'en = observation elevated into an assertion certificate
Xul   = the only point at which verified possibility collapses into persistent reality
```

For the full algebraic grounding of the incidence model, typed projections, and Fold ADT,
see `docs/SEMANTIC_FOLDS.md`.
