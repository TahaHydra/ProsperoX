#include "graphics/shader/recompiler/ir/passes/ResourceTracking.h"

#include "common/assert.h"
#include "graphics/shader/recompiler/ir/ShaderIR.h"
#include "graphics/shader/recompiler/ir/passes/SrtWalker.h"

#include <algorithm>
#include <fmt/format.h>
#include <span>
#include <utility>

namespace Libs::Graphics::ShaderRecompiler::IR {
namespace {

// A counter that reaches the descriptor index through more phis than this is
// not a shape this pass can still call a counter.
constexpr size_t MaxCountedKeyChain = 16;

constexpr uint32_t SamplerBorderClampMask    = (1u << 2u) | (1u << 5u) | (1u << 8u);
constexpr uint32_t SamplerDword3ReservedMask = 0x3ffff000u;

uint32_t PossibleU32Bits(Value value) {
	value = value.Resolve();
	if (value.IsImmediate()) {
		return value.GetType() == Type::U32 ? value.U32() : UINT32_MAX;
	}
	const auto* inst = value.TryInstruction();
	if (inst == nullptr) {
		return UINT32_MAX;
	}
	switch (inst->GetOpcode()) {
		case ValueOpcode::BitwiseAnd32:
			return PossibleU32Bits(inst->Arg(0)) & PossibleU32Bits(inst->Arg(1));
		case ValueOpcode::BitwiseOr32:
			return PossibleU32Bits(inst->Arg(0)) | PossibleU32Bits(inst->Arg(1));
		case ValueOpcode::ShiftLeftLogical32: {
			const auto shift = inst->Arg(1).Resolve();
			return shift.IsImmediate() && shift.GetType() == Type::U32
			           ? PossibleU32Bits(inst->Arg(0)) << (shift.U32() & 31u)
			           : UINT32_MAX;
		}
		default: return UINT32_MAX;
	}
}

Value CanonicalizeSampleAdjustDword3(Value value) {
	for (;;) {
		value            = value.Resolve();
		const auto* inst = value.TryInstruction();
		if (inst == nullptr || inst->GetOpcode() != ValueOpcode::BitwiseOr32) {
			return value;
		}
		const auto left           = inst->Arg(0).Resolve();
		const auto right          = inst->Arg(1).Resolve();
		const bool left_reserved  = (PossibleU32Bits(left) & ~SamplerDword3ReservedMask) == 0;
		const bool right_reserved = (PossibleU32Bits(right) & ~SamplerDword3ReservedMask) == 0;
		if (left_reserved && right_reserved) {
			return Value(0u);
		}
		if (left_reserved) {
			value = right;
		} else if (right_reserved) {
			value = left;
		} else {
			return value;
		}
	}
}

uint32_t ByteExtent(const MemoryInfo& memory) {
	const auto bytes = std::max((memory.data_bits + 7u) / 8u, 1u);
	const auto count = std::max(memory.data_dwords, 1u);
	const auto end   = static_cast<uint64_t>(memory.offset) + static_cast<uint64_t>(bytes) * count;
	return end > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(end);
}

class Tracker {
public:
	explicit Tracker(Program& program): m_program(program), m_info(program.info) {
		m_info.buffers.clear();
		m_info.images.clear();
		m_info.samplers.clear();
		m_info.sampled_pairs.clear();
		m_info.uses_dma   = false;
		m_info.writes_dma = false;
	}

	void Run() {
		if (m_program.resource_tracking_complete) {
			Fail(0, "resources already tracked");
		}
		if (!m_program.srt_plan_complete) {
			Fail(0, "SRT plan is not ready");
		}
		PlanIndirectImages();
		for (auto* block: m_program.blocks) {
			for (auto& inst: *block) {
				Collect(inst);
			}
		}
		LinkImageAliases();
		for (const auto& patch: m_handle_patches) {
			patch.handle->SetFlags<uint32_t>(patch.resource);
		}
		for (const auto& patch: m_memory_patches) {
			auto& memory    = m_program.memory_info[patch.index];
			memory.resource = patch.resource;
			if (patch.has_sampler) {
				memory.sampler = patch.sampler;
			}
		}
		for (const auto& plan: m_indirect_images) {
			plan.handle->SetArg(0, plan.key);
			for (uint32_t dword = 0; dword < 4u; dword++) {
				plan.handle->SetArg(dword + 1u, plan.roots[dword + 4u]);
			}
			for (uint32_t dword = 5u; dword < plan.roots.size(); dword++) {
				plan.handle->SetArg(dword, plan.key);
			}
			RetireIndirectReads(plan.memory, plan.exclusive, plan.read_count);
		}
		for (const auto& plan: m_indirect_samplers) {
			// A sampler handle's dwords are never read again -- the resource
			// index in its flags is what the emitter uses -- so point them at
			// the key and let the descriptor reads die with it.
			for (uint32_t dword = 0; dword < 4u; dword++) {
				plan.handle->SetArg(dword, plan.key);
			}
			RetireIndirectReads(plan.memory, plan.exclusive, 4u);
		}
		std::erase_if(m_program.dynamic_reads, [&](Value value) {
			const auto* inst = value.Resolve().TryInstruction();
			return IsRetiredIndirectRead(inst);
		});
		m_program.descriptor_sources         = std::move(m_sources);
		m_program.info                       = std::move(m_info);
		m_program.resource_tracking_complete = true;
	}

private:
	struct HandlePatch {
		Inst*    handle   = nullptr;
		uint32_t resource = 0;
	};

	struct MemoryPatch {
		uint32_t index       = 0;
		uint32_t resource    = 0;
		uint32_t sampler     = 0;
		bool     has_sampler = false;
	};

	struct IndirectImagePlan {
		Inst*                      handle     = nullptr;
		uint32_t                   read_count = 8;
		uint32_t                   source     = 0;
		Value                      key;
		std::array<Value, 8>       roots {};
		std::array<uint32_t, 8>    memory {};
		std::array<const Inst*, 8> reads {};
		std::array<bool, 8>        exclusive {};
	};

	// A sampler selected by the same kind of runtime key. It is planned on its
	// own rather than as part of an image's plan, because several sample sites
	// share one image descriptor while naming different samplers.
	struct IndirectSamplerPlan {
		Inst*                      handle = nullptr;
		uint32_t                   source = 0;
		Value                      key;
		std::array<uint32_t, 4>    memory {};
		std::array<const Inst*, 4> reads {};
		std::array<bool, 4>        exclusive {};
	};

	[[noreturn]] void Fail(uint32_t pc, const std::string& reason) const {
		const auto message = fmt::format("shader resource tracking: pc=0x{:08x} {}\n {}", pc, reason,
		                                 DescribeProvenance(m_program.origin));
		EXIT("%s", message.c_str());
		std::abort();
	}

	void MakeSource(const Inst& handle, uint32_t width, bool sampler, bool sample_adjust,
	                DescriptorSource& descriptor, uint32_t pc) const {
		if (handle.NumArgs() != width) {
			Fail(pc, fmt::format("{} has {} descriptor dwords, expected {}",
			                     ValueOpcodeName(handle.GetOpcode()), handle.NumArgs(), width));
		}
		descriptor.dword_count = width;
		for (uint32_t i = 0; i < width; i++) {
			descriptor.dwords[i] = handle.Arg(i).Resolve();
		}
		if (sample_adjust) {
			descriptor.dwords[3] = CanonicalizeSampleAdjustDword3(descriptor.dwords[3]);
		}
		const auto dword0 = descriptor.dwords[0].Resolve();
		if (sampler && dword0.IsImmediate() && dword0.GetType() == Type::U32 &&
		    (dword0.U32() & SamplerBorderClampMask) == 0) {
			// Border color and its table index are unused unless a clamp axis selects border mode.
			descriptor.dwords[3] = Value(0u);
		}
	}

	bool ValidateSource(const DescriptorSource& descriptor, uint32_t& bad_dword) const {
		for (uint32_t i = 0; i < descriptor.dword_count; i++) {
			bad_dword = i;
			if (descriptor.dwords[i].Resolve().GetType() != Type::U32) {
				return false;
			}
			if (!ValidateRuntimeValue(m_program, descriptor.dwords[i])) {
				return false;
			}
		}
		return true;
	}

	uint32_t InternSource(const DescriptorSource& descriptor) {
		for (uint32_t candidate = 0; candidate < m_sources.size(); candidate++) {
			const auto& current = m_sources[candidate];
			if (current.dword_count != descriptor.dword_count ||
			    current.indirect_image != descriptor.indirect_image) {
				continue;
			}
			bool same = true;
			for (uint32_t i = 0; i < descriptor.dword_count; i++) {
				same = same && EquivalentValue(m_program, current.dwords[i], descriptor.dwords[i]);
			}
			if (same) {
				return candidate;
			}
		}
		m_sources.push_back(descriptor);
		return static_cast<uint32_t>(m_sources.size() - 1);
	}

	static bool ImmediateU32(Value value, uint32_t& result) {
		value = value.Resolve();
		if (!value.IsImmediate() || value.GetType() != Type::U32) {
			return false;
		}
		result = value.U32();
		return true;
	}

	static bool UsesOnly(const Inst& value, std::span<const Inst* const> users) {
		return !value.Uses().empty() && std::ranges::all_of(value.Uses(), [&](const Use& use) {
			return std::ranges::find(users, use.user) != users.end();
		});
	}

	// A scalar dword read, through a buffer descriptor or through a raw 64-bit
	// pointer. Both are a base an offset is added to, and a descriptor table is
	// reached either way.
	const MemoryInfo* ScalarReadMemory(const Inst& read, uint32_t& index,
	                                   bool* is_address = nullptr) const {
		const auto op = read.GetOpcode();
		if ((op != ValueOpcode::ReadConstBuffer && op != ValueOpcode::LoadAddressU32) ||
		    read.NumArgs() < 2u) {
			return nullptr;
		}
		index = read.Flags<MemoryFlags>().index;
		if (index >= m_program.memory_info.size()) {
			return nullptr;
		}
		const auto& memory = m_program.memory_info[index];
		if (memory.data_bits != 32u || memory.data_dwords != 1u) {
			return nullptr;
		}
		const bool address = op == ValueOpcode::LoadAddressU32;
		const auto expected =
		    address ? ResourceKind::ScalarAddress : ResourceKind::ScalarBuffer;
		if (memory.kind != expected) {
			return nullptr;
		}
		if (is_address != nullptr) {
			*is_address = address;
		}
		return &memory;
	}

	bool MemoryIndexBelongsTo(uint32_t index, const Inst& owner) const {
		for (const auto* block: m_program.blocks) {
			for (const auto& inst: *block) {
				const auto op = inst.GetOpcode();
				if ((BufferAccessOf(op) == BufferAccess::None &&
				     AddressOpcodeInfoOf(op).access == AddressAccess::None &&
				     ImageOpcodeInfoOf(op).access == ImageAccess::None) ||
				    &inst == &owner) {
					continue;
				}
				if (inst.Flags<MemoryFlags>().index == index) {
					return false;
				}
			}
		}
		return true;
	}

	bool MakeRuntimeBufferSource(const Inst& handle, uint32_t pc, uint32_t& source,
	                             DescriptorSource& descriptor) {
		if (handle.GetOpcode() != ValueOpcode::GetBufferResource) {
			return false;
		}
		MakeSource(handle, 4u, false, false, descriptor, pc);
		uint32_t bad_dword = 0;
		if (!ValidateSource(descriptor, bad_dword)) {
			return false;
		}
		source = InternSource(descriptor);
		return true;
	}

	// The base a descriptor table is read from: a four-dword buffer descriptor
	// or a two-dword pointer, padded to four so both occupy the same slot in the
	// lookup's own source.
	bool MakeRuntimeTableSource(const Inst& handle, uint32_t pc, bool& is_address,
	                            uint32_t& source, DescriptorSource& descriptor) {
		uint32_t width = 0;
		if (handle.GetOpcode() == ValueOpcode::GetBufferResource) {
			width      = 4u;
			is_address = false;
		} else if (handle.GetOpcode() == ValueOpcode::GetAddressResource) {
			width      = 2u;
			is_address = true;
		} else {
			return false;
		}
		MakeSource(handle, width, false, false, descriptor, pc);
		uint32_t bad_dword = 0;
		if (!ValidateSource(descriptor, bad_dword)) {
			return false;
		}
		source = InternSource(descriptor);
		descriptor.dwords[2] = Value(0u);
		descriptor.dwords[3] = Value(0u);
		return true;
	}

	// Matches `index * stride + offset` however the shader spelled it. A compiler
	// emits the multiply as a shift whenever the stride is a power of two, and
	// folds the constant term in either order, so all of those are the same
	// addressing pattern and none of them says anything about the index itself.
	bool MatchScaledOffset(Value value, Value& index, uint32_t& stride, uint32_t& offset) const {
		value           = value.Resolve();
		offset          = 0;
		auto* candidate = value.TryInstruction();
		if (candidate != nullptr && candidate->GetOpcode() == ValueOpcode::IAdd32 &&
		    candidate->NumArgs() == 2u) {
			uint32_t immediate = 0;
			if (ImmediateU32(candidate->Arg(0), immediate)) {
				value = candidate->Arg(1).Resolve();
			} else if (ImmediateU32(candidate->Arg(1), immediate)) {
				value = candidate->Arg(0).Resolve();
			} else {
				return false;
			}
			offset = immediate;
		}
		const auto* scale = value.TryInstruction();
		if (scale == nullptr || scale->NumArgs() != 2u) {
			return false;
		}
		if (scale->GetOpcode() == ValueOpcode::IMul32) {
			if (ImmediateU32(scale->Arg(0), stride)) {
				index = scale->Arg(1).Resolve();
			} else if (ImmediateU32(scale->Arg(1), stride)) {
				index = scale->Arg(0).Resolve();
			} else {
				return false;
			}
		} else if (scale->GetOpcode() == ValueOpcode::ShiftLeftLogical32) {
			uint32_t shift = 0;
			if (!ImmediateU32(scale->Arg(1), shift) || shift >= 32u) {
				return false;
			}
			stride = uint32_t {1} << shift;
			index  = scale->Arg(0).Resolve();
		} else {
			return false;
		}
		return stride != 0u && index.TryInstruction() != nullptr;
	}

	// A descriptor the shader looks up at runtime, in the shape both images and
	// samplers arrive in:
	//     key        = material[selector * selector_stride + selector_offset]
	//     descriptor = heap[key * heap_stride + heap_offset]
	// Nothing in that shape says what the descriptor turns out to be, so an image
	// and the sampler beside it are matched by the same code and each ends up
	// with its own source over its own pair of buffers.
	struct IndirectDescriptorMatch {
		DescriptorSource           source;
		Value                      key;
		std::array<uint32_t, 8>    memory {};
		std::array<const Inst*, 8> reads {};
		std::array<bool, 8>        exclusive {};
	};

	// A key that is a loop counter takes only the values the loop lets it reach.
	// The counter is a phi that starts at zero and is stepped by one through
	// however many phis the structurizer left between the increment and the
	// header; the bound is whatever constant the loop compares it against. That
	// is a real bound on the lookup, and the only one available when the
	// descriptors sit behind a raw pointer.
	bool MatchCountedKeyLimit(Value key, uint32_t& limit) const {
		const auto* counter = key.Resolve().TryInstruction();
		if (counter == nullptr || counter->GetOpcode() != ValueOpcode::Phi) {
			return false;
		}

		// Every value the counter's own definition passes through, so the
		// comparison can name any of them.
		std::vector<const Inst*> chain;
		std::vector<const Inst*> pending {counter};
		bool                     starts_at_zero = false;
		while (!pending.empty()) {
			const auto* current = pending.back();
			pending.pop_back();
			if (std::ranges::find(chain, current) != chain.end()) {
				continue;
			}
			if (chain.size() >= MaxCountedKeyChain) {
				return false;
			}
			chain.push_back(current);
			for (size_t index = 0; index < current->NumArgs(); index++) {
				const auto arg = current->Arg(index).Resolve();
				uint32_t   immediate = 0;
				if (ImmediateU32(arg, immediate)) {
					if (immediate == 0u && current->GetOpcode() == ValueOpcode::Phi) {
						starts_at_zero = true;
						continue;
					}
					if (immediate == 1u && current->GetOpcode() == ValueOpcode::IAdd32) {
						continue;
					}
					return false;
				}
				const auto* next = arg.TryInstruction();
				if (next == nullptr || (next->GetOpcode() != ValueOpcode::Phi &&
				                        next->GetOpcode() != ValueOpcode::IAdd32)) {
					return false;
				}
				pending.push_back(next);
			}
		}
		if (!starts_at_zero) {
			return false;
		}

		// The loop's own test against a constant is the bound. Take the
		// smallest one any of the counter's values is compared against, so a
		// second test inside the body cannot widen the set.
		bool     found = false;
		uint32_t bound = 0;
		for (const auto* block: m_program.blocks) {
			for (const auto& inst: *block) {
				const auto opcode = inst.GetOpcode();
				const bool inclusive = opcode == ValueOpcode::SLessThanEqual32 ||
				                       opcode == ValueOpcode::ULessThanEqual32;
				if (opcode != ValueOpcode::SLessThan32 && opcode != ValueOpcode::ULessThan32 &&
				    !inclusive) {
					continue;
				}
				const auto* counted = inst.Arg(0).Resolve().TryInstruction();
				uint32_t    against = 0;
				if (counted == nullptr || std::ranges::find(chain, counted) == chain.end() ||
				    !ImmediateU32(inst.Arg(1), against)) {
					continue;
				}
				if (inclusive) {
					if (against == UINT32_MAX) {
						continue;
					}
					against++;
				}
				bound = found ? std::min(bound, against) : against;
				found = true;
			}
		}
		if (!found || bound == 0u) {
			return false;
		}
		limit = bound;
		return true;
	}

	bool TryMatchIndirectDescriptor(const Inst& handle, uint32_t read_count, uint32_t pc,
	                                IndirectDescriptorMatch& match) {
		Inst*    heap_handle = nullptr;
		Value    heap_offset;
		uint32_t heap_base = 0;

		const std::array<const Inst*, 1> handle_users {&handle};
		for (uint32_t dword = 0; dword < read_count; dword++) {
			auto* read = handle.Arg(dword).Resolve().TryInstruction();
			if (read == nullptr) {
				return false;
			}
			uint32_t    memory_index = 0;
			const auto* memory       = ScalarReadMemory(*read, memory_index);
			if (memory == nullptr || !MemoryIndexBelongsTo(memory_index, *read)) {
				return false;
			}
			if (read->NumArgs() < 2u) {
				return false;
			}
			// The descriptor is read as one consecutive run; where inside the
			// record that run starts is the guest's business, not ours.
			if (dword == 0u) {
				heap_base = memory->offset;
			} else if (memory->offset != heap_base + dword * sizeof(uint32_t)) {
				return false;
			}
			auto* current_handle = read->Arg(0).Resolve().TryInstruction();
			if (current_handle == nullptr ||
			    (heap_handle != nullptr && current_handle != heap_handle)) {
				return false;
			}
			heap_handle = current_handle;
			if (dword == 0u) {
				heap_offset = read->Arg(1).Resolve();
			} else if (!EquivalentValue(m_program, heap_offset, read->Arg(1))) {
				return false;
			}
			match.memory[dword] = memory_index;
			match.reads[dword]  = read;
			// A dword something else also consumes stays a real load. Only one this
			// handle owns outright stops being emitted once the lookup is planned.
			match.exclusive[dword] = UsesOnly(*read, handle_users);
		}

		Value    key_value;
		uint32_t heap_stride = 0;
		uint32_t heap_extra  = 0;
		if (!MatchScaledOffset(heap_offset, key_value, heap_stride, heap_extra)) {
			return false;
		}

		// The key a record yields is often a byte or a half inside the dword it
		// was loaded with, so a mask sits between the record and the index.
		// Enumerating the masked value keeps the key the table was built from
		// and the key the shader computes the same number.
		uint32_t key_mask       = 0xffffffffu;
		Value    material_value = key_value;
		if (auto* masked = key_value.TryInstruction();
		    masked != nullptr && masked->GetOpcode() == ValueOpcode::BitwiseAnd32 &&
		    masked->NumArgs() == 2u) {
			uint32_t immediate = 0;
			if (ImmediateU32(masked->Arg(0), immediate)) {
				material_value = masked->Arg(1).Resolve();
				key_mask       = immediate;
			} else if (ImmediateU32(masked->Arg(1), immediate)) {
				material_value = masked->Arg(0).Resolve();
				key_mask       = immediate;
			}
		}

		DescriptorSource heap_source;
		uint32_t         heap_source_index = 0;
		bool             heap_is_address   = false;
		if (!MakeRuntimeTableSource(*heap_handle, pc, heap_is_address, heap_source_index,
		                            heap_source)) {
			return false;
		}

		// A key read out of a bounded buffer gives the exact set of keys this
		// dispatch can produce. Anything else -- a loop counter, a thread id --
		// is still bounded by the heap: past its end there is no descriptor to
		// read. The second form needs a heap that states its own extent, so a
		// raw pointer only works with a key table.
		auto             material_source_index = DescriptorSource::IndirectImage::NoKeyTable;
		uint32_t         selector_stride       = 0;
		uint32_t         selector_offset       = 0;
		DescriptorSource material_source;
		uint32_t         material_memory_index = 0;
		bool             material_is_address   = false;
		auto*            material_read         = material_value.TryInstruction();
		const auto*      material_memory =
		    material_read != nullptr
		                 ? ScalarReadMemory(*material_read, material_memory_index, &material_is_address)
		                 : nullptr;
		if (material_memory != nullptr && !material_is_address && material_read->NumArgs() >= 2u &&
		    MemoryIndexBelongsTo(material_memory_index, *material_read)) {
			auto* material_handle = material_read->Arg(0).Resolve().TryInstruction();
			Value selector;
			if (material_handle != nullptr &&
			    MatchScaledOffset(material_read->Arg(1), selector, selector_stride,
			                      selector_offset) &&
			    MakeRuntimeBufferSource(*material_handle, pc, material_source_index,
			                            material_source)) {
				selector_offset += material_memory->offset;
			} else {
				material_source_index = DescriptorSource::IndirectImage::NoKeyTable;
			}
		}
		uint32_t key_limit = 0;
		if (material_source_index == DescriptorSource::IndirectImage::NoKeyTable) {
			// A buffer heap states its own extent, which bounds the keys on its
			// own. A raw pointer does not, so the key has to bound itself.
			if (heap_is_address && !MatchCountedKeyLimit(key_value, key_limit)) {
				return false;
			}
			selector_stride = 0;
			selector_offset = 0;
			material_source.dwords.fill(Value(0u));
		}

		match.source.dword_count = 8u;
		std::copy(material_source.dwords.begin(), material_source.dwords.begin() + 4u,
		          match.source.dwords.begin());
		std::copy(heap_source.dwords.begin(), heap_source.dwords.begin() + 4u,
		          match.source.dwords.begin() + 4u);
		match.source.indirect_image = DescriptorSource::IndirectImage {
		    .material_source = material_source_index,
		    .heap_source     = heap_source_index,
		    .selector_stride = selector_stride,
		    .selector_offset = selector_offset,
		    .key_arg         = 0u,
		    .heap_stride     = heap_stride,
		    .heap_offset     = heap_extra + heap_base,
		    .dword_count     = read_count,
		    .key_mask        = key_mask,
		    .heap_is_address = heap_is_address,
		    .key_limit       = key_limit,
		};
		match.key = key_value;
		return true;
	}

	bool TryMakeIndirectImage(Inst& handle, uint32_t pc, IndirectImagePlan& plan) {
		if (handle.GetOpcode() != ValueOpcode::GetImageResource || handle.NumArgs() != 8u) {
			return false;
		}

		// An r128 image descriptor is four dwords; GetImageResource always has
		// eight arguments and the translator pads the tail with zero.
		uint32_t   read_count = 8u;
		const auto padded     = [&](uint32_t dword) {
			uint32_t immediate = 0;
			return ImmediateU32(handle.Arg(dword), immediate) && immediate == 0u;
		};
		if (padded(4u) && padded(5u) && padded(6u) && padded(7u)) {
			read_count = 4u;
		}

		IndirectDescriptorMatch match;
		if (!TryMatchIndirectDescriptor(handle, read_count, pc, match)) {
			return false;
		}
		plan.handle     = &handle;
		plan.read_count = read_count;
		plan.source     = InternSource(match.source);
		plan.key        = match.key;
		plan.roots      = match.source.dwords;
		plan.memory     = match.memory;
		plan.reads      = match.reads;
		plan.exclusive  = match.exclusive;
		return true;
	}

	bool TryMakeIndirectSampler(Inst& handle, uint32_t pc, IndirectSamplerPlan& plan) {
		if (handle.GetOpcode() != ValueOpcode::GetSamplerResource || handle.NumArgs() != 4u) {
			return false;
		}
		IndirectDescriptorMatch match;
		if (!TryMatchIndirectDescriptor(handle, 4u, pc, match)) {
			return false;
		}
		plan.handle = &handle;
		plan.source = InternSource(match.source);
		plan.key    = match.key;
		for (uint32_t dword = 0; dword < 4u; dword++) {
			plan.memory[dword]    = match.memory[dword];
			plan.reads[dword]     = match.reads[dword];
			plan.exclusive[dword] = match.exclusive[dword];
		}
		return true;
	}

	const IndirectImagePlan* FindIndirectImage(const Inst& handle) const {
		const auto found =
		    std::find_if(m_indirect_images.begin(), m_indirect_images.end(),
		                 [&](const IndirectImagePlan& plan) {
			    return plan.handle == &handle;
		    });
		return found == m_indirect_images.end() ? nullptr : &*found;
	}

	const IndirectSamplerPlan* FindIndirectSampler(const Inst& handle) const {
		const auto found = std::find_if(m_indirect_samplers.begin(), m_indirect_samplers.end(),
		                                [&](const IndirectSamplerPlan& plan) {
			return plan.handle == &handle;
		});
		return found == m_indirect_samplers.end() ? nullptr : &*found;
	}

	template <size_t N, size_t M>
	void RetireIndirectReads(const std::array<uint32_t, N>& memory,
	                         const std::array<bool, M>& exclusive, uint32_t count) {
		for (uint32_t dword = 0; dword < count; dword++) {
			if (exclusive[dword]) {
				m_program.memory_info[memory[dword]].planning_only = true;
			}
		}
	}

	template <size_t N, size_t M>
	static bool Retired(const std::array<const Inst*, N>& reads,
	                    const std::array<bool, M>& exclusive, uint32_t count, const Inst* inst) {
		for (uint32_t dword = 0; dword < count; dword++) {
			if (exclusive[dword] && reads[dword] == inst) {
				return true;
			}
		}
		return false;
	}

	bool IsRetiredIndirectRead(const Inst* inst) const {
		return std::any_of(m_indirect_images.begin(), m_indirect_images.end(),
		                   [&](const IndirectImagePlan& plan) {
			       return Retired(plan.reads, plan.exclusive, plan.read_count, inst);
		       }) ||
		       std::any_of(m_indirect_samplers.begin(), m_indirect_samplers.end(),
		                   [&](const IndirectSamplerPlan& plan) {
			       return Retired(plan.reads, plan.exclusive, 4u, inst);
		       });
	}

	bool IsIndirectPlanningMemory(uint32_t index) const {
		const auto retired = [&]<size_t N, size_t M>(const std::array<uint32_t, N>& memory,
		                                             const std::array<bool, M>& exclusive,
		                                             uint32_t                   count) {
			for (uint32_t dword = 0; dword < count; dword++) {
				if (exclusive[dword] && memory[dword] == index) {
					return true;
				}
			}
			return false;
		};
		return std::any_of(m_indirect_images.begin(), m_indirect_images.end(),
		                   [&](const IndirectImagePlan& plan) {
			       return retired(plan.memory, plan.exclusive, plan.read_count);
		       }) ||
		       std::any_of(m_indirect_samplers.begin(), m_indirect_samplers.end(),
		                   [&](const IndirectSamplerPlan& plan) {
			       return retired(plan.memory, plan.exclusive, 4u);
		       });
	}

	void PlanIndirectImages() {
		for (auto* block: m_program.blocks) {
			for (auto& inst: *block) {
				const auto image_info = ImageOpcodeInfoOf(inst.GetOpcode());
				if (image_info.access == ImageAccess::None || inst.NumArgs() == 0u) {
					continue;
				}
				const auto pc     = inst.Flags<MemoryFlags>().pc;
				auto*      handle = inst.Arg(0).Resolve().TryInstruction();
				if (handle != nullptr && FindIndirectImage(*handle) == nullptr) {
					IndirectImagePlan plan;
					if (TryMakeIndirectImage(*handle, pc, plan)) {
						m_indirect_images.push_back(std::move(plan));
					}
				}
				if (!image_info.needs_sampler || inst.NumArgs() < 2u) {
					continue;
				}
				auto* sampler_handle = inst.Arg(1).Resolve().TryInstruction();
				if (sampler_handle == nullptr || FindIndirectSampler(*sampler_handle) != nullptr) {
					continue;
				}
				IndirectSamplerPlan sampler_plan;
				if (TryMakeIndirectSampler(*sampler_handle, pc, sampler_plan)) {
					m_indirect_samplers.push_back(std::move(sampler_plan));
				}
			}
		}
	}

	void GetHandle(Value value, ValueOpcode expected, uint32_t width, uint32_t pc, Inst*& handle,
	               uint32_t& source, bool sampler = false, bool sample_adjust = false) {
		handle = value.Resolve().TryInstruction();
		if (handle == nullptr || handle->GetOpcode() != expected) {
			Fail(pc, fmt::format("memory operation requires {}", ValueOpcodeName(expected)));
		}
		DescriptorSource descriptor;
		MakeSource(*handle, width, sampler, sample_adjust, descriptor, pc);
		uint32_t bad_dword = 0;
		if (expected == ValueOpcode::GetImageResource) {
			for (; bad_dword < descriptor.dword_count; bad_dword++) {
				const auto* value = descriptor.dwords[bad_dword].Resolve().TryInstruction();
				if (value != nullptr && value->GetOpcode() == ValueOpcode::ReadConstBuffer) {
					Fail(pc, fmt::format("{} dword {} is not a valid runtime value: {}",
					                     ValueOpcodeName(expected), bad_dword,
					                     ValueGraphToString(descriptor.dwords[bad_dword])));
				}
			}
			bad_dword = 0;
		}
		if (!ValidateSource(descriptor, bad_dword)) {
			Fail(pc, fmt::format("{} dword {} is not a valid runtime value: {}",
			                     ValueOpcodeName(expected), bad_dword,
			                     ValueGraphToString(descriptor.dwords[bad_dword])));
		}
		source = InternSource(descriptor);
	}

	void ValidateAddressHandle(Value value, uint32_t pc) const {
		const auto* handle = value.Resolve().TryInstruction();
		if (handle == nullptr || handle->GetOpcode() != ValueOpcode::GetAddressResource) {
			Fail(pc, "address operation requires GetAddressResource");
		}
		if (handle->NumArgs() != 2) {
			Fail(pc, "GetAddressResource must have two address dwords");
		}
	}

	uint32_t AddBuffer(uint32_t source, const MemoryInfo& memory, ValueOpcode op, uint32_t pc) {
		for (uint32_t i = 0; i < m_info.buffers.size(); i++) {
			if (m_info.buffers[i].source == source) {
				Merge(m_info.buffers[i], memory, op, pc);
				return i;
			}
		}
		if (m_info.buffers.size() >= ShaderInfo::MaxBuffers) {
			return UINT32_MAX;
		}
		BufferResource resource;
		resource.source       = source;
		resource.first_use_pc = pc;
		Merge(resource, memory, op, pc);
		m_info.buffers.push_back(resource);
		return static_cast<uint32_t>(m_info.buffers.size() - 1);
	}

	static void Merge(BufferResource& resource, const MemoryInfo& memory, ValueOpcode op,
	                  uint32_t pc) {
		const auto access        = BufferAccessOf(op);
		const bool atomic        = access == BufferAccess::Atomic;
		const bool write         = access == BufferAccess::Write || atomic;
		resource.first_use_pc    = std::min(resource.first_use_pc, pc);
		resource.max_byte_extent = std::max(resource.max_byte_extent, ByteExtent(memory));
		resource.read            = resource.read || !write || atomic;
		resource.written         = resource.written || write;
		resource.atomic          = resource.atomic || atomic;
		resource.formatted       = resource.formatted || memory.formatted;
		resource.scalar          = resource.scalar || op == ValueOpcode::ReadConstBuffer ||
		                           memory.kind == ResourceKind::ScalarBuffer;
	}

	uint32_t AddImage(uint32_t source, const MemoryInfo& memory, ValueOpcode op, uint32_t pc) {
		const auto resource_class = ImageOpcodeInfoOf(op).resource_class;
		const auto mip   = resource_class == ImageResourceClass::Storage && memory.image_has_mip
		                       ? ImageMipMode::DynamicStorage
		                       : ImageMipMode::None;
		const bool depth = (memory.image_sample_flags & Decoder::ImageSampleFlagCompare) != 0;
		for (uint32_t i = 0; i < m_info.images.size(); i++) {
			auto& image = m_info.images[i];
			if (image.source == source && image.resource_class == resource_class &&
			    image.dimension == memory.image_dimension && image.mip_mode == mip &&
			    image.depth_compare == depth && image.r128 == memory.image_r128) {
				Merge(image, op, pc);
				return i;
			}
		}
		if (m_info.images.size() >= ShaderInfo::MaxImages) {
			return UINT32_MAX;
		}
		ImageResource image;
		image.source         = source;
		image.first_use_pc   = pc;
		image.resource_class = resource_class;
		image.dimension      = memory.image_dimension;
		image.mip_mode       = mip;
		image.depth_compare  = depth;
		image.r128           = memory.image_r128;
		Merge(image, op, pc);
		m_info.images.push_back(image);
		return static_cast<uint32_t>(m_info.images.size() - 1);
	}

	static void Merge(ImageResource& image, ValueOpcode op, uint32_t pc) {
		const auto access  = ImageOpcodeInfoOf(op).access;
		const bool atomic  = access == ImageAccess::Atomic;
		const bool write   = access == ImageAccess::Write || atomic;
		image.first_use_pc = std::min(image.first_use_pc, pc);
		image.read         = image.read || !write || atomic;
		image.written      = image.written || write;
		image.atomic       = image.atomic || atomic;
	}

	uint32_t AddSampler(uint32_t source, uint32_t pc) {
		for (uint32_t i = 0; i < m_info.samplers.size(); i++) {
			if (m_info.samplers[i].source == source) {
				m_info.samplers[i].first_use_pc = std::min(m_info.samplers[i].first_use_pc, pc);
				return i;
			}
		}
		if (m_info.samplers.size() >= ShaderInfo::MaxSamplers) {
			return UINT32_MAX;
		}
		m_info.samplers.push_back({source, pc});
		return static_cast<uint32_t>(m_info.samplers.size() - 1);
	}

	void AddSampledPair(uint32_t image, uint32_t sampler, uint32_t pc) {
		for (auto& pair: m_info.sampled_pairs) {
			if (pair.image == image && pair.sampler == sampler) {
				pair.first_use_pc = std::min(pair.first_use_pc, pc);
				return;
			}
		}
		if (m_info.sampled_pairs.size() >= ShaderInfo::MaxSampledPairs) {
			Fail(pc, "sampled image/sampler pair limit exceeded");
		}
		m_info.sampled_pairs.push_back({image, sampler, pc});
	}

	void AddHandlePatch(Inst* handle, uint32_t resource, uint32_t pc) {
		for (const auto& patch: m_handle_patches) {
			if (patch.handle == handle) {
				if (patch.resource != resource) {
					Fail(pc, fmt::format("{} is reused with incompatible resource classes",
					                     ValueOpcodeName(handle->GetOpcode())));
				}
				return;
			}
		}
		m_handle_patches.push_back({handle, resource});
	}

	void AddMemoryPatch(uint32_t index, uint32_t resource, uint32_t sampler, bool has_sampler,
	                    uint32_t pc) {
		for (auto& patch: m_memory_patches) {
			if (patch.index != index) {
				continue;
			}
			if (patch.resource != resource ||
			    (has_sampler && patch.has_sampler && patch.sampler != sampler)) {
				Fail(pc, "memory metadata is reused with incompatible resources");
			}
			if (has_sampler) {
				patch.sampler     = sampler;
				patch.has_sampler = true;
			}
			return;
		}
		m_memory_patches.push_back({index, resource, sampler, has_sampler});
	}

	void Collect(Inst& inst) {
		const auto op           = inst.GetOpcode();
		const auto buffer       = BufferAccessOf(op);
		const auto address_info = AddressOpcodeInfoOf(op);
		const auto image_info   = ImageOpcodeInfoOf(op);
		if (buffer == BufferAccess::None && address_info.access == AddressAccess::None &&
		    image_info.access == ImageAccess::None) {
			return;
		}
		const auto flags = inst.Flags<MemoryFlags>();
		if (flags.index >= m_program.memory_info.size()) {
			Fail(flags.pc, fmt::format("memory metadata index {} is out of range", flags.index));
		}
		if (inst.NumArgs() == 0) {
			Fail(flags.pc, "memory operation has no resource handle");
		}
		const auto& memory = m_program.memory_info[flags.index];
		if (memory.planning_only || IsIndirectPlanningMemory(flags.index)) {
			return;
		}
		Inst*    handle   = nullptr;
		uint32_t source   = 0;
		uint32_t resource = 0;

		if (buffer != BufferAccess::None) {
			GetHandle(inst.Arg(0), ValueOpcode::GetBufferResource, 4, flags.pc, handle, source);
			resource = AddBuffer(source, memory, op, flags.pc);
			if (resource == UINT32_MAX) {
				Fail(flags.pc, "buffer resource limit exceeded");
			}
			AddHandlePatch(handle, resource, flags.pc);
			AddMemoryPatch(flags.index, resource, 0, false, flags.pc);
			return;
		}
		if (address_info.access != AddressAccess::None) {
			if (!IsAddressResourceKind(memory.kind)) {
				Fail(flags.pc, "address operation has invalid resource kind");
			}
			if (memory.kind == ResourceKind::Scratch) {
				handle = inst.Arg(0).Resolve().TryInstruction();
				if (handle == nullptr || handle->GetOpcode() != ValueOpcode::GetScratchResource ||
				    handle->NumArgs() != 0) {
					Fail(flags.pc, "scratch operation requires GetScratchResource");
				}
				if (m_program.scratch_dwords == 0) {
					Fail(flags.pc, "scratch operation requires a nonzero AGC per-thread size");
				}
				return;
			}
			ValidateAddressHandle(inst.Arg(0), flags.pc);
			m_info.uses_dma = true;
			if (address_info.access == AddressAccess::Write) {
				m_info.writes_dma = true;
			}
			return;
		}

		if (memory.kind != ResourceKind::Image ||
		    image_info.resource_class == ImageResourceClass::None) {
			Fail(flags.pc, "image operation has invalid resource kind");
		}
		handle               = inst.Arg(0).Resolve().TryInstruction();
		const auto* indirect = handle != nullptr ? FindIndirectImage(*handle) : nullptr;
		if (indirect != nullptr) {
			source = indirect->source;
		} else {
			GetHandle(inst.Arg(0), ValueOpcode::GetImageResource, 8, flags.pc, handle, source);
		}
		resource = AddImage(source, memory, op, flags.pc);
		if (resource == UINT32_MAX) {
			Fail(flags.pc, "image resource limit exceeded");
		}
		AddHandlePatch(handle, resource, flags.pc);
		uint32_t sampler = 0;
		if (image_info.needs_sampler) {
			if (inst.NumArgs() < 2) {
				Fail(flags.pc, "sampled image operation has no sampler handle");
			}
			Inst*      sampler_handle = nullptr;
			uint32_t   sampler_source = 0;
			const bool sample_adjust =
			    (memory.image_sample_flags & Decoder::ImageSampleFlagAdjust) != 0;
			auto*       candidate         = inst.Arg(1).Resolve().TryInstruction();
			const auto* indirect_sampler = candidate != nullptr ? FindIndirectSampler(*candidate)
			                                                    : nullptr;
			if (indirect_sampler != nullptr) {
				sampler_handle = candidate;
				sampler_source = indirect_sampler->source;
			} else {
				GetHandle(inst.Arg(1), ValueOpcode::GetSamplerResource, 4, flags.pc, sampler_handle,
				          sampler_source, true, sample_adjust);
			}
			sampler = AddSampler(sampler_source, flags.pc);
			if (sampler == UINT32_MAX) {
				Fail(flags.pc, "sampler resource limit exceeded");
			}
			AddHandlePatch(sampler_handle, sampler, flags.pc);
			AddSampledPair(resource, sampler, flags.pc);
		}
		AddMemoryPatch(flags.index, resource, sampler, image_info.needs_sampler, flags.pc);
	}

	const DescriptorSource* Source(uint32_t source) const {
		return source < m_sources.size() ? &m_sources[source] : nullptr;
	}

	void LinkImageAliases() {
		for (auto& buffer: m_info.buffers) {
			const auto* buffer_source = Source(buffer.source);
			if (buffer_source == nullptr || buffer_source->dword_count != 4) {
				continue;
			}
			for (uint32_t image = 0; image < m_info.images.size(); image++) {
				const auto* image_source = Source(m_info.images[image].source);
				if (image_source == nullptr || image_source->dword_count != 8 ||
				    image_source->indirect_image.has_value()) {
					continue;
				}
				bool alias = true;
				for (uint32_t dword = 0; dword < 4; dword++) {
					alias = alias && EquivalentValue(m_program, buffer_source->dwords[dword],
					                                 image_source->dwords[dword]);
				}
				if (alias) {
					buffer.image_alias = image;
					break;
				}
			}
		}
	}

	Program&                       m_program;
	ShaderInfo                     m_info;
	std::vector<DescriptorSource>  m_sources;
	std::vector<HandlePatch>       m_handle_patches;
	std::vector<MemoryPatch>       m_memory_patches;
	std::vector<IndirectImagePlan>   m_indirect_images;
	std::vector<IndirectSamplerPlan> m_indirect_samplers;
};

} // namespace

void TrackResources(Program& program) {
	Tracker(program).Run();
}

} // namespace Libs::Graphics::ShaderRecompiler::IR
