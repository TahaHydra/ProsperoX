# Phase 6 — Ghost of Yōtei checkpoint

2026-09-20. Target: Windows 11, Ryzen 7 7800X3D, RX 7800 XT, 32 GB.
**Phase 6 remains OPEN. Phase 7 has not started.** This checkpoint records
where Ghost of Yōtei PPSA26344 stands, what was crossed to get there, and what
the next blocker actually is. Bendy PPSA27624 was re-run against the same
build.

## Where Ghost stands

Ghost boots, links, runs guest code, reaches AGC submission, builds pipelines,
and renders. It compiles **88 compute, 10 vertex and 21 pixel shaders**, runs
its full post-processing chain at 3840x2160, and gets **thirteen frames in**
before the title itself faults on a null pointer.

That fault is now the only thing in the way, and it is on the guest's side of
the boundary: the title's renderer init returns failure at startup, the caller
turns that into `-4` and carries on, and thirteen frames later a lookup into
the table that init never allocated reads a null pointer. Everything the
emulator is asked to do up to that point it does.

For contrast, at the start of the previous session Ghost compiled 6 compute
shaders, had never compiled a vertex or pixel shader, and had issued **zero**
draws and dispatches.

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
| Formatted buffer load through a runtime descriptor | Lowered to four raw dword reads plus a module function that applies the descriptor's own format and destination-select fields, switching over every format the guest can name |
| **Device lost, reproducibly, at the same dispatch** | A selection region entered from outside its header dropped the whole shader onto the dispatcher's state machine: one switch over 158 blocks inside a loop, run by 147456 invocations, which the GPU never finished. The structurizer now copies the blocks the outside edge reaches, after routing has had its turn |
| A lost device said nothing about why | `VK_EXT_device_fault` is enabled when the driver offers it, and a loss reported by a submit or a wait logs the driver's description, address ranges and vendor codes |
| Dispatches above the device's workgroup limit | Split into legal chunks issued with a base workgroup offset, so every workgroup still sees the id it would have had |
| PM4 nesting limit exceeded | Bit 20 of `INDIRECT_BUFFER` chains rather than nests. Ghost builds its frame as a long chain; each link was being kept on the stack |
| `DS_ADD_U64` / `DS_SUB_U64` | Carried out on the two halves, the low half's atomic reporting whether the high half takes a carry |
| 16-bit `V_CMPX` compares | Added to the VOPC table and translated like their 32-bit counterparts |
| The GPU wrote zeros over the title's own memory | A shader address that went nowhere still records a page, and the page table spans the whole guest address space. Pages outside every range the title mapped for the GPU are now ignored |
| PS5 PlayGo file reported as invalid | "plgx" accepted alongside "plgo"; they put the chunk count in the same place |

## What is still in the way

1. **The title's renderer init fails at startup.** `eboot.bin+0x2fbf40`
   returns false to `eboot.bin+0x2505f0`, which returns `-4`. The init runs
   far enough to store its configuration but never reaches the allocations at
   its end, so the 39-entry table it owns stays null. Thirteen frames later
   `eboot.bin+0x2e1c00` indexes entry 28 of that table and reads
   `[null+0x18]`. The init makes 95 calls and reaches no emulated library
   directly, so finding the branch that gives up needs guest-side tracing the
   emulator does not have yet — a conditional guest breakpoint, or a call
   trace over the title's own code.
2. **Compilation dominates the frame.** The command processor is blocked
   while a pipeline is built, so the draw rate says more about compile cost
   than about rendering.
3. **Imports remain unimplemented** in `--audit-game`. Most may never be
   called; each one that is terminates the title.

## Distance to a menu

The renderer runs; the title stops itself. Everything between here and a menu
is behind one guest-side failure whose cause is not yet identified, so any
percentage is a guess about what that failure turns out to need. If it is one
more library return value, the menu is close. If the init depends on a
subsystem that is not implemented at all, it is not.

## Bendy

Unchanged as a regression target: 85–90 vertex, 80–85 pixel and 10 compute
shaders over a two-minute run, no errors on stderr, no device loss. The PM4
chaining and fault-page changes leave it exactly where it was.

## Tests

`shader_cfg_tests` builds and passes again — it had stopped compiling when
`BuildGraph` started carrying provenance, so nothing in it had run since. The
first thing it caught was a disagreement about out-of-range scalar buffer
reads, which the hardware answers with zero; the walk now does the same and
keeps refusing only what cannot be encoded at all.

`resource_tracking_tests`, `resource_materialization_tests`,
`scalar_provenance_tests` and `shader_vertex_metadata_tests` pass.
`shader_recompiler_compute_tests` stops early on this device, which does not
support attachment-feedback-loop dynamic state; everything it reaches passes.
