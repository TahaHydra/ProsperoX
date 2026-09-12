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

#include <algorithm>
#include <array>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

using namespace Libs::Graphics;

struct StressState {
    uint64_t seed = 0;
    uint64_t frame = 0;
    uint32_t op = 0;
    uint64_t guest = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t delay_ms = 0;
};

static StressState g_state;

static uint64_t ParseNumber(const char* text, const char* what)
{
    char* end = nullptr;
    errno = 0;
    const auto value = std::strtoull(text, &end, 0);
    if (errno || end == text || *end != '\0' || *text == '-') {
        std::fprintf(stderr, "PHASE5_RANDOM_FAIL invalid %s: %s\n", what, text);
        std::exit(2);
    }
    return value;
}

static void Check(bool ok, const char* what)
{
    if (ok) {
        return;
    }

    std::fprintf(
        stderr,
        "PHASE5_RANDOM_FAIL reason=%s seed=0x%016llx frame=%llu op=%u "
        "guest=0x%016llx extent=%ux%u delay_ms=%u\n",
        what,
        static_cast<unsigned long long>(g_state.seed),
        static_cast<unsigned long long>(g_state.frame),
        g_state.op,
        static_cast<unsigned long long>(g_state.guest),
        g_state.width,
        g_state.height,
        g_state.delay_ms);

    std::abort();
}

struct ExtentChoice {
    uint32_t width;
    uint32_t height;
};

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    Check(argc <= 4, "usage: seconds [seed [replay_frames]]");
    const uint64_t seconds = argc >= 2 ? ParseNumber(argv[1], "duration") : 30;

    Check(seconds >= 1 && seconds <= 600, "duration must be 1..600 seconds");

    uint64_t seed = 0;
    if (argc >= 3) {
        seed = ParseNumber(argv[2], "seed");
    } else {
        seed = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
    }

    g_state.seed = seed;
    const uint64_t replay_frames = argc == 4 ? ParseNumber(argv[3], "replay_frames") : 0;
    Check(argc != 4 || replay_frames >= 60, "replay must include warmup");
    std::mt19937_64 rng(seed);

    std::printf(
        "PHASE5_RANDOM_START seconds=%llu seed=0x%016llx\n",
        static_cast<unsigned long long>(seconds),
        static_cast<unsigned long long>(seed));

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

        window.window = SDL_CreateWindow(
            "ProsperoX Phase 5 randomized stress",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            320,
            240,
            SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);

        Check(window.window != nullptr, "SDL Vulkan window");

        window.CreateVulkan();

        auto& graphics = window.graphic_ctx;
        const auto properties = graphics.GetPhysicalDeviceProperties();

        Check(
            properties.vendorID == 0x1002 &&
            properties.deviceID == 0x747e,
            "requires RX 7800 XT");

        Check(
            graphics.debug_messenger != nullptr,
            "Vulkan validation must be enabled");

        std::printf(
            "PHASE5_RANDOM_DEVICE name=%s vendor=%04x device=%04x driver=%u\n",
            properties.deviceName.data(),
            properties.vendorID,
            properties.deviceID,
            properties.driverVersion);

        auto& context = *window.render_context;
        context.InitializeGpu(nullptr);

        auto& scheduler = context.GetCommandScheduler();

        HW::Context registers {};
        HW::UserConfig user {};
        HW::Shader shaders {};

        scheduler.Begin(registers, user, shaders);

        auto& resources = context.GetGpuResources();
        auto& cache = resources.GetTextureCache();

        constexpr uint64_t base = 0x209000000ull;
        constexpr uint64_t allocation_size = 0x40000ull;

        int64_t direct_offset = -1;

        Check(
            Libs::LibKernel::Memory::KernelAllocateDirectMemory(
                0,
                Libs::LibKernel::Memory::KernelGetDirectMemorySize(),
                allocation_size,
                0x10000,
                0,
                &direct_offset) == 0,
            "allocate direct memory");

        void* mapped = reinterpret_cast<void*>(base);

        Check(
            Libs::LibKernel::Memory::KernelMapDirectMemory(
                &mapped,
                allocation_size,
                3,
                0x10,
                direct_offset,
                0x10000) == 0 &&
                mapped == reinterpret_cast<void*>(base),
            "map direct memory");

        auto* guest_bytes = static_cast<uint8_t*>(mapped);
        std::memset(mapped, 0x5a, allocation_size);
        std::vector<uint8_t> expected(allocation_size, 0x5a);
        std::vector<uint8_t> observed(allocation_size);
        const auto verify = [&](const char* stage) {
            Check(Libs::LibKernel::Memory::TryReadBacking(base, observed.data(), allocation_size),
                  "read backing for byte oracle");
            for (size_t i = 0; i < observed.size(); ++i) {
                if (observed[i] != expected[i]) {
                    std::fprintf(stderr, "PHASE5_RANDOM_MISMATCH stage=%s byte=%zu expected=%u actual=%u\n",
                                 stage, i, expected[i], observed[i]);
                    Check(false, stage);
                }
            }
        };

        constexpr std::array<ExtentChoice, 6> extents {{
            {32, 8},
            {64, 8},
            {64, 16},
            {96, 16},
            {128, 16},
            {128, 32},
        }};

        // Deliberately includes overlapping guest ranges.
        constexpr std::array<uint64_t, 6> offsets {{
            0x0000,
            0x2000,
            0x4000,
            0x8000,
            0x10000,
            0x18000,
        }};

        constexpr std::array<uint32_t, 7> normal_delays {{
            0, 1, 4, 8, 12, 16, 33
        }};

        constexpr uint64_t sentinel_offset = allocation_size - 0x1000;

        const auto start = std::chrono::steady_clock::now();
        auto last_report = start;

        uint64_t frames = 0;
        uint64_t operations = 0;

        uint64_t warm_bytes = 0;
        uint64_t warm_count = 0;
        uint64_t peak_bytes = 0;
        uint64_t peak_count = 0;

        constexpr uint64_t growth_budget =
            256ull * 1024ull * 1024ull;

        while (replay_frames ? frames < replay_frames :
               std::chrono::steady_clock::now() - start < std::chrono::seconds(seconds)) {

            g_state.frame = frames;

            SDL_Event event {};
            while (SDL_PollEvent(&event)) {
            }

            resources.MapMemory(base, allocation_size);

            const uint8_t sentinel =
                static_cast<uint8_t>((rng() >> 8) & 0xff);

            guest_bytes[sentinel_offset] = sentinel;
            expected[sentinel_offset] = sentinel;

            // A game frame can submit several image/resource operations.
            const uint32_t ops_this_frame =
                1u + static_cast<uint32_t>(rng() % 3u);

            for (uint32_t op = 0; op < ops_this_frame; ++op) {
                g_state.op = op;

                const auto extent =
                    extents[static_cast<size_t>(rng() % extents.size())];

                const uint64_t resource_offset =
                    offsets[static_cast<size_t>(rng() % offsets.size())];

                const uint64_t guest_addr =
                    base + resource_offset;

                const uint64_t image_bytes =
                    static_cast<uint64_t>(extent.width) *
                    static_cast<uint64_t>(extent.height) * 4ull;

                Check(
                    resource_offset + image_bytes < sentinel_offset,
                    "generated image overlaps sentinel");

                g_state.guest = guest_addr;
                g_state.width = extent.width;
                g_state.height = extent.height;

                // CPU -> GPU ownership transition.
                std::printf("PHASE5_RANDOM_OP seed=0x%016llx frame=%llu op=%u guest=0x%llx extent=%ux%u\n",
                            static_cast<unsigned long long>(seed), static_cast<unsigned long long>(frames),
                            op, static_cast<unsigned long long>(guest_addr), extent.width, extent.height);
                // Explicitly invalidate the entire write, including all pages. HandleFault
                // describes a single faulting byte and is not a range-write notification.
                Check(resources.InvalidateMemory(guest_addr, image_bytes), "CPU write ownership");

                const uint8_t cpu_pattern =
                    static_cast<uint8_t>(rng() & 0xff);

                std::memset(
                    guest_bytes + resource_offset,
                    cpu_pattern,
                    static_cast<size_t>(image_bytes));
                std::fill_n(expected.begin() + resource_offset, image_bytes, cpu_pattern);

                TextureCache::ImageDesc desc {};
                desc.type = TextureCache::BindingType::Storage;

                desc.info.data = {
                    guest_addr,
                    image_bytes
                };

                desc.info.pixel_format =
                    vk::Format::eR8G8B8A8Unorm;

                desc.info.guest_format =
                    Prospero::BufferFormat::k8_8_8_8UNorm;

                desc.info.extent = {
                    extent.width,
                    extent.height,
                    1
                };

                desc.info.pitch = extent.width;
                desc.info.bytes_per_block = 4;

                desc.info.mip_layout[0] = {
                    0,
                    image_bytes,
                    extent.width,
                    extent.height
                };

                desc.view_info.format =
                    desc.info.pixel_format;

                desc.view_info.usage =
                    vk::ImageUsageFlagBits::eStorage;

                const auto id =
                    cache.FindImage(desc);

                (void)cache.FindTexture(id, desc);

                auto& image =
                    cache.GetImage(id);

                ImageViewInfo view =
                    desc.view_info;

                view.usage =
                    vk::ImageUsageFlagBits::eColorAttachment;

                const auto attachment =
                    image.FindView(view);

                auto& command =
                    scheduler.Current();

                image.Transit(
                    vk::ImageLayout::eColorAttachmentOptimal,
                    vk::AccessFlagBits2::eColorAttachmentWrite,
                    {},
                    command.Handle());

                // UNORM endpoints are exact. Clear half the image so that the
                // other half proves the CPU upload was preserved by the GPU.
                const float r = static_cast<float>(rng() & 1);
                const float g = static_cast<float>(rng() & 1);
                const float b = static_cast<float>(rng() & 1);

                vk::RenderingAttachmentInfo color {};
                color.imageView = attachment;
                color.imageLayout =
                    vk::ImageLayout::eColorAttachmentOptimal;
                color.loadOp =
                    vk::AttachmentLoadOp::eClear;
                color.storeOp =
                    vk::AttachmentStoreOp::eStore;

                color.clearValue.color.float32 =
                    std::array<float, 4> {
                        r, g, b, 1.0f
                    };

                vk::RenderingInfo rendering {};
                rendering.renderArea.extent =
                    vk::Extent2D {
                        extent.width / 2,
                        extent.height
                    };

                rendering.layerCount = 1;
                rendering.colorAttachmentCount = 1;
                rendering.pColorAttachments = &color;

                command.Handle().beginRendering(&rendering);
                command.Handle().endRendering();

                cache.MarkGpuWritten(id);
                Check(cache.HasPendingCpuRead(guest_addr, image_bytes), "GPU write armed demand publication");
                for (uint32_t y = 0; y < extent.height; ++y) {
                    for (uint32_t x = 0; x < extent.width / 2; ++x) {
                        const size_t i = resource_offset + (y * extent.width + x) * 4;
                        expected[i] = static_cast<uint8_t>(r * 255);
                        expected[i + 1] = static_cast<uint8_t>(g * 255);
                        expected[i + 2] = static_cast<uint8_t>(b * 255);
                        expected[i + 3] = 255;
                    }
                }

                // Fault publication before VideoOut changes representation. The
                // exact byte oracle checks the GPU clear AND untouched CPU pixels.
                Check(resources.HandleFault(PageFaultAccess::Read, guest_addr + rng() % image_bytes),
                      "GPU to CPU publication");
                Check(!cache.HasPendingCpuRead(guest_addr, image_bytes), "GPU publication retired");
                verify("GPU clear and CPU upload");

                // Sometimes emulate a CPU patch immediately after GPU ownership.
                if ((rng() % 4u) == 0u) {
                    const uint64_t patch_offset =
                        static_cast<uint64_t>(
                            rng() % image_bytes);

                    const uint64_t remaining =
                        image_bytes - patch_offset;

                    const size_t patch_size =
                        static_cast<size_t>(
                            std::min<uint64_t>(
                                remaining,
                                1u + (rng() % 128u)));

                    Check(resources.InvalidateMemory(guest_addr + patch_offset, patch_size),
                          "partial CPU rewrite ownership");

                    const uint8_t patch_value = static_cast<uint8_t>(rng() & 0xff);

                    std::memset(
                        guest_bytes +
                            resource_offset +
                            patch_offset,
                        patch_value,
                        patch_size);
                    std::fill_n(expected.begin() + resource_offset + patch_offset, patch_size, patch_value);
                    verify("partial CPU rewrite preserves other bytes");
                    // Reacquire and download the uploaded patch to distinguish
                    // successful CPU stores from correct buffer/image authority.
                    const auto refreshed = cache.FindImage(desc);
                    (void)cache.FindTexture(refreshed, desc);
                    Check(cache.HasPendingCpuRead(guest_addr, image_bytes), "patch upload read ownership");
                    Check(resources.HandleFault(PageFaultAccess::Read, guest_addr), "patch upload publication");
                    verify("GPU roundtrip of partial CPU patch");
                }

                auto display =
                    desc.info;

                display.pixel_format =
                    vk::Format::eR8G8B8A8Srgb;

                display.guest_format =
                    Prospero::BufferFormat::k8_8_8_8Srgb;

                auto& prepared =
                    window.presenter->PrepareFrame(
                        scheduler.Current(),
                        display);

                scheduler.Finish();

                // Small random CPU/GPU scheduling jitter before presentation.
                if ((rng() % 8u) == 0u) {
                    std::this_thread::yield();
                }

                window.presenter->Present(prepared);

                // GPU -> CPU publication.
                const uint64_t probe =
                    guest_addr +
                    static_cast<uint64_t>(
                        rng() % image_bytes);

                Check(
                    resources.HandleFault(
                        PageFaultAccess::Read,
                        probe),
                    "GPU to CPU publication");

                Check(
                    guest_bytes[sentinel_offset] == sentinel,
                    "unrelated padding corrupted");

                ++operations;
                std::printf("PHASE5_RANDOM_OP_PASS frame=%llu op=%u operations=%llu\n",
                            static_cast<unsigned long long>(frames), op,
                            static_cast<unsigned long long>(operations));
            }

            resources.UnmapMemory(
                base,
                allocation_size);

            // Do not collect at exactly the same cadence every frame.
            if ((rng() % 4u) != 0u) {
                resources.RunGarbageCollector();
            }

            scheduler.Finish();
            scheduler.DrainPriorityOperations();

            ++frames;

            VmaTotalStatistics stats {};
            vmaCalculateStatistics(
                graphics.allocator,
                &stats);

            const uint64_t bytes =
                stats.total.statistics.allocationBytes;

            const uint64_t count =
                stats.total.statistics.allocationCount;

            if (frames == 60) {
                warm_bytes = bytes;
                warm_count = count;
            }

            if (frames >= 60) {
                peak_bytes =
                    std::max(peak_bytes, bytes);

                peak_count =
                    std::max(peak_count, count);

                Check(
                    bytes <=
                        warm_bytes +
                        growth_budget,
                    "live allocation byte budget");

                Check(
                    count <=
                        warm_count + 256,
                    "live allocation count budget");
            }

            // Mostly realistic frame pacing, with occasional game-like hitches.
            uint32_t delay_ms = 0;

            if ((rng() % 100u) < 5u) {
                delay_ms =
                    50u +
                    static_cast<uint32_t>(
                        rng() % 71u);
            } else {
                delay_ms =
                    normal_delays[
                        static_cast<size_t>(
                            rng() %
                            normal_delays.size())];
            }

            g_state.delay_ms = delay_ms;

            if (delay_ms != 0) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(
                        delay_ms));
            }

            const auto now =
                std::chrono::steady_clock::now();

            if (
                now - last_report >=
                std::chrono::seconds(5)) {

                last_report = now;

                std::printf(
                    "PHASE5_RANDOM_PROGRESS "
                    "seconds=%.1f frames=%llu ops=%llu "
                    "live_bytes=%llu allocations=%llu "
                    "seed=0x%016llx\n",
                    std::chrono::duration<double>(
                        now - start).count(),
                    static_cast<unsigned long long>(
                        frames),
                    static_cast<unsigned long long>(
                        operations),
                    static_cast<unsigned long long>(
                        bytes),
                    static_cast<unsigned long long>(
                        count),
                    static_cast<unsigned long long>(
                        seed));
            }
        }

        resources.RunGarbageCollector();
        scheduler.Finish();
        scheduler.DrainPriorityOperations();

        const double elapsed =
            std::chrono::duration<double>(
                std::chrono::steady_clock::now() -
                start).count();

        Check(
            frames >= 60 && warm_count > 0,
            "insufficient randomized frames for measured warmup");

        Check(
            Libs::LibKernel::Memory::KernelMunmap(
                base,
                allocation_size) == 0,
            "guest unmap");

        Check(
            Libs::LibKernel::Memory::KernelReleaseDirectMemory(
                direct_offset,
                allocation_size) == 0,
            "release direct memory");

        context.ShutdownGpu();

        std::printf(
            "PHASE5_RANDOM_PASS "
            "seconds=%.3f frames=%llu ops=%llu "
            "seed=0x%016llx "
            "warm_bytes=%llu peak_bytes=%llu "
            "warm_allocations=%llu peak_allocations=%llu\n",
            elapsed,
            static_cast<unsigned long long>(frames),
            static_cast<unsigned long long>(operations),
            static_cast<unsigned long long>(seed),
            static_cast<unsigned long long>(warm_bytes),
            static_cast<unsigned long long>(peak_bytes),
            static_cast<unsigned long long>(warm_count),
            static_cast<unsigned long long>(peak_count));
    }

    std::printf("PHASE5_RANDOM_TEARDOWN_PASS seed=0x%016llx\n",
                static_cast<unsigned long long>(seed));
    return 0;
}
