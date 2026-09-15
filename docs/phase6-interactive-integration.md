# Phase 6 — Interactive integration checkpoint

**Historical record.** The current September 16 pause checkpoint records real
Bendy Chapter 1 gameplay with working audio/controller/video, unresolved low FPS
and repeating intro, and the latest validation. See
[Phase 6 pause checkpoint](phase6-pause-checkpoint.md). The older blockers and
"no gameplay" statements below describe their dated runs, not the current tree.

Status: **in progress, not complete**, 2026-09-14. Target: Windows 11,
Ryzen 7 7800X3D, RX 7800 XT, 32 GB DDR5. Baseline is `b00d5a8` plus the
retained Phase 5 validation changes. No Phase 7 work has started.

Latest AGC checkpoint: validated per-renderer ring/offchip configuration and
explicit rejection at unsupported native tessellation draw consumers. Bendy
passes these calls and the newly qualified POSIX condition destructor, then
stops at `VAzswvTOCzI[Posix_v1][libkernel_v1.1]` (`unlink`) in IL2CPP.
See [configuration checkpoint](phase6-tessellation-configuration.md).

The primary real-title target is now **Bendy and the Dark Revival, PPSA27624**
(`01.000.003` in local metadata). Its unknown transformation history remains
recorded, but the independently specified strict FSELF profile now accepts its
containers. Guest execution reaches worker-thread, scripting-metadata and
initial graphics-state construction and level asset reads. No menu
or gameplay is claimed. See [container investigation](phase6-bendy-container.md)
for the loader/TLS/export changes, exact runtime boundary, and regression logs.

Latest full regression: **80/81 passed in 51.95 seconds**,
including all **13 Phase 6 tests**. No memory-allocation failures remain. The one
failure is the known unsupported feedback-loop capability in the monolithic
shader test. Real RX 7800 XT Vulkan/synchronization validation reports no errors.
Phase 3 completes 10,000 release lifecycles; the standard Phase 5 presentation
check passes 323 frames with warm/peak 1,317,171,216 bytes and 13 allocations.
Evidence: `phase6-ring-regression-20260914`.
Earlier counts below remain historical evidence. Phase 6 is not closed.

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
