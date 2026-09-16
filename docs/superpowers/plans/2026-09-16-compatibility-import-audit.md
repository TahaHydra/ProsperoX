# Compatibility Import Audit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add static `--audit-game` and recursive `--audit-library` modes that enumerate every statically referenced PS5 import, classify it against ProsperoX HLE and bundled guest modules, and emit deterministic console/JSON compatibility reports without executing guest code.

**Architecture:** Reuse `RuntimeLinker::LoadProgram` to parse/map SELF/ELF metadata exactly as normal runtime loading does, but never call relocation, module start, or guest entry execution. Add a non-mutating import-inspection API to the runtime linker, a focused `loader/importAudit.*` component for extraction/classification/aggregation, and a small CLI operation layer in `main.cpp`. The auditor loads all eligible binaries first, then classifies imports in a second pass so bundled SPRX exports are available before any import is labeled missing.

**Tech Stack:** C++20, existing ProsperoX `Loader::Elf64`/`RuntimeLinker`/`SymbolDatabase`, `std::filesystem`, `nlohmann_json`, CMake/CTest, PowerShell Windows Phase 0 toolchain.

**Spec:** `docs/superpowers/specs/2026-09-16-compatibility-import-audit-design.md`

## Global Constraints

- Audit mode must never execute guest entry points, module initializers, GPU work, audio work, or game code.
- Audit mode may map executable segments through the existing loader, but must stop before `RelocateAll`, `StartAllModules`, or `RuntimeLinker::Execute`.
- Never create compatibility aliases automatically from equal NIDs.
- Never resolve across symbol types.
- Never downgrade a strong unresolved import to weak.
- Exact resolution must preserve NID, symbol type, library/version, and module/version identity.
- Malformed SELF/ELF input is reported as `Malformed`; it is not reinterpreted as a compatibility result.
- Existing bounded aliases in `SymbolDatabase::FindExactOrCompatible` remain the only production compatibility aliases.
- No title-ID-specific resolver hacks.
- Static audit findings are informational: unresolved imports do not make the audit process itself fail.
- Tests use synthetic/original fixtures only; no SDK, firmware, commercial bytes, title hashes, or commercial paths.

---

### Task 1: Add non-mutating import-resolution introspection

**Files:**
- Modify: `src/loader/symbolDatabase.h`
- Modify: `src/loader/symbolDatabase.cpp`
- Modify: `src/loader/runtimeLinker.h`
- Modify: `src/loader/runtimeLinker.cpp`
- Create: `tests/ImportAuditTests.inc`
- Modify: `tests/Phase0RuntimeProbes.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: existing `Program`, `DynamicInfo`, `LibraryId`, `ModuleId`, `SymbolResolve`, `SymbolRecord`.
- Produces:

```cpp
namespace Loader {

enum class ImportResolutionSource {
    None,
    ExactHle,
    CompatibleHle,
    GuestModule,
};

struct ImportResolutionInspection {
    ImportResolutionSource source = ImportResolutionSource::None;
    SymbolRecord           record {};
};

[[nodiscard]] bool DecodeImportIdentity(const Program& program, const std::string& encoded_name,
                                        SymbolType type, SymbolResolve* out);

// SymbolDatabase
[[nodiscard]] std::vector<SymbolRecord> FindAllByNid(const std::string& nid,
                                                      SymbolType type) const;

// RuntimeLinker
[[nodiscard]] ImportResolutionInspection InspectImport(const Program& requester,
                                                       const SymbolResolve& request) const;
```

`DecodeImportIdentity()` is the shared source of truth for converting `NID#library-id#module-id` into a fully qualified `SymbolResolve`. `RuntimeLinker::Resolve()` must call it rather than duplicating ID lookup logic. `InspectImport()` is read-only and must check exact HLE, bounded-compatible HLE, then guest exports in that order.

- [ ] **Step 1: Write failing synthetic tests for decode, exact HLE, compatible HLE, guest resolution, and same-NID enumeration**

Add `tests/ImportAuditTests.inc` with a `PhaseImportAudit::ResolutionInspection()` test. Use synthetic identities only:

```cpp
namespace PhaseImportAudit {

static Loader::SymbolResolve MakeRequest(const char* nid, const char* library,
                                         const char* module,
                                         Loader::SymbolType type = Loader::SymbolType::Func) {
    return {nid, library, 1, module, 1, 1, type};
}

int ResolutionInspection() {
    using namespace Loader;

    SymbolDatabase symbols;
    const auto exact = MakeRequest("AuditExactNid", "AuditLib", "AuditMod");
    symbols.Add(exact, 0x1000, "AuditExact");
    symbols.Add(MakeRequest("AuditCandidateNid", "CanonicalLib", "CanonicalMod"),
                0x2000, "AuditCandidate");

    const auto all = symbols.FindAllByNid("AuditCandidateNid", SymbolType::Func);
    Phase1::Check(all.size() == 1 && all[0].vaddr == 0x2000,
                  "audit enumerates all same-NID same-type HLE candidates");

    SymbolResolve wrong_type = MakeRequest("AuditCandidateNid", "CanonicalLib", "CanonicalMod",
                                           SymbolType::Object);
    Phase1::Check(symbols.FindAllByNid(wrong_type.name, wrong_type.type).empty(),
                  "audit candidate enumeration preserves symbol type");

    // Existing OpenPsId bounded rule is the compatibility fixture; it is synthetic
    // test metadata and exercises the actual compatibility path.
    SymbolResolve compat{"DLORcroUqbc", "OpenPsId", 1, "libkernel", 1, 1,
                         SymbolType::Func};
    // The test target already registers Libs::InitAll on its runtime linker.
    auto* rt = Common::Singleton<RuntimeLinker>::Instance();
    Program requester;
    requester.rt = rt;
    requester.dynamic_info = std::make_unique<DynamicInfo>();
    requester.dynamic_info->import_libs.push_back({"L", 1, "OpenPsId"});
    requester.dynamic_info->import_modules.push_back({"M", 1, 1, "libkernel"});

    SymbolResolve decoded{};
    Phase1::Check(DecodeImportIdentity(requester, "DLORcroUqbc#L#M",
                                       SymbolType::Func, &decoded) &&
                      SymbolDatabase::GenerateName(decoded) ==
                          "DLORcroUqbc[OpenPsId_v1][libkernel_v1.1][Func]",
                  "audit shares runtime qualified-import decoding");

    const auto compatible = rt->InspectImport(requester, decoded);
    Phase1::Check(compatible.source == ImportResolutionSource::CompatibleHle &&
                      compatible.record.vaddr != 0,
                  "audit identifies bounded compatible HLE resolution");

    std::puts("IMPORT_AUDIT_PASS resolution inspection");
    return 0;
}

} // namespace PhaseImportAudit
```

Wire `--import-audit-resolution` into `tests/Phase0RuntimeProbes.cpp` and add:

```cmake
add_test(NAME import_audit_resolution
    COMMAND $<TARGET_FILE:phase0_runtime_probes> --import-audit-resolution)
```

- [ ] **Step 2: Run the new test and verify RED**

Run:

```powershell
. .\scripts\phase0\windows-env.ps1
cmake --build --preset phase0-windows --parallel 10 --target phase0_runtime_probes
ctest --test-dir .\_Build\phase0-windows -R "^import_audit_resolution$" --output-on-failure
```

Expected: build/test failure because `FindAllByNid`, `DecodeImportIdentity`, and `InspectImport` do not exist yet.

- [ ] **Step 3: Implement `FindAllByNid`**

Add to `SymbolDatabase`:

```cpp
std::vector<SymbolRecord> SymbolDatabase::FindAllByNid(const std::string& nid,
                                                       SymbolType type) const {
    const auto prefix = nid + "[";
    const auto suffix = fmt::format("[{}]", Common::EnumName(type).c_str());
    std::vector<SymbolRecord> out;
    for (const auto& symbol: m_symbols) {
        if (Common::StartsWith(symbol.name, prefix) && Common::EndsWith(symbol.name, suffix)) {
            out.push_back(symbol);
        }
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });
    return out;
}
```

Add `<algorithm>` if not already directly included.

- [ ] **Step 4: Extract shared import identity decoding from `RuntimeLinker::Resolve`**

Move the current `ids = Common::Split(name, '#')`, `FindLibrary`, `FindModule`, and `SymbolResolve` construction into:

```cpp
bool DecodeImportIdentity(const Program& program, const std::string& encoded_name,
                          SymbolType type, SymbolResolve* out) {
    if (out == nullptr || program.dynamic_info == nullptr) return false;
    const auto ids = Common::Split(encoded_name, '#');
    if (ids.size() != 3) return false;

    const auto lib = std::find_if(program.dynamic_info->import_libs.begin(),
                                  program.dynamic_info->import_libs.end(),
                                  [&](const LibraryId& value) { return value.id == ids[1]; });
    const auto mod = std::find_if(program.dynamic_info->import_modules.begin(),
                                  program.dynamic_info->import_modules.end(),
                                  [&](const ModuleId& value) { return value.id == ids[2]; });
    if (lib == program.dynamic_info->import_libs.end() ||
        mod == program.dynamic_info->import_modules.end()) return false;

    *out = {ids[0], lib->name, lib->version, mod->name,
            mod->version_major, mod->version_minor, type};
    return true;
}
```

Change `RuntimeLinker::Resolve()` to call this helper. Preserve existing unresolved behavior by setting `out_info->vaddr = 0` and the generated qualified name when decoding succeeds but no record resolves.

- [ ] **Step 5: Implement `RuntimeLinker::InspectImport` and make `Resolve` share it**

Use exact HLE first, then bounded compatibility, then guest module lookup with the existing `FindProgram` semantics:

```cpp
ImportResolutionInspection RuntimeLinker::InspectImport(const Program& requester,
                                                        const SymbolResolve& request) const {
    if (m_symbols != nullptr) {
        const auto qualified = SymbolDatabase::GenerateName(request);
        if (const auto* exact = m_symbols->FindExact(qualified); exact != nullptr)
            return {ImportResolutionSource::ExactHle, *exact};
        if (const auto* compatible = m_symbols->FindExactOrCompatible(qualified);
            compatible != nullptr)
            return {ImportResolutionSource::CompatibleHle, *compatible};
    }

    const ModuleId wanted_module{"", request.module_version_major,
                                 request.module_version_minor, request.module};
    const LibraryId wanted_library{"", request.library_version, request.library};
    if (auto* provider = FindProgram(wanted_module, wanted_library);
        provider != nullptr && provider->export_symbols != nullptr) {
        if (const auto* record = provider->export_symbols->FindExact(
                SymbolDatabase::GenerateName(request)); record != nullptr) {
            return {ImportResolutionSource::GuestModule, *record};
        }
    }
    return {};
}
```

Because `FindProgram` is currently non-const, either make its lookup logically const or implement a private const overload; do not cast away constness.

- [ ] **Step 6: Re-run the resolution test and existing import tests**

Run:

```powershell
cmake --build --preset phase0-windows --parallel 10 --target phase0_runtime_probes
ctest --test-dir .\_Build\phase0-windows -R "^(import_audit_resolution|phase1_imports)$" --output-on-failure
```

Expected: both tests PASS.

- [ ] **Step 7: Commit Task 1**

```powershell
git add src/loader/symbolDatabase.h src/loader/symbolDatabase.cpp src/loader/runtimeLinker.h src/loader/runtimeLinker.cpp tests/ImportAuditTests.inc tests/Phase0RuntimeProbes.cpp CMakeLists.txt
git commit -m "loader: expose import resolution inspection"
```

---

### Task 2: Implement the audit data model and pure classification rules

**Files:**
- Create: `src/loader/importAudit.h`
- Create: `src/loader/importAudit.cpp`
- Modify: `tests/ImportAuditTests.inc`
- Modify: `tests/Phase0RuntimeProbes.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 `DecodeImportIdentity`, `InspectImport`, `FindAllByNid`.
- Produces:

```cpp
namespace Loader::ImportAudit {

enum class Status {
    ExactHle,
    CompatibleHle,
    AliasCandidate,
    GuestModule,
    WeakUnresolved,
    MissingHle,
    Malformed,
};

struct ImportRecord {
    std::string binary;
    std::string nid;
    SymbolType  symbol_type = SymbolType::Unknown;
    bool        weak = false;
    SymbolResolve request {};
    std::string qualified_name;
    Status      status = Status::Malformed;
    std::string resolved_name;
    std::vector<std::string> candidates;
    uint64_t    references = 1;
};

struct BinaryResult {
    std::string path;
    bool parsed = false;
    std::string diagnostic;
    std::vector<ImportRecord> imports;
};

struct GameResult {
    std::filesystem::path root;
    std::string title_id;
    std::string title_name;
    std::vector<BinaryResult> binaries;
    std::vector<ImportRecord> imports;
    bool partial = false;
};

struct GlobalImportRecord {
    ImportRecord import;
    std::vector<std::string> games;
    std::vector<std::string> binaries;
};

struct LibraryResult {
    std::vector<GameResult> games;
    std::vector<GlobalImportRecord> global_imports;
    bool partial = false;
};

[[nodiscard]] ImportRecord ClassifyImport(const RuntimeLinker& linker,
                                          const Program& requester,
                                          const SymbolResolve& request,
                                          SymbolType type, bool weak,
                                          std::string binary);
```

- [ ] **Step 1: Add failing pure classification tests**

Extend `tests/ImportAuditTests.inc` with cases for exact HLE, compatible HLE, same-NID alias candidate, wrong-type non-candidate, strong missing, and weak missing. The alias test must use a synthetic `SymbolDatabase` entry with the same NID under a different qualification and assert that the candidate is reported but not resolved.

Example assertions:

```cpp
Phase1::Check(alias.status == Status::AliasCandidate &&
                  alias.candidates.size() == 1 && alias.resolved_name.empty(),
              "same-NID different-qualification stays review-only alias candidate");
Phase1::Check(missing.status == Status::MissingHle,
              "unknown strong import is missing HLE");
Phase1::Check(weak.status == Status::WeakUnresolved,
              "unknown weak import remains weak unresolved");
```

Wire `--import-audit-classification` and a `import_audit_classification` CTest entry.

- [ ] **Step 2: Verify RED**

```powershell
cmake --build --preset phase0-windows --parallel 10 --target phase0_runtime_probes
ctest --test-dir .\_Build\phase0-windows -R "^import_audit_classification$" --output-on-failure
```

Expected: FAIL because `Loader::ImportAudit` does not exist.

- [ ] **Step 3: Implement classification in the required order**

`ClassifyImport()` must:

```cpp
const auto inspected = linker.InspectImport(requester, request);
switch (inspected.source) {
    case ImportResolutionSource::ExactHle:       status = Status::ExactHle; break;
    case ImportResolutionSource::CompatibleHle:  status = Status::CompatibleHle; break;
    case ImportResolutionSource::GuestModule:    status = Status::GuestModule; break;
    case ImportResolutionSource::None:            break;
}
```

If unresolved and `weak == true`, return `WeakUnresolved` before candidate search. Otherwise call `linker.Symbols()->FindAllByNid(request.name, type)`. Non-empty same-type results become `AliasCandidate`; empty results become `MissingHle`. Sort candidate qualified names lexicographically.

- [ ] **Step 4: Add deterministic dedup key helper**

Add:

```cpp
[[nodiscard]] std::string ImportKey(const ImportRecord& record);
```

The key is exactly:

```text
NID|SymbolType|library@version|module@major.minor
```

Do not include binary path or weak/strong state in the identity; when combining duplicates, preserve strong if any reference is strong and sum `references`.

- [ ] **Step 5: Re-run classification + Task 1 tests**

```powershell
ctest --test-dir .\_Build\phase0-windows -R "^(import_audit_resolution|import_audit_classification|phase1_imports)$" --output-on-failure
```

Expected: all PASS.

- [ ] **Step 6: Commit Task 2**

```powershell
git add src/loader/importAudit.h src/loader/importAudit.cpp tests/ImportAuditTests.inc tests/Phase0RuntimeProbes.cpp CMakeLists.txt
git commit -m "loader: add import audit classification"
```

---

### Task 3: Extract imports from loaded binaries and classify a complete game module graph

**Files:**
- Modify: `src/loader/importAudit.h`
- Modify: `src/loader/importAudit.cpp`
- Modify: `tests/ImportAuditTests.inc`
- Modify: `tests/Phase0RuntimeProbes.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `RuntimeLinker::LoadProgram`, public `Program::dynamic_info`, Task 2 classification.
- Produces:

```cpp
namespace Loader::ImportAudit {

struct GameOptions {
    std::filesystem::path input;
};

[[nodiscard]] GameResult AuditGame(RuntimeLinker* linker, const GameOptions& options);

// Testable lower-level helper: the Program must already be loaded and all game
// companion modules must already be present in the same RuntimeLinker.
[[nodiscard]] BinaryResult AuditProgram(const RuntimeLinker& linker,
                                        const Program& program,
                                        const std::filesystem::path& game_root);
}
```

- [ ] **Step 1: Extend the existing synthetic ELF fixture with one qualified imported function**

Reuse the dynamic-metadata construction pattern already present in `Phase1RuntimeTests.inc`: `DT_OS_STRTAB`, `DT_OS_SYMTAB`, `DT_OS_SYMTABSZ`, `DT_OS_RELA`, `DT_OS_RELASZ`, and `PT_OS_DYNLIBDATA`. Extend the synthetic string table with:

```text
\0AuditMissingNid#L#M\0AuditLib\0AuditMod\0
```

Add matching `DT_OS_IMPORT_LIB` and `DT_OS_NEEDED_MODULE` values whose encoded IDs are `L` and `M`, and one undefined global `STT_FUNC` symbol referenced by a relocation.

Do not use any commercial binary bytes.

- [ ] **Step 2: Add a failing `AuditProgram` test**

Load the synthetic binary through the real `RuntimeLinker::LoadProgram`, call `AuditProgram`, and assert:

```cpp
Phase1::Check(result.parsed && result.imports.size() == 1,
              "audit extracts every synthetic imported relocation");
Phase1::Check(result.imports[0].qualified_name ==
                  "AuditMissingNid[AuditLib_v1][AuditMod_v1.1][Func]",
              "audit decodes qualified import identity using runtime metadata");
Phase1::Check(result.imports[0].status == Status::MissingHle,
              "synthetic strong unknown import is classified missing");
```

Also duplicate the relocation and assert the output has one deduplicated import with `references == 2`.

- [ ] **Step 3: Verify RED**

```powershell
cmake --build --preset phase0-windows --parallel 10 --target phase0_runtime_probes
ctest --test-dir .\_Build\phase0-windows -R "^import_audit_program$" --output-on-failure
```

Expected: FAIL because `AuditProgram` does not yet extract relocation imports.

- [ ] **Step 4: Implement bounded relocation extraction**

In `AuditProgram()` validate before indexing:

```cpp
const auto* info = program.dynamic_info.get();
if (info == nullptr || info->str_table == nullptr || info->symbol_table == nullptr) {
    return malformed("missing dynamic symbol metadata");
}
if (info->symbol_table_entry_size != 0 &&
    info->symbol_table_entry_size != sizeof(Elf64_Sym)) {
    return malformed("unsupported symbol entry size");
}
```

For both `jmprela_table` and `rela_table`, require size to be a multiple of `sizeof(Elf64_Rela)`. For each relocation, only inspect `R_X86_64_64`, `R_X86_64_GLOB_DAT`, and `R_X86_64_JUMP_SLOT`. Bounds-check `GetSymbol()` against `symbol_table_total_size / sizeof(Elf64_Sym)`, skip local and defined-self symbols, convert `STT_NOTYPE`, `STT_FUNC`, `STT_OBJECT` to `SymbolType`, and call `DecodeImportIdentity()`.

If a relocation or string-table reference is structurally invalid despite the loader accepting the container, mark the binary `Malformed` with a bounded diagnostic rather than reading past metadata.

- [ ] **Step 5: Implement game binary discovery and two-pass loading**

`AuditGame()` must normalize directory/file input like `--game`:

```cpp
if (std::filesystem::is_directory(input)) {
    root = input;
    eboot = root / "eboot.bin";
} else {
    root = input.parent_path();
    eboot = input;
}
```

Build a candidate set containing:

1. eboot;
2. `.sprx`/`.prx` files in root, `sce_module`, and `sce_modules`;
3. exact `DT_NEEDED` basenames discovered after loading, resolved through a recursively built `basename -> paths` index under the game root.

Load each unique candidate with `RuntimeLinker::LoadProgram()` but do **not** call `RelocateProgram`, `RelocateAll`, `StartModule`, `StartAllModules`, `PreloadAdjacentPrograms`, or `Execute`.

After the load closure is complete, run `AuditProgram()` for every successfully loaded program. This second pass is mandatory so `InspectImport()` can classify guest-module providers.

- [ ] **Step 6: Add a synthetic companion-module guest-resolution test**

Create two synthetic runtime-loadable ELF fixtures: eboot imports `AuditGuestNid#L#M`; companion `.sprx` exports the exact same qualified identity. Load both into the same linker, audit eboot, and assert `Status::GuestModule` rather than `MissingHle`.

- [ ] **Step 7: Add malformed binary coverage**

Use the existing truncation/malformed dynamic fixtures. Assert `GameResult.partial == true`, `BinaryResult.parsed == false`, and `diagnostic` is non-empty. The auditor must continue scanning other binaries in the same synthetic game.

- [ ] **Step 8: Run program/game audit tests and existing loader regressions**

```powershell
ctest --test-dir .\_Build\phase0-windows -R "^(import_audit_|phase1_executables|phase1_imports|phase6_self_repack)$" --output-on-failure
```

Expected: all selected tests PASS.

- [ ] **Step 9: Commit Task 3**

```powershell
git add src/loader/importAudit.h src/loader/importAudit.cpp tests/ImportAuditTests.inc tests/Phase0RuntimeProbes.cpp CMakeLists.txt
git commit -m "loader: audit static imports for a complete game"
```

---

### Task 4: Add deterministic global aggregation and JSON/console reporting

**Files:**
- Modify: `src/loader/importAudit.h`
- Modify: `src/loader/importAudit.cpp`
- Modify: `tests/ImportAuditTests.inc`

**Interfaces:**
- Consumes: `GameResult`, `ImportRecord`.
- Produces:

```cpp
namespace Loader::ImportAudit {
[[nodiscard]] LibraryResult Aggregate(std::vector<GameResult> games);
[[nodiscard]] nlohmann::ordered_json ToJson(const GameResult& result);
[[nodiscard]] nlohmann::ordered_json ToJson(const LibraryResult& result);
void Print(const GameResult& result, FILE* out = stdout);
void Print(const LibraryResult& result, FILE* out = stdout);
[[nodiscard]] bool WriteJson(const std::filesystem::path& path,
                             const nlohmann::ordered_json& value,
                             std::string* error);
}
```

- [ ] **Step 1: Add failing aggregation tests**

Construct three in-memory `GameResult`s where two games reference the same unresolved qualified import multiple times. Assert global aggregation counts two affected games, not relocation count, and deduplicates binary lists.

```cpp
Phase1::Check(global.games.size() == 2,
              "global audit counts affected games rather than relocations");
Phase1::Check(global.import.references == 5,
              "global audit preserves total static reference count");
```

- [ ] **Step 2: Add failing deterministic ordering/JSON tests**

Insert imports in reverse/random order and assert two serializations are byte-identical. Assert blocker ordering is affected-game count descending, then `AliasCandidate` before `MissingHle`, then qualified name.

- [ ] **Step 3: Verify RED**

```powershell
cmake --build --preset phase0-windows --parallel 10 --target phase0_runtime_probes
ctest --test-dir .\_Build\phase0-windows -R "^import_audit_(aggregation|json)$" --output-on-failure
```

Expected: FAIL because aggregation/reporting APIs do not exist.

- [ ] **Step 4: Implement aggregation**

Key global records by `ImportKey(record)`. Merge game identity using `title_id` when non-empty, otherwise normalized game-root path. Merge binary paths uniquely. Sum static `references`. If any reference is strong, the merged record must not become `WeakUnresolved`.

- [ ] **Step 5: Implement schema-version-1 JSON**

Top-level shape:

```json
{
  "schema_version": 1,
  "mode": "game",
  "generated_by": "ProsperoX",
  "games": [],
  "global_imports": []
}
```

For game mode, `global_imports` is omitted or an empty array consistently; choose empty array so consumers have one stable schema. Every import object includes `qualified_name`, `nid`, `symbol_type`, `weak`, `status`, `resolved_name`, `candidates`, `references`, and normalized `binary`.

Use `nlohmann::ordered_json` and sort every semantically unordered array before insertion.

- [ ] **Step 6: Implement concise console reporting**

Print one summary per game and detailed sections only for `AliasCandidate`, `MissingHle`, and `Malformed`. Library mode prints the global summary first, followed by globally deduplicated blocker sections with affected-game counts.

Do not label or score a game's overall compatibility.

- [ ] **Step 7: Re-run aggregation/JSON tests**

```powershell
ctest --test-dir .\_Build\phase0-windows -R "^import_audit_(aggregation|json)$" --output-on-failure
```

Expected: both PASS.

- [ ] **Step 8: Commit Task 4**

```powershell
git add src/loader/importAudit.h src/loader/importAudit.cpp tests/ImportAuditTests.inc
git commit -m "loader: report and aggregate import audits"
```

---

### Task 5: Implement recursive library scanning

**Files:**
- Modify: `src/loader/importAudit.h`
- Modify: `src/loader/importAudit.cpp`
- Modify: `tests/ImportAuditTests.inc`

**Interfaces:**
- Consumes: `AuditGame`, `Aggregate`.
- Produces:

```cpp
namespace Loader::ImportAudit {
struct LibraryOptions {
    std::filesystem::path root;
};
[[nodiscard]] LibraryResult AuditLibrary(const SymbolDatabase& hle_template,
                                         const LibraryOptions& options);
[[nodiscard]] std::vector<std::filesystem::path>
DiscoverGameRoots(const std::filesystem::path& root);
}
```

Implementation may create one fresh runtime-linker instance per game so loaded guest modules from one title can never satisfy another title's imports.

- [ ] **Step 1: Add failing discovery test with multiple synthetic roots**

Create a temporary tree:

```text
root/A/eboot.bin
root/B/deeper/eboot.bin
root/not-a-game/random.bin
```

Assert `DiscoverGameRoots()` returns exactly `A` and `B/deeper`, normalized and sorted, with no nested duplicate if a discovered game contains another unrelated data directory.

- [ ] **Step 2: Verify RED**

```powershell
ctest --test-dir .\_Build\phase0-windows -R "^import_audit_library$" --output-on-failure
```

Expected: FAIL because recursive discovery/library auditing does not exist.

- [ ] **Step 3: Implement bounded recursive discovery**

Use `std::filesystem::recursive_directory_iterator` with `directory_options::skip_permission_denied`. A directory is a game root only when it contains a regular file named `eboot.bin`. Once a game root is found, do not recursively treat its subdirectories as additional game roots unless they independently contain another `eboot.bin`.

Sort and deduplicate normalized paths before scanning.

- [ ] **Step 4: Implement isolated per-game audit execution**

For every game root, create a fresh linker/HLE registration context, call `AuditGame`, immediately clear/destroy that linker, then continue. Never retain `Program*` or `SymbolRecord*` pointers between games; only value-type audit results survive aggregation.

- [ ] **Step 5: Add partial-scan test**

Make one valid synthetic game and one malformed game. Assert both appear in `LibraryResult.games`, `LibraryResult.partial == true`, and valid-game findings still aggregate.

- [ ] **Step 6: Re-run library + aggregation tests**

```powershell
ctest --test-dir .\_Build\phase0-windows -R "^import_audit_(library|aggregation|json)$" --output-on-failure
```

Expected: all PASS.

- [ ] **Step 7: Commit Task 5**

```powershell
git add src/loader/importAudit.h src/loader/importAudit.cpp tests/ImportAuditTests.inc
git commit -m "loader: scan game libraries for import gaps"
```

---

### Task 6: Add `--audit-game`, `--audit-library`, and `--audit-json` CLI modes without Graphics initialization

**Files:**
- Modify: `src/main.cpp`
- Create: `src/importAuditRunner.h`
- Create: `src/importAuditRunner.cpp`
- Modify: `CMakeLists.txt`
- Create: `tests/ImportAuditCliTests.cmake`

**Interfaces:**
- Consumes: Task 3/5 audit APIs and Task 4 reporting.
- Produces:

```cpp
enum class CommandMode { RunGame, AuditGame, AuditLibrary };

struct CommandOptions {
    CommandMode mode = CommandMode::RunGame;
    Emulator::RunOptions run;
    std::filesystem::path audit_input;
    std::filesystem::path audit_json;
};

namespace ImportAuditRunner {
int RunGameAudit(const std::filesystem::path& input,
                 const std::filesystem::path& json_output);
int RunLibraryAudit(const std::filesystem::path& root,
                    const std::filesystem::path& json_output);
}
```

- [ ] **Step 1: Add CLI parsing tests first**

`tests/ImportAuditCliTests.cmake` must invoke the built emulator and verify:

```text
--audit-game <missing>                     -> usage failure
--game X --audit-game X                    -> mutually exclusive failure
--audit-game X --audit-library Y           -> mutually exclusive failure
--audit-json out.json without audit mode   -> usage failure
```

Create synthetic temporary audit inputs in the CMake script rather than pointing at commercial titles.

- [ ] **Step 2: Verify RED**

Build `kyty_emulator` and run the new CTest entries. Expected: FAIL because audit CLI switches are unknown.

- [ ] **Step 3: Refactor argument parsing around `CommandOptions`**

Keep existing `--game` behavior byte-for-byte compatible. Add usage text:

```text
kyty_emulator --audit-game <dir|elf> [--audit-json <file>]
kyty_emulator --audit-library <root> [--audit-json <file>]
```

Audit modes are mutually exclusive with each other and normal `--game` execution. Return usage exit code `2` for malformed audit CLI, while preserving existing normal-run behavior where practical.

- [ ] **Step 4: Implement minimal audit subsystem initialization**

`importAuditRunner.cpp` must initialize only what static loading needs:

```cpp
Common::Subsystems subsystems;
subsystems.Initialize<Config::Lifecycle>();
Config::ConfigOptions cfg;
cfg.printf_direction = Config::OutputDirection::Silent;
Config::Load(cfg);
subsystems.Initialize<Log::Lifecycle>();
subsystems.Initialize<Libs::LibKernel::Memory::Lifecycle>();
```

Then create/register the HLE symbol database and run the audit. Do **not** initialize `Controller`, `Audio`, or `Libs::Graphics::Lifecycle`.

If `RuntimeLinker::LoadProgram` proves to require another non-executing subsystem, add only that required subsystem and cover it with the headless test; do not initialize Graphics as a shortcut.

- [ ] **Step 5: Enforce audit exit-code contract**

Return:

```text
0 = completed, findings allowed
2 = CLI usage error
3 = requested audit root cannot be scanned
4 = partial audit because one or more candidate binaries were malformed/unreadable
```

If `--audit-json` is supplied, write the report before returning `4` for a partial audit.

- [ ] **Step 6: Add a headless proof that Graphics is not initialized**

Run a synthetic `--audit-game` in a test environment without selecting/creating a Vulkan device and assert the command completes. Also capture stdout/stderr and assert it contains no `Initialized: Graphics` line.

- [ ] **Step 7: Run CLI tests and normal CLI smoke tests**

```powershell
cmake --build --preset phase0-windows --parallel 10 --target kyty_emulator phase0_runtime_probes
ctest --test-dir .\_Build\phase0-windows -R "^(import_audit_|phase1_imports|phase6_self_repack)$" --output-on-failure
```

Expected: all selected tests PASS.

- [ ] **Step 8: Commit Task 6**

```powershell
git add src/main.cpp src/importAuditRunner.h src/importAuditRunner.cpp CMakeLists.txt tests/ImportAuditCliTests.cmake
git commit -m "cli: add static game import audit modes"
```

---

### Task 7: Validate the real workflow, document usage, and protect regressions

**Files:**
- Modify: `README.md`
- Modify: `docs/superpowers/specs/2026-09-16-compatibility-import-audit-design.md` only if implementation details require a factual clarification; do not silently change scope.

**Interfaces:**
- Consumes: finished audit CLI.
- Produces: documented commands and fresh verification evidence.

- [ ] **Step 1: Document the user workflow**

Add concise examples:

```powershell
# One game
.\_Build\phase0-windows\kyty_emulator.exe --audit-game "E:\GOY\PPSA26344" --audit-json ".\_Build\goy-imports.json"

# Whole collection
.\_Build\phase0-windows\kyty_emulator.exe --audit-library "E:\Games" --audit-json ".\_Build\library-imports.json"
```

Document that `AliasCandidate` means same NID/type exists under another qualification and requires ABI review; it is not auto-fixed. Document that static audit cannot see future runtime-only `sceKernelDlsym` requests.

- [ ] **Step 2: Run the complete focused regression suite**

```powershell
. .\scripts\phase0\windows-env.ps1
cmake --build --preset phase0-windows --parallel 10 --target kyty_emulator phase0_runtime_probes
ctest --test-dir .\_Build\phase0-windows -R "^(import_audit_|phase0_unresolved_import|phase1_imports|phase1_executables|phase1_load_rollback|phase6_self_repack)$" --output-on-failure
```

Expected: 0 failures.

- [ ] **Step 3: Run Ghost static audit without executing the game**

```powershell
.\_Build\phase0-windows\kyty_emulator.exe --audit-game "E:\GOY\PPSA26344" --audit-json ".\_Build\ghost-of-yotei-import-audit.json"
```

Verify the command prints no `Initialized: Graphics`, does not open a game window, and returns a complete list instead of stopping at `X-Nm5KLREeg[AgcDriver_v1][AgcDriver_v1.1][Func]`.

The report should include the already-approved OpenPsId and Coredump qualifications as `CompatibleHle`; AgcDriver must be reported according to evidence from the current symbol database, not force-mapped during this task.

- [ ] **Step 4: Run a library audit on a user-selected games root**

```powershell
.\_Build\phase0-windows\kyty_emulator.exe --audit-library "E:\Games" --audit-json ".\_Build\prosperox-library-import-audit.json"
```

If the user's collection uses another root, substitute only the root path. Verify multiple game roots are scanned independently and global affected-game counts are deduplicated by game.

- [ ] **Step 5: Review the generated global backlog before adding any new aliases**

For every `AliasCandidate`, require separate evidence that the requested qualification and existing implementation are ABI-compatible. Do not mass-convert candidates to `FindExactOrCompatible` rules as part of the audit feature.

- [ ] **Step 6: Commit documentation after verification**

```powershell
git add README.md
git commit -m "docs: explain compatibility import auditing"
```

- [ ] **Step 7: Final branch verification**

Run:

```powershell
git status --short
ctest --test-dir .\_Build\phase0-windows -R "^(import_audit_|phase0_unresolved_import|phase1_imports|phase1_executables|phase1_load_rollback|phase6_self_repack)$" --output-on-failure
```

Expected: clean intended working tree and 0 selected test failures before merge/PR decisions.

---

## Plan self-review

- **Spec coverage:** Single-game audit, recursive library scan, eboot + bundled modules, second-pass guest resolution, exact/compatible/guest/weak/alias/missing/malformed classification, deterministic JSON, console summary, global affected-game aggregation, exit codes, headless/no-Graphics behavior, and synthetic tests are each mapped to a task.
- **Deferred scope preserved:** No auto-alias generation, no HLE stub generation, no external symbol database download, no network calls, no GUI, no runtime `sceKernelDlsym` trace merging, and no compatibility scoring.
- **Type consistency:** `SymbolResolve`, `ImportRecord`, `GameResult`, `LibraryResult`, `ImportResolutionInspection`, `AuditGame`, and `AuditLibrary` names are consistent across tasks.
- **No placeholder implementation steps:** Every production behavior has a concrete interface, test, command, and expected outcome.
