# Phase 3: submission, completion and visibility

Target: Windows 11, Ryzen 7 7800X3D, Radeon RX 7800 XT, 32 GB RAM.
Status: complete for this target's Phase 3 conformance gate, 2026-09-10.
Other hosts remain excluded under `platform-scope.md`. No commercial game,
firmware, SDK payload, asset or key is an input to these changes or tests.

## Contract and implementation

Previously, parsing EOP/release/flip packets wrote guest labels immediately,
even when their Vulkan submission was blocked. Those writes now retire through
the command scheduler's existing ordered priority callback queue. Interrupts
follow data publication and labels; ordinary deferred resource reclamation
follows the priority callbacks. Existing submission IDs, timeline ticks and
per-queue resumable PM4 cursors remain the source of ordering. This phase does
not replace the scheduler or introduce a second serial-number system.

Writeback releases snapshot the cache's exact GPU-dirty byte ranges into
dedicated Vulkan download buffers. Transfer barriers, timeline completion and
host invalidation precede copying to guest backing. Completion destinations
already cached on the GPU also receive a device-side update, preventing a
later cache download from restoring an old label. Callbacks use the backing
store directly, avoiding a page-fault callback that could deadlock against the
GPU thread draining completions. Mapping ownership is captured when recording;
a vanished guest mapping cannot silently fall back to a native pointer.

Staging has a 64 MiB pending budget and copies in at most 4 MiB pieces. Budget
pressure waits for the current timeline boundary, not device/queue idle.
Dirty tracking remains conservative until the GPU-thread acquire/invalidation
path consumes it. Consequently repeated writeback releases can download the
same bytes again. Optimize only after profiling; preserving the ownership
boundary matters more than reducing traffic here.

Wait operands, conditional-IB comparisons, predicates and indirect draw,
dispatch and count arguments now use explicit command-memory acquisition.
GPU-dirty cache bytes are synchronized before CPU command interpretation,
independent of whether page protection would have caused a fault.

Timestamp values use the existing 100 MHz host reference-clock domain, sampled
after the represented GPU work retires. These are host retirement timestamps,
not exact emulation of the console's GPU clock. Native/uncached destinations
remain asynchronous. Cached destinations conservatively drain the preceding
timeline boundary, sample once and update both device and backing copies via
the normal completion path. This cost is specific to cached timestamps.

CPU and GPU flip paths use the same deferred completion boundary. A flip's
label is published before its ready callback. Surface acquisition, present
timing and visible display correctness remain later presentation work.

The PM4 dispatcher validates packet type and declared size before handler
payload reads. Existing supported handlers retain their exact shape checks;
indirect recursion is capped at 64 active buffers. Unsupported packet semantics
continue to report an error. This is not a claim that every unknown packet is
supported or that arbitrary native guest pointers are sandboxed.

An unsatisfied WAIT_REG_MEM retains its continuation. A legitimate producer
can resume it, including across graphics/compute queues and nested buffers.
Shutdown explicitly cancels pending guest submissions, counts cancellations,
drains host commands and recorded GPU priority completions, and does not
execute the canceled suffix or invent its label. Normal `Done()` still drains
work; it does not reinterpret an unsatisfied wait as success.

## Acceptance fixtures

| Fixture | What it establishes |
| --- | --- |
| `phase0_eop_visibility` | Original no-fault destination reproducer: parsing and blocked submission cannot publish an EOP label |
| `phase3_completion` | 10,000 seed `0x505833` real-device lifecycles; GPU output, reused 32/64-bit labels, interrupt/no-interrupt, writeback variants, retirement clocks; controlled timeline dependency before parsing |
| `phase3_ownership` | Protected guest backing, cached 32/64-bit labels, data-before-label/event, repeated readback, GPU-produced indirect dimensions/draw arguments/count and predicates, cached clock, 64 MiB staging pressure, nested cross-queue resume, masked 64-bit wait, no-producer cancellation |
| `phase3_flip_boundary` | Actual shared CPU/GPU flip publication helper behind a blocked timeline; label precedes ready callback; no WSI surface |
| `phase3_packet_bounds` | Guard-page truncated EOP, release and 64-bit wait streams must reject with the structured packet diagnostic; access violations do not pass |
| `gpu_command_lane` | Existing CE/DCB waits, nested continuations, host FIFO, GDS, release variants and unmap/drain integration |
| `command_scheduler_timeline`, `stream_buffer_ring` | Timeline retirement, priority/normal callback ordering and reuse/wrap |
| `pm4_context_state`, `buffer_cache_dirty_gc` | Parser state, exact dirty ranges, lifecycle and cache reuse regression coverage |

Indirect acquisition fixtures produce arguments with real Vulkan transfers and
observe the renderer's consumed dimensions/count boundary. They deliberately
do not claim guest shader execution or rendered output; Phase 4 and Phase 5
retain those responsibilities. Commercial runtime compatibility, actual
Demon's Souls progress and visible flip timing are not measured by this gate.

## Reproduction

From the repository root in PowerShell:

```powershell
. .\scripts\phase0\windows-env.ps1
cmake --preset phase0-windows
cmake --build --preset phase0-windows
.\scripts\phase3\windows-validation.ps1 -EvidenceDir _Build/evidence/phase3-new-run
```

The focused script requires the RX 7800 XT by name, enables
`VK_LAYER_KHRONOS_validation` and synchronization validation
(`VK_LAYER_VALIDATE_SYNC=1`), and preserves CTest XML/logs, device inventory,
revision, tracked diff and layer hash. Existing evidence is never overwritten.
The harness fails on Vulkan API validation errors and logs all validation
messages, including loader/environment warnings. Implicit overlay layers are
disabled using the existing Phase 0 environment script. This machine has a
stale Epic overlay manifest that the loader reports independently of API
validation; its diagnostic is preserved in logs.

The locally extracted official LunarG SDK is **1.4.357.0**, from
[LunarG's SDK distribution](https://sdk.lunarg.com/sdk/download/1.4.357.0/windows/vulkansdk-windows-X64-1.4.357.0.exe).
Archive SHA-256:
`81F474711E9042F4CD22B31B2F7A8870DB2E428B21586FB43DD80150BE97310D`.
The archive lives in `_Build/tools/downloads/`; 7-Zip extracted its Bin payload
to `_Build/tools/vulkan-1.4.357.0/Bin`. No installer or machine-wide layer
registration was used. These third-party binaries are ignored build tools,
not source additions. Preserve their upstream licenses if redistributing them.

For a fresh extraction after downloading and verifying that hash:

```powershell
& 'C:\Program Files\7-Zip\7z.exe' x _Build/tools/downloads/vulkansdk-windows-X64-1.4.357.0.exe '-o_Build/tools/vulkan-1.4.357.0' 'Bin/VkLayer_khronos_validation.dll' 'Bin/VkLayer_khronos_validation.json' 'Bin/vulkaninfoSDK.exe' -y
```

The full baseline runner is `scripts/phase0/run.py --build-dir
_Build/phase0-windows --output-dir <new-directory> --gpu required`. It records
source hashes, tool versions, hardware and exact known-failure signatures.
Its fixtures use a fresh temporary directory inside the evidence directory;
`TEMP`, `TMP` and `TMPDIR` are recorded. The default host temporary directory
caused two path fixtures to fail under this restricted run; both passed using
the workspace directory, without emulator filesystem changes.
Use the same Phase 3 validation environment when investigating synchronization
messages. A baseline gate may pass with explicitly matched Phase 4/5 failures;
that is distinct from all tests passing.

Synchronization design references: [Khronos synchronization examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html)
and the [Vulkan synchronization specification](https://docs.vulkan.org/spec/latest/chapters/synchronization.html).
All new emulator logic and synthetic fixtures are original project work; no
upstream emulator implementation was copied in this phase.

## Remaining boundaries

All guest queues still map to the existing serialized host execution model.
Fine-grained queue overlap and readback reduction are performance work.
Image ownership/format support, shader wave correctness and full WSI behavior
remain in their planned later phases. Linux, NVIDIA, other AMD devices and
other drivers are untested, not inferred passes. Port the Windows guard-page
fixture and repeat the hardware synchronization matrix when that scope resumes.

## Validation record

The full Windows emulator and test aggregate build succeeded with the pinned
Phase 0 toolchain (`cmake --build --preset phase0-windows`). Logs are under
`_Build/phase3-build*.log`. Existing compiler warnings remain visible.

The focused RX 7800 XT suite in `_Build/evidence/phase3-core-2` passed 10/10,
including 10,000 controlled seeded completion lifecycles, with zero Vulkan API
validation errors. The driver reports AMD proprietary **26.8.1 (LLPC)**,
Vulkan API **1.4.349**, device **1002:747e**. The validation layer is **1.4.357**.

The complete baseline in `_Build/evidence/phase3-full-2` ran all **57** tests:
**49 passed**, **6 known D24 format failures**, **1 known wave-mask assertion
crash**, **1 explicitly unavailable attachment-feedback-loop capability**.
It recorded **zero unexpected failures**, real GPU execution and validated
SPIR-V. This is a passing baseline gate, not a claim that all emulator tests
pass. Phase 1/2 host regressions passed. The resolved `P0-EOP` failure allowance
was removed; recurrence now fails the baseline. All 10 baseline-runner
self-tests also passed.

Earlier evidence is preserved, including `phase3-core-1` (a development error
in address classification, subsequently fixed) and `phase3-full-1` (the two
temporary-directory fixture failures). These are not acceptance passes.

The final focused rerun in `_Build/evidence/phase3-final` adds protected 32-bit
release and staging-budget coverage and passed **10/10** in **6.06 seconds**, with
zero Vulkan API validation errors. It ran the same 10,000-iteration stress
gate and forced 1,025 consecutive 64 KiB writeback snapshots through the
64 MiB pending-staging limit. The final build log is
`_Build/phase3-build-final.log`.

Phase 4 has not been started. No game-runtime or gameplay result is claimed.
