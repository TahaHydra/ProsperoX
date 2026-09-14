#include "graphics/guest_gpu/tessellationState.h"
#include "graphics/guest_gpu/gpu_defs.h"
#include "kernel/memory.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>

namespace Libs::Graphics {

const char* TessellationState::Validate(const TfRingConfig& config) {
    constexpr uint64_t address_limit = 1ull << 40;
    if (config.base == 0 || (config.base & 255) != 0) return "base_alignment";
    if (config.size_bytes == 0 || (config.size_bytes & 3) != 0) return "size_dword_alignment";
    if (config.base >= address_limit || config.size_bytes > address_limit - config.base)
        return "address_range";
    const auto end = config.base + config.size_bytes;
    for (auto address = config.base; address < end;) {
        LibKernel::Memory::VirtualQueryInfo mapping{};
        if (LibKernel::Memory::KernelVirtualQuery(reinterpret_cast<const void*>(address), 0,
                &mapping, sizeof(mapping)) != 0 || !mapping.is_committed ||
            mapping.start > address || mapping.end <= address) return "unmapped";
        // Both the shader producer and the tessellator consumer require GPU access.
        if ((mapping.protection & 0x30) != 0x30) return "gpu_access";
        address = std::min<uint64_t>(end, mapping.end);
    }
    return nullptr;
}

const char* TessellationState::Configure(uint64_t base, uint32_t size_bytes) {
    TfRingConfig next{base, size_bytes};
    if (const auto* reason = Validate(next)) return reason;
    std::lock_guard lock(m_mutex);
    next.generation = m_ring.generation + 1;
    m_ring = next;
    return nullptr;
}

TfRingConfig TessellationState::Snapshot() const {
    std::lock_guard lock(m_mutex);
    return m_ring;
}

const char* TessellationState::ConfigureOffchip(uint32_t control, uint32_t buffering) {
    // Only the independently observed control=0 path is specified. Do not
    // reinterpret unknown selectors/flags or an incidental third CPU register.
    if (control != 0) return "offchip_control";
    if (buffering > 0x3ff) return "offchip_buffering";
    std::lock_guard lock(m_mutex);
    m_offchip = {buffering, m_offchip.generation + 1};
    return nullptr;
}

HsOffchipConfig TessellationState::OffchipSnapshot() const {
    std::lock_guard lock(m_mutex);
    return m_offchip;
}

void TessellationState::Reset() {
    std::lock_guard lock(m_mutex);
    m_ring = {};
    m_offchip = {};
}

bool TessellationState::UsesNativeTessellation(uint32_t stages, uint32_t primitive) {
    // AMD VGT_SHADER_STAGES_EN: LS_EN bits 0..1 and HS_EN bit 2.
    // Host tessellation used to emulate rectangle lists is independent.
    return (stages & 7u) != 0 || primitive == static_cast<uint32_t>(Prospero::PrimitiveType::kPatch);
}

void TessellationState::RequireDrawSupport(uint32_t stages, uint32_t primitive) const {
    if (!UsesNativeTessellation(stages, primitive)) return;
    const auto config = Snapshot();
    const auto offchip = OffchipSnapshot();
    const auto* reason = config.generation == 0 ? "not_configured" : Validate(config);
    std::fprintf(stderr,
        "AGC_NATIVE_TESSELLATION_UNSUPPORTED stages=0x%08" PRIx32 " primitive=%" PRIu32
        " ring=0x%016" PRIx64 " size_bytes=%" PRIu32 " generation=%" PRIu64 " mapping=%s"
        " offchip_buffering=%" PRIu32 " offchip_generation=%" PRIu64 "\n",
        stages, primitive, config.base, config.size_bytes, config.generation,
        reason ? reason : "valid", offchip.buffering, offchip.generation);
    std::fflush(stderr);
    std::quick_exit(86);
}

} // namespace Libs::Graphics
