# Phase 6 — gameplay pause checkpoint

2026-09-16. Target: Windows 11, Ryzen 7 7800X3D, RX 7800 XT, 32 GB.
**Phase 6 remains OPEN. Phase 7 has not started.** Development is paused at
this checkpoint, based on `d89bc319971dd6fa59f4127e71bf2605ebfa1a41` on
`debug/bendy-performance` plus the accompanying diagnostics, tests and docs.
The old Json2-stop report is historical and has been surpassed.

## Runtime progress

**Bendy and the Dark Revival PPSA27624 reaches real controllable Chapter 1
gameplay. Audio, controller input, video, menus and gameplay work on the target
PC.** These are the user's observations of the current local development build,
not a claim that this checkpoint's automated tests replayed the Chapter 1 route.
Menu/video can reach approximately 60 FPS; gameplay is commonly 12–15 FPS.
The intro sometimes repeats multiple times. This is a separate unresolved
media/lifecycle issue, not evidence that the PM4 performance problem is fixed.

Retained work includes strict generic loader/FSELF validation, qualified AGC
initialization, per-renderer ring/offchip state, command encoding/submission and
completion, POSIX condition/file operations and DCC first presentation. The
first-owner DCC path uses coherent, full-range, uniform metadata to materialize
a proven clear; it does not upload compressed color as linear pixels or use
a game/shader hash to bypass validation. Native tessellation remains an explicit
unsupported consumer despite accepting validated ring configuration.

`Zw7uUVPulbw[AgcDriver_v1][AgcDriver_v1.1]` now resolves to the existing
`AgcDriverGetEqContextId`; the separately reviewed event-type accessor also
resolves in that identity. Graphics events return the low 32 bits of `data`
as their context ID and `ident` as their type. Null and legacy-filter behavior
are retained. Queries do not modify events; these explicit exports retain their
exact versions.

The later gameplay commit adds `FindExactOrCompatible` to late HLE resolution.
Exact HLE entries win, followed by these compatibility mappings, then exact
guest exports. The mappings preserve NID and symbol type:

- `Agc_v1/Agc_v1.1` to `Graphics5_v1/Graphics5_v1.1`;
- `Json2_v1/Json_v1.1` to `Json2_v1/Json2_v1.1`;
- only SslInit (`hdpVEUDFW3s`), `Ssl_v1/Ssl_v2.1` to `Ssl_v1/Ssl_v1.1`.

**The AGC and Json mappings are identity-wide, not per-export allowlists.**
There is no arbitrary NID-only fallback, but the old claim that the effective
runtime cannot alias other Graphics5 exports is no longer true. New checkpoint
tests exercise the actual late resolver's priority and rejection boundaries.
They do not establish ABI equivalence for every aliased export. Per-export ABI
review/narrowing remains debt; this pause does not broaden the policy further.

The qualified POSIX `write` export calls the errno-translating wrapper rather
than the kernel-error ABI. Focused tests verify real mounted-file bytes and
closed-descriptor error translation. Existing open/close/lseek/fstat/unlink and
condition tests remain. Negative-descriptor write is still an explicit unsupported
path; full POSIX error conformance is not claimed.

LibcInternal mspace create/malloc currently provide a capacity-bounded bump
allocator over supplied storage, with locking for flag 0. Aligned-arena bounds,
failure without cursor advancement, exhaustion and concurrent non-overlap are
covered. This is **not a complete allocator**: free/realloc/destroy/reuse,
unaligned bases and full guest allocator semantics remain unvalidated or missing.
No title-specific addresses or game-name dispatch were added in this checkpoint.

Input remains PPSA27624, version `01.000.003`, at the user-supplied local path.
Executable SHA-256:
`149EEA474A0C79EC6B8E79F9F352F37A8180B1FA7ED294B09937E6D119D9C548`.
Its transformation history remains unknown. Structural acceptance does not
authenticate a retail executable. No commercial files were modified or added
to version control; runtime evidence and shader dumps stay under ignored `_Build`.
The pre-existing root `_SaveData` directory is preserved and now ignored.

## Performance evidence and experiment disposition

The user's latest bad-area capture reports approximately:

| Counter | Observed rate/cost |
|---|---|
| Graphics submissions | 12–15/s |
| Graphics process attempts | 430–456/s |
| Blocked process attempts | 418–443/s |
| Flushes | 1,700–1,800/s |
| Vulkan queue submissions | 2,500–2,900/s |
| Scheduler finish | 824–1,043 calls/s; 137–177 ms/s |
| Guest idle wait | 193–247 ms/s |
| Presentation/acquire | Comparatively cheap in this capture |

These ranges are supplied observations, not a new controlled benchmark of this
commit. The strongest next lead is **PM4 blocked/retry and flush/submit churn**.
Process counts include retries, not just distinct guest submissions. A future
investigation should correlate wait reasons and progress with queue publication;
do not infer that removing waits or batching submissions is correct from counts
alone. No scheduling, PM4 retry, flush or Vulkan submission policy is changed here.

`PROSPEROX_PERF_TRACE=1` retains opt-in guest submission/process/blocked/wait-reason
counters and wall-time scopes for scheduler, presenter, frame-pool and swapchain
operations. Unset it (or set `0`) for normal operation. It is read once per process.
Reports use roughly one-second windows; bucket `calls` is a raw window count,
while `/s` fields are normalized. `vk_queue_submit` and `vk_queue_present` timing
include their host queue-mutex wait. Nested/concurrent wall times are not additive,
are not GPU timestamps, and logging perturbs timing. `process_elapsed` replaces
the misleading `process_cpu` label. Atomic exchanges yield approximate windows.

The local `GuestGpu::Done()` experiment removed synchronous `WaitForIdle()`.
Although one attempted build stopped at trailing whitespace, the inspected
`.ninja_log` and emulator timestamp show a later successful build at approximately
00:48 on September 16 containing the experiment. **Build success is not semantic
validation.** No matching completion/ordering regression evidence was found.
The experiment is reverted to the committed synchronous behavior; a local backup
is retained under ignored `_Build`. It is not shipped as a completed optimization.

## Focused Kyty review and provenance

The September 15 review fetched public `KytyPS5/KytyPS5` main at
`6a2987a7b29bbfefd7b102470e61a50a610c3e4a`. Reviewed recent commits and actual
diffs against ProsperoX, rather than merging the branch. Both repositories carry
GPLv2; retained source notices and this ledger identify the adaptations.
The listed upstream commits are authored by nmzik; `18a1e0f` also credits Bipin.
These adaptations are already in `d89bc31`; this pause preserves them instead of
redoing the old survey or merging newer upstream changes.

| Reference | Decision and local behavior |
|---|---|
| [d63fb62](https://github.com/KytyPS5/KytyPS5/commit/d63fb62367a94ec8aeff58148dcc911a3f3fd895) | Adapted CS_DONE 64-bit write-confirm interrupt acceptance and its real compute-queue regression. Phase 3's completion ordering remains in use. |
| [18a1e0f](https://github.com/KytyPS5/KytyPS5/commit/18a1e0fadc56b08f996a14a4ab2c71c4563154e8) | Adapted removal of the global condition-waiter registry and its signal-delivery calls. A suspended thread could hold that registry lock while exception delivery tried to acquire it. Existing `PthreadWakeForSignal` and bounded condition polling remain. |
| [7b5a33f](https://github.com/KytyPS5/KytyPS5/commit/7b5a33f8785308bd6ad569a8dcae7d5771443085) | Adapted front-face VGPR encoding to float +1/-1 bit patterns in ProsperoX's own SPIR-V emitter. Added both-facing raster readback tests. |
| [968cf3c](https://github.com/KytyPS5/KytyPS5/commit/968cf3c03ffb90ec83f8e493604170325e253564) | Adapted PS_W32_EN decoding, pixel cache-key width and propagation through the program compiler. Added register/key tests and real wave32/wave64 pixel-mask readback. |
| [a1fbc41](https://github.com/KytyPS5/KytyPS5/commit/a1fbc411462b95a403e5a2570c71295942c6408d) | Adapted preservation of scalar prolog loads when replacing embedded vertex fetches, including the upstream synthetic shared-load regression. Normal optimization can remove dead loads; fetch detection cannot discard a load with other consumers. |
| [ea092a9](https://github.com/KytyPS5/KytyPS5/commit/ea092a9472b1b7a0a830c889042e1939a7a04fd0) | Adapted DCC clear encoding through the requested view format using an attachment clear when backing and view formats differ. Added the FLOAT-over-UNORM alias case to the existing deterministic DCC GPU fixture. |
| [01df42a](https://github.com/KytyPS5/KytyPS5/commit/01df42a5f329fa598f99b7006fedf2059d72acb9), [437e69e](https://github.com/KytyPS5/KytyPS5/commit/437e69ef1b7690046b18e2565720c784755da5d2) | Reference for coherent metadata inspection. ProsperoX retains its bounded first-owner presentation clear and Phase 5 pending-slice model. The broader replacement is deferred, as explained below. |

The DCC fixed-color encoding is independently consistent with AMD PAL's
[Gfx9Dcc::GetBlackOrWhiteClearCode](https://github.com/GPUOpen-Drivers/pal/blob/dev/src/core/hw/gfxip/gfx9/gfx9MaskRam.cpp)
(GFX10 branch). This is a behavior reference, not copied AMD source.

## Intentionally deferred upstream work

- The complete DCC metadata rewrite and [dd408ff](https://github.com/KytyPS5/KytyPS5/commit/dd408ffd006ffa210eeb994829f2a9b2f9dd2188)
  discovery/upload ordering must be integrated together with ProsperoX's
  publication and private-buffer binding contracts. Upstream consumes clears
  by writing expanded `0xff` metadata. ProsperoX cannot claim expanded guest
  color bytes merely because its Vulkan image contains expanded pixels.
  Repeated metadata clears of an existing owner, mixed per-slice metadata,
  metadata ownership/retirement and sampler/attachment alias ordering remain
  explicit work; this checkpoint handles proven first-owner display clears.
- `29e9ea6` / `586cbd4` masked and independent stencil compare/replacement
  values, and `bce8924` dynamic stencil state: useful additional conformance
  cases, but require adaptation to the existing static/dynamic pipeline split.
  Current unsupported combinations retain their explicit boundary.
- `bbebb64` array-mip promotion and `3f80151` explicit Vulkan vertex-buffer
  binding sizes are follow-up coverage for guest descriptor bounds and cache
  identity. They are not evidence that existing Phase 5 alias tests should be
  removed or replaced.
- `7e89b97` front-shader allocation-register handling needs an evidenced
  consumer contract before treating more state as ignorable.
- `8641476` zero homogeneous-position culling was motivated by an Nvidia
  artifact. Do not introduce its shader workaround on the AMD-only target
  without reproducing the relevant behavior.
- Upstream native LS/HS/TES tessellation, VR/HMD, microphone/AudioIn, launcher,
  foreign-host and transfer-save changes are outside this bounded checkpoint.
  Keep native tessellation's explicit first-consumer rejection; do not claim
  the validated ring configuration implements tessellation.

## Validation

Fresh PowerShell process using `scripts/phase0/windows-env.ps1`, VS x64 tools,
LLVM 21.1.8, Release preset, reconfigured CMake/Ninja dependency build. Built
`kyty_emulator`, `kyty_tests`, `phase2_kernel_probe`, `phase5_presentation_tests`
and `phase5_random_stress_tests` successfully. This was a fresh tool environment
and dependency rebuild, not a deletion/rebuild of all third-party artifacts.
Filesystem fixtures used an isolated temporary directory under `_Build`.

| Run | Result |
|---|---|
| Focused resolver/POSIX/AGC driver/completion/scheduler/command-lane/presentation | 8/8 pass, 14.91 s; zero failures/timeouts |
| Full serial CTest | **84/85 pass, 1 failure, 0 timeouts**, 44.74 s; all 17 Phase 6 tests pass |
| Opt-in trace enabled: AGC driver, GPU command lane, presentation | 3/3 pass, 12.41 s; zero failures/timeouts |
| Randomized GPU resource stress, trace enabled | Pass and teardown pass, 30.055 s; 1,184 frames / 2,326 operations; seed `0x5058362026091601` |

Full-suite failure: `shader_recompiler_compute` reports
`PHASE0_UNAVAILABLE attachment feedback loop dynamic state is unsupported by this device`.
This is an unavailable capability **counted as a failure**, not a pass or silently
skipped case. Later cases inside that monolithic executable are not all reached;
the separately registered focused GPU fixtures run independently.

Real GPU: RX 7800 XT (`1002:747e`, driver `8389003`), Khronos layer package
1.4.357.0, Vulkan validation and synchronization validation enabled. Test logs
contain zero VUIDs, zero synchronization hazards and no nonzero validation-error
reports. The missing external EOS layer JSON / disabled implicit-layer messages
are loader warnings, not validation passes or errors hidden by the harness.
Synthetic audio uses the existing dummy transport; it does not revalidate
physical speaker output or controller manipulation.

The full-suite standard presentation run produced 709 frames in 10.005 s,
with VMA warm/peak 1,317,171,216 bytes and 13 allocations. Random stress used
warm/peak 1,317,235,728 / 1,317,273,616 bytes and 17/17 allocations. These are
short resource regressions, not a new one-hour soak or commercial-game benchmark.

Local evidence (ignored): `_Build/checkpoint-review-20260916/` contains configure,
build, focused/full/trace-enabled logs and JUnit results, full LastTest captures,
and `random-stress.log`. Retain the seed and frame/operation count for replay;
elapsed-time runs may stop at different frames. No evidence dumps are committed.

Historical September 15 evidence: 82/84 CTest passes, one `phase6_driver`
timeout and the existing `shader_recompiler_compute` attachment-feedback-loop
capability failure. Neither was a pass. The driver timeout did not recur in
the three current runs; this does not establish that every lifetime/deadlock
scenario is fixed. Existing exact-lookup-only tests do not
cover the broader late compatibility resolver; the new focused fixture does.

## Remaining Phase 6

1. Investigate PM4 blocked/retry and flush/submit churn without weakening guest
   completion, visibility or ordering contracts. Establish controlled same-route
   captures with validation and instrumentation settings recorded.
2. Diagnose repeating intro video separately: capture media end-of-stream,
   restart/seek and guest state transitions. Its cause is not established.
3. Complete required NGS graph/mixing and codec behavior; PCM end-of-stream,
   clock and underrun/device-loss conformance remain open. Bendy's audible audio
   does not establish full NGS fidelity for other guest workloads.
4. Complete mounted SaveData transactions and recovery. Persistent memory
   slots are not the mounted-save transaction implementation.
5. Finish media seek/reordering/clocks, user/dialog lifetime, controller
   reconnect/rumble and clean stop/relaunch.
6. Harden the compatibility mappings and libc allocator, and retain the deferred
   resource/DCC/stencil/tessellation limits above. Gameplay is integration evidence,
   not proof that every generic ABI or resource path is correct.
7. Turn the observed boot/menu/Chapter 1 gameplay into a recorded repeatable
   area-transition/checkpoint-reload/save/relaunch route, then ten successful
   launches and three 30-minute sessions. These complete Phase 6 gates remain
   unfulfilled. See the [historical integration record](phase6-interactive-integration.md)
   for service implementation details; its older runtime blockers are superseded.
