#include "graphics/shader/recompiler/ir/passes/DynamicBuffers.h"

#include "graphics/shader/recompiler/ir/passes/SrtWalker.h"

#include "common/assert.h"

#include <algorithm>
#include <array>
#include <bit>

namespace Libs::Graphics::ShaderRecompiler::IR {
namespace {

struct BufferShape {
	ValueOpcode address_opcode = ValueOpcode::Void;
	uint32_t    data_bits      = 32;
	uint32_t    dwords         = 1;
	bool        store          = false;
};

BufferShape ShapeOf(ValueOpcode opcode) {
	switch (opcode) {
		case ValueOpcode::LoadBufferU8: return {ValueOpcode::LoadAddressU8, 8, 1, false};
		case ValueOpcode::LoadBufferU16: return {ValueOpcode::LoadAddressU16, 16, 1, false};
		case ValueOpcode::LoadBufferU32: return {ValueOpcode::LoadAddressU32, 32, 1, false};
		case ValueOpcode::LoadBufferU32x2: return {ValueOpcode::LoadAddressU32, 32, 2, false};
		case ValueOpcode::LoadBufferU32x3: return {ValueOpcode::LoadAddressU32, 32, 3, false};
		case ValueOpcode::LoadBufferU32x4: return {ValueOpcode::LoadAddressU32, 32, 4, false};

		case ValueOpcode::StoreBufferU8: return {ValueOpcode::StoreAddressU8, 8, 1, true};
		case ValueOpcode::StoreBufferU16: return {ValueOpcode::StoreAddressU16, 16, 1, true};
		case ValueOpcode::StoreBufferU32: return {ValueOpcode::StoreAddressU32, 32, 1, true};
		case ValueOpcode::StoreBufferU32x2: return {ValueOpcode::StoreAddressU32, 32, 2, true};
		case ValueOpcode::StoreBufferU32x3: return {ValueOpcode::StoreAddressU32, 32, 3, true};
		case ValueOpcode::StoreBufferU32x4: return {ValueOpcode::StoreAddressU32, 32, 4, true};
		// A scalar buffer load carries no index, no per-lane offset and no
		// predicate, so it is shaped here and handled on its own below.
		case ValueOpcode::ReadConstBuffer: return {ValueOpcode::LoadAddressU32, 32, 1, false};
		default: return {};
	}
}

ValueOpcode CompositeConstructFor(uint32_t dwords) {
	switch (dwords) {
		case 2u: return ValueOpcode::CompositeConstructU32x2;
		case 3u: return ValueOpcode::CompositeConstructU32x3;
		default: return ValueOpcode::CompositeConstructU32x4;
	}
}

ValueOpcode CompositeExtractFor(uint32_t dwords) {
	switch (dwords) {
		case 2u: return ValueOpcode::CompositeExtractU32x2;
		case 3u: return ValueOpcode::CompositeExtractU32x3;
		default: return ValueOpcode::CompositeExtractU32x4;
	}
}

bool DescriptorIsDynamic(const ResourcePlan& program, const Inst* handle) {
	if (handle == nullptr || handle->GetOpcode() != ValueOpcode::GetBufferResource ||
	    handle->NumArgs() != 4u) {
		return false;
	}
	for (size_t dword = 0; dword < handle->NumArgs(); dword++) {
		if (!ValidateRuntimeValue(program, handle->Arg(dword))) {
			return true;
		}
	}
	return false;
}

} // namespace

void LowerDynamicBuffers(Program& program) {
	if (!program.srt_plan_complete || program.resource_tracking_complete) {
		return;
	}
	for (auto* block: program.blocks) {
		for (auto it = block->begin(); it != block->end(); ++it) {
			auto&      inst  = *it;
			if (BufferAccessOf(inst.GetOpcode()) == BufferAccess::None || inst.NumArgs() == 0u) {
				continue;
			}
			const auto shape = ShapeOf(inst.GetOpcode());
			if (shape.address_opcode == ValueOpcode::Void) {
				if (DescriptorIsDynamic(program,
				                        inst.Arg(0).Resolve().TryInstruction())) {
					LOGF("dynamic buffer: %s has a runtime descriptor but no address form\n",
					     std::string(ValueOpcodeName(inst.GetOpcode())).c_str());
				}
				continue;
			}
			const bool scalar        = inst.GetOpcode() == ValueOpcode::ReadConstBuffer;
			const auto expected_args = scalar ? 2u : (shape.store ? 6u : 5u);
			if (inst.NumArgs() != expected_args) {
				continue;
			}
			auto* handle = inst.Arg(0).Resolve().TryInstruction();
			if (!DescriptorIsDynamic(program, handle)) {
				continue;
			}
			const auto flags = inst.Flags<MemoryFlags>();
			if (flags.index >= program.memory_info.size()) {
				continue;
			}
			const auto memory = program.memory_info[flags.index];
			// A typed access carries its format in the instruction and its
			// destination swizzle nowhere this pass can use, so it stays
			// unsupported and is still reported against the descriptor.
			if (memory.typed || (memory.formatted && (shape.store || scalar))) {
				LOGF("dynamic buffer: %s has a runtime descriptor but is typed=%d formatted=%d\n",
				     std::string(ValueOpcodeName(inst.GetOpcode())).c_str(), memory.typed ? 1 : 0,
				     memory.formatted ? 1 : 0);
				continue;
			}

			const auto emit = [&](ValueOpcode opcode, std::initializer_list<Value> args,
			                      uint64_t inst_flags = 0) {
				return Value(&*block->PrependNewInst(it, opcode, args, inst_flags));
			};

			const auto dword0  = handle->Arg(0).Resolve();
			const auto dword1  = handle->Arg(1).Resolve();
			const auto dword2  = handle->Arg(2).Resolve();
			const auto dword3  = handle->Arg(3).Resolve();
			const auto index   = memory.idxen && !scalar ? inst.Arg(1).Resolve() : Value(0u);
			const auto voffset = scalar ? inst.Arg(1).Resolve() : inst.Arg(2).Resolve();
			const auto soffset = scalar ? Value(0u) : inst.Arg(3).Resolve();
			const auto data    = shape.store ? inst.Arg(4).Resolve() : Value {};
			const auto active  = scalar         ? Value(true)
			                     : shape.store  ? inst.Arg(5).Resolve()
			                                    : inst.Arg(4).Resolve();

			// V#: the base is 48 bits across dword0 and the low half of dword1,
			// the stride is dword1[29:16], swizzling is dword1[31], and dword2 is
			// the record count.
			const auto base_high = emit(ValueOpcode::BitwiseAnd32, {dword1, Value(0xffffu)});
			const auto stride    = emit(
			       ValueOpcode::BitwiseAnd32,
			       {emit(ValueOpcode::ShiftRightLogical32, {dword1, Value(16u)}), Value(0x3fffu)});
			const auto swizzled_buffer =
			    emit(ValueOpcode::INotEqual32,
			         {emit(ValueOpcode::BitwiseAnd32, {dword1, Value(0x80000000u)}), Value(0u)});
			// The addressable extent is records of stride bytes, or plain bytes
			// when the descriptor declares no stride.
			const auto strided    = emit(ValueOpcode::IMul32, {stride, dword2});
			const auto has_stride = emit(ValueOpcode::INotEqual32, {stride, Value(0u)});
			const auto limit = emit(ValueOpcode::SelectU32, {has_stride, strided, dword2});

			const auto resource = emit(ValueOpcode::GetAddressResource, {dword0, base_high});
			// The record offset and the scalar offset go in on different sides
			// of the swizzle, so they are added separately.
			const auto record_offset =
			    emit(ValueOpcode::IAdd32, {voffset, Value(memory.offset)});

			// A swizzled buffer interleaves index_stride records of element_size
			// bytes; both come out of dword3 as exponents. Whether the buffer is
			// swizzled is a runtime bit, so both addressings are computed and the
			// descriptor picks.
			const auto element_log2 = emit(
			    ValueOpcode::IAdd32,
			    {emit(ValueOpcode::BitwiseAnd32,
			          {emit(ValueOpcode::ShiftRightLogical32, {dword3, Value(19u)}), Value(3u)}),
			     Value(1u)});
			const auto index_log2 = emit(
			    ValueOpcode::IAdd32,
			    {emit(ValueOpcode::BitwiseAnd32,
			          {emit(ValueOpcode::ShiftRightLogical32, {dword3, Value(21u)}), Value(3u)}),
			     Value(3u)});
			const auto element_mask = emit(
			    ValueOpcode::ISub32,
			    {emit(ValueOpcode::ShiftLeftLogical32, {Value(1u), element_log2}), Value(1u)});
			const auto index_mask = emit(
			    ValueOpcode::ISub32,
			    {emit(ValueOpcode::ShiftLeftLogical32, {Value(1u), index_log2}), Value(1u)});
			const auto index_msb =
			    emit(ValueOpcode::ShiftRightLogical32, {index, index_log2});
			const auto index_lsb = emit(ValueOpcode::BitwiseAnd32, {index, index_mask});

			const auto element_bytes = std::max(shape.data_bits / 8u, 1u);

			// One element of the access: where it lands in the buffer, and
			// whether the descriptor says that is inside it.
			struct Placement {
				Value address;
				Value in_range;
				Value predicate;
			};
			const auto place = [&](uint32_t element, uint32_t bytes) {
				const auto offset =
				    element == 0u
				        ? record_offset
				        : emit(ValueOpcode::IAdd32,
				               {record_offset, Value(element * bytes)});
				const auto linear = emit(ValueOpcode::IAdd32,
				                         {emit(ValueOpcode::IMul32, {index, stride}), offset});
				const auto offset_msb =
				    emit(ValueOpcode::ShiftRightLogical32, {offset, element_log2});
				const auto offset_lsb =
				    emit(ValueOpcode::BitwiseAnd32, {offset, element_mask});
				const auto swizzled = emit(
				    ValueOpcode::IAdd32,
				    {emit(ValueOpcode::IAdd32,
				          {emit(ValueOpcode::ShiftLeftLogical32,
				                {emit(ValueOpcode::IMul32, {index_msb, stride}), index_log2}),
				           emit(ValueOpcode::ShiftLeftLogical32,
				                {emit(ValueOpcode::ShiftLeftLogical32,
				                      {offset_msb, index_log2}),
				                 element_log2})}),
				     emit(ValueOpcode::IAdd32,
				          {emit(ValueOpcode::ShiftLeftLogical32, {index_lsb, element_log2}),
				           offset_lsb})});
				const auto record =
				    scalar ? linear
				           : emit(ValueOpcode::SelectU32, {swizzled_buffer, swizzled, linear});
				const auto address = emit(ValueOpcode::IAdd32, {record, soffset});

				// Out of range reads zero and drops writes, the way the
				// hardware's own bounds check does. A structured access is
				// bounded by the record count and the record size; a raw one by
				// the whole extent.
				const auto structured =
				    emit(ValueOpcode::LogicalAnd,
				         {emit(ValueOpcode::ULessThan32, {index, dword2}),
				          emit(ValueOpcode::ULessThanEqual32,
				               {emit(ValueOpcode::IAdd32, {offset, Value(bytes)}), stride})});
				const auto raw_range =
				    emit(ValueOpcode::ULessThanEqual32,
				         {emit(ValueOpcode::IAdd32, {record, Value(bytes)}), limit});
				const auto in_range =
				    memory.idxen && !scalar
				        ? emit(ValueOpcode::SelectU1, {has_stride, structured, raw_range})
				        : raw_range;
				return Placement {address, in_range,
				                  emit(ValueOpcode::LogicalAnd, {active, in_range})};
			};

			const auto load_dword = [&](const Placement& at, uint32_t element, uint32_t count) {
				auto component            = memory;
				component.kind            = ResourceKind::Global;
				component.resource        = 0;
				component.sampler         = 0;
				component.offset          = 0;
				component.data_dwords     = 1u;
				component.data_bits       = shape.data_bits;
				component.component_index = element;
				component.component_count = count;
				component.address_is_full = true;
				component.idxen           = false;
				component.offen           = false;
				component.typed           = false;
				component.formatted       = false;
				program.memory_info.push_back(component);
				const MemoryFlags new_flags {
				    .index = static_cast<uint32_t>(program.memory_info.size() - 1u),
				    .pc    = flags.pc};
				return emit(shape.address_opcode,
				            {resource, at.address, Value(0u), at.predicate},
				            std::bit_cast<uint64_t>(new_flags));
			};

			if (memory.formatted) {
				// The element is read as four raw dwords -- no known format is
				// wider -- and the descriptor's own format and swizzle fields
				// turn them into channels, because neither was resolvable when
				// the binding would have been specialized.
				std::array<Value, 4> words {};
				for (uint32_t element = 0; element < 4u; element++) {
					const auto at = place(element, sizeof(uint32_t));
					words[element] =
					    emit(ValueOpcode::SelectU32,
					         {at.in_range, load_dword(at, element, 4u), Value(0u)});
				}
				const auto format = emit(
				    ValueOpcode::BitwiseAnd32,
				    {emit(ValueOpcode::ShiftRightLogical32, {dword3, Value(12u)}), Value(0x7fu)});
				const auto swizzle =
				    emit(ValueOpcode::BitwiseAnd32, {dword3, Value(0xfffu)});
				const auto unpacked =
				    emit(ValueOpcode::UnpackBufferFormatU32x4,
				         {format, swizzle, words[0], words[1], words[2], words[3]});
				std::array<Value, 4> channels {};
				for (uint32_t channel = 0; channel < shape.dwords; channel++) {
					channels[channel] = emit(ValueOpcode::CompositeExtractU32x4,
					                         {unpacked, Value(channel)});
				}
				Value result = channels[0];
				if (shape.dwords == 2u) {
					result = emit(CompositeConstructFor(2u), {channels[0], channels[1]});
				} else if (shape.dwords == 3u) {
					result =
					    emit(CompositeConstructFor(3u), {channels[0], channels[1], channels[2]});
				} else if (shape.dwords == 4u) {
					result = emit(CompositeConstructFor(4u),
					              {channels[0], channels[1], channels[2], channels[3]});
				}
				inst.ReplaceUsesWith(result);
				inst.Invalidate();
				continue;
			}

			std::array<Value, 4> loaded {};
			for (uint32_t element = 0; element < shape.dwords; element++) {
				const auto at = place(element, element_bytes);
				if (shape.store) {
					const auto element_data =
					    shape.dwords == 1u
					        ? data
					        : emit(CompositeExtractFor(shape.dwords), {data, Value(element)});
					auto component            = memory;
					component.kind            = ResourceKind::Global;
					component.resource        = 0;
					component.sampler         = 0;
					component.offset          = 0;
					component.data_dwords     = 1u;
					component.data_bits       = shape.data_bits;
					component.component_index = element;
					component.component_count = shape.dwords;
					component.address_is_full = true;
					component.idxen           = false;
					component.offen           = false;
					component.typed           = false;
					component.formatted       = false;
					program.memory_info.push_back(component);
					const MemoryFlags new_flags {
					    .index = static_cast<uint32_t>(program.memory_info.size() - 1u),
					    .pc    = flags.pc};
					emit(shape.address_opcode,
					     {resource, at.address, Value(0u), element_data, at.predicate},
					     std::bit_cast<uint64_t>(new_flags));
					continue;
				}
				const auto value = load_dword(at, element, shape.dwords);
				loaded[element] =
				    shape.data_bits == 32u
				        ? emit(ValueOpcode::SelectU32, {at.in_range, value, Value(0u)})
				        : value;
			}

			if (!shape.store) {
				Value result = loaded[0];
				if (shape.dwords == 2u) {
					result = emit(CompositeConstructFor(2u), {loaded[0], loaded[1]});
				} else if (shape.dwords == 3u) {
					result = emit(CompositeConstructFor(3u), {loaded[0], loaded[1], loaded[2]});
				} else if (shape.dwords == 4u) {
					result = emit(CompositeConstructFor(4u),
					              {loaded[0], loaded[1], loaded[2], loaded[3]});
				}
				inst.ReplaceUsesWith(result);
			}
			inst.Invalidate();
		}
	}
}

} // namespace Libs::Graphics::ShaderRecompiler::IR
