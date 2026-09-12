# Phase 5 final validation

Baseline: `08dd235` (Windows 11, Ryzen 7 7800X3D, Radeon RX 7800 XT).
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

The revised test clears half each image with exact RGBA8 UNORM endpoints and
checks the other half against the uploaded CPU pattern. It asserts pending
read ownership before publication and retirement afterward. An independent
byte array checks the entire guest allocation, including unrelated ranges and
padding. Partial writes use range invalidation and are reacquired and read
back through the cache to verify their GPU roundtrip. Presentation follows
these content oracles. Command buffers are reacquired after readback because
readback may finish and rotate the scheduler's current command buffer.

The generator retains variable extents, sequential overlapping/reused ranges,
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

The deterministic 3,600-second soak is running. The randomized 30-second test
is queued to run only after that succeeds. Results will be recorded here after
the processes exit. No commercial input is used. Phase 6 has not started.
