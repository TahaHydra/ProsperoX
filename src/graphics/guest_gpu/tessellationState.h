#pragma once

#include <cstdint>
#include <mutex>

namespace Libs::Graphics {

struct TfRingConfig {
    uint64_t base = 0;
    uint32_t size_bytes = 0;
    uint64_t generation = 0;
};
struct HsOffchipConfig {
    uint32_t buffering = 0; // AMD OFFCHIP_BUFFERING: block count minus one.
    uint64_t generation = 0;
};

// Per-renderer guest driver configuration. This does not own or touch guest
// storage. A future native tessellation backend must acquire/pin it at use.
class TessellationState {
public:
    // nullptr means accepted. Other results describe an emulator support
    // boundary, not an invented Sony error code. Failure preserves old state.
    const char* Configure(uint64_t base, uint32_t size_bytes);
    [[nodiscard]] TfRingConfig Snapshot() const;
    const char* ConfigureOffchip(uint32_t control, uint32_t buffering);
    [[nodiscard]] HsOffchipConfig OffchipSnapshot() const;
    void Reset();
    static const char* Validate(const TfRingConfig& config);
    static bool UsesNativeTessellation(uint32_t stages, uint32_t primitive);
    void RequireDrawSupport(uint32_t stages, uint32_t primitive) const;
private:
    mutable std::mutex m_mutex;
    TfRingConfig m_ring;
    HsOffchipConfig m_offchip;
};

} // namespace Libs::Graphics
