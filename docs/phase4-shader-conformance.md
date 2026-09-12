# Phase 4: shader semantics and dynamic sampled resources

Target: Windows 11, Ryzen 7 7800X3D, Radeon RX 7800 XT, 32 GB RAM.
Starting checkpoint: `0754eba` (Phase 4A). Investigation: 10–11 September 2026.
This checkpoint includes Phase 4A; it does not start Phase 5.
No commercial files were accessed or added. The acceptance inputs are synthetic.

Status: **Phase 4 complete for the selected contracts on the target PC,
11 September 2026. Stop before Phase 5.**

## Decisions and implementation

Retain the decoder, CFG/structurizer, typed IR/SSA, resource plan, SPIR-V backend
and paired-wave64 execution. The expanded corpus found specific correctness
defects; a replacement compiler or a bulk upstream merge was not justified.

- **Heterogeneous sampled tables:** each candidate is sampled using its own
  numeric class, dimension, address layout, conversion and sampler requirement.
  Branches merge the resulting four guest register words, rather than coercing
  all candidates to the first image's SPIR-V type. A native/point-filter mixture
  retains separate sampler variants. Storage/gather shapes not supported by the
  existing indirect planner remain explicit unsupported cases.
- **Runtime versus specialization:** addresses, image views and the sorted key
  mapping remain dispatch data. The type/topology remains part of specialization.
  Changed addresses do not create a new module; changed types can. The existing
  read-first-lane/material-buffer/descriptor-heap provenance proof and guest CFG
  remain intact. No rewrite of a waterfall loop's mask, termination or side
  effects was imported.
- **Capacity:** remove the 32-image ceiling. Production materialization receives
  the lesser of the device's per-stage and descriptor-set sampled-image limits.
  A separate 65,536-entry compiler work bound prevents unbounded enumeration.
  The standalone materializer defaults to 128 images; it is not a claim about
  another host's capabilities. The 40-candidate reproducer exceeds the old cap.
- **Unsupported mutable data:** material/heap tables overlapping writable buffers
  are rejected transactionally. Unbounded DMA/image aliases and swizzled writable
  buffers are conservatively unsupported with indirect tables. A non-null invalid
  indirect descriptor is rejected instead of silently becoming a null image.
  Architectural scalar-buffer OOB reads and null descriptors retain their existing
  specified zero/null behavior. Table contents may change between dispatches.
- **Subvector loops:** decode, classify, translate and branch on
  `S_SUBVECTOR_LOOP_BEGIN/END`, preserving raw EXEC words, destination alias order
  and SCC. Both the public decoder and CFG-local branch classifier are updated.
  The latter omission was exposed by actual GPU output during development.
- **Guest wave32 on physical64:** lane IDs and ADDTID use logical lanes 0–31;
  ballots select the correct physical 32-lane word; shuffle and DS/append-consume
  source lanes remain in the corresponding physical half. Existing paired
  guest64-on-host32 handling remains intact.
- **Binary16 rounding:** use explicit nearest-even conversion for the selected
  F32-to-F16 and packed-half arithmetic paths. An existing numeric-inline-F16
  case returned `bbbb3117` instead of `bbbb3118` before this fix. Structural tests
  no longer demand `PackHalf2x16` where its replacement is intentional.

This is correctness work. There is no new game-specific path, performance claim,
shader cache format migration or Phase 5 resource-cache redesign.

The first full validation run also exposed an existing harness setup defect:
its runtime context never queried block-texel-view support, unlike production.
The harness now queries the BC3 image flags before reporting that capability.
This fixes the raw UINT view validation error in the stencil-binding fixture
without changing production image/cache code or hiding a validation message.

## Source refresh and provenance

API/patch snapshots are retained locally in `_Build/phase4-research/` (ignored).
The earlier source-level comparisons are in [Phase 4A](phase4a-wave-contracts.md).
The following references were refreshed and selected patches read before adapting
them. Preserve the repository GPL-2.0 license and all original notices.

| Reference | Revision / author | Decision |
| --- | --- | --- |
| [KytyPS5 main](https://github.com/KytyPS5/KytyPS5) | `2e315a3c62bf036c8225d5057ada1d70cd8063f1` | Refreshed recent commits, branch inventory, issues and open PRs. Recent AJM, directory-stream and texture-upload changes do not justify changing completed runtime phases here. |
| [Kyty PR 383](https://github.com/KytyPS5/KytyPS5/pull/383) | `c51c4dc23fa5eaa0446c20b694ce28c98b040586`, Techx3 | **GPL-compatible adaptation:** per-candidate typed sampling and mixed sampler planning, adjusted to this checkout's numeric-class IR. New 40-image, runtime-remapping and capability tests are original ProsperoX fixtures. |
| [Kyty PR 362](https://github.com/KytyPS5/KytyPS5/pull/362) | `2f8776e54bf94ecadcd72efd980f66ab72a8bacb`, Techx3 | **GPL-compatible adaptation:** deterministic F16 RNE helper and ALU wiring; adapted synthetic tie fixture. The ordered-representable-value oracle is original and independent of the emitter's bit algorithm. |
| [Kyty subvector commit](https://github.com/KytyPS5/KytyPS5/commit/c913951f7b13fe179d94dd187dfbd2097654256e) | `c913951f7b13fe179d94dd187dfbd2097654256e`, nmzik | **GPL-compatible adaptation:** decoder/CFG/translator behavior and synthetic mask fixtures. Captured binary shader data from the upstream test patch was not imported. Adapted to preserve Phase 4A's scalar branch contract. |
| [Kyty PR 500](https://github.com/KytyPS5/KytyPS5/pull/500) | `f69e86d66f4bb244ae4246a73ffcd34f23bfbe94`, TarkusR; updated since Phase 4A | Read the waterfall pass and relevant constituent changes. Its recognizer rewrites selected mask loops; not a proof for arbitrary loops with other side effects. Behavioral/architecture reference only; no bulk import. |
| [Kyty PR 458](https://github.com/KytyPS5/KytyPS5/pull/458) | `df7c3ac625d2cedbd88cfc1ebc812be2e4bf9119` | Additional wave32 SAVEEXEC variants remain a later opcode-coverage lead; retain the completed Phase 4A variants and tests. |
| [SharpEmu](https://github.com/sharpemu/sharpemu) | `ca90d175412206173f7640d44493263d9b913af7`, unchanged | Arithmetic/wave conformance ideas; no copied implementation. A second emulator is not the architectural oracle. |
| [Prosper](https://github.com/mattias800/prosper) | `d1f1aae5dd04c5146721c1e62a067f5a468a0f75` | Latest change concerns header-inspection tooling. Useful inspection ideas; no license granting source reuse, so no code or tests copied. |
| [shadPS4](https://github.com/shadps4-emu/shadPS4) | `7a8caf12b60133a3303acef31b055c1d0ebe08c5`, unchanged | Lane-exchange/IR reference; GCN behavior is not automatically an RDNA2 oracle. No port. |
| [RPCSX](https://github.com/RPCSX/rpcsx) | `e8ae1481ab7ba04d5c6bef89dd852aabba2c88ff`, unchanged | Raw scalar-mask representation remains useful architecture evidence. No port. |

Primary semantic references: AMD's public
[RDNA2 ISA](https://www.amd.com/content/dam/amd/en/documents/radeon-tech-docs/instruction-set-architectures/rdna2-shader-instruction-set-architecture.pdf),
especially §12.2 subvector execution and the scalar/vector instruction definitions;
Khronos [descriptor indexing](https://docs.vulkan.org/guide/latest/extensions/VK_EXT_descriptor_indexing.html),
[GLSL SPIR-V extended instructions](https://registry.khronos.org/SPIR-V/specs/unified1/GLSL.std.450.html),
and [subgroup size control](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineShaderStageRequiredSubgroupSizeCreateInfo.html).
Statically indexed, individually typed branch operations avoid requiring a
heterogeneous Vulkan descriptor array. Arbitrary nonuniform implicit derivatives
are not established by the compute fixture; the accepted production table proof
uses a scalar read-first-lane selector. Preserve that restriction.

## Acceptance coverage

New fixtures live in `tests/Phase4ShaderTests.inc`; existing semantic fixtures
are registered together under new Phase 4 CTest selectors so an unrelated early
device-capability test cannot prevent the shader corpus from running.

| Gate | Oracle / scope |
| --- | --- |
| `phase4_shader_corpus` | Every selected compute fixture, then 13 graphics fixtures; required compute subgroup64. Decode, translate, SPIR-V validation and exact readback, except five explicitly marked compile-only fixtures. |
| `phase4_shader_corpus_32` | Same compute corpus at required physical subgroup32, including paired guest64 execution. Graphics is not relabeled as subgroup32. |
| `phase4_indirect_images` | 40 distinct 1D/2D, float/uint images, one/two mip levels, explicit LOD 1, different keys per lane. Two dispatches reverse runtime mapping without recompiling; exact 64-word readback each time. |
| Resource tracking/materialization | Actual provenance planning and CPU specialization; >32 candidates; limit boundary 40/39; changed addresses; mixed native/point samplers; unreadable memory; null descriptors; wrapped/OOB scalar reads; conditional tables; malformed graphs and writable overlap rejection. |
| New subvector fixtures | Eight mask/body/alias cases at each guest width; scalar iteration counts, raw saved/final words, SCC, and restored VGPR contents. |
| New wave32 multiwave fixture | Four guest32 waves in one 128-invocation workgroup: lane reads, first-active-lane reads, low/high ballots, LDS write/barrier/read from the other half of the workgroup. |
| New F16 oracle | 66 values covering signed zero, overflow/infinity and values immediately below/at/above ten ties including subnormal/normal boundaries. Nearest representable value is found numerically, independently of backend bit shifts. |
| Retained Phase 4A matrix | 19 contract cases and 25 regressions at each required host width: partial waves, inactive sentinels, EXEC/VCC/SCC, restored lanes, irreducible CFG, cross-half LDS, WQM and scratch. |

The existing corpus additionally exercises integer/typed-buffer packing and bounds,
FMA, NaN/signed-zero/denormal cases, LDS/GDS/buffer/image atomics, loops, interpolation,
WQM ancillary reads, multisample inputs and fragment export/discard behavior.
The graphics tests exercise raster/helper-related behavior but do not constitute
exhaustive fragment-helper conformance for arbitrary shaders.

Coverage output separates declared opcode labels from executed variants.
`[compile]` lines mean decode/translation/SPIR-V validation **without execution**;
`[compute]` and `[graphics]` success lines follow GPU readback. The final corpus
line gives both counts. Pending opcode labels remain printed and are not called
supported merely because a decoder entry exists.

## Reproduction and result

```powershell
. .\scripts\phase0\windows-env.ps1
cmake --preset phase0-windows
cmake --build --preset phase0-windows --parallel 6
.\scripts\phase3\windows-validation.ps1 -EvidenceDir _Build/evidence/phase4-new -Tests 'phase4|phase3_|phase0_wave_mask|phase0_eop_visibility|shader_cfg|scalar_provenance|resource_tracking|resource_materialization|command_scheduler_timeline|stream_buffer_ring|gpu_command_lane|pm4_context_state|buffer_cache_dirty_gc|compute_meta_clear_classification'
python scripts/phase0/run.py --build-dir _Build/phase0-windows --output-dir _Build/evidence/phase4-baseline-new --gpu required
python -m unittest discover -s scripts/phase0 -p test_runner.py -v
```

Final results:

- Full Windows build, including `kyty_emulator`: **passed**. Logs:
  `_Build/phase4-build-full.log` and `_Build/phase4-build-final-2.log`.
- Focused gate: **23/23 passed**, including the corrected BC3 harness fixture.
  `_Build/evidence/phase4-final-2-20260911/ctest.xml` and `ctest.log` contain the
  commands, readbacks and validation summaries. **Zero Vulkan/synchronization
  validation errors.** Actual device `1002:747e`, AMD driver 26.8.1 (LLPC), Vulkan
  1.4.349, pinned validation SDK 1.4.357.0; device/layer details are in that folder.
- Each host compute width: **294 decoded/translated/SPIR-V-validated cases;
  289 executed and five compile-only**. Additionally, **13 graphics cases** and
  the **40-image/two-dispatch** reproducer passed. These overlap other selectors;
  do not sum them as distinct opcode coverage.
- Full baseline **with validation and synchronization validation enabled**:
  **57 passes, six known Phase 5 D24 failures, one explicitly unavailable
  attachment-feedback capability; zero unexpected failures**. See
  `_Build/evidence/phase4-baseline-2-20260911/summary.json`, per-test logs,
  `results.json` and source/toolchain hashes in `manifest.json`.
  `all_correctness_tests_passed` deliberately remains **false**.
- Baseline-runner unit tests: **10/10 passed**, `_Build/phase4-runner-tests.log`.

The first broader validation run is retained at
`_Build/evidence/phase4-baseline-20260911/`; it exposed the harness's missing
block-view capability query. No failure allowance was added. The final run
proves that correction with validation enabled. Earlier Phase 4A full-baseline
logs used validation disabled; its focused validation gate was enabled.
Final documentation and the SrtRuntime budget comment were completed after
execution; neither changes the tested executable behavior.

## Boundaries retained for subsequent work

This checkpoint proves the selected semantic corpus on this PC, not all RDNA2
opcodes, all float-control modes, arbitrary descriptor graphs or gameplay.
The old direct buffer/sampler limits, shader compilation cost for very large
typed switches, and conservative rejection of possible same-dispatch descriptor
aliases remain visible constraints. Relax those only with a new minimized oracle
and real-device test. In particular, never replace the read-only table proof with
an unchecked CPU snapshot or assume that any waterfall loop is safely removable.

Phase 5 owns the existing D24/depth and resource-authority failures, aliases,
tiling/compression and stable-rendering work. No texture-cache workaround was added
to make those tests green. Broader hardware/platform support remains deferred.
Commercial runtime investigation is not necessary for these acceptance results;
lawful game files will be useful later to extract new minimized integration cases.
There is no claim of Demon's Souls gameplay or improved game compatibility here.
