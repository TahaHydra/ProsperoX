# Active hardware scope

User decision, 2026-09-09: prioritize Windows 11 on Ryzen 7 7800X3D,
Radeon RX 7800 XT and 32 GB DDR5-6000 until a fully working game is
demonstrated on this PC. Other platforms and GPU vendors are deferred.

This supersedes cross-platform acceptance criteria in the original engineering
plan. Windows builds, CPU conformance and actual Radeon Vulkan execution remain
required. Unsupported features and correctness failures remain visible. Game
progress does not justify fabricated HLE success or synchronization shortcuts.

The commercial files are not inputs to the current synthetic conformance work.

## Deferred portability work

- Complete and repeat the Linux build and host conformance baseline. The first
  temporary WSL build was interrupted; the replacement build was stopped when
  this scope changed. Neither is a completed Linux validation result.
- Run Linux Vulkan tests on an actual supported GPU; WSL currently exposes only
  llvmpipe. Software rendering is not evidence of Radeon conformance.
- Validate native calling conventions, TLS, fault handlers and filesystem
  behavior on other host operating systems.
- Validate subgroup/wave behavior, format support, synchronization and shader
  output on NVIDIA and additional AMD hardware/drivers.
- Restore a required multi-platform CI/release matrix after the target-PC
  gameplay milestone. Existing portable code should not be removed needlessly.

Record newly discovered platform dependencies here instead of implementing
another backend or extending the validation matrix during the current phases.

Phase 1 adaptations deferred: the new filesystem fixture uses NTFS junctions;
the native-state fixture uses Windows intrinsics and AVX; the TLS helper saves
Windows-enabled XSTATE with XSAVE/XRSTOR. Non-Windows test registration and
runtime exception compiler flags require adaptation before those builds resume.
The original platform code remains available, but its current behavior is not
validated by this Windows-only milestone.

Windows TLS sites now use per-site jumps in the shared module patch pool,
including continuation metadata for red-zone analysis. Port that interaction
and its live-red-zone regressions before enabling another native backend.
Host CPUID remains visible on the 7800X3D; a portable guest CPU profile is deferred.

Phase 3 adaptations deferred: replace the Windows `VirtualAlloc`/`VirtualProtect`
guard-page fixture on other hosts; obtain matching Vulkan validation binaries;
repeat timeline, dirty-backing, indirect-argument, callback and staging-pressure
tests on each host/GPU. The completion implementation uses the existing Vulkan
and backing-store interfaces, but only Windows 11 / RX 7800 XT is validated.

Phase 4A adaptations deferred: rerun the raw wave-control and inactive-lane
matrix on each driver/device before enabling it as a supported host. Required
subgroup32/64 test pipelines currently require both sizes and compute-stage
subgroup-size control on the RX 7800 XT. Other devices need explicit capability
classification; their absence must not silently count as a pass. Graphics
helper invocations and multiple guest waves per larger host subgroup need
additional fixtures. No production subgroup-size policy changed in Phase 4A.
