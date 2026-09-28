# K'UHUL XCFE Grammar Kit

Executable XCFE control-flow grammar kit for the KHANARY / K'UHUL semantic runtime stack.

This kit preserves KHANARY.CPP-relative paths so the XCFE validators can run directly. The core contract is: English-like control words lower to explicit XCFE nodes, XCFE node classes say **what** control work is needed, micronaut tracks say **who** is qualified, and Xul remains the only topology commit boundary.

## Files

| File | Description |
|------|-------------|
| `data/grammar/key-grammar-json.runtime.ebnf` | Key-grammar EBNF containing the XCFE control-flow row, plan, sugar, and lowering productions. |
| `data/grammar/control-flow.key-grammar.jsonl` | One row per control word/operator with phase, authority, XCFE op, arity, role, and lowering metadata. |
| `data/grammar/control-capability-gaps.jsonl` | Deterministic capability-gap artifact derived from control rows and track capabilities. |
| `data/schema/micronaut-track*.schema.json` | Track schema/type contracts used by the capability surface. |
| `data/tracks/*.json` | Micronaut/track capability specimens used by the gap checker. |
| `src/micronaut/control_flow.*` | Native authority for control operator planning and XCFE lowering. |
| `src/kuhul/xcfe_executor.*` | Executable XCFE op library and native control-key dispatch. |
| `src/kuhul/xcfe_node_class.h` | Closed XCFE node-class ISA and canonical authority-phase mapping. |
| `src/kuhul/xul_gate.h` | Commit boundary reference: verified/authorized deltas commit only through Xul. |
| `src/io/mcp_server.*` | Formal-model advertisement surface checked against executor registration. |
| `src/io/request_router.*` | Eligibility-ordering guard: entropy may recommend but may not authorize. |
| `src/tests/test_control_flow.cpp` | Native control-flow regression specimen. |
| `tools/grammar/mini_transpilers.py` | Validator/transpiler with `control-check` and `capability-gap --check`. |
| `tools/check_xcfe_kit.py` | Kit-local wrapper that runs the XCFE validators. |
| `docs/GLOSSARY.md`, `docs/CONFORMANCE.md`, `docs/KHANARY-CPP-SPEC.md` | Human-readable XCFE/conformance references used by the checks and reviewers. |

## Validate

```powershell
python tools/check_xcfe_kit.py
```

Expected result includes:

```text
PASS | control-check
PASS | capability-gap: control-capability-gaps.jsonl matches 27 operators, 0 gap(s)
```

## Invariant

```text
XCFE NodeClass = what control work is needed
Micronaut track capability = who is qualified
Entropy/relevance = recommendation only
XulGate = only topology commit boundary
```

## Part of

[KHANARY.CPP](https://github.com/cannaseedus-bot) — K'UHUL semantic runtime stack.
