// Original synthetic resource/presentation workload. No guest game or firmware input.
#define SDL_MAIN_HANDLED
#include "SDL.h"
#include "common/emulatorConfig.h"
#include "common/logging/log.h"
#include "common/subsystems.h"
#include "common/threads.h"
#include "graphics/guest_gpu/hardwareContext.h"
#include "graphics/host_gpu/renderer/renderContext.h"
#include "graphics/presentation/presenter.h"
#include "graphics/presentation/window/windowInternal.h"
#include "kernel/memory.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

using namespace Libs::Graphics;
static void Check(bool ok, const char* what) {
  if (!ok) { std::fprintf(stderr, "PHASE5_PRESENT_FAIL %s\n", what); std::abort(); }
}

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const unsigned seconds = argc == 2 ? static_cast<unsigned>(std::strtoul(argv[1], nullptr, 10)) : 10;
  Check(seconds >= 1 && seconds <= 7200, "duration must be 1..7200 seconds");
  Common::InitializeThreads();
  Common::Subsystems subsystems;
  subsystems.Initialize<Config::Lifecycle>();
  Config::ConfigOptions options;
  options.vulkan_validation_enabled = true;
  options.printf_direction = Config::OutputDirection::Console;
  Config::Load(options);
  subsystems.Initialize<Log::Lifecycle>();
  subsystems.Initialize<Libs::LibKernel::Memory::Lifecycle>();
  Check(SDL_InitSubSystem(SDL_INIT_VIDEO) == 0, "SDL video initialization");
  {
    WindowContext window;
    window.graphic_ctx.screen_width = 320;
    window.graphic_ctx.screen_height = 240;
    window.window = SDL_CreateWindow("ProsperoX Phase 5 validation", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, 320, 240, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
    Check(window.window != nullptr, "SDL Vulkan window");
    window.CreateVulkan();
    auto& graphics = window.graphic_ctx;
    const auto properties = graphics.GetPhysicalDeviceProperties();
    Check(properties.vendorID == 0x1002 && properties.deviceID == 0x747e, "requires real RX 7800 XT");
    Check(graphics.debug_messenger != nullptr, "validation layer must be enabled");
    std::printf("PHASE5_PRESENT_DEVICE name=%s vendor=%04x device=%04x driver=%u\n",
        properties.deviceName.data(), properties.vendorID, properties.deviceID, properties.driverVersion);
    auto& context = *window.render_context;
    context.InitializeGpu(nullptr);
    auto& scheduler = context.GetCommandScheduler();
    HW::Context registers{}; HW::UserConfig user{}; HW::Shader shaders{};
    scheduler.Begin(registers, user, shaders);
    auto& resources = context.GetGpuResources();
    auto& cache = resources.GetTextureCache();
    constexpr uint64_t base = 0x209000000ull, allocation_size = 0x10000;
    int64_t direct_offset = -1;
    Check(Libs::LibKernel::Memory::KernelAllocateDirectMemory(0,
        Libs::LibKernel::Memory::KernelGetDirectMemorySize(), allocation_size, allocation_size,
        0, &direct_offset) == 0, "allocate direct memory");
    void* mapped = reinterpret_cast<void*>(base);
    Check(Libs::LibKernel::Memory::KernelMapDirectMemory(&mapped, allocation_size, 3, 0x10,
        direct_offset, allocation_size) == 0 && mapped == reinterpret_cast<void*>(base), "map direct memory");
    TextureCache::ImageDesc desc{};
    desc.type = TextureCache::BindingType::Storage;
    desc.info.data = {base, 64 * 16 * 4};
    desc.info.pixel_format = vk::Format::eR8G8B8A8Unorm;
    desc.info.guest_format = Prospero::BufferFormat::k8_8_8_8UNorm;
    desc.info.extent = {64, 16, 1}; desc.info.pitch = 64; desc.info.bytes_per_block = 4;
    desc.info.mip_layout[0] = {0, 64 * 16 * 4, 64, 16};
    desc.view_info.format = desc.info.pixel_format;
    desc.view_info.usage = vk::ImageUsageFlagBits::eStorage;
    const auto start = std::chrono::steady_clock::now();
    auto report = start;
    uint64_t frames = 0, warm_bytes = 0, warm_count = 0, peak_bytes = 0, peak_count = 0;
    constexpr uint64_t growth_budget = 128ull * 1024 * 1024;
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(seconds)) {
      SDL_Event event; while (SDL_PollEvent(&event)) {}
      resources.MapMemory(base, allocation_size);
      std::memset(mapped, 0x5a, allocation_size);
      const auto id = cache.FindImage(desc);
      (void)cache.FindTexture(id, desc);
      auto& image = cache.GetImage(id);
      ImageViewInfo view = desc.view_info;
      view.usage = vk::ImageUsageFlagBits::eColorAttachment;
      const auto attachment = image.FindView(view);
      auto& command = scheduler.Current();
      image.Transit(vk::ImageLayout::eColorAttachmentOptimal, vk::AccessFlagBits2::eColorAttachmentWrite,
                    {}, command.Handle());
      vk::RenderingAttachmentInfo color{};
      color.imageView = attachment; color.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
      color.loadOp = vk::AttachmentLoadOp::eClear; color.storeOp = vk::AttachmentStoreOp::eStore;
      color.clearValue.color.float32 = std::array<float,4>{float(frames & 1), float((frames >> 1) & 1),
                                                          float((frames >> 2) & 1), 1.f};
      vk::RenderingInfo rendering{};
      rendering.renderArea.extent = vk::Extent2D{64,16}; rendering.layerCount = 1;
      rendering.colorAttachmentCount = 1; rendering.pColorAttachments = &color;
      command.Handle().beginRendering(&rendering);
      command.Handle().endRendering();
      cache.MarkGpuWritten(id);
      auto display = image.info;
      display.pixel_format = vk::Format::eR8G8B8A8Srgb;
      display.guest_format = Prospero::BufferFormat::k8_8_8_8Srgb;
      auto& frame = window.presenter->PrepareFrame(command, display);
      scheduler.Finish();
      window.presenter->Present(frame);
      Check(resources.HandleFault(PageFaultAccess::Read, base), "CPU publication");
      // Presentation completion is the oracle here. The image is intentionally
      // synthetic and its CPU backing may be invalidated by the transfer path.
      Check(static_cast<const uint8_t*>(mapped)[4096] == 0x5a, "padding sentinel");
      resources.UnmapMemory(base, allocation_size);
      resources.RunGarbageCollector();
      scheduler.Finish(); scheduler.DrainPriorityOperations();
      ++frames;
      VmaTotalStatistics stats{}; vmaCalculateStatistics(graphics.allocator, &stats);
      const auto bytes = stats.total.statistics.allocationBytes;
      const auto count = stats.total.statistics.allocationCount;
      if (frames == 60) { warm_bytes = bytes; warm_count = count; }
      if (frames >= 60) {
        peak_bytes = std::max(peak_bytes, bytes); peak_count = std::max(peak_count, uint64_t(count));
        Check(bytes <= warm_bytes + growth_budget && count <= warm_count + 128, "live allocation budget");
      }
      if (std::chrono::steady_clock::now() - report >= std::chrono::seconds(30)) {
        report = std::chrono::steady_clock::now();
        std::printf("PHASE5_PRESENT_PROGRESS seconds=%.1f frames=%llu live_bytes=%llu live_allocations=%u\n",
            std::chrono::duration<double>(report-start).count(), frames, bytes, count);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    Check(frames >= 60, "warmup completed");
    Check(Libs::LibKernel::Memory::KernelMunmap(base, allocation_size) == 0, "guest unmap");
    Check(Libs::LibKernel::Memory::KernelReleaseDirectMemory(direct_offset, allocation_size) == 0, "release direct memory");
    context.ShutdownGpu();
    std::printf("PHASE5_PRESENT_PASS seconds=%.3f frames=%llu warm_bytes=%llu peak_bytes=%llu "
        "warm_allocations=%llu peak_allocations=%llu budget_bytes=%llu\n", elapsed, frames,
        warm_bytes, peak_bytes, warm_count, peak_count, growth_budget);
  }
}
