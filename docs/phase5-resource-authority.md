# Phase 5 — Resource authority and stable rendering

Phase 5 establishes the resource ownership and presentation contracts needed by
the shader and queue work completed in Phases 1–4. The supported validation
machine for this checkpoint is Windows 11, Ryzen 7 7800X3D, Radeon RX 7800 XT,
32 GB DDR5-6000. No firmware, game, or other commercial input was used.

## Resource authority

ProsperoX keeps guest backing memory, host images, and cached buffers as separate
representations. GPU writes mark the host representation and publish page-level
read ownership. A CPU fault or explicit invalidation enters
`GpuResourceManager::SynchronizeCpuImages`, which asks `TextureCache` to read
back only the required linear storage-image subresource and publishes the result
before the CPU access continues. Page watchers retain disjoint dirty ranges, so
partial writes do not overwrite unrelated bytes or padding. The current demand
readback scope is deliberately conservative: one-subresource, non-tiled,
non-depth, non-block, non-metadata, non-stencil, non-volume storage images.

Buffer uploads use the same protected guest-memory path and avoid re-entrant GPU
upload reads. Bindings are prepared in three passes: resolve streams, publish
buffer owners, then finalize image and descriptor state. A commit is rejected
unless publication completed, preventing stale bindings after replacement.

DCC and HTile pending state is represented as a range set. Capacity is derived
from the PM4 slice field instead of a 32-bit mask, and render-target views consume
only the affected layers. Stencil association preserves initialized data when a
depth image changes its stencil address; unsupported MSAA transfer cases remain
explicit rather than silently claiming initialized contents. Null image backing
is keyed by format, image type, and sample count so an incompatible fallback
shape cannot be reused.

## Presentation authority

Swapchain acquire semaphores are consumed once per frame and tracked by acquire
serial. When `VK_EXT_swapchain_maintenance1` is available, per-image present
fences are attached to `vkQueuePresentKHR` and waited before destruction. The
presenter drops minimized-window frames, consumes suboptimal acquires, and
recreates only on an actual out-of-date result. This avoids semaphore reuse and
resize loops while keeping destruction safe.

The design follows the Vulkan synchronization guidance on per-image semaphore
reuse and maintenance1 present fences:

* <https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html>
* <https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainPresentFenceInfoKHR.html>
* <https://docs.vulkan.org/guide/latest/synchronization.html>

## Provenance and decisions

The following upstream work was studied and adapted as behavior, with no
firmware, proprietary assets, or copyrighted game data copied:

| Source | Use in ProsperoX |
| --- | --- |
| Kyty PR #532 | Three-pass buffer binding publication and stale-owner prevention; adapted to the existing cache interfaces. |
| Kyty PR #537 | CPU demand readback, page read watchers, and non-reentrant upload reads; narrowed to the validated linear storage-image scope. |
| Kyty PR #543 | Range-based DCC pending slices; generalized to PM4-derived DCC/HTile capacity. |
| Kyty PR #545 | Stencil preservation and clear/GC ordering; unsupported transfers remain explicit. |
| Kyty PR #500 and #477 | Depth, tiler, and sampled-view behavior used as differential references; heuristic format substitution was not imported. |
| SharpEmu and arielPS5 | Behavioral and test references only; no wholesale transplant. |

AMD's public RDNA2 ISA remains the shader contract reference:
<https://www.amd.com/content/dam/amd/en/documents/radeon-tech-docs/instruction-set-architectures/rdna2-shader-instruction-set-architecture.pdf>.

## Validation

The focused real-device gate passed all 13 selected tests with validation
enabled and zero Vulkan validation errors. It covers page-manager behavior,
layered/view/format/depth cache cases, CPU image readback, private buffer upload,
DCC slice ranges, metadata classification, image overlap, HTile clear, and
buffer ranges. The presenter test identified the device as AMD Radeon RX 7800 XT
(vendor `1002`, device `747e`, driver `8389003`). A ten-second CTest presentation
run completed 859 frames with 1,317,171,216 live VMA bytes and 13 live
allocations, unchanged from warm-up. A separate direct run remained stable for
90 seconds (7,681 frames) with the same live allocation totals before being
stopped at the user's request; the exact 3,600-second soak is intentionally
deferred and is the only remaining Phase 5 validation action.

No commercial runtime files were accessed. Phase 6 has not started.

The all-68-test CTest sweep was also executed. Its process-order run was not a
valid clean gate: legacy host probes intermittently failed while reserving the
13,824 MiB guest direct-memory window, and a small number of older filesystem
and path probes were order-sensitive. The same binaries pass when rerun alone,
and the complete Phase 5-focused 13-test gate above remained 13/13. This host
commit-limit/flakiness issue is recorded for follow-up rather than counted as a
Phase 5 resource or Vulkan failure.
