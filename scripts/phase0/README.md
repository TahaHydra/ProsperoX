# Phase 0: build and conformance baseline

Active scope (2026-09-09): Windows 11 on Ryzen 7 7800X3D / Radeon RX 7800 XT.
Linux and other GPU/platform validation below is deferred by user decision;
see `docs/platform-scope.md`. A missing capability is never a correctness pass.

This gate requires no commercial files. It builds the existing emulator and tests,
records the entire CTest inventory, and runs original synthetic probes against
production paths. The harness itself does not change guest behavior; Phase 1
runtime repairs and their acceptance matrix are documented in
`docs/phase1-runtime-boundary.md`.

## Windows

Prerequisites: Visual Studio C++ build tools/Windows SDK, CMake >= 3.25, Git,
7-Zip, Python >= 3.10, and a working Vulkan driver for hardware tests.
The bootstrap downloads official, SHA-256-verified portable LLVM 21.1.8 and
glslang 16.5.0 archives into ignored `_Build/tools`. It does not run the LLVM
installer or modify system PATH. Existing Visual Studio supplies Ninja and the CRT.

From the repository root in PowerShell:

```powershell
./scripts/phase0/bootstrap-windows.ps1
. ./scripts/phase0/windows-env.ps1
git submodule update --init --recursive --jobs 6
cmake --preset phase0-windows
cmake --build --preset phase0-windows
python -m unittest discover -s scripts/phase0 -p 'test_*.py'
python scripts/phase0/run.py --build-dir _Build/phase0-windows --output-dir _Build/evidence/windows-1 --gpu required
python scripts/phase0/run.py --build-dir _Build/phase0-windows --output-dir _Build/evidence/windows-2 --gpu required
python scripts/phase0/compare.py _Build/evidence/windows-1 _Build/evidence/windows-2
```

Replace `python` with an installed Python executable if Windows resolves an App
Execution Alias. Do not use arbitrary executables advertised as missing DLL fixes.
The test environment disables optional implicit Vulkan overlays for these child
processes and uses SDL's dummy audio output. It does not disable the GPU driver.
Keep configure/build stdout and stderr as evidence alongside the results.

## Ubuntu 24.04 / WSL

Install build dependencies using the distribution package manager:

```bash
sudo apt-get update
sudo apt-get install --no-install-recommends -y clang-18 lld-18 cmake ninja-build \
  glslang-tools spirv-tools python3 git libasound2-dev libdbus-1-dev libgl1-mesa-dev \
  libpulse-dev libudev-dev libwayland-dev libx11-dev libxcursor-dev libxext-dev \
  libxfixes-dev libxi-dev libxkbcommon-dev libxrandr-dev libxss-dev \
  zlib1g-dev libbz2-dev liblzma-dev vulkan-tools mesa-vulkan-drivers
git submodule update --init --recursive --jobs 6
cmake --preset phase0-linux
cmake --build --preset phase0-linux
python3 -m unittest discover -s scripts/phase0 -p 'test_*.py'
python3 scripts/phase0/run.py --build-dir _Build/phase0-linux \
  --output-dir _Build/evidence/linux-1 --gpu required
```

If the environment has no declared supported GPU, use `--gpu unavailable
--gpu-reason 'specific observed reason'`. This records every GPU entry as
**unavailable**, not passed. WSL does not automatically expose the Windows Radeon
as a native Vulkan device. Software Vulkan can be a separate experimental lane;
it does not satisfy the physical GPU acceptance criterion.

To verify a clean rebuild, use a second build directory via `cmake --preset ...
-B <new-directory>`, then `cmake --build <new-directory> --target kyty_emulator
kyty_tests --parallel <bounded-jobs>`. The comparison checks implementation/test
content and outcomes, not identical absolute paths or timing. A fresh checkout
plus the exact implementation patch is the strongest reproduction test. Preserve
the source revision, patch, submodule revisions, CMake cache and archive hashes.

## Inventory and results

The audited source had 36 Windows / 33 non-Windows CTest entries. Phase 0 adds
four conformance probes plus one explicit host-only SPIR-V validation entry:
**41 Windows / 38 non-Windows entries**. Multiple names select different paths in
one executable; counts are registrations, not independent semantic coverage units.
Phase 1 adds ten reviewed entries, bringing the current Windows inventory to 51.

`inventory.json` is the reviewed name list. `test-labels.cmake` classifies host,
native, GPU and SPIR-V requirements. Removal, addition, duplication or lack of
classification fails the runner until the inventory is deliberately reviewed.
All binaries are built even on a machine with no GPU.

`run.py` consumes CTest's resolved commands and properties and executes each in
an isolated child process, sequentially. It honors normal `WILL_FAIL` tests;
signals/NTSTATUS crashes, missing binaries and timeouts never become successes.
Logs, manifests, source hashes, commands, raw exits, durations, inventory and
JSON summaries are saved in a new output directory. Existing evidence cannot
be overwritten. Generated SPIR-V is checked by the linked SPIRV-Tools validator
in the existing compiler harness, including the explicit host-only entry.

A known failure remains `fail` or `crash` in the output. The optional regression
allowance matches test, platform, exact exit/status and a narrow diagnostic
signature; it also has a defect ID, owner phase and reproducer. The baseline
gate may pass with those recorded defects, while `all_correctness_tests_passed`
remains false. Unexpected failures fail the gate. A passing aggregate proves
only its assertions; an early abort leaves later internal cases unexecuted.

## Probe contracts

| Probe | Actual path and original input | Expected correctness |
| --- | --- | --- |
| unresolved import | Real private linker registration and emitted SysV thunk, original nonexistent global function | Explicit unsupported failure; never invent a return value |
| path containment | Real mount resolver, synthetic traversal and unmapped paths; no host file access | Resolved path stays contained or is denied; unmapped path denied |
| wave mask | Existing original `BranchVccnzUsesWaveMask` ISA fixture through compiler, SPIR-V validator and real GPU readback | A scalar VCCNZ decision selects 42 for every lane, including lanes whose local VCC bit is zero |
| EOP visibility | Actual six-dword EVENT_WRITE_EOP packet through PM4 processing; GPU fill behind an unsignaled host timeline semaphore | Completion label stays zero before retirement, becomes one afterward, and GPU readback is 0x505830 |

The EOP probe uses ordinary host memory to avoid a resource page fault masking
early writes. It always releases the gate and drains work before reporting a
mismatch. It distinguishes parsing, submission, retirement and data/label
visibility; it tests one 64-bit no-interrupt packet form. Guest interrupt, writeback,
32-bit and cross-queue variants belong to Phase 3, not an implied result here.

The runtime probe includes the existing linker translation unit in a test-only
target so private thunk machinery can be tested without exporting a production
test API. CMake excludes its separate object for that target. No Sony SDK, game,
firmware, key or commercial shader fixture is introduced.

## CI and gate self-test

Windows hosted CI builds the full targets, runs host/native/SPIR-V tests,
and accounts explicitly for unavailable GPU tests. Evidence uploads use
`if: always()` so failed runs retain diagnostics. Hardware tests run using the
same command with `--gpu required` on a declared supported device. This change
does not register this personal computer as a public self-hosted CI runner.

The runner's unit suite launches real child processes to prove that a controlled
output mismatch, missing binary, unexpected exit and timeout cannot pass. It
also tests narrow known-failure matching and missing/duplicate test discovery.
Performance numbers from this baseline are diagnostic timings, not emulator FPS.
