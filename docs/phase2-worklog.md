# Phase 2 implementation ledger

Status: complete for the Windows-native Phase 2 gate. The target host is
Windows 11 with Ryzen 7 7800X3D, Radeon RX 7800 XT and 32 GB RAM. No commercial
inputs were used; cross-host work remains deferred.

Phase 2 established safe kernel object lifetimes, deterministic waits and
truthful asynchronous boundaries before GPU work. Semaphores and event flags
now use monotonically allocated shared handles, pin operations across delete or
cancel, reject stale identities, validate modes and counts, and wake waiters
with defined results. A shared monotonic deadline helper drives semaphore,
event-flag, address-wait and event-queue waits, including exact fake-clock
tests. Event-queue callbacks run outside queue locks and may re-enter safely.

The Windows socket fixture now covers descriptor ownership, PEEK/WAITALL
behavior, nonblocking empty reads and EOF short reads. AJM batch initialize,
start, wait and cancel validate inputs and maintain host-side batch state rather
than returning success for arbitrary IDs.

Final evidence from the configured Windows build:

* `phase2_kernel` passed exact timeout, wake-before-wait, cancellation,
  deletion, stale-handle, overflow, event-mask and re-entrant queue contracts.
* `phase2_kernel_stress` passed 10,000 seeded real-thread semaphore/event
  lifecycles; every worker joined with no lost wakeups or invalid handle reuse.
* The Phase 2 regression selection (kernel, stress, queue lifetime, filesystem,
  address waits and AudioOut2) passed 6/6.
* The combined Phase 1, Phase 2, memory and audio selection passed 18/18.

Complete AJM asynchronous decode execution, guest scheduler priority ordering,
production audio-device timing and GPU submission/completion semantics remain
explicit later work. They need runtime traces for meaningful validation and are
outside the Phase 2 gate; Phase 3 has not been started.
