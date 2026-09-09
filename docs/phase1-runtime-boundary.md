# Phase 1: executable and native-runtime boundary

Target: Windows 11, Ryzen 7 7800X3D, Radeon RX 7800 XT, 32 GB RAM.
Date: 2026-09-09. The platform decision in [platform-scope.md](platform-scope.md)
supersedes the original cross-platform acceptance requirements.

This milestone establishes a tested, explicitly bounded loader/native boundary.
The target-PC conformance milestone is implemented and validated. The supported
matrix below is the acceptance boundary; the follow-up table records behavior
that has not been established by this milestone.
It does not establish commercial-game boot or gameplay compatibility. All new
inputs are original synthetic fixtures. No commercial files, Sony SDK, firmware,
assets or keys were accessed or added. Neither upstream repository was modified.

## Implemented behavior

### Executables and mapping

`src/loader/elf.cpp` validates header/table extents, integer overflow, header
strides, segment sizes and alignment, SELF payload mappings, section strings,
dynamic termination, bounded strings/symbols/relocations and supported metadata.
Malformed inputs receive a retained diagnostic. Dynamic metadata is capped at
64 MiB per declared table. The existing Common::File read interface bounds
individual segment payloads to UINT32_MAX bytes.

Supported containers are raw ELF and the existing uncompressed SELF layout,
including its supported dynamic-data trailer mapping. Compression or inconsistent
SELF mappings are rejected; this is not a decryption or package-extraction tool.
Unsized ordinary ELF symbol tables are explicitly unsupported. No heuristic
symbol-count inference was introduced.

TLS initialized bytes must correspond to an enclosing mapped segment's bytes;
the entire TLS image must fit its declared mapped memory extent. TLS alignments
through the 16 KiB guest page alignment are supported; larger requirements are
rejected. The TCB has independent 32-byte alignment.

`RuntimeLinker::LoadProgram` retains its diagnostic, releases mappings, restores
the allocation cursor and avoids registering a program when mapping/metadata
construction fails with a recoverable exception. The emulator and module-load HLE
callers now handle a failed load explicitly. Empty relocation tables are valid.
Read-only payload and BSS behavior are exercised using actually mapped modules.

### Symbols and relocation

Resolution uses NID, library name/version, module name/version and symbol type.
The policy is explicit: a matching HLE symbol takes precedence over a matching
loaded guest export. The existing HLE `libSce` name normalization is retained.
Global NID-only fallback was removed from initial and late resolution.

The supported relocation matrix is R_X86_64_64, GLOB_DAT, JUMP_SLOT, RELATIVE
and the existing local-module DTPMOD64 form. Fixtures exercise mapped writes,
local definitions, weak absence, addends and exact late resolution. Cross-module
DTPMOD64 with a symbol operand and other unsupported relocation forms are
rejected by validation.

Missing weak symbols have S=0, with the relocation's applicable addend semantics.
Missing strong data stops with `UNRESOLVED_STRONG_DATA`; the first unresolved
strong function call stops with `UNRESOLVED_STRONG_IMPORT`. Both use exit 86.
The legacy PLT failure path also stops explicitly and validates program identity
before dereferencing it. These paths no longer fabricate a successful return.
Diagnostic termination uses quick_exit to avoid waiting for guest cleanup while
holding the loader lock. It intentionally does not run normal shutdown handlers.

Late import lookup and GOT patching share the loader lock, avoiding references
into an import-record vector while another load changes it. The generated import
thunk is tested with eight integer and eight floating-point arguments.

### Native execution, TLS and module lifecycle

Windows TLS replacement uses decoded instruction boundaries. TLS-looking bytes
inside an instruction immediate are not patched. Supported FS:0 loads include
the seven low general-purpose destination registers other than RSP and zero to
three leading operand-size prefixes. The supported immediate FS:0x28 store
performs the original four-byte store and preserves following guest traps.

Each Windows TLS site jumps to a trampoline in the existing 8 MiB module patch
pool. It reserves the guest red zone before any stack write, restores RSP and
returns by jump. Native tests verify all 128 red-zone bytes across loads/stores.
The red-zone analysis treats these protected sites as fall-through operations
and never relocates them into another span. A combined fixture verifies that
memory accesses after TLS sites are still discovered and protected.

The TCB callback helper preserves flags, general registers other than its return
register, and Windows-enabled extended processor state using XSAVE/XRSTOR.
The native state fixture deliberately clobbers YMM0 in the host callback and
checks its entire 256-bit value, arithmetic flags and the returned pointer.
Correctness takes priority over the cost of this state save.

TLS continues to use ProsperoX's per-module/per-thread backing. The concurrency
fixture runs 32 threads, 16 allocation/deletion cycles and two module blocks,
checking initialization, BSS, isolation, TCB self pointers and zero live blocks
after joining. Separate actually loaded modules test alignments 1, 8, 32 and
16384, including a TLS image whose size is not a TCB-alignment multiple.

Module start order respects dependency components. Dependencies outside a cycle
start first; cycle members use stable load order with a diagnostic. Started
module history makes repeated starts/stops idempotent and finalization reverses
actual startup order. Modules without an init entry still satisfy dependencies
and may have a finalizer. Synthetic native callbacks verify DAG and cycle cases.

CPU execution remains native on the specified 7800X3D. CPUID is still host-visible;
this milestone does not claim an emulated PS5/Zen 2 feature profile. The Windows
helper checks OSXSAVE and the OS-enabled state size. A guest CPU feature policy
requires a concrete guest consumer and architectural tests before interception
is added. Other CPUs and operating systems are outside current acceptance.

### Mounted filesystem

Resolution requires a guest absolute path and a matching mount. Longest matching
mount wins. Parent traversal cannot leave that mount. Unmapped paths, Windows
drive paths, alternate data streams, reserved device components and ambiguous
Windows path spellings are denied. Canonical containment rejects NTFS junction
escapes. KernelOpen denies failed resolution before performing host I/O.

Tests include actual NTFS junctions, attempted HLE create/truncate operations,
an unchanged outside sentinel and successful creation inside the selected mount.
This is tested path containment, not a host security sandbox: a hostile host
process concurrently replacing junctions would require a handle-based traversal
design. The normal tested HLE paths do not create those host junction races.

## Validation and evidence

The complete Windows build produces the emulator and all registered test
executables. The focused acceptance run passes all 13 selected entries: ten
Phase 1 entries, the two repaired Phase 0 probes and the existing red-zone suite.
The parser fixture includes every truncation of its minimal ELF, explicit
malformed tables/ranges/SELF/TLS cases, and 1000 deterministic mutations with
seed 0x505831. Mutated files are parsed in a child test process and never executed.
This is a finite regression corpus, not exhaustive fuzzing or a security proof.

Full hardware inventory: **51 entries**. Both final hardware runs observed
**41 pass, 7 fail, 2 assertion crashes, 1 unavailable**. The comparison returned
`reproduced: true` with no errors. Radeon execution and SPIR-V validation were
observed in both runs. All ten Phase 1 entries and both repaired Phase 0 probes
pass. The nine
remaining failures are retained with their exact signatures, not counted as
correctness passes. `baseline_gate_passed` requires no unexpected outcomes;
`all_correctness_tests_passed` remains false.

Evidence is retained under the ignored `_Build` directory:

- `phase1-build-combined.log`: full target build after TLS/red-zone integration.
- `phase1-tests-acceptance.log`: 13 passing focused loader/native entries.
- `phase1-tests-combined.log`: combined TLS/red-zone and existing red-zone tests.
- `phase1-runner-tests.log`: ten passing Python gate regression tests.
- `evidence/phase1-windows-acceptance-a` and `-b`: final full hardware runs;
  consult their summary.json, manifest.json and per-test raw logs for results.
- `phase1-acceptance-comparison.json`: final repeated-run comparison.

The manifests retain source hashes, dirty status, dependency/toolchain revisions,
hardware/driver information, commands, exits and timeouts. The Phase 0 frozen
source and independent clean-build evidence remain intact. Phase 1 incremental
builds are not claimed as a second independent clean build or byte-identical
binary reproduction. Comparison verifies recorded source/test content and
outcomes; tool/cache details remain available in the manifests for inspection.

## Remaining defects and limits

| Item | Ownership and required evidence |
| --- | --- |
| Socket PEEK/WAITALL assertion | Phase 2; repair guest socket wait/data consumption semantics using its existing synthetic reproducer. |
| EOP completion visible before retirement | Phase 3; label and actual GPU work must become observable in the correct order. |
| Scalar wave-mask branch produces mixed lane results | Phase 4; correct shader semantics, validated by GPU readback. |
| Six image/resource tests reject D24S8 format/usage combinations | Phase 5; correct Radeon resource representation and conversions, without weakening assertions. |
| Aggregate compute test requires unsupported attachment-feedback dynamic state | Record unavailable; split capability-independent cases so an early skip does not conceal later coverage. No claim that its unexecuted cases pass. |
| Module unload during guest execution; cached TLS identity across address reuse | Phase 2 lifetime work needs quiescence/generation tests. Current TLS fixture covers joined workers, not hostile concurrent unload. |
| Recovering from a relocation failure after publication | Current strong failures terminate deliberately. General atomic rollback of an already-published dependency graph remains a loader/lifetime follow-up. |
| Code/data intermixing in executable segments | The TLS scan is linear decoding, not complete control-flow discovery. It stops on undecodable tails; additional patch forms and embedded-data layouts need original reproducer fixtures. |
| Guest startup/constructors and CPU feature policy | Retain current PS5 startup ownership pending ABI evidence. Do not claim generic ELF constructors or PS5 CPUID virtualization. |

The patch pool is now retained on Windows even if fault-oriented red-zone
patching is disabled, because TLS correctness needs it. Pool exhaustion or an
unreachable relative jump rejects the load; dynamic pool sizing is future work.
Other FS-relative instruction forms, RSP destinations, larger TLS alignment,
unsupported containers and relocation forms are not implied by the supported
matrix. No game-specific workaround was added.

## Provenance and later commercial validation

All Phase 1 fixtures and changes are original work within the existing project.
Existing copyright/license notices, including the shadPS4 notices in the red-zone
patcher, are preserved. No SharpEmu/arielPS5 implementation was copied.
SharpEmu's loader/ABI/path tests were behavioral references for the audit;
its `src/SharpEmu.Core/Runtime/SharpEmuRuntime.cs` comments near line 445 also
support retaining guest-driven PS5 initialization until its ABI is identified.
That comment is a reference, not proof of behavior for every build.

Later lawful runtime validation must pin the exact executable/module hashes,
region and version, observe the first unsupported import, verify real TLS/module
startup behavior and record rendering/audio/input progress. The user-provided
US 1.05 directory was not opened. Historical US 1.004.000 evidence does not make
1.05 an equivalent baseline. These validations are not prerequisites for the
synthetic Phase 1 milestone and have not been claimed as completed.
