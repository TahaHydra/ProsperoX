#include "graphics/shader/recompiler/ShaderProvenance.h"

#include "graphics/shader/recompiler/frontend/decode/ShaderDecoder.h"

#include <algorithm>
#include <fmt/format.h>

namespace Libs::Graphics::ShaderRecompiler {

std::string DescribeProvenance(const Provenance& origin) {
	return fmt::format("shader stage={} source={} hash=0x{:016x} guest=0x{:016x}..0x{:016x} "
	                   "size={} bytes wave{}",
	                   origin.stage, origin.source, origin.hash, origin.code_base,
	                   origin.code_base + origin.code_size_bytes, origin.code_size_bytes,
	                   origin.wave_size);
}

std::string DescribeCodeWindow(std::span<const uint32_t> code, uint64_t code_base, uint32_t pc,
                               uint32_t words_before, uint32_t words_after) {
	if (code.empty()) {
		return "  <no code>\n";
	}

	const auto pc_word = pc / sizeof(uint32_t);
	const auto first   = pc_word > words_before ? pc_word - words_before : uint64_t {0};
	const auto last    = std::min<uint64_t>(code.size(), pc_word + words_after + 1u);

	std::string out;
	for (auto i = first; i < last; i++) {
		const auto offset = static_cast<uint32_t>(i * sizeof(uint32_t));
		out += fmt::format("  {} +0x{:04x} guest=0x{:016x} 0x{:08x}\n", i == pc_word ? "->" : "  ",
		                   offset, code_base + offset, code[i]);
	}
	if (pc_word >= code.size()) {
		out += fmt::format("  -> +0x{:04x} is past the end of the declared code ({} words)\n", pc,
		                   code.size());
	}
	return out;
}

std::string DescribeInstructionWindow(const Decoder::Program& program, uint32_t pc, uint32_t before,
                                      uint32_t after) {
	const auto& instructions = program.instructions;
	size_t      index        = instructions.size();
	for (size_t i = 0; i < instructions.size(); i++) {
		if (instructions[i].pc == pc) {
			index = i;
			break;
		}
	}
	if (index == instructions.size()) {
		return "  <pc is not an instruction boundary in the decoded program>\n";
	}

	const auto first = index > before ? index - before : size_t {0};
	const auto last  = std::min(instructions.size(), index + after + 1u);

	std::string out;
	for (auto i = first; i < last; i++) {
		const auto& inst = instructions[i];
		out += fmt::format("  {} +0x{:04x} words={} {}\n", i == index ? "->" : "  ", inst.pc,
		                   inst.word_count, Decoder::InstructionToString(inst));
	}
	return out;
}

std::string DescribeShaderFailure(const Provenance& origin, const Decoder::Program& program,
                                  uint32_t pc) {
	std::string out = DescribeProvenance(origin);
	out += "\n raw words:\n";
	out += DescribeCodeWindow(program.code, origin.code_base, pc, 6, 6);
	out += " decoded instructions:\n";
	out += DescribeInstructionWindow(program, pc, 6, 2);
	return out;
}

} // namespace Libs::Graphics::ShaderRecompiler
