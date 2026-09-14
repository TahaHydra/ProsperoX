# Phase 6: tessellation configuration checkpoint

2026-09-14. Windows 11 / Ryzen 7 7800X3D / RX 7800 XT / 32 GB.
Phase 6 remains open. This supersedes the unresolved-ring boundary in
[the earlier AGC checkpoint](phase6-agc-initialization.md). Previous AGC,
qualified resolution and loader behavior are retained.

## Evidence and supported contract

`XlNp7jzGiPo[AgcDriver_v1][AgcDriver_v1.1]` is now individually registered.
Its SysV arguments are a pointer and a 32-bit **byte size**. Runtime capture
observed `base=0x213226500, size=0x3fff8` (262,136 bytes). Static tracing of
the caller at `0x901095770` shows the same byte count used for allocation:
the size helper multiplies the element count by a stride of one. The wrapper
at `0x9003b2650` tests nonzero, 256-byte-aligned storage. These observations
inform the generic contract; no title/address/size constants enter production.

The prior Kyty-derived global setters had no consumers. Current
[Kyty AGC source](https://github.com/KytyPS5/KytyPS5/blob/main/src/libs/agc.cpp)
still provides no evidence of a complete native tessellation implementation.
SharpEmu's `AgcExports.cs` independently identifies the pointer/size register
arguments but returns success without implementing the ring. Neither noop was
adopted. Public AMD PAL provides the hardware interpretation:

- [gfx9ShaderRing.cpp](https://github.com/GPUOpen-Drivers/pal/blob/dev/src/core/hw/gfxip/gfx9/gfx9ShaderRing.cpp): tessellation-factor allocation sizes versus DWORD register sizes; offchip buffering is block count minus one.
- [gfx9ShaderRingSet.cpp](https://github.com/GPUOpen-Drivers/pal/blob/dev/src/core/hw/gfxip/gfx9/gfx9ShaderRingSet.cpp): 256-byte base encoding and ring register programming.
- [gfx9_plus_merged_registers.h](https://github.com/GPUOpen-Drivers/pal/blob/dev/src/core/hw/gfxip/gfx9/chip/gfx9_plus_merged_registers.h): LS/HS stage bits and the gfx10.3 ten-bit `OFFCHIP_BUFFERING` field.

These are behavioral references, not source transplants or a claim that Vulkan
exposes the guest hardware ring. Downloaded reference snapshots are retained in
ignored `_Build/pal-gfx9ShaderRing.cpp`, `_Build/pal-gfx9ShaderRingSet.cpp` and
`_Build/pal-gfx9plus-reg.h`; existing license notices are unchanged.

`TessellationState`, owned by the renderer, now:

- validates a nonzero 256-byte-aligned base, nonzero DWORD-aligned byte size,
  bounded 40-bit guest range and committed GPU-readable/writable mappings over
  the **entire** range;
- records exact configuration and generation under a mutex, preserving the
  old configuration on rejected replacement, and resets at renderer shutdown;
- neither allocates nor writes, owns or pins the guest backing;
- revalidates configuration at native tessellation consumption and reports an
  explicit unsupported boundary before any shader/resource/pipeline work.

The accepted subset is an emulator support contract, not an assertion about
every legal Sony argument or error code. Unsupported setup reports its reason
and exits 86 rather than returning an invented guest error. Future native
tessellation support must establish stage execution, ring addressing, lifetime
pinning and synchronization. Configuration alone does not implement those.

Both indexed and automatic draw consumers reject LS/HS-enabled or patch draws
with `AGC_NATIVE_TESSELLATION_UNSUPPORTED`. Zero-work draws remain no-ops.
The check precedes the missing-vertex-shader early return, which previously
could silently discard such work. Existing rectangle-list host tessellation
is independent and remains supported. Indirect draws reach these consumers.

## Following runtime blockers

Ring acceptance exposed `MM4IZSEYytQ[AgcDriver_v1][AgcDriver_v1.1]`.
The guest wrapper at `0x9003b2670` takes an allocation size, shifts by 15,
subtracts one, moves the resulting count into ESI, zeros EDI and tail-calls
the import. It does not establish a third argument. The observed allocation
is 16 MiB, giving `(control=0, buffering=511)` for 512 blocks.

The narrowly registered HS-offchip setter now takes **two uint32 arguments**,
retains control-zero configuration with a ten-bit biased count, rejects
unknown control/field values, and supplies its state to the native-draw
diagnostic. It does not interpret EDI as a pointer or incidental RDX as data.

The next run reached real 48 kHz stereo audio-device creation, the FMOD worker
and initial level asset reads before missing POSIX `pthread_cond_destroy`.
Added only `RXXqi4CtF8w[Posix_v1][libkernel_v1.1]` using a proper POSIX wrapper
over the existing real condition destructor. Positive pthread errors preserve
errno; the legacy kernel export keeps its kernel error convention.

The final bounded run proceeds past this and stops at:

```text
VAzswvTOCzI[Posix_v1][libkernel_v1.1][Func]
Media/Modules/Il2cppUserAssemblies.prx, relocation 119, patch 0x94392c1b0
```

This is `unlink`; the legacy kernel registration exists, but a qualified POSIX
wrapper remains absent. Next work: capture the requested path, establish
mounted-file mutation semantics and test success/ENOENT/directory/error
translation before exposing it. The existing mount resolver confines paths,
but mount access policy and unlink-on-open lifetime also warrant review.
No global NID fallback or blanket alias was added.

Evidence runs, each with input hashes and logs:

- `phase6-bendy-ring-20260914`: ring accepted; missing HS-offchip import.
- `phase6-bendy-offchip-20260914`: 13.037 seconds, exit 86 at condition destroy;
  real audio-device open succeeded.
- `phase6-bendy-cond-20260914`: 19.013 seconds, natural exit 86 at `unlink`.
  RX 7800 XT selected. Windows WASAPI instead reported the requested audio
  endpoint unavailable in this run; audible output is not validated.

All are under `_Build/evidence/`. No native tessellation draw, guest GPU
submission, presented game frame, menu or gameplay is demonstrated. Game
executable hash remains `149EEA474A0C79EC6B8E79F9F352F37A8180B1FA7ED294B09937E6D119D9C548`.
No commercial payload or proprietary SDK content was added to source/tests.

## Regression coverage

`phase6_ring` checks strict qualified identities, exact byte counts, invalid
ranges, full-range permissions, replacement rollback, untouched payload,
unmap revalidation, reset and offchip bounds. `phase6_tessellation_boundary`
uses actual renderer state and draw consumers on Vulkan in three
subprocesses (indexed, auto, patch), verifies empty-draw
behavior, then requires exit 86 and a valid-mapping diagnostic.

The initial draw-boundary regression failed because drawing returned silently;
the implemented consumer check made it pass. Offchip and condition regressions
first failed on missing qualified exports. `phase6_posix_files` now also tests
real condition init/destroy, handle release and distinct POSIX/kernel errors.
Assertions were not weakened. Full regression results are recorded below.

Final build succeeded for `kyty_tests`, `kyty_emulator`, `phase2_kernel_probe`,
`phase5_presentation_tests` and `phase5_random_stress_tests`. CTest:
**80/81 passed in 51.95 seconds**, including **13/13 Phase 6 tests**.
Evidence: `_Build/evidence/phase6-ring-regression-20260914` contains full test
output, JUnit, validation environment and reference-source hashes. Build logs:
`_Build/phase6-ring-regression-build.log` and
`_Build/phase6-ring-regression-final-build.log`.

The sole failure is unchanged: `shader_recompiler_compute` reports
`PHASE0_UNAVAILABLE attachment feedback loop dynamic state is unsupported by this device`.
It remains a failure, not a skip or pass; later cases in that monolithic test
do not execute. No new regression failure was found. The real RX 7800 XT
(vendor 1002, device 747e, driver 8389003) ran with Vulkan validation and
synchronization validation including submit-time checks. Logs contain no VUID,
SYNC-HAZARD or reported positive validation error counts.

Phase 3 completed 10,000 real-device release lifecycles. The standard Phase 5
presentation test completed **323 frames / 10.023 seconds**, with warm and peak
**1,317,171,216 bytes / 13 allocations**. This is a short regression check,
not another one-hour soak or a performance comparison. The randomized stress
executable was built but its standalone stress mode was not run in this checkpoint.

Phase 6 is **not complete**: the next real-title blocker is qualified POSIX
`unlink`; native tessellation and the previously documented interactive,
audio, save and gameplay completion gates remain open. No Phase 7 work.
