# Phase 6 — Ghost of Yōtei shader-pipeline checkpoint

2026-09-20. Target: Windows 11, Ryzen 7 7800X3D, RX 7800 XT, 32 GB.
**Phase 6 remains OPEN. Phase 7 has not started.** This checkpoint records
where Ghost of Yōtei PPSA26344 stands, what was crossed to get there, and what
the next blocker actually is. Bendy PPSA27624 was re-run against the same
build.

## Where Ghost stands

Ghost boots, links, runs guest code, reaches AGC submission, builds pipelines,
and **executes draws and dispatches**. Over a 150-second run it compiles
**57 compute, 6 vertex and 6 pixel shaders** and sustains **4.7–5.0 draws and
40–78 dispatches a second** between compiles. One run reached the 200-second
cutoff without terminating at all.

It is not stable. Most wall-clock time goes into compiling newly encountered
shaders — the command processor is blocked while that happens — and each new
shader can still reach an instruction or a descriptor shape the recompiler
does not implement. The current stop is a shader using MUBUF opcodes 0x83 and
0x19 and a VOP1 SDWA destination selector.

For contrast, at the start of this session Ghost compiled 6 compute shaders,
had never compiled a vertex or pixel shader, and had issued **zero** draws and
dispatches.

## What was crossed

| Blocker | Resolution |
| --- | --- |
| Shader failures could not be attributed | Provenance carried from the register that supplied the address, through the CFG and into every IR pass; failures now also list every unsupported instruction in the shader |
| `image_atomic_fmax` decoded as unknown | Completed the GFX10 MIMG atomic table from LLVM's `MIMGInstructions.td` |
| Float image atomics unsupported by the driver | `shaderImageFloat32AtomicMinMax` is false here, so the compare runs as a compare-exchange loop over the raw texel bits through an R32_UINT view |
| `src0 = 0xe9` unsupported operand | DPP8 decoded for VOP1/VOP2/VOPC, both fetch-inactive escapes |
| Four Agc entry points unreachable | Reviewed individually and added to the qualified list |
| Descriptor held inside a larger record | Indirect lookup carries heap stride and in-record offset, accepts shifts, handles r128 |
| Samplers and pointer-backed tables | Planned by the same routine as images, each with its own source; key may be masked, or a loop counter bounded by the heap's own extent |
| The descriptor walk faulted the host | It no longer dereferences an address it has not validated; unreadable flat slots are zero rather than fatal; value equivalence walks with an explicit stack |
| Double-precision instructions | `IR::Type::F64`, native SPIR-V doubles via PackDouble2x32, the GFX10 conversions, rounding group, reciprocal, sqrt, and the VOP3 arithmetic |
| Buffer descriptor chosen at runtime | Buffer access lowered to address arithmetic over the four descriptor dwords, including the swizzled indexed form and the hardware's bounds behaviour |
| Flat stores refused for lack of ownership tracking | A second page bitmap in the fault buffer records what a shader wrote; those pages are marked GPU-modified |
| Packed 32-bit and 8-bit sRGB formats | Sampled as raw dwords and unpacked with normalization; one- and two-channel sRGB made renderable |
| Half-precision compares, 64-bit LDS bitwise | Filled in the four runs of six; DS_AND/OR/XOR_B64 as a pair of 32-bit atomics, which is exact for bitwise operations |

## What is still in the way

1. **The next shader's instructions.** MUBUF 0x83 and 0x19 and a VOP1 SDWA
   destination selector. Deciding what MUBUF 0x83 is on GFX10 needs the
   opcode table checked against a source, not recalled.
2. **Compilation dominates the frame.** The command processor is blocked
   while a pipeline is built, so the draw rate says more about compile cost
   than about rendering.
3. **77 imports are unimplemented** in `--audit-game`, three of them under
   `Agc_v1`. Most may never be called; each one that is terminates the title.

## Bendy

Re-run on the same build, Bendy now reaches far more content than before this
session: **71 vertex, 71 pixel and 10 compute shaders** and about **20,500
draws and 1,560 dispatches a second**, against 9/7/5 shaders and 870 draws a
second earlier in the session. No errors on stderr. This is a large enough
change that it is worth looking at on screen rather than taking the counters
at face value.

## Tests

`--phase4-corpus` passes throughout: `decoded_translated_validated=299
compute_executed=294 compile_only=5 graphics_executed=13`. New fixtures that
execute on the GPU: DPP8 reversal and in-group broadcast; the float image
minimum and maximum over the compare-exchange loop; the signed image maximum
over its direct opcode.
