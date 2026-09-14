# Phase 6: qualified AGC initialization

2026-09-13. Initialization is confirmed in Bendy after the user freed memory.
Reviewed qualified bindings now reach shader and primitive-state creation.
Full regression: **78/79 passed**. Phase 6 remains open; no menu or gameplay.

## Evidence and contract

Bendy imports `23LRUSvYu1M[Agc_v1][Agc_v1.1][Func]`, eboot relocation 524,
GOT `0x901dc9d60`. Read-only static tracing follows its PLT at `0x9019e0c00`
to the call at `0x9003b1aed`: immediately preceding instructions set RDI to
**0x901f739b0** and ESI to **13**. These use the prior main-module base and
were initially static evidence. The fresh runtime log now confirms
`AGC_INIT state=0x0000000901f739b0 version=13 caller_state=preserved`.

Inspected local Kyty git objects:

* [4330cb9462fbed1e7f5130e9eea128e8a518a667](https://github.com/KytyPS5/KytyPS5/commit/4330cb9462fbed1e7f5130e9eea128e8a518a667)
  removes writes to caller state. Already present; retained.
* [4498d75b9b4d62e442be8dfa818c1e24f5404cf6](https://github.com/KytyPS5/KytyPS5/commit/4498d75b9b4d62e442be8dfa818c1e24f5404cf6)
  provides v13 defaults. Already present; no tables were copied or replaced.
* SharpEmu's existing `AgcExports.cs` independently uses pointer/version under
  `libSceAgc` and preserves state. Its symbolic name/error code are not treated
  as authoritative specifications of Sony's invalid-argument behavior.

The reviewed entries below are additionally registered under **Agc 1 / Agc
1.1** or **AgcDriver 1 / AgcDriver 1.1**. Graphics5/Graphics5Driver remain
registered. No resolver changes, wildcard versions, NID fallback or blanket
aliasing were introduced. Existing command builders, default tables, shader
relocation and primitive-state behavior are retained.

| Identity | NID | Reviewed ABI |
|---|---|---|
| Agc | `23LRUSvYu1M` | initializer: opaque state pointer, version |
| Agc | `2JtWUUiYBXs` / `wRbq6ZjNop4` | public/internal defaults: version, returned table pointer |
| Agc | `BfBDZGbti7A` | Trinity query: one-byte output pointer |
| Agc | `wr23dPKyWc0` | release-memory builder: buffer plus register/stack parameters |
| Agc | `57labkp+rSQ` | acquire-memory builder: buffer, engine, cache controls, range, poll interval |
| Agc | `f3dg2CSgRKY` | shader creation: output pointer, relative header, code pointer |
| Agc | `D9sr1xGUriE` | primitive state: CX/UC output arrays, optional HS, GS, primitive type |
| AgcDriver | `w2rJhmD+dsE` / `DL2RXaXOy88` | graphics-event registration/removal |

Kyty [b77978fd1e02d88e832abaaa5b33a4fe74c45f3d](https://github.com/KytyPS5/KytyPS5/commit/b77978fd1e02d88e832abaaa5b33a4fe74c45f3d)
corrects the Trinity ABI to an output pointer. This was already in ProsperoX;
Bendy passes `0x901f739b8` in RDI and does not use a return value. Sharp's
return-value query differs and was not adopted. Sharp independently supports
the other call signatures; its private packet encoding and permissive/stub
behavior are not copied. All new fixtures are original synthetic data.

Initialization preserves all caller bytes for non-null opaque state and the
implemented versions 0..13. Null state or higher versions stop with
`AGC_INIT_UNSUPPORTED`, exit 86: an explicit emulator support boundary, not a
fabricated guest error return. Actual state/version are logged when reached.
Feature negotiation remains unimplemented; default-getter fallbacks are unchanged.

## Current tests and runtime

Each newly exposed call group first failed its missing-qualified-binding test,
then passed after registration. `Phase6AgcTests.inc` additionally checks:
defaults through the resolved SysV ABI, wrong identity/version rejection,
one-byte Trinity output with adjacent canaries, driver invalid-queue errors,
eight-dword release/acquire packets with exact-fit buffers and surrounding
canaries, stack arguments and pointer returns, shader field-relative relocation
including nested/null pointers and PGM address bits, and bounded primitive
register outputs. An unreviewed shader-fusion export remains unresolved.

Full build succeeds for `kyty_tests`, `kyty_emulator`, `phase2_kernel_probe`,
`phase5_presentation_tests`, and `phase5_random_stress_tests`.
Full CTest **78/79 passed in 51.16 seconds**, including **11/11 Phase 6**.
No guest-memory allocation failures remain in this run. The one failure is the
pre-existing `shader_recompiler_compute` capability requirement:
`PHASE0_UNAVAILABLE attachment feedback loop dynamic state is unsupported by this device`.
Its later monolithic cases do not execute; this is not an all-green suite.
No test expectations or capability checks were relaxed.

Real RX 7800 XT (vendor 1002, device 747e, driver 8389003), Vulkan validation and
submit-time synchronization validation enabled: **zero reported VUID/errors or
SYNC-HAZARD diagnostics**. Phase 3 completes 10,000 controlled release lifecycles.
The standard 10-second Phase 5 presentation regression passes **795 frames**,
warm/peak VMA **1,317,171,216 bytes**, warm/peak **13 allocations**.
No additional hour-long soak was run.

Evidence: `_Build/evidence/phase6-agc-resume-regression-20260913` contains full
CTest output, JUnit, LastTest log and validation settings;
`_Build/phase6-agc-resume-final-build.log` records the build.

Successive bounded Bendy runs under `_Build/evidence/phase6-bendy-agc-*`
(`resume-20260913-2016`, `defaults-20260913`, `trinity-20260913`,
`events-20260913`, `release-20260913`, `acquire-20260913`, `shader-20260913`,
`prim-20260913`) identify each next import independently. Each naturally exits
86 at its next unresolved import; these are progress observations, not passes.
The final run lasts **2.53 seconds**. Graphics event registration succeeds with
queue **3**, ID **0**, null user data. Release-memory receives action 0x28,
destination 1, data selection 3, and label `0x213225b30`. It and acquire-memory
construction proceed, followed by **51 shader-creation calls** and primitive
setup. No guest GPU submission, presented game frame, menu or gameplay is
claimed. Input executable hash remains unchanged in the per-run manifests.

## Historical boundary: tessellation-ring contract (superseded)

The [2026-09-14 configuration checkpoint](phase6-tessellation-configuration.md)
now implements validated ring/offchip state and an explicit native-draw
consumer boundary. Bendy proceeds past these calls and condition destruction
to a qualified POSIX `unlink` import. The observations below are historical.

The final actual stop is
`XlNp7jzGiPo[AgcDriver_v1][AgcDriver_v1.1][Func]`, eboot relocation **513**,
patch **0x901dc9d08**, corresponding to `AgcDriverSetTFRing`.
The guest wrapper at `0x9003b2650` checks a nonzero, 256-byte-aligned pointer
before tail-calling the import. Actual ring size has not yet been captured.

ProsperoX's legacy setter only stores `tf_ring_base/tf_ring_size` in
`g_tessellation_driver_state`; a source search finds no consumer. The adjacent
HS-offchip setter has the same limitation. Sharp explicitly returns success
without implementing that ring. Neither is evidence of correct guest ring
behavior. The Vulkan tessellation pipeline currently found is rectangle-list
emulation, not proof that arbitrary native HS/ring operation is supported.
**The ring export remains unresolved under AgcDriver**; it was not exposed as
a success-only workaround. Next work must capture the actual configuration,
establish the setup/lifetime and first-use contract, and either connect it to a
tested generic implementation or fail explicitly when unsupported usage occurs.
The missing configuration contract does not prove Bendy will execute a
tessellation draw; startup setup alone is insufficient to infer that.

Refreshed upstream leads: [#410](https://github.com/KytyPS5/KytyPS5/pull/410)
documents the corrected zero-argument CopyData size-helper ABI;
[#589](https://github.com/KytyPS5/KytyPS5/pull/589) and
[#598](https://github.com/KytyPS5/KytyPS5/pull/598) describe null-downward-cursor
space accounting. Their current descriptions were checked. No matching Bendy
allocation failure occurred here, so no allocator/shader changes were imported.
Inspect the actual focused diff and add a failing synthetic case if that
specific failure is reached later. Do not adopt dropped-LOD gather behavior.

## Earlier memory-blocked checkpoint (superseded by the run above)

New tests first failed on the missing identity. Final build succeeds for
`kyty_tests`, `kyty_emulator`, `phase2_kernel_probe`. Focused CTest **14/14 passes**
in 6.06 seconds, including all **11 Phase 6 tests**.

`phase6_agc_init` tests exact identity/version matching, retained Graphics5,
absence of unreviewed shader aliases, repeated 0..13 initialization on a
read-only canary page, and v13 defaults. Internal registers 0x81/0x101 are 1/3
while v11 stays 0/0; public v13 retains the v11-plus 0x201 bit patch and expected
table shape. `phase6_agc_unsupported` requires the diagnostic and exit 86 for
null/14/UINT32_MAX cases in separate subprocesses.

Evidence: `_Build/evidence/phase6-agc-regression-20260913` (CTest, JUnit, full
test log and memory status); `_Build/phase6-agc-red*` and
`_Build/phase6-agc-final-build.log`.

Bendy retry: `_Build/evidence/phase6-bendy-agc-init-20260913`, **exit 321 before
guest execution**, unable to commit 13,824 MiB of guest direct-memory backing.
It did not reach AGC or Vulkan initialization. No new runtime/GPU progress is
claimed. `static-call-site.txt` records the call trace. Game files remain
unchanged; no commercial payloads or SDK code were added to source/tests.

Post-build Windows available commit: **11,427,065,856 bytes (10.64 GiB)**.
Free approximately **5 GiB** to provide a 15 GiB launch margin. No applications
or paging-file settings were changed. Earlier full-suite memory failures remain
outstanding; the focused passing result does not supersede them.

## Reproduction

Use a new evidence directory for each bounded run. Continue from the
[current checkpoint](phase6-tessellation-configuration.md); the earlier ring
and memory-blocked results are historical.
No Phase 7 work.

```powershell
. ./scripts/phase0/windows-env.ps1
cmake --build --preset phase0-windows --parallel 6 --target kyty_tests kyty_emulator phase2_kernel_probe
ctest --test-dir _Build/phase0-windows -R '^phase6_|^phase1_(paths|tls|executables)$' --output-on-failure
./scripts/phase6/windows-runtime.ps1 -GamePath 'C:\Dev\ps5\Bendy\PPSA27624-app0' -EvidenceDir '<new evidence directory>' -Seconds 90
```
