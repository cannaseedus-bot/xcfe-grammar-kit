# KHANARY conformance

Run from the repository root:

```powershell
python -m pip install -r tools/requirements-conformance.txt
python tools/run_conformance.py
```

The runner configures `build/conformance` with the Visual Studio 2022 x64 generator,
builds the runtime and checks, then runs CTest. This output tree does not overwrite
the executable in `build/Release` or stop running services. It normalizes inherited
Windows environment keys to avoid the MSBuild `Path`/`PATH` collision. Existing
toolchain settings in this build tree are preserved; use a separate tree via
`--build-dir` when a different configuration is needed. DirectML remains optional;
the configure output states whether it is enabled.

For an already rebuilt tree, `python tools/run_conformance.py --skip-build` runs
verification only. `--config Debug` selects the debug configuration. CTest alone:

```powershell
ctest --test-dir build/conformance -C Release --no-tests=error --output-on-failure
```

`build/conformance/conformance-summary.json` is the stable result; the adjacent
`conformance.log` contains paths, timing and diagnostics. The summary distinguishes
unexecuted GPU/WGSL/Supernaut work from passing CPU and asset checks. These tests do
not establish GPU numerical equivalence or full compiler semantics.

## Admission contracts

`data/schema/micronaut-track.schema.json` and its shared types govern track
admission. A track must provide `@no-eval: true`, a nonempty `id`, a declared phase,
and a string array `governs`. Missing IDs/phases are rejected instead of inferred.
Declared numeric ranges, relation types and sandbox port types are checked before
registry mutation. Unknown extension properties remain allowed where the schema
says `additionalProperties: true`.

`data/schema/manifest-map.schema.json` accepts arrays containing strings or typed
XCFE conditionals. A conditional has `@if` (string or boolean), required `@then`
(token array), optional `@else`, and no unrelated keys. Nested conditionals use the
same definition. The registry validates the whole file before committing entries.
It lowers a conditional to a compact JSON token; loading/resolving it does not
execute it. Explicit `XCFEExecutor::eval(parse(token), context)` performs branch
selection. The shipped `words.manifest.json` retains its existing conditional.

Both native loaders resolve their schemas from the data root's sibling `schema/`
directory. Standalone callers must retain the `data/{tracks,manifest,schema}`
layout. Missing/unsupported schemas deny admission; schema files are trusted local
configuration, not request-supplied definitions. `src/util/schema.cpp` implements
the finite assertion vocabulary used by these schemas with offline sibling/$defs
references. It is not a general Draft 2020-12 implementation. Python's jsonschema
validator independently checks the same specimens and negative fixtures in
`data/conformance/contracts.json`.

Strict JSON admission requires one complete value, valid JSON whitespace/numbers,
unique keys, valid UTF-8 and paired Unicode escapes. Trailing bytes, unknown escapes,
unescaped controls, non-finite/out-of-range binary64 numbers, and nesting beyond
256 values are rejected. Serializing object keys and string controls now escapes
them correctly. Ordinary serialization also sorts keys and retains binary64
round-trip precision; consumers must not depend on the former unordered key layout
or six-digit `%g` rounding.

## Canonical identity

`json-c14n-v1` is this repository's compact binary64 JSON contract, not RFC 8785:

- Object keys sort by unsigned UTF-8 byte order; arrays preserve order.
- Strings must contain valid Unicode scalars; there is no Unicode normalization.
  Quote/backslash and controls are escaped using the short JSON escape where one
  exists, otherwise lowercase `\u00xx`; other scalars remain UTF-8.
- Numbers use the shortest round-tripping C++ `to_chars(general)` binary64 form.
  Zero, including negative zero, is `0`; exponent `e` is lowercase, a positive
  exponent has no `+`, and exponent leading zeros are removed. NaN/Infinity fail.
- There is no whitespace or terminal newline. SHA-256 binds these UTF-8 bytes.
  `data/conformance/canonical.json` provides independent byte and hash goldens.

`util::canonical_hash` returns `sha256:<lowercase hex>`. `cycle_identity_hash`
constructs an object containing exactly `topology_version`, `topology_hash`,
`candidate_delta_hash`, and `certificate_hash`, as required by
`data/schema/cycle-hash.spec.json`. The version is a nonempty string or a nonnegative
integer at most 2^53-1; each hash must already be a lowercase SHA-256 identity with
the prefix. Other run metadata is excluded. This computes identity, not a claim
that a certificate or topology is authorized or current.

The read-only JSON-RPC `cycle_identity` method accepts those fields and returns
`{"@kind":"khanary.cycle.identity.v1","ok":true,"hash":"sha256:..."}`;
invalid inputs return the same kind with `ok:false` and an error. It uses the same
native function as the canonical conformance suite. Existing projection ABI
hashing still binds verbatim payload bytes; old journal payloads are not rewritten.

## Dispatch authority

`RequestRouter::set_authorizer` binds a host policy callback. No callback, a denied
decision or an exception denies dispatch. The runtime binds phase-driver policy
and XCFE transition authorization using a host-created context. RPC payloads are
never copied into that authority context. `xcfe_authorized` and the legacy
`xcfe_authorize` payload keys may only narrow permission: either false or a
nonboolean value denies; true cannot override the host. Capability eligibility
and `MISS_capability` remain separate from authorization.

Driver-lock audit/enforce configuration is unchanged. This is execution-policy
provenance, not network authentication. Tests prove the registered handler is not
called for unbound, forged, denied, malformed or exception-producing policy cases.

## SDDS v1

The CPU reader in `src/gpu/sdds_format.cpp` validates the complete little-endian
SDDS container before `DDSLoader` allocates GPU resources. Magic/version, exact
chunk count, bounded file size (1.75 GiB), nonempty null-padded unique names,
nonzero element counts, recognized formats, exact data sizes, CRCs and EOF must
all agree. A short final chunk, bad CRC or trailing data rejects the whole file.
The count is limited to 65,536 nonempty chunks. Empty containers are invalid.

Format 0 uses four bytes per F32 element; format 1 counts packed uint32 words
(four Q8 values per word), matching the existing TensorBuffer ABI; format 2 uses
two bytes per F16 element. The loader sets stride explicitly. GPU allocation or
upload failure throws before a shard vector is returned. `validate_crc` delegates
to the same complete format validation. The separate model `.dds` forward path
and any SCXQ2 representation are not declared equivalent to SDDS by these checks.

## Verdict ABI and coverage

Native and Python suites emit one JSON stdout record of kind
`khanary.contract.verdict.v1`. Human diagnostics go to stderr. Exit codes are
0 (all assertions pass), 1 (conformance mismatch), and 2 (invalid invocation,
missing fixtures/dependencies or harness error). Cases are sorted by ID. A suite
hash is SHA-256 of LF-joined `suite|case-id|PASS-or-FAIL` rows, UTF-8 without a
terminal newline. The aggregate uses sorted `suite|verdict|summary_hash` rows.
No timings or absolute paths enter either hash. These are result fingerprints,
not repository-content digests; errors are distinguished by verdict/exit code.

The root CTest suite covers existing projection/AIML/attention behavior, router
policy and provenance, schema/native-loader parity, strict JSON rejection,
canonical byte/hash vectors, SDDS truncation/format rejection, KLSL parsing and IR
smoke coverage, and geometry asset integrity/schema bindings. The KLSL smoke also
checks malformed dimensions cannot stall parsing. It does not validate generated
HLSL/WGSL numerical equivalence; those compiler backends remain Partial.

Geometry checking reproduces packaged asset integrity, not historical hardware
experiments. The mesh hash and size, KNU glyph/version fields, kernel presence and
birdsong sidecar schema are checked locally. The upstream generator and GPU scratch
harnesses remain outside this checkout. Revisit GPU parity and training only when
those harnesses and a numerical reference are available.
