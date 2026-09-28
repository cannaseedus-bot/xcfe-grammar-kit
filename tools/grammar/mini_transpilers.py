#!/usr/bin/env python3
"""
KHANARY key-grammar mini transpilers.

 Six small, dependency-free transpilers over the key-grammar surfaces:

  ebnf-sync            Extract the runtime EBNF strings (kAimlEbnf,
                       kKeyGrammarJsonEbnf) from src/main.cpp and regenerate
                       data/grammar/*.runtime-grammar.ebnf so the checked-in
                       artifacts always match the runtime emitter.

  greeting-bank-compile
                       Compile data/grammar/greeting-bank.json (the
                       phase-supplied greeting bank) into:
                         - data/alice/greetings.aiml  (AIML categories so the
                           bank is queryable through the AIML/mx2lm surface)
                         - data/grammar/greeting-bank.key-grammar.jsonl
                           (typed gram-channel rows per template, computed
                           with the same taxonomy semantics as
                           src/micronaut/mx2lm-ngram.cpp)

  advisor-bootstrap-compile
                       Compile data/grammar/advisor-bootstrap.json into the
                       generated request-to-expert AIML projection and typed
                       grammar rows. The JSON contract is authoritative.

  advisor-check         Replay generated advisor AIML and verify route,
                       expert, card, and ADVISOR-µ track bindings.

  plan-bootstrap-compile
                       Compile data/grammar/plan-bootstrap.json into plan.aiml
                       and typed planning grammar rows.

  plan-check             Replay generated plan AIML and verify PLAN-µ/task-card
                       bindings.

  validate             Validate key-grammar JSONL rows against the v2 EBNF
                       contract: required fields, gram classes, roles, phase
                       bindings, widths, positions.

  gyro-align           Check gyroscope/track/field alignment: every gyro
                       measurement track exists as a semantic track, every
                       track has a field.xml node, and report which track
                       governs words are not covered by the gyro book's
                       measurement vocabulary.

  greetings-check      Replay the generated data/alice/greetings.aiml through
                       the AIML pattern-matching semantics (wildcard capture,
                       normalization, first-match-wins ordering) and assert the
                       phase queries resolve to the intended kind — including
                       that Pop.reactive_mirror / Yax.synthesized_greeting are
                       not shadowed by their Pop/Yax parents and that the
                       generic hint category is the last resort.

  control-check        Validate the control-flow grammar: the EBNF contract's
                       declared control vocabulary (control_word, control_phase,
                       control_xcfe_op, control_block_role, control_arity) and
                       the runtime-emitted control-flow.key-grammar.jsonl must
                       agree — every operator row satisfies the contract bounds,
                       and the artifact covers exactly the declared vocabulary.
                       Also asserts this file's CONTROL_RULES mirror (used for
                       coarse-symbol expectations) equals the declared
                       vocabulary, so the Python side cannot silently drift from
                       the C++ operator authority.

  fold-geometry-check  Assert the fold/phase invariants promised by
                       docs/GLOSSARY.md against the implementation: PHASE_COUNT=6,
                       C6 angles 0,π/3,…,5π/3, arc_weight = cos(Δθ), vertical
                       Δθ=π/3 with weight 0.5, horizontal Δθ=0 with weight 1.0,
                       the engine fold staying binary + phase (3 parameters, not
                       the semantic Fold arity), and the canonical Fold / SHF /
                       Unfold / bounded TRAVERSE formal-model strings. Fails when
                       the glossary, the machine-readable definitions, and
                       phase_engine/fold_engine/FieldDag disagree.

Usage:
  py -3 tools\\grammar\\mini_transpilers.py <command> [--repo-root <path>]

All paths default relative to the repository root (parent of tools/).
"""

import argparse
import json
import math
import re
import subprocess
import sys
import tempfile

# CTest may launch Python with the Windows cp1252 console encoding. Contract
# diagnostics contain K-UHUL symbols, so make stdout deterministic UTF-8.
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")
import xml.etree.ElementTree as ET
from pathlib import Path

# ── Gram channel contract (mirrors record_prompt_gram_channels + the v2 EBNF) ──

GRAM_CLASSES = ["unigram", "bigram", "trigram", "quadgram",
                "coarse_gram", "function_gram", "tool_gram"]

GRAM_ROLES = ["semantic_address", "context_association", "structural_shape",
              "function_word_structure", "tool_dispatch_intent"]

PHASES = ["Pop", "Wo", "Yax", "Sek", "Ch'en", "Xul"]

CHANNEL_CONTRACT = {
    # channel      role                      phase   width-from
    "unigram":     ("semantic_address",       "Pop",  1),
    "bigram":      ("context_association",    "Wo",   2),
    "trigram":     ("context_association",    "Wo",   3),
    "quadgram":    ("context_association",    "Wo",   4),
    "coarse_gram": ("structural_shape",       "Yax",  None),
    "function_gram": ("function_word_structure", "Yax", None),
    "tool_gram":   ("tool_dispatch_intent",   "Sek",  None),
}

CONTROL_RULES = {
    # Mirrors src/micronaut/control_flow.cpp (control_operators()). control-check
    # asserts this set equals the EBNF-declared control_word vocabulary, so a
    # change on the C++ side cannot silently drift from this mirror.
    "if", "else", "then", "when", "how", "who", "why", "for",
    "from", "as", "equals", "where", "is", "are", "equal",
    "and", "or", "not", "because", "requires", "depends",
    "evidence", "shows", "proves", "verify", "check", "assert",
}

FUNCTION_WORDS = {
    "a", "an", "the", "this", "that", "these", "those",
    "i", "you", "he", "she", "it", "we", "they", "me", "him", "her", "us", "them",
    "my", "your", "his", "its", "our", "their", "mine", "yours", "ours", "theirs",
    "is", "am", "are", "was", "were", "be", "being", "been",
    "do", "does", "did", "can", "could", "will", "would", "shall", "should", "may", "might", "must",
    "have", "has", "had",
    "to", "of", "in", "on", "at", "by", "for", "with", "without", "from", "into", "over", "under", "through", "across", "about", "as",
    "and", "or", "but", "nor", "so", "yet",
    "if", "then", "than", "because", "while", "when", "where", "who", "whom", "whose", "which", "what", "why", "how",
    "not", "no", "yes",
}

TOOL_TOKENS = {
    "tool", "tools", "function", "functions", "call", "invoke", "run", "execute", "command", "commands",
    "mcp", "api", "rpc", "plugin", "route", "router", "delegate", "dispatch", "plan",
    "search", "research", "lookup", "query", "fetch", "scrape", "read", "write",
    "build", "compile", "test", "lint", "deploy", "convert", "quantize",
    "web", "file", "shell", "terminal", "runtime",
}

TOOL_ACTION_TOKENS = {
    "call", "invoke", "run", "execute", "dispatch", "delegate", "search", "research",
    "lookup", "query", "fetch", "build", "compile", "test", "lint", "deploy",
    "convert", "quantize", "read", "write", "plan",
}

TOOL_TARGET_TOKENS = {
    "tool", "tools", "function", "functions", "command", "commands", "api", "mcp",
    "plugin", "route", "router", "web", "file", "shell", "terminal", "runtime",
    "model", "grammar", "dataset",
}

TOP_PER_CHANNEL = 12


# ── C++-faithful tokenizer (Mx2LmNGram::tokenize) ───────────────────────────

def tokenize(text):
    """Lowercase tokens of ASCII alnum + apostrophe + underscore.

    Mirrors src/micronaut/mx2lm-ngram.cpp: is_token_char = isalnum || '\'' ||
    '_'; everything else (including UTF-8 continuation bytes) separates.
    """
    out = []
    cur = []
    for ch in text:
        if ch.isascii() and (ch.isalnum() or ch in ("'", "_")):
            cur.append(ch.lower())
        elif cur:
            out.append("".join(cur))
            cur = []
    if cur:
        out.append("".join(cur))
    return out


def count_ngrams(tokens, width):
    freq = {}
    if width == 0 or len(tokens) < width:
        return freq
    for i in range(len(tokens) - width + 1):
        key = " ".join(tokens[i:i + width])
        freq[key] = freq.get(key, 0) + 1
    return freq


def coarse_symbol_for_token(tok):
    t = tok.lower()
    if not t:
        return "c"
    if t in CONTROL_RULES:
        return "ctrl:" + t
    if t in FUNCTION_WORDS:
        return "f:" + t
    if t in TOOL_TOKENS:
        return "t:" + t
    return "c"


def classify_gram_taxonomy(text):
    """Mirror Mx2LmNGram::classify_gram_taxonomy(tokenize(text))."""
    norm = [t.lower() for t in tokenize(text) if t]
    out = {c: {} for c in GRAM_CLASSES}
    if not norm:
        return out

    for tok in norm:
        out["unigram"][tok] = out["unigram"].get(tok, 0) + 1

    for width, channel in ((2, "bigram"), (3, "trigram"), (4, "quadgram")):
        for gram, count in count_ngrams(norm, width).items():
            out[channel][gram] = out[channel].get(gram, 0) + count

    coarse_seq = [coarse_symbol_for_token(t) for t in norm]
    for width in (2, 3, 4):
        for gram, count in count_ngrams(coarse_seq, width).items():
            out["coarse_gram"][gram] = out["coarse_gram"].get(gram, 0) + count

    for n in (1, 2, 3):
        if len(norm) < n:
            break
        for i in range(len(norm) - n + 1):
            window = norm[i:i + n]
            if all(w in FUNCTION_WORDS for w in window):
                gram = " ".join(window)
                out["function_gram"][gram] = out["function_gram"].get(gram, 0) + 1

    for n in (1, 2, 3, 4):
        if len(norm) < n:
            break
        for i in range(len(norm) - n + 1):
            window = norm[i:i + n]
            has_tool = any(w in TOOL_TOKENS for w in window)
            has_action = any(w in TOOL_ACTION_TOKENS for w in window)
            has_target = any(w in TOOL_TARGET_TOKENS for w in window)
            if not has_tool:
                continue
            if n > 1 and not (has_action and has_target):
                continue
            gram = " ".join(window)
            out["tool_gram"][gram] = out["tool_gram"].get(gram, 0) + 1

    return out


def top_ranked(freq, limit=TOP_PER_CHANNEL):
    ranked = sorted(freq.items(), key=lambda kv: (-kv[1], kv[0]))
    return ranked[:limit]


def first_position(gram_tokens, all_tokens):
    """First-occurrence index (0-based) of the gram in the token stream."""
    width = len(gram_tokens)
    if width == 0 or not all_tokens:
        return None
    for i in range(len(all_tokens) - width + 1):
        if all_tokens[i:i + width] == gram_tokens:
            return i
    return None


def typed_gram_channels(text, provenance):
    """Build the gram_channels object with the v2 structured fields.

    Mirrors compile_dictionary_key_grammar_files push_channel(): gram, count,
    ordered slot width, role, position (order evidence, lexical channels
    only), phase binding, provenance.
    """
    tokens = tokenize(text.lower())
    taxonomy = classify_gram_taxonomy(text)
    channels = {}
    for channel, (role, phase, _) in CHANNEL_CONTRACT.items():
        ranked = top_ranked(taxonomy[channel])
        if not ranked:
            continue
        rows = []
        for gram, count in ranked:
            gram_tokens = gram.split(" ")
            row = {
                "gram": gram,
                "count": count,
                "width": len(gram_tokens),
                "role": role,
            }
            if channel != "coarse_gram":
                pos = first_position(gram_tokens, tokens)
                if pos is not None:
                    row["position"] = pos
            row["phase"] = phase
            row["provenance"] = provenance
            rows.append(row)
        channels[channel] = rows
    return channels


# ── ebnf-sync ────────────────────────────────────────────────────────────────

EBNF_RAW_RE = re.compile(r'R"EBNF\((.*?)\)EBNF"', re.DOTALL)


def cmd_ebnf_sync(repo_root):
    main_cpp = repo_root / "src" / "main.cpp"
    grammar_dir = repo_root / "data" / "grammar"
    text = main_cpp.read_text(encoding="utf-8")
    matches = EBNF_RAW_RE.findall(text)
    if len(matches) < 2:
        print(f"FAIL: expected >= 2 R\"EBNF(...)EBNF\" literals in {main_cpp}, found {len(matches)}")
        return 1
    # The aiml spec is the first literal; the key-grammar spec may be split across
    # several adjacent literals (the C++ compiler caps a single string literal at
    # 16384 bytes, and this spec is larger than that), so join the remainder.
    aiml_ebnf = matches[0]
    key_grammar_ebnf = "".join(matches[1:])
    targets = [
        (grammar_dir / "aiml.runtime-grammar.ebnf", aiml_ebnf),
        (grammar_dir / "key-grammar-json.runtime.ebnf", key_grammar_ebnf),
    ]
    for path, content in targets:
        # The runtime writes the raw literal plus a trailing newline.
        path.write_text(content + "\n", encoding="utf-8", newline="\n")
        print(f"PASS | wrote {path.relative_to(repo_root)} ({len(content.splitlines())} lines)")
    return 0


# ── greeting-bank-compile ────────────────────────────────────────────────────

def aiml_normalize_pattern(text):
    """Mirror AIMLEngine::normalize(): uppercase, punctuation → space."""
    out = []
    in_ws = False
    for ch in text:
        if ch.isascii() and (ch.isalnum() or ch in "*_"):
            out.append(ch.upper())
            in_ws = False
        else:
            if not in_ws:
                out.append(" ")
                in_ws = True
    return "".join(out).strip()


def cmd_greeting_bank_compile(repo_root):
    bank_path = repo_root / "data" / "grammar" / "greeting-bank.json"
    bank = json.loads(bank_path.read_text(encoding="utf-8"))
    if bank.get("schema") != "key.grammar.greeting_bank.v1":
        print(f"FAIL: unexpected greeting bank schema: {bank.get('schema')!r}")
        return 1
    entries = bank.get("greetings", [])

    # ── AIML categories ────────────────────────────────────────────────────
    lines = [
        "<aiml version=\"1.0.1\">",
        "  <!-- GENERATED by tools/grammar/mini_transpilers.py greeting-bank-compile",
        "       from data/grammar/greeting-bank.json — do not edit by hand;",
        "       re-run the transpiler after changing the bank. -->",
        "  <category>",
        "    <pattern>GREETING BANK</pattern>",
        "    <template><mx2lm>greeting bank phases Pop Wo Yax Sek Ch'en Xul kinds "
        "canonical learned associative status confirmatory recall reactionary "
        "synthesized</mx2lm></template>",
        "  </category>",
    ]
    for entry in sorted(
            entries,
            key=lambda e: -len(aiml_normalize_pattern(
                "GREETING " + e.get("phase", "") + " *").split())):
        phase = entry.get("phase", "")
        kind = entry.get("kind", "")
        pattern = aiml_normalize_pattern("GREETING " + phase + " *")
        lines.append("  <category>")
        lines.append(f"    <pattern>{pattern}</pattern>")
        lines.append(
            f"    <template><mx2lm>greeting phase={phase} kind={kind} "
            f"bank=greeting_bank <star/></mx2lm></template>")
        lines.append("  </category>")
    lines.append("  <category>")
    lines.append("    <pattern>GREETING *</pattern>")
    lines.append(
        "    <template><mx2lm>greeting bank=greeting_bank hint GREETING BANK "
        "for phases <star/></mx2lm></template>")
    lines.append("  </category>")
    lines.append("</aiml>")
    aiml_path = repo_root / "data" / "alice" / "greetings.aiml"
    aiml_path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"PASS | wrote {aiml_path.relative_to(repo_root)} ({len(entries) + 2} categories)")

    # ── Key-grammar JSONL rows ──────────────────────────────────────────────
    jsonl_path = repo_root / "data" / "grammar" / "greeting-bank.key-grammar.jsonl"
    rows = 0
    with jsonl_path.open("w", encoding="utf-8", newline="\n") as f:
        for entry in entries:
            phase = entry.get("phase", "")
            kind = entry.get("kind", "")
            for idx, template in enumerate(entry.get("templates", []), start=1):
                row = {
                    "word": f"{phase.lower()}-{idx}",
                    "key": f"GREETING:{phase.upper()}:{idx}",
                    "unit": "unigram",
                    "class": "GREETING",
                    "book": "Book(GREETING)",
                    "definition": template,
                    "lexicon_source": "data/grammar/greeting-bank.json",
                    "gram_channels": typed_gram_channels(
                        template, "greeting_bank_template"),
                }
                f.write(json.dumps(row, ensure_ascii=False, separators=(",", ":")) + "\n")
                rows += 1
    print(f"PASS | wrote {jsonl_path.relative_to(repo_root)} ({rows} rows)")
    return 0


# ── advisor-bootstrap-compile / advisor-check ────────────────────────────────

def cmd_advisor_bootstrap_compile(repo_root):
    source_path = repo_root / "data" / "grammar" / "advisor-bootstrap.json"
    source = json.loads(source_path.read_text(encoding="utf-8"))
    if source.get("schema") != "key.grammar.advisor_bootstrap.v1":
        print(f"FAIL: unexpected advisor schema: {source.get('schema')!r}")
        return 1
    routes = source.get("routes", [])
    if not routes or source.get("track") != "ADVISOR-µ":
        print("FAIL: advisor source must declare ADVISOR-µ and at least one route")
        return 1

    lines = [
        "<aiml version=\"1.0.1\">",
        "  <!-- GENERATED by tools/grammar/mini_transpilers.py advisor-bootstrap-compile",
        "       from data/grammar/advisor-bootstrap.json — do not edit by hand;",
        "       rerun the transpiler after changing the authored advisor contract. -->",
        "  <category>",
        "    <pattern>ADVISOR BANK</pattern>",
        "    <template><mx2lm>advisor track=ADVISOR-µ phase=Yax card_view=true authority=none fields=task,target,evidence,gaps,proposal,authority,next</mx2lm></template>",
        "  </category>",
        "  <category>",
        "    <pattern>SHOW EXPERT CARDS</pattern>",
        "    <template><mx2lm>tool-u op=list phase=Pop card_view=true category=expert</mx2lm></template>",
        "  </category>",
    ]
    # AIML is first-match-wins; specific routes must precede ADVISE *.
    for route in sorted(routes, key=lambda r: -len(aiml_normalize_pattern(r["pattern"]).split())):
        lines.extend([
            "  <category>",
            f"    <pattern>{aiml_normalize_pattern(route['pattern'])}</pattern>",
            f"    <template><mx2lm>{route['prefix']} advisor=ADVISOR-µ expert={route['expert']} phase={route['phase']} next={route['next']} card_view=true authority=none proposal=ready <star/></mx2lm></template>",
            "  </category>",
        ])
    lines.append("</aiml>")
    aiml_path = repo_root / "data" / "alice" / "advisor.aiml"
    aiml_path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"PASS | wrote {aiml_path.relative_to(repo_root)} ({len(routes) + 2} categories)")

    jsonl_path = repo_root / "data" / "grammar" / "advisor-bootstrap.key-grammar.jsonl"
    with jsonl_path.open("w", encoding="utf-8", newline="\n") as out:
        for index, route in enumerate(routes, start=1):
            definition = f"{route['pattern']} -> {route['expert']} ({route['phase']}->{route['next']})"
            row = {
                "word": f"advisor-{index}",
                "key": f"ADVISOR:{route['expert']}:{index}",
                "unit": "unigram",
                "class": "ADVISOR",
                "book": "Book(ADVISOR)",
                "definition": definition,
                "lexicon_source": "data/grammar/advisor-bootstrap.json",
                "gram_channels": typed_gram_channels(definition, "advisor_route"),
            }
            out.write(json.dumps(row, ensure_ascii=False, separators=(",", ":")) + "\n")
    print(f"PASS | wrote {jsonl_path.relative_to(repo_root)} ({len(routes)} rows)")
    return 0


def cmd_advisor_check(repo_root):
    source_path = repo_root / "data" / "grammar" / "advisor-bootstrap.json"
    aiml_path = repo_root / "data" / "alice" / "advisor.aiml"
    track_path = repo_root / "data" / "tracks" / "ADVISOR-u.semantic-tracks.v1.json"
    if not source_path.exists() or not aiml_path.exists() or not track_path.exists():
        print("FAIL | advisor-check: source, generated AIML, or ADVISOR-µ track missing")
        return 1
    source = json.loads(source_path.read_text(encoding="utf-8"))
    track = json.loads(track_path.read_text(encoding="utf-8"))
    try:
        ET.fromstring(aiml_path.read_text(encoding="utf-8"))
    except ET.ParseError as exc:
        print(f"FAIL | advisor-check: XML not well-formed: {exc}")
        return 1
    cats = _parse_aiml_categories(aiml_path.read_text(encoding="utf-8"))
    failures = 0
    if track.get("id") != source.get("track"):
        failures += 1
        print("FAIL | advisor-check: source/track id mismatch")
    if track.get("ui", {}).get("card") != source.get("card", {}).get("symbol"):
        failures += 1
        print("FAIL | advisor-check: source/track card mismatch")
    for route in source.get("routes", []):
        query = route["pattern"].replace("*", "example")
        got = _aiml_match(cats, query)
        wanted = f"expert={route['expert']}"
        if wanted not in got or "authority=none" not in got or "card_view=true" not in got:
            failures += 1
            print(f"FAIL | advisor-check: {route['pattern']} -> {got[:180]}")
        else:
            print(f"PASS | advisor route: {route['pattern']} -> {route['expert']}")
    total = len(source.get("routes", []))
    print(f"{'PASS' if not failures else 'FAIL'} | advisor-check: {total - failures}/{total} routes passed")
    return 1 if failures else 0


# ── plan-bootstrap-compile / plan-check ─────────────────────────────────────

def cmd_plan_bootstrap_compile(repo_root):
    source_path = repo_root / "data" / "grammar" / "plan-bootstrap.json"
    source = json.loads(source_path.read_text(encoding="utf-8"))
    if source.get("schema") != "key.grammar.plan_bootstrap.v1" or source.get("track") != "PLAN-µ":
        print("FAIL: plan source must declare key.grammar.plan_bootstrap.v1 and PLAN-µ")
        return 1
    routes = source.get("routes", [])
    if not routes:
        print("FAIL: plan source has no routes")
        return 1
    card = source.get("card", {})
    fields = ",".join(card.get("fields", []))
    verbs = ",".join(card.get("active_verbs", []))
    lines = [
        "<aiml version=\"1.0.1\">",
        "  <!-- GENERATED from data/grammar/plan-bootstrap.json; do not edit by hand. -->",
        "  <category>",
        "    <pattern>PLAN BANK</pattern>",
        f"    <template><mx2lm>plan: track=PLAN-µ phase=Wo card_view=true task_list=true authority=none fields={fields} verbs={verbs}</mx2lm></template>",
        "  </category>",
    ]
    for route in sorted(routes, key=lambda r: -len(aiml_normalize_pattern(r["pattern"]).split())):
        lines.extend([
            "  <category>",
            f"    <pattern>{aiml_normalize_pattern(route['pattern'])}</pattern>",
            f"    <template><mx2lm>plan: track=PLAN-µ phase={route.get('phase', 'Wo')} next={route.get('next', 'Yax')} card_view=true task_list=true authority=none verbs={verbs} goal=<star/></mx2lm></template>",
            "  </category>",
        ])
    lines.append("</aiml>")
    aiml_path = repo_root / "data" / "alice" / "plan.aiml"
    aiml_path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"PASS | wrote {aiml_path.relative_to(repo_root)} ({len(routes) + 1} categories)")
    jsonl_path = repo_root / "data" / "grammar" / "plan-bootstrap.key-grammar.jsonl"
    with jsonl_path.open("w", encoding="utf-8", newline="\n") as out:
        for index, route in enumerate(routes, start=1):
            definition = f"{route['pattern']} -> PLAN-µ ({route.get('phase', 'Wo')}->{route.get('next', 'Yax')})"
            out.write(json.dumps({
                "word": f"plan-{index}", "key": f"PLAN:{index}", "unit": "unigram",
                "class": "PLAN", "book": "Book(PLAN)", "definition": definition,
                "lexicon_source": "data/grammar/plan-bootstrap.json",
                "gram_channels": typed_gram_channels(definition, "plan_route"),
            }, ensure_ascii=False, separators=(",", ":")) + "\n")
    print(f"PASS | wrote {jsonl_path.relative_to(repo_root)} ({len(routes)} rows)")
    return 0


def cmd_plan_check(repo_root):
    source_path = repo_root / "data" / "grammar" / "plan-bootstrap.json"
    aiml_path = repo_root / "data" / "alice" / "plan.aiml"
    track_path = repo_root / "data" / "tracks" / "PLAN-u.semantic-tracks.v1.json"
    if not all(p.exists() for p in (source_path, aiml_path, track_path)):
        print("FAIL | plan-check: source, generated AIML, or PLAN-µ track missing")
        return 1
    source = json.loads(source_path.read_text(encoding="utf-8"))
    track = json.loads(track_path.read_text(encoding="utf-8"))
    try:
        ET.fromstring(aiml_path.read_text(encoding="utf-8"))
    except ET.ParseError as exc:
        print(f"FAIL | plan-check: XML not well-formed: {exc}")
        return 1
    failures = 0
    if track.get("id") != "PLAN-µ" or track.get("ui", {}).get("tasks_field") != "tasks":
        print("FAIL | plan-check: track/task-card declaration mismatch"); failures += 1
    cats = _parse_aiml_categories(aiml_path.read_text(encoding="utf-8"))
    for route in source.get("routes", []):
        got = _aiml_match(cats, route["pattern"].replace("*", "example"))
        if "track=PLAN-µ" not in got or "task_list=true" not in got or "authority=none" not in got:
            print(f"FAIL | plan route: {route['pattern']} -> {got[:180]}"); failures += 1
        else:
            print(f"PASS | plan route: {route['pattern']}")
    total = len(source.get("routes", []))
    print(f"{'PASS' if not failures else 'FAIL'} | plan-check: {total - failures}/{total} routes passed")
    return 1 if failures else 0


# ── validate ─────────────────────────────────────────────────────────────────

def validate_lexicon_row(row, path, lineno, errors, warnings):
    where = f"{path}:{lineno}"
    for field_name in ("word", "key", "unit", "class", "book", "definition",
                       "lexicon_source"):
        if field_name not in row:
            errors.append(f"{where}: lexicon_row missing required field {field_name!r}")
    if row.get("unit") != "unigram":
        errors.append(f"{where}: lexicon_row unit must be \"unigram\"")
    channels = row.get("gram_channels", {})
    if not isinstance(channels, dict):
        errors.append(f"{where}: gram_channels must be an object")
        return
    for channel, grams in channels.items():
        if channel not in GRAM_CLASSES:
            errors.append(f"{where}: unknown gram_class {channel!r}")
            continue
        if not isinstance(grams, list):
            errors.append(f"{where}: channel {channel!r} must be an array")
            continue
        for g in grams:
            if not isinstance(g, dict):
                errors.append(f"{where}: {channel} gram must be an object")
                continue
            if "gram" not in g or "count" not in g or "width" not in g or "role" not in g:
                errors.append(f"{where}: {channel} typed_gram missing required fields")
                continue
            if g["role"] not in GRAM_ROLES:
                errors.append(f"{where}: {channel} typed_gram bad role {g['role']!r}")
            # Ordered slot width: coarse symbols (c / f:x / ctrl:x / t:x) each
            # count as one structural slot.
            slots = len(str(g["gram"]).split(" "))
            width = g["width"]
            if width != slots:
                legacy = len(str(g["gram"]).replace(":", " ").split())
                if channel == "coarse_gram" and width == legacy:
                    warnings.append(
                        f"{where}: coarse_gram typed_gram width {width} is the legacy "
                        f"colon-split count (contract slot width is {slots}); regenerate "
                        f"with --build-lexicon-grammar to refresh")
                else:
                    errors.append(
                        f"{where}: {channel} typed_gram width {width} != slot count {slots}")
            if "position" in g and not isinstance(g["position"], int):
                errors.append(f"{where}: {channel} typed_gram position must be an integer")
            if channel == "coarse_gram" and "position" in g:
                errors.append(
                    f"{where}: coarse_gram typed_gram must not carry a lexical position")
            if "phase" in g and g["phase"] not in PHASES:
                errors.append(f"{where}: {channel} typed_gram bad phase {g['phase']!r}")
            role, phase, _ = CHANNEL_CONTRACT[channel]
            if g["role"] != role:
                errors.append(
                    f"{where}: {channel} typed_gram role {g['role']!r} violates channel "
                    f"contract (expected {role!r})")
            if "phase" in g and g["phase"] != phase:
                errors.append(
                    f"{where}: {channel} typed_gram phase {g['phase']!r} violates channel "
                    f"contract (expected {phase!r})")


def validate_relation_row(row, path, lineno, errors):
    where = f"{path}:{lineno}"
    for field_name in ("from", "relation", "to", "source_word", "evidence",
                       "weight_kind", "weight"):
        if field_name not in row:
            errors.append(f"{where}: relation_row missing required field {field_name!r}")


def validate_greeting_bank(bank, path, errors):
    if bank.get("schema") != "key.grammar.greeting_bank.v1":
        errors.append(f"{path}: greeting bank schema must be key.grammar.greeting_bank.v1")
    phases = {e.get("phase") for e in bank.get("greetings", [])}
    required = {"Pop", "Wo", "Yax", "Sek", "Ch'en", "Xul",
                "Pop.reactive_mirror", "Yax.synthesized_greeting"}
    missing = required - phases
    if missing:
        errors.append(f"{path}: greeting bank missing phase entries: {sorted(missing)}")
    kinds = {"canonical", "learned", "associative", "reactionary",
             "synthesized", "status", "confirmatory", "recall"}
    for entry in bank.get("greetings", []):
        if entry.get("kind") not in kinds:
            errors.append(
                f"{path}: greeting entry phase={entry.get('phase')!r} "
                f"has bad kind {entry.get('kind')!r}")
        if not entry.get("templates"):
            errors.append(
                f"{path}: greeting entry phase={entry.get('phase')!r} has no templates")


def cmd_validate(repo_root, sample_limit=None):
    errors = []
    warnings = []
    lexicon_path = repo_root / "data" / "grammar" / "english-dictionary.lexicon.key-grammar.jsonl"
    relations_path = repo_root / "data" / "grammar" / "english-dictionary.definition-relations.key-grammar.jsonl"
    bank_jsonl_path = repo_root / "data" / "grammar" / "greeting-bank.key-grammar.jsonl"
    bank_path = repo_root / "data" / "grammar" / "greeting-bank.json"

    rows_checked = 0
    for path in (lexicon_path, bank_jsonl_path):
        if not path.exists():
            print(f"SKIP | {path.relative_to(repo_root)} (missing)")
            continue
        with path.open(encoding="utf-8") as f:
            for lineno, line in enumerate(f, start=1):
                if sample_limit and rows_checked >= sample_limit:
                    break
                line = line.strip()
                if not line:
                    continue
                row = json.loads(line)
                validate_lexicon_row(row, path.name, lineno, errors, warnings)
                rows_checked += 1
                if sample_limit is None and rows_checked % 50000 == 0:
                    print(f"  ... {rows_checked} rows checked")

    rel_checked = 0
    with relations_path.open(encoding="utf-8") as f:
        for lineno, line in enumerate(f, start=1):
            if sample_limit and rel_checked >= sample_limit:
                break
            line = line.strip()
            if not line:
                continue
            row = json.loads(line)
            validate_relation_row(row, relations_path.name, lineno, errors)
            rel_checked += 1

    bank = json.loads(bank_path.read_text(encoding="utf-8"))
    validate_greeting_bank(bank, bank_path.name, errors)

    for e in errors[:40]:
        print("FAIL |", e)
    if len(errors) > 40:
        print(f"... and {len(errors) - 40} more errors")
    for w in warnings[:5]:
        print("WARN |", w)
    if len(warnings) > 5:
        print(f"... and {len(warnings) - 5} more warnings "
              f"(legacy coarse_gram widths in the checked-in lexicon artifact; "
              f"refreshes on the next --build-lexicon-grammar run)")
    print(f"{'PASS' if not errors else 'FAIL'} | validate: "
          f"{rows_checked} lexicon rows, {rel_checked} relation rows, "
          f"{len(errors)} error(s), {len(warnings)} warning(s)")
    return 1 if errors else 0


# ── gyro-align ───────────────────────────────────────────────────────────────

def track_filename_for_id(track_id):
    return track_id.replace("µ", "u") + ".semantic-tracks.v1.json"


def cmd_gyro_align(repo_root):
    problems = []
    gyro_path = repo_root / "data" / "gyro_abi_v1.json"
    tracks_dir = repo_root / "data" / "tracks"
    field_xml_path = repo_root / "data" / "world" / "field.xml"

    gyro = json.loads(gyro_path.read_text(encoding="utf-8"))
    book = next(v for k, v in gyro.items() if k.startswith("⟁GYRO_ABI"))
    measurement_tracks = book["measurement_tracks"]
    track_governs = book["track_governs"]

    # Collect all track files.
    track_ids = {}
    for path in sorted(tracks_dir.glob("*.semantic-tracks.v1.json")):
        track = json.loads(path.read_text(encoding="utf-8"))
        track_ids[track.get("id", path.stem)] = (track, path)

    # 1. Every gyro measurement track must exist as a semantic track.
    for mt in measurement_tracks:
        if mt not in track_ids:
            problems.append(
                f"gyro measurement track {mt!r} has no data/tracks file")
        elif track_filename_for_id(mt) != track_ids[mt][1].name:
            problems.append(
                f"measurement track {mt!r} filename mismatch: expected "
                f"{track_filename_for_id(mt)}, found {track_ids[mt][1].name}")

    # 2. Every track should appear as a field.xml node.
    field_text = field_xml_path.read_text(encoding="utf-8")
    for track_id, (track, path) in sorted(track_ids.items()):
        if f'@node:{track_id}' not in field_text:
            problems.append(
                f"track {track_id!r} ({path.name}) has no field.xml node")

    # 3. Track governs coverage vs the gyro book measurement vocabulary.
    measurement_vocab = set()
    for mt in measurement_tracks:
        measurement_vocab.update(track_governs.get(mt, []))
    uncovered = {}
    for track_id, (track, _) in sorted(track_ids.items()):
        if track_id in measurement_tracks:
            continue
        for word in track.get("governs", []):
            if word not in measurement_vocab:
                uncovered.setdefault(track_id, []).append(word)
    for track_id, words in sorted(uncovered.items()):
        if words:
            print(f"info | track {track_id!r} governs words outside the gyro "
                  f"measurement vocabulary: {', '.join(words[:12])}"
                  f"{' ...' if len(words) > 12 else ''}")

    for p in problems:
        print("FAIL |", p)
    print(f"{'PASS' if not problems else 'FAIL'} | gyro-align: "
          f"{len(measurement_tracks)} measurement tracks, {len(track_ids)} "
          f"semantic tracks, {len(problems)} problem(s)")
    return 1 if problems else 0


# ── greetings-check ──────────────────────────────────────────────────────────

def _aiml_normalize(text: str) -> str:
    """Mirror AIMLEngine pattern normalization: keep ASCII alphanumerics and
    wildcards, upper-case, collapse everything else to single spaces."""
    out, in_ws = [], False
    for ch in text:
        if ch.isascii() and (ch.isalnum() or ch in "*_"):
            out.append(ch.upper())
            in_ws = False
        else:
            if not in_ws:
                out.append(" ")
                in_ws = True
    return "".join(out).strip()


def _xml_text_between(content: str, tag: str) -> str:
    start = content.find("<" + tag + ">")
    if start == -1:
        return ""
    start += len(tag) + 2
    end = content.find("</" + tag + ">", start)
    return content[start:end] if end != -1 else content[start:]


def _parse_aiml_categories(content: str):
    """Categories in document order — the engine's first-match-wins order."""
    cats, pos = [], 0
    while True:
        start = content.find("<category>", pos)
        if start == -1:
            break
        end = content.find("</category>", start)
        if end == -1:
            break
        body = content[start + len("<category>"):end]
        pos = end + len("</category>")
        pattern = _aiml_normalize(_xml_text_between(body, "pattern"))
        template = _xml_text_between(body, "template")
        if pattern and template:
            cats.append((pattern, template))
    return cats


def _aiml_pattern_match(pattern: str, text: str):
    """Backtracking '*' wildcard matcher over normalized token lists."""
    ptoks, itoks = pattern.split(), text.split()

    def rec(pi, ii):
        if pi == len(ptoks) and ii == len(itoks):
            return []
        if pi == len(ptoks):
            return None
        if ptoks[pi] == "*":
            j = len(itoks)
            while True:
                if j < ii:
                    return None
                caps = rec(pi + 1, j)
                if caps is not None:
                    return [" ".join(itoks[ii:j])] + caps
                if j == 0:
                    return None
                j -= 1
        if ii >= len(itoks) or ptoks[pi] != itoks[ii]:
            return None
        sub = rec(pi + 1, ii + 1)
        return None if sub is None else sub

    return rec(0, 0)


def _aiml_match(cats, query: str) -> str:
    norm = _aiml_normalize(query)
    for pattern, template in cats:
        caps = _aiml_pattern_match(pattern, norm)
        if caps is not None:
            return template.replace("<star/>", caps[0] if caps else "")
    return ""


def cmd_greetings_check(repo_root):
    aiml_path = repo_root / "data" / "alice" / "greetings.aiml"
    if not aiml_path.exists():
        print("FAIL | greetings-check: data/alice/greetings.aiml missing "
              "(run greeting-bank-compile)")
        return 1

    content = aiml_path.read_text(encoding="utf-8")
    try:
        ET.fromstring(content)
    except ET.ParseError as exc:
        print(f"FAIL | greetings-check: XML not well-formed: {exc}")
        return 1
    print("PASS | xml well-formed")

    cats = _parse_aiml_categories(content)
    if not cats:
        print("FAIL | greetings-check: no categories parsed")
        return 1
    print(f"info | {len(cats)} categories parsed")

    # Each check is (name, query, substring that must appear in the response).
    checks = [
        ("GREETING BANK overview",
         "GREETING BANK", "greeting bank phases"),
        ("reactive mirror not shadowed by Pop",
         "greeting pop.reactive_mirror hi", "kind=reactionary"),
        ("synthesized greeting not shadowed by Yax",
         "greeting yax.synthesized_greeting test", "kind=synthesized"),
        ("Wo phase query",
         "GREETING WO friend", "phase=Wo kind=learned"),
        ("Ch'en phase (lowercase + apostrophe)",
         "greeting ch'en friend", "phase=Ch'en kind=confirmatory"),
        ("Xul phase is recall kind",
         "GREETING XUL hi", "kind=recall"),
        ("Sek phase query",
         "GREETING SEK hi", "kind=status"),
        ("generic fallback is last resort",
         "GREETING something else", "hint GREETING BANK"),
        ("star capture surfaced (normalized upper)",
         "GREETING XUL recall me", "RECALL ME"),
    ]

    failures = 0
    for name, query, want in checks:
        got = _aiml_match(cats, query)
        if want in got:
            print(f"PASS | {name}")
        else:
            failures += 1
            print(f"FAIL | {name}")
            print(f"   query: {query}")
            print(f"   got:   {got[:160]}")
            print(f"   want:  {want}")

    total = len(checks)

    # ── Authored-contract guard: bank placeholders must be resolvable ───────
    # Invariant: forall p in Placeholders(bank): p in ResolverVocabulary.
    # An authored {mood} must fail the check by name, not become a silent
    # runtime skip discovered only by an empty greeting. The vocabulary is read
    # from the implementation, so this guard cannot drift from the resolver.
    bank_path = repo_root / "data" / "grammar" / "greeting-bank.json"
    resolver_path = repo_root / "src" / "micronaut" / "greeting_style.cpp"
    total += 1
    if not bank_path.exists():
        failures += 1
        print("FAIL | placeholder contract: greeting-bank.json is missing")
    elif not resolver_path.exists():
        failures += 1
        print("FAIL | placeholder contract: src/micronaut/greeting_style.cpp is missing")
    else:
        placeholders = set(re.findall(r"\{([A-Za-z_][A-Za-z0-9_]*)\}",
                                      bank_path.read_text(encoding="utf-8")))
        resolver_vocab = set(re.findall(r'replace_placeholder\(out,\s*"([^"]+)"',
                                        resolver_path.read_text(encoding="utf-8")))
        unresolved = sorted(placeholders - resolver_vocab)
        if unresolved:
            failures += 1
            print(f"FAIL | placeholder contract: bank uses placeholders the resolver "
                  f"does not implement: {unresolved} (implemented: {sorted(resolver_vocab)})")
        else:
            print(f"PASS | placeholder contract: all {len(placeholders)} bank "
                  f"placeholders resolvable")

    print(f"{'PASS' if not failures else 'FAIL'} | greetings-check: "
          f"{total - failures}/{total} checks passed")
    return 1 if failures else 0


# ── control-check ────────────────────────────────────────────────────────────

CONTROL_SUGAR_WORDS = ["if", "else", "then", "when", "how", "who", "why",
                       "for", "from", "as", "equals"]

CONTROL_SUGAR_PRODUCTIONS = ["control_clause", "conditional_clause",
                             "temporal_clause", "iteration_clause",
                             "origin_clause", "comparison_clause",
                             "query_clause"]


def _ebnf_production_terms(text, name):
    """Quoted terminals of one EBNF production (`name = ... ;`)."""
    match = re.search(r"^" + re.escape(name) + r"\s*=\s*(.*?);", text,
                      re.DOTALL | re.MULTILINE)
    if not match:
        return None
    return re.findall(r'"((?:[^"\\]|\\.)*)"', match.group(1))


def cmd_control_check(repo_root):
    ebnf_path = repo_root / "data" / "grammar" / "key-grammar-json.runtime.ebnf"
    rows_path = repo_root / "data" / "grammar" / "control-flow.key-grammar.jsonl"
    glossary_path = repo_root / "docs" / "GLOSSARY.md"
    glossary_text = glossary_path.read_text(encoding="utf-8") if glossary_path.exists() else ""
    problems = []
    if not glossary_text:
        problems.append("docs/GLOSSARY.md is missing")

    if not ebnf_path.exists():
        print("FAIL | control-check: key-grammar EBNF artifact missing (run ebnf-sync)")
        return 1
    text = ebnf_path.read_text(encoding="utf-8")

    vocabulary = _ebnf_production_terms(text, "control_word") or []
    phases = set(_ebnf_production_terms(text, "control_phase") or [])
    xcfe_ops = set(_ebnf_production_terms(text, "control_xcfe_op") or [])
    block_roles = set(_ebnf_production_terms(text, "control_block_role") or [])
    arities = set(_ebnf_production_terms(text, "control_arity") or [])
    forms = set(_ebnf_production_terms(text, "control_form") or [])

    if not vocabulary:
        problems.append("EBNF declares no control_word vocabulary")
    if not phases:
        problems.append("EBNF declares no control_phase set")
    if not xcfe_ops:
        problems.append("EBNF declares no control_xcfe_op set")
    if not block_roles:
        problems.append("EBNF declares no control_block_role set")
    if not arities:
        problems.append("EBNF declares no control_arity bounds")
    if not forms:
        problems.append("EBNF declares no control_form set")

    # The sugar grammar must be declared, not merely referenced.
    for production in CONTROL_SUGAR_PRODUCTIONS:
        if not re.search(r"^" + re.escape(production) + r"\s*=", text, re.MULTILINE):
            problems.append(f"EBNF is missing the sugar production {production!r}")
    if "key.grammar.control_flow_plan.v1" not in text:
        problems.append("EBNF is missing the control plan schema")
    if "xcfe_control_node" not in text:
        problems.append("EBNF is missing the XCFE lowering production")

    for word in CONTROL_SUGAR_WORDS:
        if word not in vocabulary:
            problems.append(f"control-flow word {word!r} missing from the EBNF vocabulary")

    # The Python mirror used for coarse-symbol expectations must not drift.
    if set(vocabulary) != CONTROL_RULES:
        only_py = sorted(CONTROL_RULES - set(vocabulary))
        only_ebnf = sorted(set(vocabulary) - CONTROL_RULES)
        problems.append("CONTROL_RULES mirror differs from the EBNF vocabulary "
                        f"(python-only={only_py}, ebnf-only={only_ebnf})")

    if not rows_path.exists():
        problems.append("control-flow.key-grammar.jsonl missing "
                        "(run: KHANARY.exe --data-dir data --build-lexicon-grammar)")
        for problem in problems:
            print("FAIL |", problem)
        print(f"FAIL | control-check: artifact missing, {len(problems)} problem(s)")
        return 1

    rows = []
    for lineno, line in enumerate(rows_path.read_text(encoding="utf-8").splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError as exc:
            problems.append(f"{rows_path.name}:{lineno}: invalid JSON: {exc}")

    if not rows:
        problems.append("control-flow artifact has no rows")

    seen = set()
    for row in rows:
        word = row.get("word", "")
        seen.add(word)
        if word not in vocabulary:
            problems.append(f"artifact word {word!r} is not in the EBNF vocabulary")
        if row.get("key") != f"control:{word}":
            problems.append(f"{word!r}: key must be 'control:{word}'")
        if not row.get("operator"):
            problems.append(f"{word!r}: missing operator authority")
        elif row.get("book") != f"ELIZA-1:{row.get('operator')}":
            problems.append(f"{word!r}: book must be 'ELIZA-1:<operator>'")
        if not row.get("role"):
            problems.append(f"{word!r}: missing semantic role")
        if row.get("xcfe_op") not in xcfe_ops:
            problems.append(f"{word!r}: xcfe_op {row.get('xcfe_op')!r} outside the contract")
        for field in ("phase", "emit_phase"):
            if row.get(field) not in phases:
                problems.append(f"{word!r}: {field} {row.get(field)!r} is not a C6 phase")
        if row.get("block_role") not in block_roles:
            problems.append(f"{word!r}: block_role {row.get('block_role')!r} outside the contract")
        if str(row.get("arity")) not in arities:
            problems.append(f"{word!r}: arity {row.get('arity')!r} outside the contract")
        if not row.get("sugar"):
            problems.append(f"{word!r}: missing sugar form")

    for word in vocabulary:
        if word not in seen:
            problems.append(f"vocabulary word {word!r} has no artifact row")
    for word in CONTROL_SUGAR_WORDS:
        if word not in seen:
            problems.append(f"control-flow sugar word {word!r} has no artifact row")

    # ── XCFE Node ISA invariants ────────────────────────────────────────
    # class(op) != none ; CanonicalPhase(class) != none ;
    # authority_phase(op) == CanonicalPhase(class(op)) ; flow_phase(op) in C6 ;
    # XcfeNodeClass != RelationFamily ; Policy not a class ;
    # and flow_phase == authority_phase is NOT required.
    isa_header = repo_root / "src" / "kuhul" / "xcfe_node_class.h"
    relation_header = repo_root / "src" / "micronaut" / "relation_isa.h"
    class_names = {}
    class_phase = {}
    if isa_header.exists():
        isa_src = isa_header.read_text(encoding="utf-8")
        class_names = dict(re.findall(r'case XcfeNodeClass::(\w+):\s*return "([^"]+)"',
                                      isa_src))
        mapping = re.search(
            r"canonical_phase_for_xcfe_class\(XcfeNodeClass node_class, Phase& out\)"
            r"[\s\S]*?\n\}", isa_src)
        if mapping:
            pending = []
            for line in mapping.group(0).splitlines():
                case = re.search(r"case XcfeNodeClass::(\w+):", line)
                if case:
                    pending.append(case.group(1))
                    continue
                assigned = re.search(r"out = Phase::(\w+);", line)
                if assigned and pending:
                    for member in pending:
                        class_phase[class_names.get(member, member)] = assigned.group(1)
                    pending = []
        else:
            problems.append("xcfe_node_class.h: canonical_phase_for_xcfe_class not found")
    else:
        problems.append("src/kuhul/xcfe_node_class.h is missing")

    if not class_names:
        problems.append("xcfe_node_class.h: no class name mapping found")
    if not class_phase:
        problems.append("xcfe_node_class.h: no canonical class -> phase mapping found")

    # C++ enum member names -> contract phase names.
    phase_label = {"Pop": "Pop", "Wo": "Wo", "Yax": "Yax", "Sek": "Sek",
                   "Chen": "Ch'en", "Xul": "Xul"}
    for cls, cpp_phase in class_phase.items():
        if cpp_phase not in phase_label:
            problems.append(f"class {cls!r} maps to unknown phase {cpp_phase!r}")
        elif phase_label[cpp_phase] not in phases:
            problems.append(f"class {cls!r} maps outside C6: {cpp_phase!r}")

    declared_classes = _ebnf_production_terms(text, "xcfe_node_class") or []
    if set(declared_classes) != set(class_names.values()):
        problems.append(
            "EBNF xcfe_node_class vocabulary differs from the header enum "
            f"(ebnf-only={sorted(set(declared_classes) - set(class_names.values()))}, "
            f"header-only={sorted(set(class_names.values()) - set(declared_classes))})")
    if "POLICY" in class_names.values():
        problems.append("POLICY must not be an XCFE node class — operation authority is "
                        "XCFE's own gate, not a node class")

    # RelationFamily must stay a distinct ISA.
    if relation_header.exists():
        family_enum = re.search(r"enum class RelationFamily \{([^}]*)\}",
                                relation_header.read_text(encoding="utf-8"))
        families = {m.upper() for m in re.findall(r"([A-Za-z_][A-Za-z0-9_]*)\s*,",
                                                  family_enum.group(1))} if family_enum else set()
        if families and families == set(class_names.values()):
            problems.append("XcfeNodeClass must not be identical to RelationFamily")
    else:
        problems.append("src/micronaut/relation_isa.h is missing")

    # Per-operator: class present, authority == CanonicalPhase(class), flow in C6.
    flow_differs = False
    for row in rows:
        word = row.get("word", "")
        row_class = row.get("class", "")
        if not row_class:
            problems.append(f"{word!r}: class(op) must not be empty")
            continue
        if row_class not in class_phase:
            problems.append(f"{word!r}: class {row_class!r} is not in the ISA")
            continue
        expected_authority = phase_label.get(class_phase[row_class], class_phase[row_class])
        if row.get("authority_phase") != expected_authority:
            problems.append(f"{word!r}: authority_phase {row.get('authority_phase')!r} != "
                            f"CanonicalPhase({row_class}) = {expected_authority!r}")
        flow = row.get("flow_phase", row.get("phase"))
        if flow not in phases:
            problems.append(f"{word!r}: flow_phase {flow!r} is not a C6 phase")
        # `phase` is the legacy spelling of `flow_phase`; if both are present they
        # must agree, or the duplicate has silently diverged.
        if "flow_phase" in row and "phase" in row and row["phase"] != row["flow_phase"]:
            problems.append(f"{word!r}: legacy 'phase' ({row['phase']!r}) != "
                            f"'flow_phase' ({row['flow_phase']!r})")
        if flow != row.get("authority_phase"):
            flow_differs = True
        # role != requires: eligibility may be derived ONLY from explicit
        # requires(op) versus declared capabilities(µ). role stays descriptive.
        requires = row.get("requires")
        if not isinstance(requires, list):
            problems.append(f"{word!r}: 'requires' must be an explicit array")
        else:
            if row.get("role") in requires:
                problems.append(
                    f"{word!r}: role {row.get('role')!r} must not be used as a capability "
                    "requirement — role and requires are different namespaces")
            for requirement in requires:
                if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_.:\-]*", str(requirement)):
                    problems.append(f"{word!r}: requirement {requirement!r} is not a capability id")
    if rows and not flow_differs:
        problems.append("no operator has flow_phase != authority_phase — the two-coordinate "
                        "model is never exercised, which suggests they were collapsed")

    # The glossary states the ISA so the human contract cannot drift.
    for label, needle in {
            "ISA separation": "Class ≠ Operator ≠ Phase",
            "authority invariant": "authority_phase = CanonicalPhase(class)",
            "flow/authority legality": "flow_phase ≠ authority_phase",
            "no Policy class": "no `Policy` class",
            "Relation ISA separation": "not the Relation ISA",
            "dispatch chain WHAT": "says WHAT kind of control operation",
            "dispatch chain WHO": "says WHO is qualified",
            "no name-calling": "XCFE does not call a Micronaut by name",
            "capability miss": "W·C·R never promotes an unqualified specialist",
            "role is not requires": "role ≠ requires",
            "entropy is not a phase": "not a seventh phase",
    }.items():
        if needle not in glossary_text:
            problems.append(f"docs/GLOSSARY.md: missing {label} ({needle!r})")

    # Exposure: the op library the executor registers and the ops advertised to
    # tool callers through the MCP formal model must be the same set — otherwise
    # control flow is executable but undiscoverable (or advertised but absent).
    executor_cpp = repo_root / "src" / "kuhul" / "xcfe_executor.cpp"
    mcp_cpp = repo_root / "src" / "io" / "mcp_server.cpp"
    registered = set()
    native = set()
    if executor_cpp.exists():
        executor_src = executor_cpp.read_text(encoding="utf-8")
        registered = set(re.findall(r'register_op\(\s*"([^"]+)"', executor_src))
        # Native control keys are dispatched in eval() before @op/@fn.
        dispatch = re.search(r"for \(const char\* control_key : \{([^}]*)\}\)", executor_src)
        if dispatch:
            native.update(k.lstrip("@") for k in re.findall(r'"(@[a-z_]+)"', dispatch.group(1)))
        if re.search(r'obj\.find\("@if"\)', executor_src):
            native.add("if")
        if not native:
            problems.append("xcfe_executor.cpp: no native control-key dispatch found")
    else:
        problems.append("src/kuhul/xcfe_executor.cpp is missing")
    advertised = set()
    if mcp_cpp.exists():
        match = re.search(r'formal_model\["xcfe_ops"\]\s*=\s*json_string\(\s*"([^"]*)"',
                          mcp_cpp.read_text(encoding="utf-8"))
        if match:
            advertised = {p.strip() for p in match.group(1).split(",") if p.strip()}
        else:
            problems.append("mcp_server.cpp: formal_model['xcfe_ops'] is missing")
    else:
        problems.append("src/io/mcp_server.cpp is missing")
    if not registered:
        problems.append("xcfe_executor.cpp: no register_op(..) calls found")
    exposed = registered | native
    if exposed != advertised:
        problems.append(
            "xcfe op library vs advertised xcfe_ops differ "
            f"(missing from advertisement={sorted(exposed - advertised)}, "
            f"advertised but not callable={sorted(advertised - exposed)})")

    # The Node ISA must be advertised too, so a tool caller can discover the
    # classes rather than only the op names.
    advertised_classes = set()
    if mcp_cpp.exists():
        # The list may be emitted as adjacent C++ string literals, so collect
        # every quoted piece of the initializer.
        match = re.search(r'formal_model\["xcfe_node_classes"\]\s*=\s*json_string\(([\s\S]*?)\);',
                          mcp_cpp.read_text(encoding="utf-8"))
        if match:
            advertised_classes = {name.strip()
                                  for piece in re.findall(r'"([^"]*)"', match.group(1))
                                  for name in piece.split(",") if name.strip()}
        else:
            problems.append("mcp_server.cpp: formal_model['xcfe_node_classes'] is missing")
    if advertised_classes != set(class_names.values()):
        problems.append(
            "XCFE node classes vs advertised xcfe_node_classes differ "
            f"(missing from advertisement={sorted(set(class_names.values()) - advertised_classes)}, "
            f"advertised but not in the ISA={sorted(advertised_classes - set(class_names.values()))})")

    # ── The dispatch chain: class says WHAT, capability says WHO ────────
    # XCFE NodeClass is "what kind of control work is needed"; a micronaut/track
    # id is "who is qualified". They must never be the same vocabulary, and a
    # requirement no track declares is reported (gyro-align style) as an
    # unqualified requirement rather than silently dispatched.
    track_ids, track_caps = set(), set()
    tracks_dir = repo_root / "data" / "tracks"
    if tracks_dir.is_dir():
        for path in sorted(tracks_dir.glob("*.json")):
            try:
                entry = json.loads(path.read_text(encoding="utf-8"))
            except json.JSONDecodeError:
                continue
            track_id = entry.get("id", "")
            if track_id:
                track_ids.add(track_id)
            track_caps.update(entry.get("capabilities", []))
    else:
        problems.append("data/tracks is missing")

    # Compare exactly: uppercasing a track id would mangle the µ (U+00B5 uppercases
    # to the Greek capital mu U+039C), silently defeating the check.
    collisions = sorted({c for c in class_names.values() if c in track_ids})
    if collisions:
        problems.append("an XcfeNodeClass must not be a micronaut/track id "
                        f"(XCFE class says WHAT is needed, a micronaut says WHO is "
                        f"qualified): {collisions}")

    roles = sorted({row.get("role", "") for row in rows if row.get("role")})
    unqualified = [role for role in roles if role not in track_caps]
    for role in unqualified:
        print(f"info | no track declares the capability {role!r} — the XCFE class "
              f"says WHAT is needed, a track's capabilities say WHO is qualified")

    # ── Entropy must not be reachable from eligibility ──────────────────
    # Order is Requirements -> Eligibility -> WCR -> EntropyMeasure -> Decision.
    # Guarded at source level in the one file that decides eligibility.
    router_cpp = repo_root / "src" / "io" / "request_router.cpp"
    if router_cpp.exists():
        router_src = router_cpp.read_text(encoding="utf-8")
        for function in ("evaluate_eligibility", "has_capability_ci", "has_required_primitive"):
            match = re.search(r"\w[\w:<>]*\s+" + function + r"\s*\([^)]*\)\s*\{([\s\S]*?)\n\}",
                              router_src)
            if not match:
                problems.append(f"request_router.cpp: {function} body not found "
                                "(entropy-ordering guard)")
                continue
            body = match.group(1).lower()
            for banned in ("entropy", "omega_throttle"):
                if banned in body:
                    problems.append(
                        f"request_router.cpp: {function} references {banned!r} — entropy may "
                        "recommend but may never authorize")
    else:
        problems.append("src/io/request_router.cpp is missing")

    for problem in problems:
        print("FAIL |", problem)
    print(f"{'PASS' if not problems else 'FAIL'} | control-check: "
          f"{len(rows)} control operators, {len(vocabulary)} declared words, "
          f"{len(forms)} plan forms, {len(exposed)} exposed ops, "
          f"{len(problems)} problem(s)")
    return 1 if problems else 0


# ── fold-geometry-check ──────────────────────────────────────────────────────

# The machine-readable fold/phase authority (src/io/mcp_server.cpp formal_model).
FOLD_FORMAL_STRINGS = {
    "fold_operator": "Fold:{contexts,relations,nodes}->h",
    "shf_signature": "SHF(h,tau)=<h,B_tau[h,*],N_tau(h),W_tau,State(h)>",
    "unfold_operator": "Unfold(h,tau)={e | B_tau[h,e] != 0}",
    "traverse_operator": "TRAVERSE(start, relation_type, direction, depth, limit, predicate)",
}

# C6 angles: 0, π/3, 2π/3, π, 4π/3, 5π/3.
FOLD_PHASE_ANGLES = {
    "PHASE_POP": 0.0,
    "PHASE_WO": math.pi / 3.0,
    "PHASE_YAX": 2.0 * math.pi / 3.0,
    "PHASE_SEK": math.pi,
    "PHASE_CHEN": 4.0 * math.pi / 3.0,
    "PHASE_XUL": 5.0 * math.pi / 3.0,
}

FOLD_VERTICAL_DEGREES = 60.0   # π/3
FOLD_VERTICAL_WEIGHT = 0.5     # cos(π/3)
FOLD_HORIZONTAL_DEGREES = 0.0
FOLD_HORIZONTAL_WEIGHT = 1.0   # cos(0)

# Invariants docs/GLOSSARY.md must state, so the human contract cannot drift.
FOLD_GLOSSARY_REQUIREMENTS = {
    "PHASE_COUNT": "PHASE_COUNT = 6",
    "vertical angle": "π/3",
    "vertical weight": "cos(π/3) = 0.5",
    "horizontal weight": "cos(0) = 1.0",
    "structure law": "Fold is structure",
    "engine-arity correction": "not the semantic `Fold` arity",
    "Unfold definition": "Unfold(h,τ) = { e | B_τ[h,e] ≠ 0 }",
    "PH boundary": "measurement, not definition",
}


def _param_count(signature_text):
    return len([p for p in signature_text.split(",") if p.strip()])


def cmd_fold_geometry_check(repo_root):
    problems = []

    def slurp(relative):
        path = repo_root / relative
        if not path.exists():
            problems.append(f"{relative} is missing")
            return ""
        return path.read_text(encoding="utf-8")

    glossary = slurp("docs/GLOSSARY.md")
    config_h = slurp("src/config.h")
    phase_h = slurp("src/kuhul/phase_engine.h")
    fold_h = slurp("src/kuhul/fold_engine.h")
    fold_cpp = slurp("src/kuhul/fold_engine.cpp")
    field_dag = slurp("src/world/field_dag.cpp")
    mcp = slurp("src/io/mcp_server.cpp")
    main_cpp = slurp("src/main.cpp")
    code_parser = slurp("src/io/code_parser.cpp")

    # 1. PHASE_COUNT = 6
    if not re.search(r"PHASE_COUNT\s*=\s*6\s*;", phase_h):
        problems.append("phase_engine.h: PHASE_COUNT must be 6")

    # 2. C6 angles are exactly 0, π/3, …, 5π/3
    for name, expected in FOLD_PHASE_ANGLES.items():
        match = re.search(r"constexpr\s+double\s+" + name + r"\s*=\s*([^;]+);", config_h)
        if not match:
            problems.append(f"config.h: {name} not found")
            continue
        expression = match.group(1).strip()
        # Only the canonical pi-scaled form is allowed: arithmetic over numbers
        # and the exact std::numbers::pi token.
        residue = expression.replace("std::numbers::pi", "")
        if re.search(r"[^0-9.\s*/+-]", residue):
            problems.append(f"config.h: {name} has an unexpected expression: {expression!r}")
            continue
        try:
            value = float(eval(expression.replace("std::numbers::pi", "math.pi"),
                               {"math": math}, {}))
        except Exception:
            problems.append(f"config.h: {name} could not be evaluated: {expression!r}")
            continue
        if abs(value - expected) > 1e-12:
            problems.append(f"config.h: {name} = {value!r}, expected {expected!r}")

    # 3. arc_weight is derived from the angles, not tuned
    if not re.search(r"arc_weight\s*\(\s*Phase\s+from,\s*Phase\s+to\s*\)\s*"
                     r"\{\s*return\s+std::cos\(\s*phase_angle\(to\)\s*-\s*"
                     r"phase_angle\(from\)\s*\)", phase_h, re.DOTALL):
        problems.append("phase_engine.h: arc_weight must be cos(angle(to) - angle(from))")
    squeezed = re.sub(r"\s+", "", phase_h)
    if "Δθ=π/3" not in squeezed:
        problems.append("phase_engine.h: vertical step must be declared Δθ=π/3")
    if "Δθ=0" not in squeezed:
        problems.append("phase_engine.h: horizontal step must be declared Δθ=0")

    # 4. The engine fold is binary + phase — not the semantic Fold arity
    for function in ("horizontal_fold", "vertical_fold"):
        match = re.search(r"FoldResult\s+" + function + r"\s*\(([^;]*?)\)\s*;",
                          fold_h, re.DOTALL)
        if not match:
            problems.append(f"fold_engine.h: {function} declaration not found")
            continue
        count = _param_count(match.group(1))
        if count != 3:
            problems.append(f"fold_engine.h: {function} takes {count} parameters — the "
                            "engine fold is binary + phase (3), not the semantic Fold arity")

    # 5. Engine weights equal cos(Δθ), and vertical steps stay primitive
    if not re.search(r"horizontal_fold[\s\S]*?weight\s*=\s*1\.0", fold_cpp):
        problems.append("fold_engine.cpp: horizontal fold weight must be 1.0")
    if "cos(0)" not in fold_cpp:
        problems.append("fold_engine.cpp: horizontal fold must document weight = cos(0)")
    if "phase_distance(from_phase, to_phase)" not in fold_cpp or "dist != 1" not in fold_cpp:
        problems.append("fold_engine.cpp: vertical fold must require exactly one "
                        "phase step (dist != 1)")
    if not re.search(r"vertical_fold[\s\S]*?arc_weight\(", fold_cpp):
        problems.append("fold_engine.cpp: vertical fold weight must derive from arc_weight")

    # 6. FieldDag edge geometry, and Δθ ↔ weight agreement
    for label, kind, degrees, weight in (
            ("vertical", "vertical_fold", FOLD_VERTICAL_DEGREES, FOLD_VERTICAL_WEIGHT),
            ("horizontal", "horizontal_fold", FOLD_HORIZONTAL_DEGREES,
             FOLD_HORIZONTAL_WEIGHT)):
        # Anchor on the edge *construction*, not a later type comparison.
        block = re.search(r'edge\.type\s*=\s*"' + re.escape(kind) + r'"[\s\S]{0,240}',
                          field_dag)
        if not block:
            problems.append(f"field_dag.cpp: {kind} edge construction not found")
            continue
        text = block.group(0)
        angle = re.search(r"delta_theta\s*=\s*([0-9.]+)f", text)
        edge_weight = re.search(r"weight\s*=\s*([0-9.]+)f", text)
        if not angle or not edge_weight:
            problems.append(f"field_dag.cpp: {label} fold must record delta_theta and weight")
            continue
        if abs(float(angle.group(1)) - degrees) > 1e-6:
            problems.append(f"field_dag.cpp: {label} fold Δθ is {angle.group(1)}°, "
                            f"expected {degrees}°")
        if abs(float(edge_weight.group(1)) - weight) > 1e-6:
            problems.append(f"field_dag.cpp: {label} fold weight is {edge_weight.group(1)}, "
                            f"expected {weight}")
        if abs(math.cos(math.radians(float(angle.group(1)))) - float(edge_weight.group(1))) > 1e-6:
            problems.append(f"field_dag.cpp: {label} fold weight must equal cos(Δθ)")

    # 7. Canonical formal-model strings survive
    for key, value in FOLD_FORMAL_STRINGS.items():
        if value not in mcp:
            problems.append(f"mcp_server.cpp: formal_model[{key!r}] must remain {value!r}")
    if "Fold is structure; K-UHUL phases are operations/states over that structure" not in mcp:
        problems.append("mcp_server.cpp: the phase_relation sentence must remain")

    # 8. Unfold stays bounded, and TRAVERSE keeps its bounds
    if ("bounded_recursive_unfold_on_miss" not in main_cpp and
            "bounded_recursive_unfold_on_miss" not in code_parser):
        problems.append("unfold must stay bounded (bounded_recursive_unfold_on_miss)")
    traverse = FOLD_FORMAL_STRINGS["traverse_operator"]
    if "depth" not in traverse or "limit" not in traverse:
        problems.append("TRAVERSE must carry depth and limit bounds")

    # 9. The human contract states the same invariants
    for label, needle in FOLD_GLOSSARY_REQUIREMENTS.items():
        if needle not in glossary:
            problems.append(f"docs/GLOSSARY.md: missing {label} ({needle!r})")

    for problem in problems:
        print("FAIL |", problem)
    print(f"{'PASS' if not problems else 'FAIL'} | fold-geometry-check: "
          f"{len(FOLD_PHASE_ANGLES)} phases, Δθ {FOLD_VERTICAL_DEGREES:.0f}°/"
          f"{FOLD_HORIZONTAL_DEGREES:.0f}°, w {FOLD_VERTICAL_WEIGHT}/{FOLD_HORIZONTAL_WEIGHT}, "
          f"{len(FOLD_FORMAL_STRINGS)} formal strings, {len(problems)} problem(s)")
    return 1 if problems else 0


# ── capability-gap ───────────────────────────────────────────────────────────


def _capability_gap_rows(repo_root):
    """Gap(op) = Req(op) - union of every track's declared capabilities."""
    rows_path = repo_root / "data" / "grammar" / "control-flow.key-grammar.jsonl"
    tracks_dir = repo_root / "data" / "tracks"

    operators = []
    if rows_path.exists():
        for line in rows_path.read_text(encoding="utf-8").splitlines():
            if line.strip():
                operators.append(json.loads(line))

    tracks = {}
    if tracks_dir.is_dir():
        for path in sorted(tracks_dir.glob("*.json")):
            try:
                entry = json.loads(path.read_text(encoding="utf-8"))
            except json.JSONDecodeError:
                continue
            track_id = entry.get("id", "")
            if track_id:
                tracks[track_id] = [str(c) for c in entry.get("capabilities", [])]

    rows = []
    for operator in operators:
        requires = [str(r) for r in operator.get("requires", [])]
        role = operator.get("role", "")
        # Eligibility is derived ONLY from explicit requires(op) versus declared
        # capabilities(µ): requires(op) subset of capabilities(µ).
        eligible = sorted(
            track_id for track_id, caps in tracks.items()
            if all(any(c.lower() == req.lower() for c in caps) for req in requires))
        if not requires:
            # Native deterministic lowering: no specialist Micronaut is required.
            resolution = "not-required"
        elif eligible:
            resolution = "satisfied"
        else:
            resolution = "micronaut-required"
        gap = bool(requires) and not eligible
        # Advisory only: tracks whose capabilities share a token with a required
        # capability are plausible promotion candidates. Diagnostic information
        # outside the authority path — it can never manufacture eligibility.
        tokens = {t for req in requires
                  for t in re.split(r"[-_:.]", req.lower()) if len(t) > 3}
        near = sorted(track_id for track_id, caps in tracks.items()
                      if any(tokens & {t for t in re.split(r"[-_:.]", c.lower()) if len(t) > 3}
                             for c in caps))
        rows.append({
            "word": operator.get("word", ""),
            "operator": operator.get("operator", ""),
            "class": operator.get("class", ""),
            "authority_phase": operator.get("authority_phase", ""),
            "role": role,
            "requires": requires,
            "eligible_tracks": eligible,
            "gap": gap,
            "resolution": resolution,
            "advisory_near_tracks": near if gap else [],
        })
    return rows, tracks


def cmd_capability_gap(repo_root, check=False):
    rows, tracks = _capability_gap_rows(repo_root)
    if not rows:
        print("FAIL | capability-gap: no operator rows (run --build-lexicon-grammar)")
        return 1
    if not tracks:
        print("FAIL | capability-gap: no tracks with capabilities found")
        return 1

    artifact = repo_root / "data" / "grammar" / "control-capability-gaps.jsonl"
    rendered = "".join(json.dumps(row, sort_keys=True) + "\n" for row in rows)
    stale = not artifact.exists() or artifact.read_text(encoding="utf-8") != rendered
    if check:
        if stale:
            print(f"FAIL | capability-gap: {artifact.name} is stale — regenerate with "
                  f"'mini_transpilers.py capability-gap'")
            return 1
        print(f"PASS | capability-gap: {artifact.name} matches {len(rows)} operators, "
              f"{sum(1 for r in rows if r['gap'])} gap(s)")
        return 0

    artifact.write_text(rendered, encoding="utf-8", newline="\n")

    gaps = [r for r in rows if r["gap"]]
    by_resolution = {}
    for row in gaps:
        by_resolution[row["resolution"]] = by_resolution.get(row["resolution"], 0) + 1
    for row in gaps:
        print(f"info | {row['word']:<10} requires {row['requires']!r} "
              f"({row['class'] or 'no class'}) -> {row['resolution']}"
              + (f" [near: {', '.join(row['advisory_near_tracks'])}]"
                 if row["advisory_near_tracks"] else ""))
    summary = ", ".join(f"{k}={v}" for k, v in sorted(by_resolution.items()))
    print(f"{'PASS' if not gaps else 'PASS (gaps recorded)'} | capability-gap: "
          f"{len(rows)} operators, {len(gaps)} gap(s) [{summary or 'none'}], "
          f"{len(tracks)} tracks")
    return 0


# ── code-grammar-check ───────────────────────────────────────────────────────

def cmd_code_grammar_check(repo_root):
    """Three independent questions per artifact G: <Source, Freshness, Shape>.

    A checker must never manufacture the authority it verifies, so a missing
    source is reported (no_source) rather than reverse-engineered, and keys the
    generator does not emit are listed rather than silently absorbed into an
    equality test.
    """
    schema_path = repo_root / "data" / "schema" / "code-grammar.schema.json"
    if not schema_path.exists():
        print("FAIL | code-grammar-check: data/schema/code-grammar.schema.json is missing")
        return 1
    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    generator = repo_root / schema.get("generator", "tools/ebnf_to_code_grammar.py")
    problems = []
    rows = []

    for entry in schema.get("languages", []):
        language = entry.get("language", "?")
        artifact_path = repo_root / entry.get("artifact", "")
        source_path = repo_root / entry.get("source", "")
        expected = bool(entry.get("source_expected", False))
        findings = []

        # ── Shape ──────────────────────────────────────────────────────────
        shape_ok = False
        if not artifact_path.exists():
            problems.append(f"{language}: artifact {entry.get('artifact')} is missing")
        else:
            try:
                artifact = json.loads(artifact_path.read_text(encoding="utf-8"))
            except json.JSONDecodeError as exc:
                problems.append(f"{language}: artifact is not valid JSON: {exc}")
                artifact = None
            if isinstance(artifact, dict):
                shape_ok = True
                for key in schema.get("required", []):
                    if key not in artifact:
                        problems.append(f"{language}: artifact missing required key {key!r}")
                        shape_ok = False
                for key in artifact:
                    if key not in schema.get("required", []) + schema.get("optional", []) + \
                            schema["authored_not_generated"]["keys"]:
                        findings.append(f"undeclared top-level key {key!r}")
                for index, token in enumerate(artifact.get("tokens", [])):
                    if not isinstance(token, dict):
                        problems.append(f"{language}: tokens[{index}] is not an object")
                        shape_ok = False
                        continue
                    for key in schema.get("token_required", []):
                        if key not in token:
                            problems.append(f"{language}: tokens[{index}] missing {key!r}")
                            shape_ok = False
                for index, edge in enumerate(artifact.get("edges", [])):
                    if not isinstance(edge, dict):
                        problems.append(f"{language}: edges[{index}] is not an object")
                        shape_ok = False
                        continue
                    for key in schema.get("edge_required", []):
                        if key not in edge:
                            problems.append(f"{language}: edges[{index}] missing {key!r}")
                            shape_ok = False

        # ── Source + freshness ─────────────────────────────────────────────
        if source_path.exists():
            generated = None
            try:
                with tempfile.TemporaryDirectory() as tmpdir:
                    out_path = Path(tmpdir) / "generated.json"
                    subprocess.run([sys.executable, str(generator),
                                    "--input", str(source_path), "--output", str(out_path)],
                                   check=True, capture_output=True)
                    generated = json.loads(out_path.read_text(encoding="utf-8"))
            except Exception as exc:  # generator failure is a finding, not a crash
                problems.append(f"{language}: regeneration failed: {exc}")
            if generated is None:
                freshness = "invalid"
            elif not artifact_path.exists():
                freshness = "invalid"
            else:
                checked_in = json.loads(artifact_path.read_text(encoding="utf-8"))
                # Compare bidirectionally: a generated key that the artifact has
                # *dropped* is drift just as much as a differing value. Comparing
                # only shared keys would let deletion escape the equality check.
                dropped = [k for k in generated if k not in checked_in]
                mismatch = [k for k in generated
                            if k in checked_in and generated[k] != checked_in[k]]
                authored_only = [k for k in checked_in if k not in generated]
                if dropped or mismatch:
                    problems.append(f"{language}: regeneration disagrees "
                                    f"(dropped={dropped}, differs={mismatch})")
                    freshness = "invalid"
                else:
                    freshness = "verified"
                    if authored_only:
                        findings.append(
                            f"authored-not-generated keys present: {authored_only}")
        elif expected:
            problems.append(f"{language}: source declared expected but "
                            f"{entry.get('source')} is absent (damage or drift)")
            freshness = "invalid"
        else:
            freshness = "not_claimable"

        if not shape_ok or freshness == "invalid":
            verdict = "INVALID"
        elif freshness == "verified":
            verdict = "VERIFIED"
        else:
            verdict = "UNVERIFIED"
        rows.append((language, "present" if source_path.exists() else "no_source",
                     freshness, "valid" if shape_ok else "invalid", verdict, findings))

    for language, source, freshness, shape, verdict, findings in rows:
        print(f"info | {language:<11} source={source:<10} freshness={freshness:<14} "
              f"shape={shape:<8} verdict={verdict}")
        for finding in findings:
            print(f"info |   {language}: {finding}")
    for problem in problems:
        print("FAIL |", problem)
    graded = [r for r in rows if r[4] != "UNVERIFIED"]
    print(f"{'PASS' if not problems else 'FAIL'} | code-grammar-check: "
          f"{len(rows)} artifacts, {len(graded)} graded "
          f"({sum(1 for r in rows if r[4] == 'UNVERIFIED')} unverified/no_source), "
          f"{len(problems)} problem(s)")
    return 1 if problems else 0


# ── main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command",
                        choices=["ebnf-sync", "greeting-bank-compile", "advisor-bootstrap-compile",
                                 "advisor-check", "plan-bootstrap-compile", "plan-check", "validate", "gyro-align", "greetings-check", "control-check",
                                 "fold-geometry-check", "capability-gap",
                                 "code-grammar-check"])
    parser.add_argument("--repo-root", default=Path(__file__).resolve().parents[2])
    parser.add_argument("--sample", type=int, default=None,
                        help="validate only the first N rows per file (for quick checks)")
    parser.add_argument("--check", action="store_true",
                        help="capability-gap: verify the artifact is current instead of writing it")
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()
    if args.command == "ebnf-sync":
        return cmd_ebnf_sync(repo_root)
    if args.command == "greeting-bank-compile":
        return cmd_greeting_bank_compile(repo_root)
    if args.command == "advisor-bootstrap-compile":
        return cmd_advisor_bootstrap_compile(repo_root)
    if args.command == "advisor-check":
        return cmd_advisor_check(repo_root)
    if args.command == "plan-bootstrap-compile":
        return cmd_plan_bootstrap_compile(repo_root)
    if args.command == "plan-check":
        return cmd_plan_check(repo_root)
    if args.command == "validate":
        return cmd_validate(repo_root, sample_limit=args.sample)
    if args.command == "gyro-align":
        return cmd_gyro_align(repo_root)
    if args.command == "greetings-check":
        return cmd_greetings_check(repo_root)
    if args.command == "control-check":
        return cmd_control_check(repo_root)
    if args.command == "fold-geometry-check":
        return cmd_fold_geometry_check(repo_root)
    if args.command == "capability-gap":
        return cmd_capability_gap(repo_root, check=args.check)
    if args.command == "code-grammar-check":
        return cmd_code_grammar_check(repo_root)
    return 2


if __name__ == "__main__":
    sys.exit(main())
