# Upstream KytyPS5 review — 2026-09-19

Reviewed range: `6a2987a..f100f78` on `KytyPS5/KytyPS5 main` (69 commits), the
work landed since ProsperoX's last formal upstream review on 2026-09-15.

ProsperoX is its own emulator, not a downstream mirror. Nothing here was
merged. Each upstream commit was read and classified; the small number marked
**PORTED** were applied deliberately, and the rest are recorded with the reason
they were not, so this range does not have to be re-read from scratch.

Classification legend:

| Class | Meaning |
| --- | --- |
| `EQUIVALENT` | ProsperoX already has the behaviour, reached independently |
| `PORTED` | Applied on this branch |
| `PORT / NEEDS EVIDENCE` | Worth having, but a wrong guess regresses a working title; port behind a runtime A/B |
| `SUPERSEDED` | ProsperoX's own design already covers or replaces it |
| `NOT RELEVANT` | CI, packaging, launcher, or another platform's build |

## Ported

| Commit | Change | Why |
| --- | --- | --- |
| `104530e` | `#undef MemoryBarrier` after `windows.h` in `regionManager.h` | One line. `windows.h` defines `MemoryBarrier` as a macro and it collides with the Vulkan barrier types this header's users name. ProsperoX does not hit it today only because of include order. |

## Already equivalent

| Commit | Change | Evidence in ProsperoX |
| --- | --- | --- |
| `79120d5` | Handle chained PM4 buffers without recursion | `CommandProcessor::ProcessIndirectBuffer` already drives an explicit `m_buffer_stack` with a 64-deep nesting limit instead of recursing (`src/graphics/guest_gpu/graphicsRun.cpp:890`). Reached independently through the event-driven PM4 work. |
| `349fae0` | Register-form SSE4a `EXTRQ` | Present in `src/loader/x64InstructionEmulator.cpp`. |
| `7c9148d` | Triangle fans in PS5 geometry shaders | Present. |
| `0d9e95d` | Centroid interpolation weights | Present. |
| `63fb822` | Continue mesh draws with primitive restart | Present. |
| `f38d738` | PS5 texture minimum LOD for streamed mip chains | Present. |
| `eda6ba3` | Json2 ABIs | Present. |

Previously adapted upstream work — CS_DONE completion, condition-waiter
deadlock changes, front-face VGPR encoding, `PS_W32_EN`, embedded vertex
fetch / scalar prolog preservation, DCC clear encoding — carries different
ProsperoX SHAs and is not re-listed here.

## Port, but only behind runtime evidence

| Commit | Change | Why it is held |
| --- | --- | --- |
| `49b2eed` | AGC command-buffer capacity calculation | The only behavioural delta for ProsperoX is the wrapped-cursor case. Today `GetAvailableSizeDW` returns 0 when `cursor_down <= cursor_up`, which makes `ReserveDW` invoke the grow callback. Upstream reinterprets the signed distance as unsigned, so the same state reports an enormous free size and the guest writes on. That matches the real ABI, and it is a plausible way for a title to stall against a command buffer that never grows — but if a working title ever reaches that state, the upstream form writes past the buffer instead of growing it. The rest of the commit (widening the return type, dropping a `UINT32_MAX` clamp that needs a 16 GB command buffer to trigger) is dead code either way. Port it as an A/B against Bendy, not blind. |
| `ded6964`, `71f52f9` | Fiber context in native TLS rather than `thread_local`; fiber ownership | ProsperoX still has the `thread_local` form (`src/libs/libKernel.cpp:2349`). The bug is real: a compiler may cache the TLS base across a call, and a fiber that resumes on a different host thread then reads another thread's slot. That is exactly the failure shape of a guest thread that goes quiet and never returns. Held because the two commits must land together, ProsperoX's fiber state has diverged, and no ProsperoX title is yet known to use `sceFiber` — confirm from a runtime capture that fibers are in use before touching this. |
| `86e5e01` | Windows guest red zones when emulating VRSQRTPS | 925 lines in `redZonePatcher.cpp`, Windows-only, and it interacts with guest code patching, which is the least forgiving thing to change without being able to run both titles. |
| `957452d` | Emulate VRSQRTPS | Pairs with the above; every supported host has AVX, so this is about result fidelity rather than availability. |

## Useful, not yet ported

Additive shader work. None of it can unblock a title that stalls before shader
compilation, and none of it is reachable from ProsperoX's current blockers, but
each adds an opcode or encoding that is otherwise unhandled, and each carries an
upstream test. Port on demand, one at a time, when a title's shader actually
needs it.

| Commit | Change |
| --- | --- |
| `ea6aae8` | `V_MAD_I16` |
| `5b7d334` | `V_CMP_NLT_F16` |
| `394e638` | `V_MUL_LO_U16` |
| `4b77686` | `V_CMP_NGT_F16` |
| `95d3d9d` | Packed integer negation |
| `bfc8cd0` | DPP8 moves |
| `f30afc1` | `S_ASHR_I64` |
| `5b54137` | `S_CBRANCH_CDBGSYS` |
| `acd7435` | Byte destinations in `V_MOV_B32` |
| `c66d977` | SNORM16 formatted stores for skinning buffers |
| `6493674` | Dual-source blending |
| `e0ecffe` | Explicit-LOD texture gathers approximated at mip zero |
| `235e24d` | Null-destination SWAPPC shader handoffs |
| `fb94a81` | Early returns as shared loop continuations |
| `bc45aaf`, `af3a3f4` | Uniform sampler choices across conditional arms |
| `441367b` | Pixel shader input aliases |
| `b594f8c` | Bind only exported colour targets, preserve MRT slots |
| `8589731` | PS5 mip-tail views during image expansion |
| `47957fa` | Stencil associations by guest range and extent |
| `fa913ec` | EXPCLEAR on expanded depth attachments |
| `c1dccd7` | Skip zero-sized compute dispatches |
| `629ae1a` | Z-pass predication and query waits |
| `1d2f59d` | Reject invalid AJM batch storage |
| `007d7ea` | Stereo RAW AAC layout from channel-pair packets |
| `32e6a43` | Json2 integer reads |
| `bab1c36` | Negative dynamic printf precision |
| `87b4417`, `57c5b20`, `ba1854b`, `cbedf57`, `f100f78` | Network and NP fixes |
| `ce629e0` | Gyroscope and motion sensors |
| `7af28c2` | Map Z/C to L2/R2 |

`165234b` (BC7-RGBA32_UINT views) and its revert `c2b7b0b` cancel out; neither
is worth carrying.

## Superseded by ProsperoX's design

`697b18b` (replace `ByteBuffer` with `std::vector`), `5b2935e` (remove the extra
virtual memory layer), `cc8e023` (remove magic_enum wrappers), `2bf78a9` (remove
string wrappers), `c5cebc1` (remove unused helpers and the `-64` suffix),
`1211958`, `5dc644e`, `218e720` (helper cleanups), `6cfde37` (avoid extra
allocations), `77680ce` (make the AMD CPU patch optional).

These are upstream refactors of code ProsperoX has already restructured. They
touch hundreds of call sites for no behavioural change, they conflict heavily,
and taking them would pull ProsperoX back toward being an unreviewed copy of
upstream. Adopting any of them should be a deliberate ProsperoX decision about
ProsperoX's own code, not an upstream merge.

## Not relevant

`9cf7963`, `9b2111d` (FFmpeg sourcing — ProsperoX bundles its own),
`e6156be` (sccache in CI), `10cae50` (Nix flake), `6c03b4a` (VS Code settings),
`0627e23` (CodeRabbit config), `4e6f4f9` (macOS SSE4a), `de1545e`, `a6144f7`,
`228cf3f` (launcher and Qt-version work).

## Ghost of Yotei in upstream

Upstream has no commit in this range that names Ghost of Yotei or addresses a
pre-render startup stall. The Ghost-related upstream work referenced in earlier
ProsperoX notes — image atomic FMIN/FMAX, DPP8, the larger bring-up branches —
is shader bring-up, and it applies only after a title reaches shader
compilation. ProsperoX's Ghost blocker is upstream of that, so no upstream
commit in this range is a candidate fix for it.
