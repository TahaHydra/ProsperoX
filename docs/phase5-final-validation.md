# Phase 5 final validation

Baseline: `08dd235` (Windows 11, Ryzen 7 7800X3D, Radeon RX 7800 XT).
Reviewed test additions were subsequently committed as `b00d5a8`; the final
layout correction described below is a local test-only change on that commit.
This checkpoint changes the added randomized test only; the deterministic
presenter workload and production implementation are retained.

## Randomized test review

The original test selected small, single-mip, single-layer linear RGBA8 images,
used supported storage/color/VideoOut roles, and recycled mapped ranges. These
are useful cache and presentation workloads. Random pacing is scheduling
jitter, not a simulation of a game's GPU queue depth. Each image operation
presents once; `frames` counts batches of one to three operations, while `ops`
counts completed image operations/presents.

The review found gaps in the original oracles:

- `GpuResourceManager::HandleFault` returns true for a mapped address. A true
  result does not prove that an image was tracked or its bytes published.
- Clearing the entire image hid failed CPU uploads. Unchecked random floating
  clear values did not provide a portable byte oracle.
- Partial CPU patches were never checked or uploaded again.
- A one-byte fault notification is not an explicit multi-page write range.
- The test could pass after 30 frames although VMA warmup required 60 frames.
- Permissive number parsing and sparse failure context weakened reproduction.
- The initial generator used tightly packed rows for widths 32 and 96, whereas
  the supported linear RGBA8 transfer layout requires 256-byte row alignment.
  This defect survived the first review and was exposed by the new byte oracle.

The revised test clears half each image with exact RGBA8 UNORM endpoints and
checks the other half against the uploaded CPU pattern. It asserts pending
read ownership before publication and retirement afterward. An independent
byte array checks the entire guest allocation, including unrelated ranges and
padding. Partial writes use range invalidation and are reacquired and read
back through the cache to verify their GPU roundtrip. Presentation follows
these content oracles. Command buffers are reacquired after readback because
readback may finish and rotate the scheduler's current command buffer.

The corrected generator retains variable extents, sequential overlapping/reused ranges,
one to three operations per batch, variable GC cadence, yields, and bounded
delays. It does not deliberately create concurrent conflicting GPU owners,
unsupported formats, fabricated metadata, or out-of-bounds resources. The
allocation-wide oracle remains strict if an unexpected supported-case failure
is found. This fixture does not establish shader correctness, general tiled
image publication, asynchronous queue pressure, resize/minimize correctness,
or commercial gameplay.

Every operation logs seed, frame index, operation index, address, and extent
before performing work. Mismatches print stage, byte offset, expected byte,
and actual byte. Standard `mt19937_64` outputs and modulo selection give a
repeatable operation stream; wall time changes the number of operations, not
the prefix. Optional `replay_frames` reproduces a fixed-length prefix:

```powershell
phase5_random_stress_tests.exe 30 0x5058352026091201
# Replay 1,000 complete batches with the same random stream:
phase5_random_stress_tests.exe 30 0x5058352026091201 1000
```

VMA bounds remain 256 MiB and 256 allocations above the 60-frame warmup.
Warmup is mandatory for success. The production Vulkan callback terminates on
validation errors; the run must also exit successfully after window teardown.

The partial clear oracle follows the Vulkan specification's
[render-area load/store semantics](https://docs.vulkan.org/spec/latest/chapters/renderpass.html).
Synchronization validation is explicitly enabled through the pinned layer's
settings, following the
[Khronos layer configuration](https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/main/docs/updating_from_VK_EXT_validation_features.md).

## Evidence and results

Evidence directory: `_Build/evidence/phase5-close-20260912-101906`.
It preserves source snapshots, revision/diff, executable/layer hashes,
environment settings, start/end timestamps, exit codes, and unabridged logs.
Implicit third-party overlays are disabled for these test processes only.
Seed: `0x5058352026091201`.

| Run | Result | Frames / operations | Warm / peak VMA bytes | Warm / peak allocations |
| --- | --- | --- | --- | --- |
| Unchanged deterministic presentation soak | PASS, 3,600.002 s, exit 0 | 311,884 frames | 1,317,171,216 / 1,317,171,216 | 13 / 13 |
| Corrected randomized stress | PASS, 30.051 s, exit 0 including teardown | 251 batches / 501 operations and presents | 1,317,231,632 / 1,317,273,616 | 17 / 17 |
| Fresh 32x8 layout probe | PASS, 3.318 s, exit 0 | 60 / 60 | 1,317,162,000 / 1,317,162,000 | 13 / 13 |
| Same-address 64x16 / 32x8 reuse probe | PASS, 3.422 s, exit 0 | 60 / 60 | 1,317,165,072 / 1,317,165,072 | 13 / 13 |

All four runs used the real RX 7800 XT (`1002:747e`, driver `8389003`) and
the pinned Vulkan 1.4.357.0 validation layer with synchronization validation
enabled. Logs contain zero Vulkan validation errors, VUIDs, or synchronization
hazards. A GENERAL loader diagnostic about a missing Epic overlay manifest is
preserved; it is not a Vulkan usage or synchronization validation error.
The deterministic soak has zero measured growth; randomized growth is 41,984
bytes with no allocation-count growth. These are live VMA statistics sampled
by the fixtures after warmup, not total process memory or every transient peak.

The full registered regression sweep in
`_Build/evidence/phase5-close-full-regression-20260912` ran 68 tests: 67 passed.
`shader_recompiler_compute` exits at the previously documented
`PHASE0_UNAVAILABLE attachment feedback loop dynamic state is unsupported by
this device`. CTest reports it as a failure, so the command exits nonzero;
it is an existing device-capability exclusion, not a clean 68/68 result. Cases
after that early exit in the monolithic executable are not covered by this
sweep. All registered focused Phase 5 tests passed. Earlier host allocation
and filesystem flakiness did not recur in this sweep.

## Initial failure and upstream comparison

The first randomized run, after the completed soak, failed on the fifth
operation (frame 2, operation 1, seed `0x5058352026091201`): a 32x8 image at
`0x209018000` after 64x16 address reuse differed at allocation byte 98,432
(128 bytes into that image). Its log and nonzero exit are retained as
`random.log` and `random-exit.txt`; they are not counted as a pass.

The diagnostic log proved that the requested, cached, and backing extents all
were 32x8. More decisively, a fresh first-ever 32x8 image reproduced the same
relative byte-128 mismatch without any previous cache owner. The test described
a 128-byte row and 1,024-byte image. `TileGetTexturePitch`, `TileGetTextureSize`,
and `TextureCalcUploadLayout` describe a 256-byte row and 2,048-byte image for
this supported linear layout. Byte 128 is row padding, not the next pixel row;
the original oracle incorrectly expected it to be GPU-cleared. The analogous
96-pixel width also needs padding (512-byte rows).

The fix independently computes the aligned stride, supplies matching pitch,
size, and mip layout, and uses that stride for the byte oracle. It checks the
generator against the tiler contract, while expected pixel contents remain
independent of renderer output. Padding and all unrelated bytes still must
match exactly. No assertion was weakened and no production source was changed.

Actual upstream diffs were inspected and retained under `_Build/phase5-research`:

| Lead | What it addresses | Decision for this reproducer |
| --- | --- | --- |
| [Kyty #266](https://github.com/KytyPS5/KytyPS5/pull/266), [#374](https://github.com/KytyPS5/KytyPS5/pull/374) | Clamp cached-alias copies to both source and destination backing dimensions, with layer/volume handling. | Useful independent extent-safety reference; not the cause here. A fresh owner fails with the malformed pitch too. No port. |
| [Kyty #375](https://github.com/KytyPS5/KytyPS5/pull/375) | Derive render area from attachment backing extent and view mip. | Useful renderer reference. This fixture records dynamic rendering directly, with correct backing/view extent and a bounded half-width render area; it does not use `RenderExecutor::AcquireRenderTargets`. No port. |
| [Kyty #450](https://github.com/KytyPS5/KytyPS5/pull/450) | Narrow storage/sampled R8UInt/R8UNorm owner mismatch recognition and publication before splitting those incompatible owners. | This is not a generic reshape fix; our fixture uses RGBA8 with the same storage format. No port. |

The evidence identifies an invalid pitch/size test fixture, not the cached
backing-dimension or incompatible-format-owner defects in those PRs. This does
not establish that every other alias path is correct. Clamping both copy
endpoints, attachment-view bounds, and pitch-aware owner identity remain useful
focused audit leads if a valid workload exposes them. The 60-frame resize mode
reuses the address across frame unmap/remap boundaries; it does not prove
simultaneous live-owner reshape behavior.

## Reproduction and closure

From the repository root, load `scripts/phase0/windows-env.ps1`, build targets
`phase5_presentation_tests` and `phase5_random_stress_tests` with the
`phase0-windows` preset, and apply the captured `environment.txt` settings.
Run the following executables from `_Build/phase0-windows`, preserving output
and exit codes in a new evidence directory:

```powershell
.\phase5_presentation_tests.exe 3600
.\phase5_random_stress_tests.exe --fresh-small
.\phase5_random_stress_tests.exe --resize-repro
.\phase5_random_stress_tests.exe 30 0x5058352026091201
# Reproduce exactly the successful randomized operation prefix:
.\phase5_random_stress_tests.exe 30 0x5058352026091201 251
```

The requested Phase 5 closure gates are complete on this machine, with the
pre-existing full-suite capability exclusion above explicitly retained.
This closes the documented supported Phase 5 checkpoint, not all GPU formats,
unimplemented feedback-loop support, or commercial-game compatibility.
No commercial input was used. Phase 6 has not started.
