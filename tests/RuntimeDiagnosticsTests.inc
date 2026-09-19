// Synthetic contract tests for opt-in runtime diagnostics.
// No SDK, firmware, or commercial bytes.
#include "libs/runtimeDiagnostics.h"

namespace RuntimeDiagnosticsTests {

int Run() {
    using Libs::RuntimeDiagnostics::State;

    State state(8);
    const auto call = state.EnterHle(
        17, "libkernel", "libkernel", "KernelEqueueWait", 0x0000000800123456ull, 0, 1'000'000);

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

    // A thread inside a call is attributable even while it is silent, and one
    // that has left a call is distinguishable from one that never made any:
    // that difference is what tells a stalled guest thread from an exited one.
    Phase1::Check(active.find("state=in_hle") != std::string::npos &&
                      active.find("call=libkernel::libkernel::KernelEqueueWait") !=
                          std::string::npos,
                  "runtime diagnostics attributes the active call to its thread");
    // The guest call site is what turns "a thread is stuck" into an address a
    // disassembler can be pointed at.
    Phase1::Check(active.find("caller=0x800123456") != std::string::npos,
                  "runtime diagnostics records the guest call site");

    state.RecordGuestThreadStart(17, "MainThread", 0x0000000800100000ull, 4242, 900'000);
    state.RecordGuestThreadStart(18, "Worker", 0x0000000800200000ull, 4243, 950'000);
    const auto named = state.BuildReport(3'500'000);
    Phase1::Check(named.find("name=MainThread") != std::string::npos &&
                      named.find("entry=0x800200000") != std::string::npos &&
                      named.find("state=running") != std::string::npos &&
                      named.find("host_tid=4242") != std::string::npos,
                  "runtime diagnostics reports guest thread identity");

    state.RecordGuestThreadExit(18, 3'000'000);
    const auto exited = state.BuildReport(3'500'000);
    Phase1::Check(exited.find("state=exited") != std::string::npos,
                  "runtime diagnostics distinguishes an exited guest thread");

    state.ExitHle(call, 3'600'000);
    const auto completed = state.BuildReport(3'700'000);
    Phase1::Check(completed.find("active_hle=0") != std::string::npos,
                  "completed HLE call leaves no active call");
    Phase1::Check(completed.find("EXIT libkernel::libkernel::KernelEqueueWait") !=
                      std::string::npos,
                  "runtime diagnostics retains recent HLE exit event");
    // A thread outside any HLE call is executing guest code the trace cannot
    // follow, so how long it has been there is the evidence that matters.
    Phase1::Check(completed.find("state=guest") != std::string::npos &&
                      completed.find("silent_ms=100") != std::string::npos &&
                      completed.find("caller=0x800123456") != std::string::npos,
                  "runtime diagnostics reports how long a thread has been in guest code");

    // A repeating call must not evict the history that led to it.
    State looping(8);
    const auto anchor = looping.EnterHle(9, "libkernel", "libkernel", "KernelBatchMap2", 0, 0, 10);
    looping.ExitHle(anchor, 20);
    for (uint64_t i = 0; i < 500; ++i) {
        const auto spin = looping.EnterHle(9, "libkernel", "libkernel", "KernelUsleep", 0, 0, 30 + i);
        looping.ExitHle(spin, 31 + i);
    }
    const auto looped = looping.BuildReport(1'000);
    Phase1::Check(looped.find("KernelBatchMap2") != std::string::npos,
                  "a poll loop does not evict the events that preceded it");
    Phase1::Check(looped.find("more of the same") != std::string::npos,
                  "a poll loop is collapsed into a count");
    Phase1::Check(looping.RecentEventCount() <= 8, "collapsing respects the ring bound");

    // Tracing is off unless asked for, is capped when it is, and records the
    // same collapsed stream the ring shows rather than every loop iteration.
    Phase1::Check(looping.TakeTracedEvents().empty(), "tracing is off by default");
    State traced(4);
    traced.SetTraceLimit(3);
    for (uint64_t i = 0; i < 20; ++i) {
        const auto name = "F" + std::to_string(i);
        traced.ExitHle(traced.EnterHle(1, "L", "M", name.c_str(), 0, 0, i), i + 1);
    }
    Phase1::Check(traced.TakeTracedEvents().size() == 3, "tracing stops at its cap");
    Phase1::Check(traced.TakeTracedEvents().empty(), "taking the trace drains it");

    State bounded(3);
    for (uint64_t i = 0; i < 4; ++i) {
        const auto token = bounded.EnterHle(static_cast<uint32_t>(i + 1), "L", "M", "F", 0, 0,
                                            10 + i * 2);
        bounded.ExitHle(token, 11 + i * 2);
    }
    Phase1::Check(bounded.RecentEventCount() == 3,
                  "runtime diagnostics recent HLE ring is bounded");

    // One return address names the primitive a thread stopped in; the frames
    // below it name the subsystem that decided to wait, which is the part a
    // stall report cannot otherwise supply. The walk anchors on the frame
    // carrying the recorded call site rather than on rbp, because whether an
    // HLE entry point keeps a frame pointer is the host compiler's choice.
    Libs::RuntimeDiagnostics::SetModuleResolver([](uint64_t address) -> std::string {
        return address >= 0x0000000900000000ull && address < 0x0000000902000000ull
                   ? std::string {"eboot.bin"}
                   : std::string {};
    });

    State walked(8);
    // Two dead slots stand in for the entry point's own frame, so the anchor
    // has to be searched for rather than assumed to be the first thing above
    // the scope object.
    alignas(16) uint64_t frames[10] {};
    const auto base = reinterpret_cast<uint64_t>(static_cast<void*>(frames));
    frames[0] = 0x00000000deadbeefull;
    frames[1] = 0x00000000deadbeefull;
    frames[2] = base + 32;
    frames[3] = 0x0000000901000010ull;
    frames[4] = base + 48;
    frames[5] = 0x0000000901000020ull;
    frames[6] = 0;
    frames[7] = 0x0000000901000030ull;
    walked.RecordGuestThreadStart(5, "MainThread", 0x0000000900000000ull, 77, 0);
    walked.RecordGuestThreadStack(5, base, sizeof(frames));
    const auto walk = walked.EnterHle(5, "libkernel", "libkernel", "KernelUsleep",
                                      0x0000000901000010ull, base, 10);
    const auto backtrace = walked.BuildReport(20);
    Phase1::Check(backtrace.find("#0 0x901000010") != std::string::npos &&
                      backtrace.find("#1 0x901000020") != std::string::npos &&
                      backtrace.find("#2 0x901000030") != std::string::npos,
                  "a stalled call reports the guest frames that led to it");
    walked.ExitHle(walk, 30);

    // A call site that no frame above the anchor carries cannot be placed in
    // the chain, and saying so beats publishing one assembled out of whatever
    // the stack happened to hold.
    const auto mismatched = walked.EnterHle(5, "libkernel", "libkernel", "KernelUsleep",
                                            0x0000000901000099ull, base, 40);
    Phase1::Check(walked.BuildReport(50).find("backtrace=unavailable") != std::string::npos,
                  "a call site no frame carries is not walked");
    walked.ExitHle(mismatched, 60);

    // With no stack recorded for the thread there is nothing to bound the
    // walk, so it does not happen.
    State unbounded(8);
    unbounded.RecordGuestThreadStart(6, "Worker", 0, 78, 0);
    const auto unknown = unbounded.EnterHle(6, "libkernel", "libkernel", "KernelUsleep",
                                            0x0000000901000010ull, base, 10);
    const auto unwalked = unbounded.BuildReport(20);
    Phase1::Check(unwalked.find("    #0 ") == std::string::npos &&
                      unwalked.find("backtrace=") == std::string::npos,
                  "a thread with no recorded stack reports no frames");
    unbounded.ExitHle(unknown, 30);
    Libs::RuntimeDiagnostics::SetModuleResolver(nullptr);

    std::puts("PHASE1_PASS runtime diagnostics expose bounded stall evidence");
    return 0;
}

} // namespace RuntimeDiagnosticsTests
