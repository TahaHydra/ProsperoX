# Phase 0 baseline — completed for the designated Windows PC

Scope revised by the user on 2026-09-09: Windows 11, Ryzen 7 7800X3D,
Radeon RX 7800 XT, 32 GB DDR5. Linux and other hardware are deferred.

The emulator and all Windows test targets built successfully with portable
LLVM 21.1.8, glslang 16.5.0, Visual Studio C++ build tools and Windows SDK.
A second build in a new build directory from frozen Phase 0 sources also
succeeded. The comparison script reported `reproduced: true` with no errors
between the original and clean-build runs. This is configuration/test-outcome
reproducibility, not a claim of byte-identical binaries.

Both runs account for **41 tests: 29 pass, 9 fail, 2 assertion crashes and
1 unavailable**. The baseline gate passes with narrowly identified defects;
the emulator is not certified correct. The unavailable aggregate GPU test needs
attachment feedback loop dynamic state, which this driver does not expose.
Other GPU selectors execute. The harness now matches production's optional
feedback feature handling.

Radeon execution and host SPIR-V validation were observed. In particular:

- Missing strong import: the original thunk returned zero (Phase 1).
- Filesystem: the original resolver allowed traversal and unmapped paths (Phase 1).
- Wave branch: expected eight values of 42; observed 42,11,11,11 twice (Phase 4).
- EOP: label was 1 before release, while GPU work was not retired; GPU readback
  after release was exactly 5265456 (Phase 3).
- Socket PEEK/WAITALL assertion failed (Phase 2).
- Six resource selectors rejected D24S8 format 129 with usage 0x23, at one or
  two samples, on this Radeon driver (Phase 5).

The Python gate has ten passing regression tests, including actual child
timeouts, missing executables, injected mismatches, expected errors versus
crashes, and explicit capability-unavailable handling.

## Evidence locations

All paths below are relative to the project root and intentionally ignored by Git:

- `_Build/evidence/windows-baseline-a`: initial finalized Phase 0 run.
- `_Build/evidence/windows-baseline-clean`: independent clean-build run.
- `_Build/phase0-build-fresh-windows.log`: successful clean build.
- `_Build/phase0-frozen-source`: source snapshot preceding Phase 1 changes.
- `_Build/evidence/windows-portability`: raw discovery evidence for each defect.

Each run retains source/file hashes and dirty status, dependency revisions,
tool versions, configuration cache, hardware/driver discovery, commands, raw
exit status, timeouts and per-test logs. The driver reported AMD 26.8.1 / LLPC,
Vulkan 1.4.349. Implicit Vulkan overlays were disabled only in test processes;
no system registry or driver configuration was changed.

The initial Linux builds did not complete and are not claimed as validated.
The active replacement Linux build was stopped at the user's Windows-only
scope change. No commercial game, firmware, SDK, keys or assets were used.
