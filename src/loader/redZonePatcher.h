// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef KYTY_LOADER_RED_ZONE_PATCHER_H_
#define KYTY_LOADER_RED_ZONE_PATCHER_H_

#include "common/common.h"

#include <span>
#include <vector>

namespace Loader {

struct RedZonePatchResult {
	uint64_t function_count                           = 0;
	uint64_t instruction_count                        = 0;
	uint64_t red_zone_function_count                  = 0;
	uint64_t memory_instruction_count                 = 0;
	uint64_t short_memory_instruction_count           = 0;
	uint64_t patched_memory_instruction_count         = 0;
	uint64_t stack_dependent_memory_instruction_count = 0;
	uint64_t control_flow_memory_instruction_count    = 0;
	uint64_t unrelocatable_memory_instruction_count   = 0;
	uint64_t indirect_red_zone_function_count         = 0;
};

void RegisterRedZonePatchModule(void* module_ptr, uint64_t module_size, void* trampoline_area_ptr,
                                uint64_t trampoline_area_size);
void UnregisterRedZonePatchModule(void* module_ptr);

// Replace one decoded TLS instruction with a jump. The trampoline reserves the
// guest red zone before its first stack write; helper is a state-preserving TCB getter.
bool PatchTlsInstruction(uint64_t address, uint8_t length, uint64_t helper,
                         uint8_t output_register, bool store, uint32_t immediate);

RedZonePatchResult PatchRedZoneMemoryInstructions(uint64_t segment_addr, uint64_t segment_size,
                                                  std::span<const uintptr_t> function_starts);

bool DecodeEhFrameFunctionStarts(uint64_t eh_frame_header_addr, uint64_t eh_frame_header_size,
                                 std::vector<uintptr_t>* function_starts);

} // namespace Loader

#endif /* KYTY_LOADER_RED_ZONE_PATCHER_H_ */
