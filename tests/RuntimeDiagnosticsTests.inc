// Synthetic contract tests for opt-in runtime diagnostics.
// No SDK, firmware, or commercial bytes.
#include "libs/runtimeDiagnostics.h"

namespace RuntimeDiagnosticsTests {

int Run() {
    using Libs::RuntimeDiagnostics::State;

    State state(8);
    const auto call = state.EnterHle(
        17, "libkernel", "libkernel", "KernelEqueueWait", 1'000'000);

    state.RecordGpuDispatch(37, 0x0000000901234000ull, 256, 1, 1, 1'100'000);
    state.RecordGpuDraw(38, "indexed", 1'150'000);
    state.RecordVideoSubmit(true, 2, 1'200'000);
    state.RecordVideoPrepared(1'210'000);
    state.RecordVideoReady(1'220'000);
    state.RecordVideoPresented(2, 1'230'000);
    state.RecordShaderPhase("compute", 0x54904fb419d79e49ull,
                            "vkCreateComputePipeline", 1'300'000);

    const auto active = state.BuildReport(3'500'000);
    Phase1::Check(active.find("KernelEqueueWait") != std::string::npos,
                  "runtime diagnostics reports active HLE call");
    Phase1::Check(active.find("active_ms=2500") != std::string::npos,
                  "runtime diagnostics reports active HLE age");
    Phase1::Check(active.find("dispatches=1") != std::string::npos &&
                      active.find("groups=256x1x1") != std::string::npos,
                  "runtime diagnostics reports last GPU dispatch");
    Phase1::Check(active.find("draws=1") != std::string::npos,
                  "runtime diagnostics reports GPU draw count");
    Phase1::Check(active.find("gpu_submitted=1") != std::string::npos &&
                      active.find("prepared=1") != std::string::npos &&
                      active.find("ready=1") != std::string::npos &&
                      active.find("presented=1") != std::string::npos,
                  "runtime diagnostics reports VideoOut lifecycle counters");
    Phase1::Check(active.find("vkCreateComputePipeline") != std::string::npos &&
                      active.find("54904fb419d79e49") != std::string::npos,
                  "runtime diagnostics reports current shader phase");

    state.ExitHle(call, 3'600'000);
    const auto completed = state.BuildReport(3'700'000);
    Phase1::Check(completed.find("active_hle=0") != std::string::npos,
                  "completed HLE call leaves no active call");
    Phase1::Check(completed.find("EXIT libkernel::libkernel::KernelEqueueWait") !=
                      std::string::npos,
                  "runtime diagnostics retains recent HLE exit event");

    State bounded(3);
    for (uint64_t i = 0; i < 4; ++i) {
        const auto token = bounded.EnterHle(static_cast<uint32_t>(i + 1), "L", "M", "F",
                                            10 + i * 2);
        bounded.ExitHle(token, 11 + i * 2);
    }
    Phase1::Check(bounded.RecentEventCount() == 3,
                  "runtime diagnostics recent HLE ring is bounded");

    std::puts("PHASE1_PASS runtime diagnostics expose bounded stall evidence");
    return 0;
}

} // namespace RuntimeDiagnosticsTests
