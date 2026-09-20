#ifndef EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERPROVENANCE_H_
#define EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERPROVENANCE_H_

#include <cstdint>
#include <span>
#include <string>

namespace Libs::Graphics::ShaderRecompiler {

namespace Decoder {
struct Program;
} // namespace Decoder

// Where a shader came from. A recompiler failure that cannot name the bytes it
// choked on is not evidence of anything: the same words appear in the padding
// between shaders, in the shader after this one, and in whatever the guest left
// in that memory earlier. Every diagnostic carries this.
struct Provenance {
	// Pipeline stage, as the guest asked for it.
	const char* stage = "?";
	// The register the shader address was read out of, so a wrong address can be
	// traced back to the command that set it rather than guessed at.
	const char* source = "?";
	uint64_t    hash   = 0;
	// Guest address of the first code word, and the size the guest declared for
	// it. A decode that runs past the declared size is reading someone else's
	// memory, which is worth being able to see at a glance.
	uint64_t code_base       = 0;
	uint32_t code_size_bytes = 0;
	uint32_t wave_size       = 0;
};

// One line naming the shader: stage, source register, hash, guest range, wave.
[[nodiscard]] std::string DescribeProvenance(const Provenance& origin);

// Raw words around a pc, with the guest address of each line, marking the one
// the failure is about. This is what proves whether a pc is a real instruction
// boundary or the middle of something else.
[[nodiscard]] std::string DescribeCodeWindow(std::span<const uint32_t> code, uint64_t code_base,
                                             uint32_t pc, uint32_t words_before,
                                             uint32_t words_after);

// The decoded instructions leading up to a pc. Read together with the raw
// window, a mismatch between the two is the signature of a bad boundary.
[[nodiscard]] std::string DescribeInstructionWindow(const Decoder::Program& program, uint32_t pc,
                                                    uint32_t before, uint32_t after);

// Everything above, as the body of a failure report.
[[nodiscard]] std::string DescribeShaderFailure(const Provenance& origin,
                                                const Decoder::Program& program, uint32_t pc);

} // namespace Libs::Graphics::ShaderRecompiler

#endif /* EMULATOR_INCLUDE_EMULATOR_GRAPHICS_SHADER_RECOMPILER_SHADERPROVENANCE_H_ */
