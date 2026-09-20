# Phase 6 — Ghost of Yōtei shader-pipeline checkpoint

2026-09-20. Target: Windows 11, Ryzen 7 7800X3D, RX 7800 XT, 32 GB.
**Phase 6 remains OPEN. Phase 7 has not started.** This checkpoint records
where Ghost of Yōtei PPSA26344 stops today, what was crossed to get there, and
what the next blocker actually is. Bendy PPSA27624 was re-run against the same
build and is unchanged.

## Where Ghost stops

Ghost boots, links, runs guest code, reaches AGC command submission and starts
building pipelines. It compiles **25 compute shaders** and terminates while
translating the 26th:

```
shader resource tracking: pc=0x000005c8 GetSamplerResource dword 0 is not a
valid runtime value: ReadConstBuffer(GetBufferResource(...), IMul32(
ReadConstBuffer(GetBufferResource(...), ShiftLeftLogical32(Phi(...), 2)), 0x368))
 shader stage=CS source=cs_regs.data_addr hash=0x34be6ffcc212383c
 guest=0x00000080003b2200..0x00000080003b2da0 size=2976 bytes wave64
```

The GPU statistics for the run are `draws=0 dispatches=0`: **Ghost has not yet
executed a single draw or dispatch, and no vertex or pixel shader has been
compiled for it at all.** Everything below is progress through pipeline
creation, not evidence of rendering.

## What the 26th shader does

It is a clustered-shading compute pass. Per iteration of a loop it reads a
material index out of one buffer, then reads a whole 0x368-byte material record
out of another:

```
0x5b0: S_MUL_I32            vcc_lo, s60, 0x368
0x5bc: S_BUFFER_LOAD_DWORDX8 s12, s52, vcc_lo ; offset=136
0x5c8: image_sample_d       v14, v0, s16, s12 ; r128=1
```

That one eight-dword load fetches an r128 image descriptor (s12..s15) and its
sampler (s16..s19) together, from a record selected by a loop-varying index.
The image half now resolves; the sampler half does not, and that is the next
blocker.

## Crossed in this session

| Blocker | Resolution |
| --- | --- |
| Shader failures could not be attributed | Provenance carried from the register that supplied the address, through the CFG and into every IR pass |
| `image_atomic_fmax` decoded as an unknown MIMG opcode | Completed the GFX10 MIMG atomic table from LLVM's `MIMGInstructions.td`, with IR opcodes and SPIR-V lowering |
| Float image atomics unsupported by the host driver | `shaderImageFloat32AtomicMinMax` is false on this RX 7800 XT, so the float compare runs as a compare-exchange loop over the raw texel bits |
| An R32_FLOAT image could not carry an atomic | The conversion for the single-component 32-bit formats is the identity, so one raw R32_UINT view serves the atomic and the ordinary accesses alike |
| `src0 = 0xe9` decoded as an unsupported operand encoding | DPP8 decoded for VOP1/VOP2/VOPC, both fetch-inactive escapes, lowered to the existing lane shuffle |
| Four Agc entry points unreachable under the title's identity | Reviewed individually and added to the qualified list; none reads emulator state |
| An image descriptor held inside a larger record was rejected | The indirect-image lookup no longer assumes a packed 32-byte table: it carries the heap stride and the descriptor's offset inside the record, accepts a shift as a multiply, and handles r128 |

## What is still in the way

1. **Indirect sampler descriptors.** The image lookup selects a resource at
   runtime by key; the sampler for that sample instruction is still resolved as
   a single compile-time descriptor. The same key has to drive both. This is
   the immediate blocker and it is the symmetric twin of the work already done
   for images.
2. **The whole graphics path is unexercised for this title.** No VS, PS or mesh
   shader has been compiled, no render target has been bound, nothing has been
   presented. Whatever Ghost's graphics shaders need is entirely unmeasured.
3. **77 imports are unimplemented** in `--audit-game`, 3 of them under `Agc_v1`
   (`AAeX-U5-P3M`, `GPbUp9jXQa8`, `ebixW91gpPw`). Most may never be called;
   each one that is called terminates the title on the first call.

## How far along this is

**Estimate: 35–45% of the way to Ghost drawing a menu.** The reasoning, not a
number to quote on its own:

- Boot, loader, TLS, kernel synchronization and the service imports Ghost
  actually calls are in reasonable shape — that work is largely behind us, and
  it is what consumed the previous sessions.
- Pipeline creation is where the title now spends its life, and the first
  compute pass is the first thing it builds. 25 of 26 compiling is not 96% of
  anything; it is one shader batch.
- The unexercised graphics path is the large unknown. It is not possible to
  size it honestly before a single draw has been attempted, and the estimate
  above assumes it behaves roughly like Bendy's, which is an assumption and
  not a measurement.

Treat the range as a statement about confidence, not a schedule.

## Bendy regression

Re-run on the same build: reaches gameplay, 840 draws/s and 240 dispatches/s
steady, 9 VS / 7 PS / 5 CS compiled, no errors on stderr. Unchanged from
before this work.

## Tests

`--phase4-corpus` passes: `decoded_translated_validated=299 compute_executed=294
compile_only=5 graphics_executed=13`. New fixtures execute on the GPU:

- `VectorDpp8ReverseLanes`, `VectorDpp8BroadcastWithinGroup`
- `ImageAtomicFmaxOnFloatImageRaisesTexel`,
  `ImageAtomicFminOnFloatImageKeepsSmallerTexel`,
  `ImageAtomicSmaxKeepsLargerSignedTexel`
