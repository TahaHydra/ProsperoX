# Phase 4A: architectural wave control

Scope: Windows 11, Ryzen 7 7800X3D, RX 7800 XT, 32 GB RAM. Starting revision:
`4795c94` (completed Phase 3). No commercial inputs were accessed. Phase 4A is
the wave-control checkpoint of Phase 4; dynamic resources and broader ISA
coverage remain subsequent checkpoints.

Status: **complete for this checkpoint on the target machine, 2026-09-10**.

## Investigation and decision

The existing decoder, CFG/structurizer, typed IR/SSA, scalar provenance and
paired-wave64 backend provide a useful foundation. The blocking P0-WAVE
reproducer exposed a semantic defect: guest scalar branches used each lane's
VCC/EXEC Boolean. Inactive lanes could leave a region even though the guest
scalar branch takes the same destination for the whole wave. SAVEEXEC and
some scalar bitwise/select paths also reconstructed architectural words from
participating invocations, losing bits in partial waves. Fixing this contract
has higher leverage than adding opcodes on top of inconsistent masks.

Research snapshots were read without merging upstream code. The following
source and history review was performed on 2026-09-10; snapshots/API responses
are retained locally under `_Build/phase4a-research/` (ignored).

| Source / pinned revision | Source-level finding and decision |
| --- | --- |
| [KytyPS5](https://github.com/KytyPS5/KytyPS5), `3da05eff716bf0ce93dd4c846e50cd240e7b5d67` | Reviewed main, the returned branch inventory (main), recent commits, open issues/PRs and selected patches/discussions. Commit `1af19eabf070ea74337bd1689b1081c2db1c296f` corrects raw VCCZ/EXECZ reads but does not cover all branch/Boolean operand paths. Apply the architectural rule consistently in our translator. Retain our Phase 1–3 runtime and scheduler. GPL-2.0; behavioral reference here. |
| [Kyty PR 470](https://github.com/KytyPS5/KytyPS5/pull/470), `d652e5cb77277c4bb6768a5528c51160c380e0be` | Raw low/high wave tests are useful; our base already contains related ballot/lane machinery. Its shared-arm discussion identifies assumptions worth testing, not a reason to replace our CFG. Architecture and regression-test inspiration only. |
| [Kyty PR 500](https://github.com/KytyPS5/KytyPS5/pull/500), `6c6e3e7edaa714ad969f627af22270cd92633caf` | Broad shader/resource bundle: dynamic descriptors, typed resources and wave work. A subgroup reduction of a lane Boolean cannot preserve arbitrary raw architectural mask bits in a partial wave. No bulk port. Descriptor/format changes are later isolated candidates; reported Demon's Souls progress is not a reproducible acceptance result here. |
| [Kyty PR 463](https://github.com/KytyPS5/KytyPS5/pull/463), `f0476f7ea7dc03108e09556a709a284d01590895`; [PR 361](https://github.com/KytyPS5/KytyPS5/pull/361) | Wave32 high-word and guest/host-width cases inform coverage. Our paired wave64 execution already exists; validate it on required host subgroup32 rather than importing an older backend. |
| [SharpEmu](https://github.com/sharpemu/sharpemu), `ca90d175412206173f7640d44493263d9b913af7` | `Gen5SpirvTranslator.cs` and `Gen5WaveMaskSpirvTests.cs`: useful compare/conditional-mask fixtures, but subgroup-any lowering and emitted-opcode assertions are not an independent architectural oracle. Metal behavior is outside the selected host. Behavioral/test inspiration only; GPL-2.0-family sources, no copying. |
| [Prosper](https://github.com/mattias800/prosper), `aeef2da18cac178c92c3c597aed94af3d2145139` | `rdna2_to_spirv.cpp::safe_execz_branches` has constrained linearization; shader-inspection wave-width proofs suggest useful future tools. This does not justify replacing our CFG. Its README explicitly grants no license: no implementation or test copying. Public conceptual reference only; title-specific executor paths are not adopted. |
| [shadPS4](https://github.com/shadps4-emu/shadPS4), `7a8caf12b60133a3303acef31b055c1d0ebe08c5` | Frontend data-share and SPIR-V warp code distinguish lane reads and host subgroup operations. Broadcast-first alone does not implement guest EXEC=0 semantics; the inspected WriteLane path is incomplete. IR/SSA architecture and differential-test ideas only, not a PS5 wave32 oracle. GPL-2.0-or-later. |
| [RPCSX](https://github.com/RPCSX/rpcsx), `e8ae1481ab7ba04d5c6bef89dd852aabba2c88ff` | `gpu/lib/gcn-shader/src/gcn.cpp` retains two-word EXEC/VCC separately from flag predicates. Useful architecture reference; GCN instruction behavior is not automatically RDNA2 behavior. GPL-2.0; no copied implementation. |

Recent Kyty subvector-loop and WQM changes, PRs 532/537 (resource binding and
visibility), 542 (mixed shader/runtime fixes), and issues 554/550/63/281 remain
leads for later minimized cases. Reports span different regions/versions and
drivers. They do not establish gameplay or a new commercial baseline for this
checkpoint. No additional upstream dependency was warranted by these findings.

The semantic authority is AMD's public [RDNA2 ISA reference](https://www.amd.com/content/dam/amd/en/documents/radeon-tech-docs/instruction-set-architectures/rdna2-shader-instruction-set-architecture.pdf),
30 November 2020, sections 3.3, 3.9, 12.3 and 12.8: scalar control is unmasked,
wave32 flag tests use the low word, SAVEEXEC performs scalar word arithmetic,
and READFIRSTLANE with EXEC=0 reads lane zero. Khronos
[subgroup control-flow guidance](https://docs.vulkan.org/guide/latest/extensions/VK_KHR_shader_subgroup_uniform_control_flow.html)
does not repair an incorrect guest scalar predicate. The test harness uses
[required subgroup size](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineShaderStageRequiredSubgroupSizeCreateInfo.html)
with feature/range/stage checks; no new production device requirement is added.
New changes and synthetic fixtures are original ProsperoX work. Preserve all
existing licenses; reassess each file's license before any future adaptation.

## Implemented contract

- One low/high-word helper drives scalar VCCZ/EXECZ reads and all four mask
  branches. Wave32 ignores the high word for flags; wave64 includes it.
- SAVEEXEC reads inputs before aliased destinations, saves raw words, applies
  the implemented B32/B64 bitwise operations, preserves the untouched high
  word for B32, and sets SCC from the result's specified width. Scalar B64
  bitwise and conditional-select destinations also retain raw words.
- The paired-wave64 branch emitter consumes that scalar condition directly.
  It no longer re-ballots a condition and mistakes partial participation for
  a zero/full architectural mask. Existing VGPR/store predication remains.
- Wave32 compare/compare-and-update-EXEC operations preserve high words.
  READFIRSTLANE explicitly selects lane zero for an empty EXEC mask.

The existing host structural test that required Boolean complement lowering
was corrected to require scalar word complement. Exact GPU output, rather
than a preferred SPIR-V instruction shape, is the semantic acceptance gate.

## Coverage and reproduction

`tests/Phase4AWaveTests.inc` contains original synthetic instruction streams
and independent scalar expected-value arithmetic (seed `0x505834a`). Each case
is decoded, translated, SPIR-V validated, executed and checked by exact buffer
readback; this is not a decode-only coverage claim.

| Fixture family | Execution-tested variants |
| --- | --- |
| Branch/status flags | EXECZ/NZ and VCCZ/NZ; zero, bit 0/31/32/63, all bits; distinct EXEC/VCC; raw status reads |
| SAVEEXEC | 24 deterministic patterns per each of AND_B64, ORN2_B64, ANDN1_B64, AND_B32, ANDN1_B32; aliased source/destination, saved/result words and SCC |
| Guest/host widths | Guest32 workgroups 1/4/32, guest64 workgroups 1/4/32/48/64; required host32 and host64 |
| Inactive lanes | Restored VGPR values; untouched store sentinels; lane-zero read while inactive; first active lane 5/40; empty-EXEC lane-zero read; scalar progress with EXEC=0 |
| Wave32 high words | Nonzero EXEC_HI/VCC_HI survive compare and CMPX |
| Existing regression selector | 25 cases including cross-half lane/LDS exchange, partial multidimensional wave64, carry, irreducible dispatcher, SAVEEXEC, WQM, mask provenance, relative move, scratch and shared return |

There are **19 new cases per host width**, plus the **25 existing cases per
width**. A guest wave64 on host32 uses the existing paired-values model, not
two independently executing host subgroups. Required subgroup32 is a test
configuration on this Radeon, not evidence of another GPU vendor's support.

```powershell
. .\scripts\phase0\windows-env.ps1
cmake --preset phase0-windows
cmake --build --preset phase0-windows
.\scripts\phase3\windows-validation.ps1 -EvidenceDir _Build/evidence/phase4a-new -Tests 'phase4a_|phase0_wave_mask|phase3_|phase0_eop_visibility|shader_cfg|scalar_provenance|resource_tracking|resource_materialization|command_scheduler_timeline|stream_buffer_ring|gpu_command_lane|pm4_context_state|buffer_cache_dirty_gc'
python scripts/phase0/run.py --build-dir _Build/phase0-windows --output-dir _Build/evidence/phase4a-baseline-new --gpu required
python -m unittest discover -s scripts/phase0 -p test_runner.py -v
```

The full inventory includes all four Phase 4A CTest entries. The resolved
P0-WAVE allowance is removed: recurrence is now an unexpected baseline failure.
Evidence directories are immutable. The validation script preserves device,
layer and test logs; the baseline runner additionally hashes untracked source
files, including the new fixture. Existing loader warnings remain logged.

## Remaining boundary

This checkpoint does not claim complete RDNA2 support, rendering or gameplay.
Dynamic descriptor waterfalls, incompatible image candidates, typed buffer
packing/conversion, broader arithmetic/memory instructions, subvector loops,
and exhaustive barrier/atomic/reconvergence tests remain later Phase 4 work.
Multiple guest waves sharing a larger host subgroup and graphics-stage helper
invocations require separate coverage; this matrix alone does not prove them.
Raw EXEC alias reads in the descriptor/indirect-selector `ReadScalarCode` path
still deserve an isolated fixture; this checkpoint exercises decoded operands.
Existing D24 resource-format failures and the unavailable attachment-feedback
capability remain explicit limitations. Other platforms/hardware stay deferred.

## Validation record

- Full emulator and test aggregate build succeeded with the pinned Phase 0
  toolchain. Build logs: `_Build/phase4a-build*.log`. Existing compiler warnings
  remain visible; no upstream source merge or new runtime dependency occurred.
- `_Build/evidence/phase4a-final`: **19/19 CTest entries passed**, 15.02 seconds,
  with Khronos validation and synchronization validation. This includes the
  new 19-case matrix and 25 existing wave cases at each host width, original
  P0-WAVE, Phase 3's 10,000 completion lifecycles/ownership/flip/packet gates,
  CFG/provenance/resource tests and scheduler/cache regressions. GPU harnesses
  report **zero Vulkan API validation errors**. Loader/overlay warnings are
  preserved and are not represented as API errors.
- Actual device: **AMD Radeon RX 7800 XT**, `1002:747e`, proprietary driver
  **26.8.1 (LLPC)**, Vulkan **1.4.349**; validation layer **1.4.357**. The new
  matrix explicitly requests subgroup64 and subgroup32; the existing
  regression selector also passes with the physical default of 64 and forced32.
- `_Build/evidence/phase4a-full`: **61 tests**, **54 passed**, **6 matched known
  D24 failures**, **1 explicitly unavailable attachment-feedback-loop dynamic
  state capability**, **zero unexpected failures**. Phase 1/2/3 gates passed.
  The unavailable aggregate does not imply all its later cases ran; independent
  Phase 4A selectors ensure the wave cases execute regardless of that capability.
- All **10 baseline-runner self-tests passed**; log:
  `_Build/phase4a-runner-tests.log`. `git diff --check` passed.

Development evidence is retained separately: `pre-wave.log` reproduces the
old SAVEEXEC mismatch; `phase4a-dev-1` records the obsolete structural assertion
failure; `phase4a-dev-2` passes all six focused entries after its correction
and addition of explicit subgroup widths. These do not replace final evidence.
There is no measured commercial boot, rendering, gameplay or performance result.
Work stops here before Phase 4B.
