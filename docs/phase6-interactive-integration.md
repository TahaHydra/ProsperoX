# Phase 6 — Interactive integration checkpoint

Status: **open**, updated 2026-09-19. Target: Windows 11, Ryzen 7 7800X3D,
RX 7800 XT, 32 GB DDR5. No Phase 7 work has started.

Development line: `integration/prosperox-consolidated`. It contains the
Bendy performance work, the Ghost of Yotei loader and import work, the
passive runtime diagnostics, and the documentation recovered from the
`rescue/` and `investigate/` side branches. See
[branch consolidation](#branch-consolidation) below.

## Current state of the primary title

The primary real-title target is **Bendy and the Dark Revival, PPSA27624**.

It boots, plays its intro video, reaches its menus, and reaches **real
controllable Chapter 1 gameplay**, with working rendering, audio, controller
input and save handling. Sustained 60 FPS is reached in warmed areas; the
first exposure to new content still costs frames.

This supersedes the 2026-09-14 statement below that "no menu or gameplay is
claimed" and the note that Bendy stopped at `VAzswvTOCzI` (`unlink`). Both
were accurate when written. The sections after this one are kept as the
historical record of how each area was brought up and what was tested; read
them as history, not as current status.

### Performance

Gameplay was 12–15 FPS when Phase 6 was paused, against ~60 FPS menus, with
the emulator rather than the GPU as the bottleneck. Four pieces of work
addressed that, in order:

1. **Event-driven PM4 waits.** `WAIT_REG_MEM` recorded its predicate and the
   GPU thread parked until something could change it, instead of waking on a
   1 ms condvar floor, re-polling the label and draining the device each
   time. Empty command buffers stopped being submitted.
2. **Completion-label ownership and batching.** A 4-byte `RELEASE_MEM` label
   no longer makes its whole 4 KiB page GPU-owned, so a guest CPU polling
   that label no longer faults into a readback. Plain labels are batched;
   interrupt and flip completions stay prompt.
3. **Same-queue in-stream waits.** When the queue that will write a
   completion value has already recorded it, a following `WAIT_REG_MEM` is
   answered from submission order rather than a CPU/GPU round trip. This is
   the default; `KYTY_GPU_INSTREAM_WAITS=0` opts out.
4. **Scalable pending-completion lookup**, once the pending set reached frame
   scale, with retired completions unable to satisfy a newer wait.

Measured with `KYTY_GPU_STATS=1`, a warmed 60 Hz session shows `submit=60`,
`process=60`, `blocked=0`, `parked_ms=0`, `flipwait_ms=0`, `readfault≈0` and
no shader or pipeline creation in steady state. Preserve these contracts;
they have regression tests.

Two measurement traps, both real and both previously mistaken for emulator
behaviour:

* A Remote Desktop session paces presentation. A ~30 Hz RDP session caps the
  emulator at ~30 FPS regardless of what it can do. `DWMFRAMEINTERVAL=15`
  plus hardware H.264 lifts it to 60.
* A dirty working tree stamps the build dirty, and
  `PipelineCache::InitializeDriverCache` refuses to persist the Vulkan driver
  pipeline cache for a dirty build — it logs `Vulkan pipeline cache: disabled
  (dirty build)`. Every such run starts from a cold pipeline cache, which is
  exactly the cost the first-use hitch is made of. Commit or stash before
  measuring warm-up.

### Media

`sceAvPlayer` had no clock of its own. With audio present, video delivery was
gated on the timestamp of the last audio frame handed over, and audio was
handed over on every guest call, so the media timeline advanced at the rate
the title polled rather than in real time. A title that pulls once per
rendered frame therefore ran the stream slow at 30 Hz and fast at 60 Hz, and
because end of stream is reached when the queues drain, the intro video ran
out early at 60 Hz and the title started it again.

Delivery is now derived from a real clock (`src/libs/avPlayerClock.h`):

* the media position comes from elapsed real time, with pause folded in, and
  is anchored once per playback to the first decoded timestamp;
* video selection takes the newest frame the clock has reached, dropping any
  it supersedes, with a half-frame-interval tolerance derived from the
  stream's own frame rate so clock drift does not discard frames;
* audio may run ahead by a bounded lead so a title can prime its output port,
  but no further;
* `AV_SYNC_MODE_NONE` still returns whatever is decoded on every call.

`AvPlayerClockTests` drives one synthetic stream at 30, 60, 120 and 15 Hz and
requires the same media duration, per-frame accounting and end of stream from
all of them. `KYTY_AVPLAYER_STATS` adds lifecycle tracing; it is off by
default.

## Ghost of Yotei — PPSA26344

Not running. Loading and import qualification are solved; the title
initializes its subsystems, opens a window, and then stops progressing with
a black screen.

Resolved so far: the FSELF container layout (a `VERSION` trailer after up to
15 bytes of zero alignment, still requiring exact EOF), the import audit
tooling (`--audit-game`, `--audit-library`, `--audit-json`) and the
qualification aliases it surfaced, and the `sceVideoOutVrrPegToFixedRate` /
`sceVideoOutVrrUnpegFromFixedRate` exports.

**The earlier diagnosis is not supported by the evidence it was drawn from.**
It read "the main guest thread makes no further HLE calls after PthreadCreate
returns" as "execution stalls before reaching the renderer". The trace could
not support that: 639 of the 1533 entry points registered with `LIB_FUNC`
never expanded `PRINT_NAME()`, and the untraced set included every primitive
a guest thread can block in — `sceKernelWaitEventFlag`, `sceKernelWaitSema`
and the `sem_*` family, `sceKernelSyncOnAddressWait{,32,64}`,
`sceKernelBatchMap`, `sceKernelMapFlexibleMemory`, `sceAjmBatchWait`,
`sceAcmBatchWait`. A thread parked in any of those was indistinguishable from
a thread that had vanished.

Those are traced now, and the report separates the two states. Each guest
thread is listed with the call it is inside and how long it has been there,
or the last call it made and how long it has been silent since, together with
the guest call site resolved to module and offset. Thread start and exit are
recorded, so a stalled thread is distinguishable from an exited one.

The next Ghost capture therefore answers directly which of these it is:

* the main thread is parked in a blocking primitive — the report names it,
  with the guest address that called it;
* the main thread is executing guest code — the report shows `state=guest`
  with a growing `silent_ms` and the address of its last crossing;
* the main thread has exited — the report shows `state=exited`.

No upstream KytyPS5 commit in `6a2987a..f100f78` addresses a pre-render
startup stall; see [the upstream review](upstream-kyty-review-2026-09-19.md).
Two upstream fibre-context fixes there match the failure shape of a guest
thread that goes quiet, and are the first thing to try if the capture shows
`sceFiber` in use.

To capture:

```powershell
$env:PROSPEROX_RUNTIME_DIAG = '1'
./_Build/phase0-windows/kyty_emulator.exe --game "E:\GOY\PPSA26344"
# _RuntimeDiag.txt gains a snapshot every two seconds; the `threads=` block
# is the one that matters.
```

## Branch consolidation

`integration/prosperox-consolidated` descends from
`diag/runtime-observability-tdd`, which already contained every commit from
`main`, `debug/bendy-performance`, `test/claude-perf-phase6`,
`claude/prospero-submit-readback-sync`, `claude/prospero-instream-waits` and
`fix/fself-aligned-version`.

Three branches held commits that were not in that line:

| Branch | Unique commits | Disposition |
| --- | --- | --- |
| `claude/prospero-perf-bottleneck-w0s3sr` | `9075a1a` | Superseded. Same wait-polling work, built on the wrong base; the tested form of it is in `f8e5183` and below. Branch left in place. |
| `rescue/astra-phase6-20260916` | `428698d`, `97f76a4` | `docs/phase6-pause-checkpoint.md` recovered. `src/common/perfTrace.h` and `tests/Phase6CheckpointTests.inc` were never wired into CMake even there, and `KYTY_GPU_STATS` supersedes them. |
| `investigate/pm4-sync-20260916` | 5 commits | The PM4 event-driven sync design note recovered. `scripts/phase6/pm4-exact-readback.patch` was an A/B for behaviour that has since shipped. |

No branch has been deleted.

## Remaining Phase 6 work

* Ghost of Yotei reaching visible rendering.
* The commercial-title acceptance route for Bendy — ten successful launches
  and three 30-minute sessions — remains a manual gate. It has not been run.
* First-use hitching on new content. Measure it against a clean (non-dirty)
  build so the Vulkan driver pipeline cache is actually in play before
  treating it as an emulator cost.
* The gates listed under "Remaining Phase 6 gates, in order" below, which are
  unchanged apart from the Bendy runtime boundary being past.

---

*Everything below this line is the record as written on 2026-09-14, when
Phase 6 was paused. It is kept because it documents how each area was brought
up and what was tested. Where it conflicts with the status above — in
particular the claim that no menu or gameplay is reached, and the Bendy
runtime boundary at `unlink` — the status above is current.*

## Implemented and tested

* Audio ports now retain an FFmpeg resampler across submissions when the host
  negotiates a different rate; SDL handles channel/sample-format conversion at
  the resulting rate. Analytic stereo sine fixtures cover 48 kHz to 24/44.1/96
  kHz with 64/256/1024-frame grains over 196,608 input frames. Output is identical
  across grain partitions, with bounded lookahead and maximum sample error below
  0.00001 in the focused run. Tests also cover combined mono/S16 conversion,
  a stalled-device timeout before input consumption, queue failure and reopening.
  A consumed converter cannot roll back after queue failure: the port reports
  failure until closed/reopened instead of accepting ambiguous retries. Close
  still cancels queued audio; end-of-stream drain, automatic device recovery and
  physical speaker validation remain open. No SDL/vendor source was changed.

* Audio output now rejects failed device opens, reports failed submissions,
  preserves queued samples on a stalled-device timeout, validates all handles
  before indexing a batch, and pins ports during submission. Fallback pacing
  uses each port's sample rate/grain size. SDL audio initialization references
  are balanced on open failure and close. AudioOut2 propagates backend errors,
  releases failed queue reservations, pins captured handles, and rejects a
  destroyed context instead of waiting forever. Unsupported object/format ports
  can no longer become successful ports with no backend.
* The headless fixture captures at the SDL transport boundary while running
  production PCM preparation. Generated mono/stereo/8-channel/12-channel float
  impulses verify frame counts and existing channel routing. Signed-16 fixtures
  verify per-channel gain and clipping. This validates the existing 12-to-8
  routing policy, not complete PS5 spatial-audio fidelity or a real speaker mix.
* Controller reconnect retains a current timestamp, duplicate removal is safe,
  and invalid PadRead counts return an error instead of terminating the host.
  A recorded synthetic sequence covers buttons, triggers, touch, disconnected
  sources, neutral reconnect, and the newest 64 entries of an 80-event history.
* SaveData memory Setup/Get/Set/Sync now operate on title/user/slot-specific
  snapshots under `_SaveData/<title>/sce_sdmemory/u<user>_s<slot>.pxm`.
  Data, parameters and icon bytes share one versioned private emulator file.
  Setup reports the previous size; reads never grow the buffer; all scatter
  ranges validate before mutation. Writes stage, flush, close and replace the
  committed snapshot before publishing the new in-memory state. Sync publishes
  a memory-sync completion event. Terminate clears process state. A checksum
  rejects corrupt snapshots. The 512 MiB cap is an explicit host policy, not a
  claimed PS5 limit. Memory-not-ready and I/O failures reach callers.
* An original generated H.264 baseline I_PCM bitstream exercises the production
  VideoDec2 decoder: 30 exact NV12 frames, PTS/DTS/attached metadata, output
  bounds, repeated drain, ten reset cycles and metadata cleanup. No video
  decoder production change was needed. This fixture does not cover B-frame
  reordering, HEVC/VP9, or AvPlayer seek/clock synchronization.

Save tests use separate write/read processes and isolated temporary directories.
They cover user/slot isolation, range rejection, scatter-write rollback,
interrupted staging, failed staging creation, failed replacement with a locked
destination, successful retry, and corrupt-file rejection. No existing user
saves were migrated or overwritten. These tests establish process-interruption
and error behavior; they do not simulate sudden power loss or RAID cache failure.
Multiple emulator processes writing the same slot are not yet coordinated.

## Real controller result

SDL identified `DualSense Wireless Controller`, VID/PID `054c:0ce6`, instance 0.
The successful 30-second run recorded **9,693 changes, six Cross presses and
six releases**, both stick axes reaching **-32768..32767**, and both triggers
reaching **0..32767**. Guest-state values and timestamps were checked throughout.
The 300 ms rumble command returned 0; physical sensation requires user
confirmation. Capture uses SDL polling plus the production controller-state API;
it does not certify every window-event translation or in-game control mapping.

Evidence: `_Build/evidence/phase6-controller-20260913-000557`, exit 0.
The earlier run in `phase6-controller-20260913-000350` recorded Cross but not
stick/trigger travel and correctly exited 2 as incomplete. Physical unplug/replug
and adaptive-trigger behavior remain untested; synthetic reconnect passes.

## Regression and runtime evidence

After the Bendy diagnostic and persistent-resampler changes, the complete suite
was rebuilt and rerun with RX 7800 XT Vulkan/synchronization validation:
**74/75 pass in 46.93 seconds**, including all ten Phase 1 cases and all seven
Phase 6 registrations. The same existing compute-test capability exclusion
remains. No `VUID-` or `SYNC-HAZARD` reports occur in the captured test log.
Evidence: `_Build/evidence/phase6-bendy-services-20260913`, including JUnit,
complete per-test output, source/build hashes and patch. This supersedes the
74-test totals below; those remain historical checkpoints. No hour-long soak
or commercial gameplay route was run at this checkpoint.

The streaming fixture rejected an initial SDL-only implementation at 44.1 kHz
(maximum waveform error 0.386445). Production now uses persistent
[libswresample](https://www.ffmpeg.org/doxygen/trunk/group__lswr.html) for rate
conversion and [SDL_AudioStream](https://wiki.libsdl.org/SDL2/SDL_NewAudioStream)
only for same-rate format/channel conversion. Final unflushed output counts
were 98,288 / 180,618 / 393,184 frames at 24 / 44.1 / 96 kHz respectively,
independent of grain size. The remaining 16 / 15 / 32 frames reflect converter
lookahead, not a claim of end-of-stream drain. This is a synthetic signal
correctness result; it does not certify NGS2 mixing or physical speaker output.

The emulator and relevant test targets build with the pinned Windows toolchain.
The focused service suite passes **7/7** (six new Phase 6 registrations plus
AudioOut2). Full real-Radeon validation in
`_Build/evidence/phase6-regression-20260913` reports **73 passes / 74 tests**.
The remaining monolithic compute test exits with the pre-existing
`PHASE0_UNAVAILABLE attachment feedback loop dynamic state is unsupported by
this device`; CTest reports it as failed. Its later cases are not claimed as
executed. There were no new Vulkan/synchronization failures. The final focused
log is `_Build/phase6-focused-final.log`.
After the final save event-lock and publication-failure fixture changes, the
same full result was repeated in
`_Build/evidence/phase6-regression-final-20260913` (43.88 seconds, 73 passes,
one capability exclusion; no VUID or synchronization-hazard reports).

The first bounded commercial-runtime attempts were made after the synthetic
gate, using the existing files directly and an isolated evidence working
directory. Neither reached guest execution; both exited 321 in the loader.
Game data was read only, with no decryption, patching, copied commercial binaries
or keys. These are diagnostic failures, not successful launches.

* Metadata: PPSA01342, `UP9000-PPSA01342_00-DEMONSSOULS00000`, version
  `01.005.000`, master `01.00`.
* `eboot.bin`, SHA-256
  `B7FD1845DD0C34038F3E5DFB2F2D6A5734036354D5BFEA513986B5B9239602A0`:
  SELF payload mapping rejected. Program header 12 (`0x6fffff01`) declares
  6,537 bytes without a segment-table owner; the bytes after the SELF-declared
  size total 6,529. Header 13 is also not covered by a payload owner. This is
  not evidence that missing bytes can safely be invented or that this custom
  layout should be accepted. Evidence: `phase6-runtime-20260913`.
* Existing `eboot.bin.esbak`, SHA-256
  `B10554618C43FDEF1235D65A211ACCAF46D22686C77D9BA50A5E37612603C81A`:
  raw ELF header, but section table offset 610,406,928, 53 entries of 64 bytes,
  in a 46,533,532-byte file. Rejected as an out-of-bounds section table.
  Evidence: `phase6-runtime-elf-20260913`.

The original executable/dumping or conversion history is needed to establish
the correct input contract. No loader check was relaxed to force progress.

## Remaining Phase 6 gates, in order

1. Continue from qualified POSIX `unlink` in IL2CPP: capture its requested path,
   test mounted-file mutation/error semantics and expose only the proven ABI.
   Preserve validated AGC setup and the explicit native tessellation boundary;
   implement native LS/HS/ring consumption only when needed and evidenced. Keep the
   dump's unknown transformation history recorded; structural acceptance is
   not authentication or proof of a canonical retail container.
2. Implement the required NGS2 sampler/graph/routing/mixing behavior.
   `Ngs2SystemRender` still clears output and advances stub voice states. The
   PCM transport tests do **not** turn that into working NGS audio. Establish
   actual supported command/voice contracts; custom racks and DSP require
   further evidence.
3. Extend the passing streaming-rate fixtures with end-of-stream drain, clock
   and underrun/device-loss behavior, AJM codec error/drain/backpressure fixtures,
   and actual speaker-output validation. Batch partial acceptance and device
   recovery need further work; no all-or-nothing multi-device submission is
   claimed by the new error propagation.
4. Establish the mounted SaveData transaction ABI before implementing
   prepare/commit/backup: current SharpEmu and ProsperoX disagree on Prepare's
   arguments and both have placeholder transaction behavior. Then validate
   transactions,
   parameter/icon APIs and recovery. The new persistent **memory** slots do not
   implement the existing mounted-save transaction stubs. Expand option/error
   conformance and interprocess ownership before claiming general SaveData.
5. Complete AvPlayer/video seek, reordering, clock tests, user/dialog lifecycle,
   physical controller reconnect/rumble confirmation, and graceful game stop
   and relaunch. Extend deterministic input recording into a verified game route.
6. Record boot/menu/controllable gameplay/area transition/checkpoint reload/
   save/relaunch on the exact Bendy build, with title-appropriate checkpoints.
   Require ten successful
   launches and three 30-minute sessions per the engineering plan. **None of
   these commercial completion criteria has been met yet.**

## References and reproduction

SharpEmu at `6d4e5b4f6561ef1d2910b34e35cce9fd4264fc08` provided behavioral
references in `AudioPcmConversionTests`, `PadExportsTests`, and
`SaveDataMemoryExportsTests`/`SaveDataExports` (including memory-sync event type
3). No Sharp source was transplanted. Existing upstream notices are retained.
Windows publication uses the documented
[MoveFileExW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw)
and [FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers)
interfaces. PCM transport follows
[SDL_QueueAudio](https://wiki.libsdl.org/SDL2/SDL_QueueAudio).
Video fixtures follow [ITU-T H.264](https://www.itu.int/rec/t-rec-h.264) and
the [FFmpeg decoder contract](https://www.ffmpeg.org/doxygen/trunk/group__lavc__decoding.html).

```powershell
. ./scripts/phase0/windows-env.ps1
cmake --build --preset phase0-windows --parallel 6 --target kyty_tests kyty_emulator
ctest --test-dir _Build/phase0-windows -R '^phase6_|^audio_out2_port$' --output-on-failure
# Manual, real hardware; press Cross to start the 30-second capture:
./_Build/phase0-windows/phase6_input_tests.exe --controller
# New evidence directory required; bounded diagnostic, not an automatic pass:
./scripts/phase6/windows-runtime.ps1 -GamePath '<directory or executable>' -EvidenceDir '<new path>' -Seconds 90
```
