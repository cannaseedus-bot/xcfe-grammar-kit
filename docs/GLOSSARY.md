# KHANARY Glossary — Fold, Tuple, Arity, Phase

Human-readable authority for the fold / phase vocabulary. The **machine-readable**
authority is the `formal_model` exposed by the MCP surface
(`src/io/mcp_server.cpp`), and
`tools/grammar/mini_transpilers.py fold-geometry-check` asserts that this
document, those strings, and the executable implementation
(`src/config.h`, `src/kuhul/phase_engine.h`, `src/kuhul/fold_engine.*`,
`src/world/field_dag.cpp`) still agree. Change one, and the check fails until the
others follow.

## The durable law

> **Fold is structure; `SHF(h,τ)` is its phase-indexed state; K'UHUL phases operate over it.**

## Terms

### Fold — structure (not a tuple)

```
Fold:{contexts, relations, nodes} -> h
```

A bounded semantic context around a handle `h`, built from three inputs
(contexts, relations, nodes). The *operation* has declared arity 3. A fold is
structure; it is not itself a tuple.

### SHF — the phase-indexed state

```
SHF(h,τ) = <h, B_τ[h,*], N_τ(h), W_τ, State(h)>
```

A 5-tuple: the handle, the Book row for `h` at phase τ, the neighbourhood at τ,
the weights at τ, and the node state. Incidence is preserved by *indexing* it
with τ rather than flattening it. A tuple describes the ordered state a fold
carries — it is **not** the fold's whole semantics: the typed relations live in
the graph outside the tuple (a tuple `⟨A,B,C⟩` does not say
`A @requires B`, `B @supports C`).

### n-ary — arity of an operation/relation

`n-tuple` = n-part **state**; `n-ary` = n-part **operation/relation**.
`Fold` is 3-ary. `TRAVERSE` is 6-ary. Neither of those is the fold engine.

### FoldEngine — binary node fold + explicit phase

```
horizontal_fold(node_a, node_b, phase)   Δθ = 0,    weight = cos(0)   = 1.0   (no XCFE authorization)
vertical_fold(node, from_phase, to_phase) Δθ = π/3, weight = cos(π/3) = 0.5   (XCFE-gated)
```

`vertical_fold` accepts **exactly one** phase step; non-primitive steps are
rejected with `dist != 1`.

> **The engine's fold is binary + phase. It is not the semantic `Fold` arity.**
> Conflating the two is the vocabulary drift this glossary exists to prevent.

### Unfold — addressable members, bounded by construction

```
Unfold(h,τ) = { e | B_τ[h,e] ≠ 0 }
```

Boundedness is part of the **semantics**, not merely the implementation: cyclic
Books make unrestricted recursive expansion non-terminating. Operational
traversal is constrained by

```
TRAVERSE(start, relation_type, direction, depth, limit, predicate)
```

and the runtime resolves misses with `bounded_recursive_unfold_on_miss`.

### Phase — deterministic position on C6

`PHASE_COUNT = 6`, at angles `0, π/3, 2π/3, π, 4π/3, 5π/3` (`src/config.h`), with

```
arc_weight(from, to) = cos(angle(to) − angle(from))
```

| Step | Δθ | weight |
| --- | --- | --- |
| vertical (adjacent phase) | π/3 | cos(π/3) = 0.5 |
| horizontal (same phase) | 0 | cos(0) = 1.0 |

A K'UHUL phase is a **deterministic geometric/control position**. It is not a
Markov state.

### XCFE Node ISA — the class coordinate

An XCFE node separates four things that must not be collapsed:

> **Class ≠ Operator ≠ Phase** — and class is not the Relation ISA (`RelationFamily` is a separate ISA).

| coordinate | meaning |
| --- | --- |
| **class** | what kind of control responsibility the node has |
| **operator** | what control operation it requests |
| **flow_phase** | **where it appears** — where the construct was encountered |
| **authority_phase** | **where its semantics are governed** |

`authority_phase = CanonicalPhase(class)` — **always**. But

```
flow_phase ≠ authority_phase      is legal
```

and must never be forced equal: the construct is encountered in one phase and governed
in another.

The closed **14-class** set, with each class's canonical phase
(`src/kuhul/xcfe_node_class.h` is the single definition):

| class | canonical phase |
| --- | --- |
| CONDITION, ROUTING, CAUSAL, STRUCTURE | Wo |
| SELECTION | Yax |
| EXECUTION, CAPABILITY, TRANSFORM | Sek |
| VERIFICATION, CONSEQUENCE | Ch'en |
| COMMIT | Xul |
| MEMORY, NEGATION, KUHUL_RUNTIME | Pop |

Worked examples of the two coordinates legitimately differing:

```
if        class CONDITION    flow Pop   authority Wo
why       class CAUSAL       flow Yax   authority Wo
then      class CONSEQUENCE  flow Wo    authority Ch'en
```

There is deliberately **no `Policy` class**: operation authority is XCFE's own gate
(`authorize_phase_transition` + the `xcfe_required` edge flag), so a Policy class would
double-encode it. That is why class is **not the Relation ISA** — relations keep their own
`RelationFamily`, and authority keeps its own mechanism.

### The dispatch chain: class, micronaut class, executor

A Micronaut is a resolver/executor that XCFE **calls** — it is never an `XcfeNodeClass`.
There are two different meanings of "class", and they must stay distinct:

```
XCFE
  class: VERIFICATION          <- WHAT kind of control work is needed
  operator: VERIFY
  authority_phase: Ch'en       <- WHERE it is governed
        |
        +- requires: verification
               |
               v
        MicronautClass: VERIFY-µ   <- WHO is qualified (capability family)
               |
             Yax / W·C·R           <- WHICH µ instance gets it
               |
             selected µ
               |
        deterministic | shader/GPU | parser | database | tool | LLM   <- HOW it runs
```

> **XCFE class** says WHAT kind of control operation is needed.
> **Micronaut class** says WHO is qualified to resolve it.
> **W·C·R** says WHICH µ gets it.
> **Executor** says HOW it actually runs.
> **K'UHUL phase** says WHERE in the cycle it is governed.

A class is a capability family, not an individual micronaut: several µ instances compete
within one family, and eligibility is

```
Eligible_µ(N) = { µ | capabilities(µ) ⊇ requirements(N) }
µ* = argmax over Eligible_µ of  W_µ C_µ R_µ(Q)
```

`XcfeNodeClass` names a *kind of control work*; a micronaut/track id names a *specialist*.
The drift guard fails if any node class collides with a micronaut id, and reports any
requirement that no track's capabilities can satisfy.

The Ω gate turns the entropy measurement into a **blocking observation**:

```
|Eligible| = 0                    -> MISS_capability          -> Factory
|Eligible| > 1 and Ĥ_route > 0.90 -> MISS_ambiguity           -> research/unfold -> requery
                                     MISS_ambiguity_exhausted -> unresolved (never a commit)
```

Entropy is **not a seventh phase, a node class, or an authority class**. It is an observer
over runtime state — `Entropy: F_τ → ℝ`, not `F_τ → F_{τ+1}`. It can refuse premature
collapse, and it can never grant it. The retry/evidence budget is bounded
(`kAmbiguityMaxAttempts`) and exhaustion stays explicitly unresolved rather than bypassing
the gate and committing anyway.

An operator record keeps **two separate namespaces**:

```json
{"operator": "VERIFY", "class": "VERIFICATION",
 "role": "verification-gate", "requires": ["VERIFY"],
 "flow_phase": "Ch'en", "authority_phase": "Ch'en"}
```

| field | job |
| --- | --- |
| `class` | WHAT kind of XCFE node |
| `role` | WHAT semantic/control role it plays (descriptive metadata) |
| `requires` | WHAT capabilities execution requires (capability ids) |
| `flow_phase` | WHERE encountered/activated |
| `authority_phase` | WHERE governed |

> **role ≠ requires.** Eligibility SHALL be derived only from explicit
> `requires(op)` versus declared `capabilities(µ)`:

```
Eligible_µ(op) = { µ | requires(op) ⊆ capabilities(µ) }
```

Semantic similarity may *recommend*; capability declarations *authorize* eligibility;
W-C-R ranks only within the authorized candidate set. A `requires` of `[]` means the
operator lowers to a native deterministic XCFE op and needs no Micronaut at all —
inventing one merely to satisfy the architecture would make the system less deterministic.

> **XCFE does not call a Micronaut by name. XCFE declares required control capability;
> eligible Micronauts satisfy that contract; W-C-R chooses among them; the selected
> Micronaut chooses or invokes the executor under XCFE authority.**

Eligibility gates *before* scoring (`request_router.cpp` admits a track to the candidate
set only when `evaluate_eligibility(...).capable`), and an empty eligible set is the
correct, named outcome:

```
W·C·R never promotes an unqualified specialist.
Eligible_µ(op) = ∅  ->  capability_miss = true, dispatch_block_reason = "MISS_capability"
                        status = "miss_capability"
```

Capability gaps are recorded as a formal artifact
(`data/grammar/control-capability-gaps.jsonl`, generated by
`mini_transpilers.py capability-gap`) with

```
Gap(op) = Req(op) − ⋃_µ Cap(µ)
```

and a `resolution` per gap: `satisfied`, `deterministic-executor` (the lowering is a
native XCFE op, so no specialist is needed), or `micronaut-required`. `advisory_near_tracks`
is a **hint for a human decision** (promote a capability vs. add a Micronaut vs. correct
the requirement declaration) and never a routing decision.

### PH model — measurement, not definition

A phase-type distribution `PH(α,S)` is the time-to-absorption of a continuous-time
Markov process with *m* transient phases and one absorbing state. It is an
**optional observational model over timestamped runtime events**:

```
K'UHUL execution → timestamped journal → latency observations → PH(α,S)
```

It would answer *"how long does a candidate stay unresolved before a terminal
outcome?"* — never *"what is a phase?"*.

Status: **not applicable yet.** Journal events record
`{channel, seq, key, value, phase, authority}` with **no timestamp**, so `α`/`S`
cannot be fitted. PH is also light-tailed, so commit latency would likely need a
Coxian chain with many phases.

### Commit model (adjacent to PH — not PH)

The confidence contract is `Ch'en_pending` → `Xul_commit`, with
`BAD_ANSWER` / `GOOD_ANSWER`
(`data/manifest/micronaut.manifest.json`). Absorption here is a **gated
decision** — feedback-gated and XCFE-authorized — not a probabilistic absorption.

## Authority hierarchy

```
GLOSSARY.md             human semantic contract        (this file)
      ↓
formal-model strings    machine-readable definition    (src/io/mcp_server.cpp)
      ↓
fold-geometry-check     invariant / drift verification (tools/grammar/mini_transpilers.py)
      ↓
phase_engine · fold_engine · FieldDag · config.h       executable implementation
      ↓
tests                   KHANARY_*_TEST
```

```powershell
py -3 tools\grammar\mini_transpilers.py fold-geometry-check
```
