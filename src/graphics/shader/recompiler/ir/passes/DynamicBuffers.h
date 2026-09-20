#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_DYNAMICBUFFERS_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_DYNAMICBUFFERS_H_

#include "graphics/shader/recompiler/ir/ShaderIR.h"

namespace Libs::Graphics::ShaderRecompiler::IR {

// Rewrites buffer accesses whose descriptor the host cannot resolve at bind
// time into plain address arithmetic over the descriptor the shader loaded.
//
// A descriptor read out of a table the shader indexes at runtime names a
// different buffer on every iteration, so there is no one buffer to bind. The
// hardware does not bind anything either: a buffer access is address
// arithmetic over the four dwords, and those dwords are values the shader
// already has. Doing that arithmetic in the shader is what the hardware does,
// not a substitute for it.
//
// Must run after the SRT plan is built, because that is what decides whether a
// descriptor is resolvable, and before resource tracking, which is what would
// otherwise refuse the shader.
void LowerDynamicBuffers(Program& program);

} // namespace Libs::Graphics::ShaderRecompiler::IR

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_DYNAMICBUFFERS_H_ */
