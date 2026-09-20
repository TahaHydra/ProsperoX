#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_FAULTMANAGER_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_FAULTMANAGER_H_

#include "common/abi.h"
#include "graphics/host_gpu/renderer/cache/streamBuffer.h"

#include <array>
#include <cstdint>
#include <functional>

namespace Libs::Graphics {

class BufferCache;

class FaultManager {
	static constexpr size_t MaxPendingFaults = 8;

public:
	FaultManager(GraphicContext& graphics, CommandScheduler& scheduler, BufferCache& buffer_cache);
	~FaultManager();
	KYTY_CLASS_NO_COPY(FaultManager);

	[[nodiscard]] Buffer* GetFaultBuffer() noexcept { return &m_fault_buffer; }
	// Answers whether a guest range is one the title gave the GPU. A page the
	// fault buffer names that lies outside every such range was produced by a
	// shader address that went nowhere, and acting on it would hand the GPU a
	// copy of memory the title keeps for itself.
	using MappedQuery = std::function<bool(uint64_t, uint64_t)>;
	void SetMappedQuery(MappedQuery query) { m_is_mapped = std::move(query); }
	// `process_writes` also scans the write bitmap, which only a shader that
	// stores through a flat address ever sets.
	void                  ProcessFaultBuffer(bool process_writes);

private:
	[[nodiscard]] bool IsGuestGpuMemory(uint64_t address, uint64_t size) const;

	GraphicContext&                            m_graphics;
	CommandScheduler&                          m_scheduler;
	BufferCache&                               m_buffer_cache;
	MappedQuery                                m_is_mapped;
	Buffer                                     m_fault_buffer;
	Buffer                                     m_download_buffer;
	std::array<uint64_t, MaxPendingFaults>      m_fault_areas {};
	uint32_t                                   m_current_area = 0;
	vk::DescriptorSetLayout                    m_fault_process_desc_layout = nullptr;
	vk::Pipeline                               m_fault_process_pipeline = nullptr;
	vk::PipelineLayout                         m_fault_process_pipeline_layout = nullptr;
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_FAULTMANAGER_H_
