#include "graphics/host_gpu/renderer/cache/faultManager.h"

#include "common/assert.h"
#include "common/logging/log.h"
#include "gpu_tiler_shaders/fault_buffer_process_spv.h"
#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/renderer/cache/bufferCache.h"
#include "graphics/host_gpu/renderer/commandScheduler.h"
#include "graphics/host_gpu/vulkanCommon.h"

#include <atomic>
#include <bit>
#include <cinttypes>
#include <cstring>
#include <limits>

namespace Libs::Graphics {

namespace {

constexpr size_t MaxPageFaults   = 1024;
constexpr size_t PageFaultListSize = MaxPageFaults * sizeof(uint64_t);
// Two lists per area: pages that could not be reached, and pages that were
// written. They are produced by the same shader over two bitmaps.
constexpr size_t PageFaultAreaSize = 2 * PageFaultListSize;

} // namespace

FaultManager::FaultManager(GraphicContext& graphics, CommandScheduler& scheduler,
                           BufferCache& buffer_cache)
    : m_graphics(graphics), m_scheduler(scheduler), m_buffer_cache(buffer_cache),
      // Two bitmaps over the same page space: reached-but-unmapped, and written.
      m_fault_buffer(graphics, scheduler, MemoryUsage::DeviceLocal, 0, AllFlags,
                     BufferCache::CACHING_NUMPAGES / 8 * 2),
      m_download_buffer(graphics, scheduler, MemoryUsage::Download, 0, AllFlags,
                        MaxPendingFaults * PageFaultAreaSize) {
	SetVulkanObjectNameF(m_graphics.device, m_fault_buffer.Handle(), "Fault Buffer");

	const vk::DescriptorSetLayoutBinding bindings[] {
	    {0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr},
	    {1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr},
	};
	vk::DescriptorSetLayoutCreateInfo layout_info {};
	layout_info.flags        = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR;
	layout_info.bindingCount = std::size(bindings);
	layout_info.pBindings    = bindings;
	RequireVulkanSuccess(
	    m_graphics.device.createDescriptorSetLayout(&layout_info, nullptr,
	                                                &m_fault_process_desc_layout),
	    "create fault-buffer descriptor layout");

	vk::ShaderModuleCreateInfo module_info {};
	module_info.codeSize = std::size(FAULT_BUFFER_PROCESS_SPV) * sizeof(uint32_t);
	module_info.pCode    = FAULT_BUFFER_PROCESS_SPV;
	vk::ShaderModule module = nullptr;
	RequireVulkanSuccess(m_graphics.device.createShaderModule(&module_info, nullptr, &module),
	                     "create fault-buffer shader module");

	vk::PipelineLayoutCreateInfo pipeline_layout_info {};
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts    = &m_fault_process_desc_layout;
	RequireVulkanSuccess(
	    m_graphics.device.createPipelineLayout(&pipeline_layout_info, nullptr,
	                                           &m_fault_process_pipeline_layout),
	    "create fault-buffer pipeline layout");

	vk::PipelineShaderStageCreateInfo stage {};
	stage.stage  = vk::ShaderStageFlagBits::eCompute;
	stage.module = module;
	stage.pName  = "main";
	vk::ComputePipelineCreateInfo pipeline_info {};
	pipeline_info.stage  = stage;
	pipeline_info.layout = m_fault_process_pipeline_layout;
	const auto result = m_graphics.device.createComputePipelines(
	    nullptr, 1, &pipeline_info, nullptr, &m_fault_process_pipeline);
	m_graphics.device.destroyShaderModule(module, nullptr);
	RequireVulkanSuccess(result, "create fault-buffer pipeline");
	SetVulkanObjectNameF(m_graphics.device, m_fault_process_pipeline, "Fault Buffer Parser");
}

FaultManager::~FaultManager() {
	m_graphics.device.destroyPipeline(m_fault_process_pipeline, nullptr);
	m_graphics.device.destroyPipelineLayout(m_fault_process_pipeline_layout, nullptr);
	m_graphics.device.destroyDescriptorSetLayout(m_fault_process_desc_layout, nullptr);
}

// A shader that computes a flat address out of registers it never initialised
// still records a page, and the page table spans the whole guest address space,
// so that page can name the title's own code or data. Building a GPU buffer
// over it would later write the GPU's copy back over memory the title owns,
// which is how a title ends up reading zero out of its own globals.
bool FaultManager::IsGuestGpuMemory(uint64_t address, uint64_t size) const {
	if (!m_is_mapped) {
		return true;
	}
	if (m_is_mapped(address, size)) {
		return true;
	}
	static std::atomic<uint32_t> log_count {0};
	if (log_count.fetch_add(1) < 64) {
		LOGF("Ignoring a faulted page outside GPU memory at 0x%016" PRIx64 ", size=0x%" PRIx64 "\n",
		     address, size);
	}
	return false;
}

void FaultManager::ProcessFaultBuffer(bool process_writes) {
	if (const auto wait_tick = m_fault_areas[m_current_area]; wait_tick != 0) {
		m_scheduler.Wait(wait_tick);
		m_scheduler.PopPendingOperations();
	}

	const auto offset = m_current_area * PageFaultAreaSize;
	auto*      mapped = m_download_buffer.Mapped().data() + offset;
	std::memset(mapped, 0, PageFaultAreaSize);
	m_download_buffer.Flush(offset, PageFaultAreaSize);

	const auto bitmap_bytes = m_fault_buffer.Size() / 2;

	vk::BufferMemoryBarrier2 pre_barrier {};
	pre_barrier.srcStageMask  = vk::PipelineStageFlagBits2::eAllCommands;
	pre_barrier.srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
	pre_barrier.dstStageMask  = vk::PipelineStageFlagBits2::eComputeShader;
	pre_barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
	pre_barrier.buffer        = m_fault_buffer.Handle();
	pre_barrier.offset        = 0;
	pre_barrier.size           = m_fault_buffer.Size();
	auto post_barrier         = pre_barrier;
	post_barrier.srcStageMask  = vk::PipelineStageFlagBits2::eComputeShader;
	post_barrier.srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
	post_barrier.dstStageMask  = vk::PipelineStageFlagBits2::eAllCommands;
	post_barrier.dstAccessMask = vk::AccessFlagBits2::eShaderWrite;

	m_scheduler.EndRendering();
	auto command = m_scheduler.Current().Handle();
	vk::DependencyInfo dependency {};
	dependency.dependencyFlags          = vk::DependencyFlagBits::eByRegion;
	dependency.bufferMemoryBarrierCount = 1;
	dependency.pBufferMemoryBarriers    = &pre_barrier;
	command.pipelineBarrier2(dependency);
	command.bindPipeline(vk::PipelineBindPoint::eCompute, m_fault_process_pipeline);

	// The shader turns one bitmap into one list and knows nothing about which
	// bitmap it was given, so the same pipeline compacts both halves.
	const auto compact = [&](uint64_t bitmap_offset, uint64_t list_offset) {
		const vk::DescriptorBufferInfo infos[] {
		    {m_fault_buffer.Handle(), bitmap_offset, bitmap_bytes},
		    {m_download_buffer.Handle(), offset + list_offset, PageFaultListSize},
		};
		std::array<vk::WriteDescriptorSet, 2> writes {};
		for (uint32_t index = 0; index < writes.size(); ++index) {
			writes[index].dstBinding      = index;
			writes[index].descriptorCount = 1;
			writes[index].descriptorType  = vk::DescriptorType::eStorageBuffer;
			writes[index].pBufferInfo     = &infos[index];
		}
		command.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute,
		                             m_fault_process_pipeline_layout, 0, writes);
		const auto num_threads    = BufferCache::CACHING_NUMPAGES / 32;
		const auto num_workgroups = (num_threads + 63) / 64;
		command.dispatch(static_cast<uint32_t>(num_workgroups), 1, 1);
	};
	compact(0, 0);
	if (process_writes) {
		compact(bitmap_bytes, PageFaultListSize);
	}

	dependency.pBufferMemoryBarriers = &post_barrier;
	command.pipelineBarrier2(dependency);

	const auto area = m_current_area;
	m_scheduler.DeferOperation([this, mapped, offset, area, process_writes] {
		m_download_buffer.Invalidate(offset, PageFaultAreaSize);
		const auto collect = [](const uint8_t* list) {
			RangeSet    ranges;
			const auto* entries = std::bit_cast<const uint64_t*>(list);
			const auto  count   = static_cast<uint32_t>(entries[0]);
			for (uint32_t index = 1; index <= count; ++index) {
				ranges.Add(entries[index], BufferCache::CACHING_PAGESIZE);
			}
			return ranges;
		};
		collect(mapped).ForEach([this](uint64_t start, uint64_t end) {
			EXIT_IF(end - start > std::numeric_limits<uint32_t>::max());
			if (!IsGuestGpuMemory(start, end - start)) {
				return;
			}
			LOGF("Accessed non-GPU cached memory at 0x%016" PRIx64 "\n", start);
			(void)m_buffer_cache.FindBuffer(start, end - start);
		});
		if (process_writes) {
			collect(mapped + PageFaultListSize).ForEach([this](uint64_t start, uint64_t end) {
				EXIT_IF(end - start > std::numeric_limits<uint32_t>::max());
				if (!IsGuestGpuMemory(start, end - start)) {
					return;
				}
				// The page now holds bytes only the GPU has.
				(void)m_buffer_cache.FindBuffer(start, end - start);
				m_buffer_cache.MarkGpuModified(start, end - start);
			});
		}
		m_fault_areas[area] = 0;
	});

	m_fault_areas[m_current_area++] = m_scheduler.CurrentTick();
	m_current_area %= MaxPendingFaults;
}

} // namespace Libs::Graphics
