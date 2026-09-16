# ProsperoX Compatibility Import Audit Design

Date: 2026-09-16
Status: Proposed design approved in chat; implementation pending written-spec review
Target branch: `fix/fself-aligned-version`

## Purpose

ProsperoX currently discovers many compatibility gaps only when guest code reaches an unresolved strong import. This creates an inefficient loop: boot a title, hit one unresolved symbol, fix it, rebuild, and boot again.

Add a static compatibility-audit subsystem that parses a title's PS5 SELF/ELF binaries with ProsperoX's existing loader, resolves every statically referenced import against the same HLE symbol database used at runtime, and produces a complete report before guest execution. The same subsystem must also scan a library of installed titles and aggregate the results into a global compatibility backlog.

This is a diagnostic and planning tool. It must never fabricate ABI compatibility or automatically create production aliases merely because two exports share a NID.

## User-facing commands

Single title:

```text
kyty_emulator --audit-game <game-dir|elf> [--audit-json <file>]
```

Library scan:

```text
kyty_emulator --audit-library <root-dir> [--audit-json <file>]
```

Normal `--game` execution remains unchanged. Audit modes are mutually exclusive with normal execution and with each other.

`--audit-game` accepts the same directory-or-ELF input convention as `--game`.

`--audit-library` recursively discovers candidate game roots. A directory qualifies as a game root when it contains `eboot.bin`; `sce_sys/param.json` or `sce_sys/param.sfo` may be used as metadata when present but is not required for discovery.

## Scope of a title scan

For each discovered game, audit:

1. `eboot.bin`.
2. Executable/shared objects under `sce_module/`, including `.sprx` and other files that parse as valid PS5 ELF/SELF modules.
3. Additional executable modules that are explicitly referenced by `DT_NEEDED` and can be resolved inside the game tree.

Do not scan arbitrary media/data files. Binary eligibility is established by the existing `Loader::Elf64` parser rather than filename alone.

No guest entry point, module initializer, GPU path, audio path, or game code is executed in audit mode.

## Core architecture

### 1. `loader/importAudit.h/.cpp`

Introduce a focused loader-side component responsible for auditing one parsed binary and aggregating title/library results.

Proposed core structures:

```text
ImportAuditStatus
  ExactHle
  CompatibleHle
  AliasCandidate
  GuestModule
  WeakUnresolved
  MissingHle
  Malformed

ImportAuditRecord
  binary path
  NID/name
  symbol type
  bind type
  requested library + version
  requested module + major/minor version
  qualified requested name
  status
  resolved qualified name when applicable
  candidate qualified names when applicable

BinaryAuditResult
  binary path
  parse status / diagnostic
  imports[]

GameAuditResult
  game root
  title metadata when available
  binaries[]
  deduplicated imports

LibraryAuditResult
  games[]
  globally deduplicated imports
  affected-game counts
```

The audit layer must use loader data structures and `SymbolDatabase`; it must not maintain a separate PS5 import parser.

### 2. Import extraction

Reuse `Loader::Elf64` dynamic metadata:

- `DT_OS_IMPORT_LIB` / `DT_OS_IMPORT_LIB_1`
- `DT_OS_NEEDED_MODULE` / `DT_OS_NEEDED_MODULE_1`
- symbol table and string table
- `DT_OS_JMPREL`, `DT_OS_RELA`, and corresponding sizes/entry sizes
- ELF symbol binding/type

Extract every relocation that references an imported symbol. Resolve the compact library/module IDs through the same metadata rules already used by `RuntimeLinker::ParseProgramDynamicInfo` and `RuntimeLinker::Resolve`.

Where production parsing logic is currently private inside `RuntimeLinker`, refactor only the minimum common parsing/resolution helpers needed by both runtime loading and audit mode. Do not duplicate those algorithms.

### 3. Resolution classification

For each import, apply the following order:

1. **ExactHle** — exact qualified HLE symbol exists.
2. **CompatibleHle** — `FindExactOrCompatible` resolves through an already-approved bounded compatibility rule.
3. **GuestModule** — an exact export from another module in the audited game's module set satisfies the requested import.
4. **WeakUnresolved** — unresolved symbol has weak binding.
5. **AliasCandidate** — unresolved strong import has one or more HLE symbols with the same NID and symbol type under a different qualification.
6. **MissingHle** — unresolved strong import has no HLE NID match and no guest-module provider.
7. **Malformed** — binary metadata is malformed or cannot be safely interpreted.

An AliasCandidate is evidence for review, not an automatic compatibility rule.

Candidate matching must preserve symbol type. Version/module/library differences must be printed explicitly.

### 4. Symbol database support

Add non-mutating query support needed by the audit layer, for example returning all records matching a NID + `SymbolType`, rather than relying on the current first-match `FindByNid` behavior.

Existing exact and bounded compatibility resolution semantics remain unchanged.

### 5. Game-module graph

Audit all eligible modules for a game before final classification so a strong import is not mislabeled MissingHle when a bundled SPRX exports it.

The first pass parses binaries and collects imports/exports. The second pass resolves imports against:

1. ProsperoX HLE symbols;
2. approved compatibility aliases;
3. exports from the game's parsed module set.

No module initializers are run.

## Library scanning and aggregation

`--audit-library` recursively discovers game roots and invokes the same single-game auditor for each root.

Global aggregation keys unresolved imports by the complete requested identity:

```text
NID + symbol type + library/version + module/version
```

For each global record, include:

- number of affected games;
- affected title IDs/names when available;
- status;
- existing HLE candidate identities, if any;
- set of binaries that reference it.

Sort the human-readable global blocker section by:

1. affected-game count descending;
2. AliasCandidate before MissingHle;
3. qualified symbol name for deterministic output.

This turns a collection of games into a compatibility work queue without making an evaluative claim about game compatibility.

## Output

### Console

Console output has three layers:

```text
GAME <title-id> <title-name>
  binaries=<n> imports=<n> exact=<n> compatible=<n> guest=<n>
  aliases=<n> missing=<n> weak=<n> malformed=<n>

[ALIAS_CANDIDATE]
X-Nm5KLREeg[AgcDriver_v1][AgcDriver_v1.1][Func]
  existing HLE candidates:
    ...

[MISSING_HLE]
...
```

Library mode additionally prints a global summary and deduplicated blockers.

### JSON

`--audit-json <file>` writes a versioned machine-readable document:

```json
{
  "schema_version": 1,
  "mode": "game|library",
  "generated_by": "ProsperoX",
  "games": [],
  "global_imports": []
}
```

The JSON schema is deterministic: arrays with no semantic ordering are explicitly sorted before serialization. Paths are emitted in normalized generic form.

JSON output enables later dashboards, CI comparisons, and compatibility-database tooling without coupling those features to the first implementation.

## Exit codes

Audit mode must distinguish tooling failure from compatibility findings:

- `0`: audit completed, even if unresolved compatibility items exist;
- `2`: command-line usage error;
- `3`: requested audit root cannot be scanned;
- `4`: audit completed only partially because one or more candidate binaries failed structurally; report still written when possible.

Missing HLE imports are findings, not process failures.

## Runtime dynamic-import coverage

Static auditing cannot predict every runtime `sceKernelDlsym` request or imports selected only by dynamic control flow.

Do not mix dynamic execution into the static audit command. Instead, preserve a second-stage design seam for a later runtime trace mode that records dynamic symbol requests using the same qualified-identity/result schema. Static and runtime records can then be merged by tooling.

The first implementation is complete without runtime trace mode, but the audit data model must not prevent adding it.

## Safety and correctness constraints

- Never execute guest code in audit mode.
- Never create aliases automatically from NID equality.
- Never downgrade a strong unresolved import to weak.
- Never resolve across symbol types.
- Never ignore library/module/version identity in exact resolution.
- Never treat an invalid SELF/ELF as a compatibility result; report it as Malformed with the existing loader diagnostic.
- Continue to use bounded, explicitly reviewed compatibility aliases in `SymbolDatabase`.
- No title-ID-specific compatibility hacks in the resolver.

## CLI integration

Extend the existing `main.cpp` argument parser with audit options while keeping `RunOptions` for execution concerns. Prefer a top-level operation/mode object rather than overloading `RunOptions` with library-scan state.

Suggested shape:

```text
CommandMode { RunGame, AuditGame, AuditLibrary }
CommandOptions
  mode
  RunOptions run
  audit_input
  audit_json
```

Normal invocation behavior and current flags stay backward-compatible.

Audit modes should initialize only the subsystems required for loader/HLE symbol registration and metadata access. In particular, do not initialize Vulkan/Graphics merely to audit imports.

## Testing strategy

Follow TDD and use only synthetic/original fixtures in repository tests.

### Unit tests

1. Exact HLE import classification.
2. Existing bounded alias classification (`CompatibleHle`).
3. Same-NID/different-library candidate classified `AliasCandidate` without resolving it.
4. Same NID but wrong symbol type is not a candidate.
5. Strong unknown import -> `MissingHle`.
6. Weak unknown import -> `WeakUnresolved`.
7. Import satisfied by a synthetic companion module -> `GuestModule`.
8. Malformed/truncated dynamic metadata -> `Malformed`, no crash/out-of-bounds read.
9. Duplicate relocations deduplicate correctly while preserving reference counts.
10. Global aggregation counts affected games, not raw relocation count.
11. Deterministic ordering and JSON serialization.

### CLI/integration tests

1. `--audit-game` rejects nonexistent input.
2. `--audit-game` and `--game` are mutually exclusive.
3. `--audit-library` discovers multiple synthetic game roots.
4. Audit mode does not initialize Graphics/Vulkan.
5. A synthetic game containing `eboot.bin` + companion module resolves guest-module imports in the second pass.
6. Existing normal game CLI parsing remains unchanged.

### Regression coverage

The already-observed OpenPsId and Coredump qualification patterns must be represented with synthetic qualified symbols only; no commercial binary bytes, paths, hashes, or title IDs belong in test fixtures.

## Initial implementation boundaries

Included in v1:

- static audit of one game;
- recursive library scan;
- eboot + bundled module scan;
- exact/compatible/guest/weak/alias-candidate/missing/malformed classification;
- console output;
- deterministic JSON output;
- global deduplication and affected-game counts;
- synthetic tests.

Explicitly deferred:

- automatically adding compatibility aliases;
- automatically generating HLE stubs;
- downloading external symbol databases;
- network calls;
- GUI integration;
- runtime `sceKernelDlsym` trace merging;
- compatibility scoring/ranking.

## Acceptance criteria

The feature is ready when:

1. Auditing a valid game parses its binaries and exits without executing guest code.
2. A single audit reports all statically referenced imports rather than stopping at the first unresolved call.
3. Already implemented-but-differently-qualified NIDs appear as AliasCandidate or CompatibleHle as appropriate.
4. Bundled guest-module exports prevent false MissingHle findings.
5. Library mode scans multiple game roots and produces one deterministic global report.
6. JSON and console summaries agree on counts.
7. Existing runtime import resolution tests and SELF-loader tests remain green.
8. New synthetic audit tests cover the classification and aggregation rules above.
9. No title-specific resolver hacks are introduced.
