# ProsperoX Event-Driven PM4 Synchronization Design

## Goal

Replace ProsperoX's blind timed retry loop for GPU memory waits with a ProsperoX-native, timeline-aware producer/consumer synchronization path, while preserving a compatibility fallback for unknown producers and reducing redundant GPU submissions only where ordering is proven.

The design is generic. It contains no title IDs, game addresses, shader hashes, or Bendy-specific behavior.

## Current ProsperoX behavior

ProsperoX already has strong GPU retirement primitives:

- `Sync::RecordEndOfPipeWrite` records completion values into the buffer cache and queues a `CommandScheduler::DeferPriorityOperation`.
- Priority operations are tagged with the scheduler's current timeline tick.
- `CommandScheduler::PriorityOperationsThread` waits for that Vulkan timeline tick before running the callback.
- Only then does `PublishCompletion` write the completion label into guest backing memory and optionally raise an interrupt.

The weak side is the consumer scheduler:

- `CommandProcessor::WaitRegMem` reads guest command memory and calls `SuspendPm4()` if the condition is false.
- `Pm4Execution` records only `m_suspended`; it does not retain the wait address or condition.
- `GuestGpu::ThreadRun` places the submission back at the head of its queue with `blocked=true`.
- When all queue fronts are blocked, the GPU worker calls `m_work_available.WaitFor(..., 100)` and then blindly clears `blocked` on every queue front.
- Completion of any submission also clears `blocked` on every queue front.
- `GuestGpu::Process` flushes the host command buffer whenever a submission slice made progress, including a slice that later blocked.

This preserves forward progress, but it turns guest synchronization into host polling and creates unnecessary PM4 retries, queue wakeups, command-buffer flushes, and Vulkan submissions.

## Design principles

1. Preserve guest ordering. A label cannot wake a waiter before the producer's host GPU work reaches the correct timeline point.
2. Preserve transient satisfaction. If a producer writes a satisfying value and the label is subsequently reset, a waiter that was logically satisfied must not miss the event.
3. Do not require every possible guest-memory producer to be known on day one. Unknown producers retain a bounded legacy polling fallback.
4. Do not force waits to succeed and do not bypass buffer-cache dirty ownership.
5. Keep the existing resumable PM4 cursor model. The fix extends `Pm4Execution`; it does not replace the command processor.
6. Reduce submissions only at proven-safe boundaries. No global "skip flush" optimization.
7. Keep diagnostics opt-in under `PROSPEROX_PERF_TRACE`.

## Architecture

### 1. Retained wait state in `Pm4Execution`

Add a single active memory-wait record to `Pm4Execution`. A PM4 execution can only be suspended at its current packet, so one active wait per execution is sufficient.

```cpp
enum class Pm4WaitMode : uint8_t {
    None,
    LegacyPoll,
    EventDriven,
};

struct Pm4MemoryWait {
    uint64_t address = 0;
    uint64_t reference = 0;
    uint64_t mask = 0;
    uint64_t last_observed = 0;
    uint32_t width = 0;
    uint32_t function = 0;
    uint32_t poll = 0;
    uint32_t wait_op = 0;
    uint64_t first_block_tick = 0;
    uint64_t retry_count = 0;
    Pm4WaitMode mode = Pm4WaitMode::None;
    bool latched = false;
};
```

`Pm4Execution` exposes only narrow `CommandProcessor`/`GuestGpu` friend access. No renderer code receives direct access to PM4 cursor internals.

### 2. Pending GPU label producers live in `GuestGpu`

Add a small thread-safe registry owned by `GuestGpu` for GPU completion labels that ProsperoX already knows will be published at a future scheduler timeline point.

Each pending producer contains:

```cpp
struct PendingLabelProducer {
    uint64_t id;
    uint64_t address;
    uint64_t value;
    uint64_t scheduler_tick;
    uint32_t width;
};
```

The registry supports:

```cpp
uint64_t RegisterPendingLabel(uint64_t address, uint64_t value,
                              uint32_t width, uint64_t scheduler_tick);
void CompletePendingLabel(uint64_t producer_id, uint64_t address,
                          uint64_t value, uint32_t width);
bool HasMatchingPendingLabel(uint64_t address, uint32_t width,
                             uint64_t reference, uint64_t mask,
                             uint32_t function) const;
```

Producer IDs prevent reuse of one guest label address from removing a later producer accidentally.

The registry is protected by `GuestGpu::m_queue_mutex`; producer completion callbacks may run on the scheduler priority thread while the command processor runs on the GPU thread.

### 3. EOP/RELEASE producers use ProsperoX's existing timeline retirement

`Sync::RecordEndOfPipeWrite` remains the source of truth for GPU-completion label ordering.

Before enqueuing the deferred priority callback it registers the pending label with `GuestGpu`, using the scheduler's current tick. The deferred callback becomes:

```text
wait for timeline tick (existing scheduler behavior)
 -> PublishCompletion to guest backing
 -> GuestGpu::CompletePendingLabel(...)
 -> trigger interrupt when requested
```

The event-driven wake therefore cannot occur before host GPU retirement.

`CompleteFlipAtEndOfPipe` follows the same rule for flip labels.

### 4. WAIT_REG_MEM first consumes a latched completion

When the same suspended PM4 packet resumes, `CommandProcessor::WaitRegMem` first compares its packet parameters with the retained `Pm4MemoryWait`.

If the retained wait is `latched`, it clears the wait record and returns without rereading memory. This preserves transient producer values.

If there is no latch, the normal memory condition remains authoritative.

### 5. Known pending producer avoids synchronous command-memory readback

Before forcing `ReadCommandMemory`, `WaitRegMem` asks `GuestGpu` whether a known pending GPU producer for the same address/width can satisfy the condition.

If a matching producer exists:

- retain the wait as `EventDriven`;
- suspend the PM4 execution immediately;
- do not call `ReadCommandMemory` merely to force that pending GPU work to retire;
- let `GuestGpu::Process` flush the progress already recorded in the current slice;
- resume only when the matching producer's timeline callback publishes and latches the value.

This is conservative: it can delay a wait until a known satisfying producer retires, but it cannot run the guest ahead of that producer.

If there is no known matching producer, ProsperoX performs the current `ReadCommandMemory` test. If still unsatisfied, the wait becomes `LegacyPoll`.

### 6. Producer completion latches and wakes only matching queue fronts

`GuestGpu::CompletePendingLabel` removes the exact pending producer ID, scans only the current front submission of each guest queue, and examines active memory waits in both graphics executions (`command_execution` and `constant_execution`).

For a wait on the same address and compatible width:

- evaluate `TestWaitRegMemValue(produced_value, reference, mask, function)`;
- when true, set `latched=true` and clear that submission's `blocked` flag;
- signal `m_work_available`.

A producer with a non-satisfying value does not wake the waiter. If it was the last known matching pending producer for an event-driven waiter, that waiter is downgraded to `LegacyPoll` and made runnable so unknown/CPU producers can still be observed.

Queue scanning is bounded by ProsperoX's fixed queue count and happens only on label production, not per PM4 packet.

### 7. WRITE_DATA notifies existing blocked waits without importing another emulator's registry

`CommandProcessor::WriteData` retains its current memory writes. After the write, it calls a ProsperoX `GuestGpu` notification method with the written range.

The notification scans blocked queue fronts. It only decodes values for addresses that an active waiter is actually watching:

- contiguous 32-bit writes support 32-bit waits;
- two contiguous dwords may satisfy a 64-bit wait using little-endian composition;
- one-address writes evaluate the values in packet order only when a waiter watches that exact address.

This avoids an O(number-of-dwords × number-of-queues) hot path for large `WRITE_DATA` packets.

Direct `WRITE_DATA` production is already guest-visible when this notification occurs, so no scheduler timeline deferral is required.

### 8. Unknown producers keep legacy polling

CPU writes, DMA cases not yet modeled as completion labels, and any other unknown producer remain correct through `LegacyPoll`.

The all-blocked scheduler behavior changes from "retry everything" to:

- if at least one blocked queue front is `LegacyPoll`, perform the existing bounded timed wait and make only legacy-poll fronts runnable on timeout;
- if every blocked front is event-driven, wait on `m_work_available` without a periodic retry;
- new submissions and host commands still signal `m_work_available` normally;
- producer completion signals it immediately.

Non-memory PM4 suspensions (`WaitCe`, `WaitDeDiff`, rewind) retain their current behavior in this change and may continue using the bounded retry path.

### 9. Completion of unrelated submissions no longer wakes event-driven memory waits

Today any completed submission clears `blocked` for all queue fronts. The new scheduler only performs this broad retry for blockers that can depend on generic queue progress. Event-driven memory waits remain blocked until their matching producer completes or they are explicitly downgraded to fallback mode.

### 10. Safe RELEASE_MEM batching

Submission reduction is a second layer on top of correct event-driven waits.

ProsperoX currently performs immediate `BufferFlush()` calls from some `RELEASE_MEM` paths. Add a small `PendingReleaseBoundary` state to `CommandProcessor` rather than copying another emulator's packet-batching class.

Only consecutive plain completion-label writes may share one host submission, with a hard cap of eight labels. A pending release batch is flushed before:

- any packet that is not another eligible plain completion-label `RELEASE_MEM`;
- any interrupt-producing release;
- any writeback requirement that needs its own visibility boundary;
- a `WAIT_REG_MEM`/acquire/barrier dependency;
- a queued host command;
- PM4 processing returns to `GuestGpu`;
- command-list completion;
- shutdown.

This keeps deferred callbacks for batched labels on the same timeline tick while ensuring no later unrelated GPU work is silently moved before a required publication boundary.

The batching optimization is enabled only after its focused tests pass. It is independently toggleable during integration so synchronization correctness can be validated without it.

## Diagnostics

Extend the existing `PROSPEROX_PERF_TRACE` path rather than adding unconditional logs.

Per-second counters:

- WAIT_REG_MEM evaluations, immediate passes, first blocks, retries, latched passes, resolved waits;
- event-driven vs legacy waits;
- producer registrations/completions by type;
- producer wakeups, legacy timeout wakeups, generic queue-progress wakeups;
- retries where `last_observed` did not change;
- number of unique blocked wait addresses;
- active/pending producer count;
- release labels batched and flushes avoided.

Top-N (bounded, e.g. 8) wait-address report:

- address and width;
- comparison function/reference/mask;
- retry count;
- total blocked wall time;
- last observed value;
- current mode;
- whether a matching pending producer exists.

`CommandScheduler::Finish` and `CommandProcessor::BufferFlush` receive reason enums for tracing only. Existing behavior is unchanged by the reason parameter.

Target reason classes:

```text
Flush: ProcessBoundary, ReleaseMem, DependencyBoundary, HostCommand, Other
Finish: CommandReadback, BufferReadback, CompletionClock, ReleaseBackpressure,
        ExplicitWait, Shutdown, Other
```

The goal is to make a Bendy trace explain causality without high-volume packet logging.

## Runtime controls

During investigation:

- `PROSPEROX_PM4_EVENT_WAIT=0` restores legacy memory-wait scheduling.
- `PROSPEROX_RELEASE_MEM_BATCH=0` disables release batching independently.
- `PROSPEROX_PERF_TRACE=1` enables diagnostics.

The event-driven path should become the default only after automated regressions and the Bendy A/B pass. The legacy implementation remains available during Phase 6 until cross-title confidence is established.

## Tests

### Pure wait semantics

Add host tests for:

1. 32-bit EQ/NE/LT/LE/GE/GT behavior through retained wait state.
2. 64-bit producer values.
3. Wrong address does not latch.
4. Wrong value does not latch.
5. Masked comparison behaves identically to `TestWaitRegMemValue`.
6. A satisfying producer followed by a reset remains latched.
7. Reused label address with a new producer ID does not remove or satisfy the wrong generation.
8. Multiple queue fronts waiting on one address all wake when their individual conditions match.

### Scheduler behavior

1. Known pending producer classifies wait as event-driven.
2. Event-driven all-blocked state does not perform timeout retries.
3. Unknown producer classifies wait as legacy poll.
4. Legacy polling still resumes when guest memory changes without a registered producer.
5. Completing an unrelated submission does not wake an event-driven wait.
6. Matching producer completion wakes the correct queue immediately.
7. Nonmatching producer completion either leaves a later matching producer in place or safely downgrades to fallback when none remains.
8. Shutdown with blocked event-driven waits does not hang.

### Timeline correctness

GPU-backed regression fixtures verify:

1. EOP label is not guest-visible and does not wake its waiter before the corresponding Vulkan timeline tick retires.
2. It becomes guest-visible and wakes immediately after retirement.
3. Interrupt remains ordered after label publication.
4. Flip completion label follows the same ordering.

### WRITE_DATA

1. 32-bit contiguous label write wakes a matching waiter.
2. 64-bit wait is satisfied by two contiguous dwords.
3. One-address packet handles watched intermediate/final values correctly.
4. Large writes with no watched address do not create per-dword notification work.

### Release batching

1. Up to eight adjacent eligible labels share a submission.
2. Ninth label forces a boundary.
3. Interrupt release forces a boundary.
4. Barrier/acquire/wait forces a boundary.
5. Ineligible packet forces a boundary.
6. Queued host command forces a boundary.
7. PM4 return/completion flushes the batch.
8. Every label is published exactly once and only after the shared timeline tick retires.

## Integration validation

Windows 11 / Ryzen 7 7800X3D / Radeon RX 7800 XT is the Phase 6 validation platform.

Validation order:

1. focused wait-registry/PM4 tests;
2. command scheduler timeline tests;
3. GPU command lane tests;
4. buffer-cache/readback tests;
5. full serial CTest baseline comparison;
6. 60-second Bendy gameplay trace with event wait ON, release batching OFF;
7. same scene with event wait OFF for A/B;
8. event wait ON + release batching ON;
9. inspect FPS, `wait[reg]`, blocked attempts, timeout wakeups, `Scheduler::Finish`, flushes and Vulkan submits;
10. Vulkan validation run for the focused GPU tests.

Success is not defined solely as higher FPS. The new path must first preserve rendering, audio/input progress, label ordering and test behavior. For Bendy, the expected architectural signal is a large reduction in unchanged WAIT_REG_MEM retries, timeout wakeups and blocked process slices. Release batching should then reduce flush/submission density without reintroducing stalls.

## Rollback boundaries

The implementation is intentionally separable:

1. wait-state diagnostics;
2. pending-producer tracking;
3. event-driven wake scheduling;
4. WRITE_DATA producer notification;
5. release batching.

Each layer gets its own commit and tests. If a layer regresses a title, it can be disabled or reverted without discarding the earlier correctness work.

## Explicit non-goals

- no game-specific wait addresses;
- no forced wait success;
- no global dirty-tracking disable;
- no speculative label publication before GPU retirement;
- no removal of the legacy fallback during Phase 6;
- no use of guest `poll` cycles as host microseconds without a proven hardware conversion;
- no rewrite of the entire Vulkan scheduler or PM4 parser.
