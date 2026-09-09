#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>

#include "common/emulatorConfig.h"
#include "common/logging/log.h"
#include "common/subsystems.h"
#include "kernel/pthread.h"
#include "kernel/memory.h"

// The full-emulator test target excludes runtimeLinker.cpp from its source
// list, so compile it into this isolated probe as Phase 0 does.
#include "loader/runtimeLinker.cpp"

namespace Phase1 {
inline void Check(bool ok, const char* contract) {
    if (!ok) {
        std::fprintf(stderr, "PHASE2_FAILURE %s\n", contract);
        std::exit(1);
    }
}
}

#include "Phase2KernelTests.inc"

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Common::InitializeThreads();
    Common::VirtualMemory::Init();
    Common::Subsystems subsystems;
    subsystems.Initialize<Config::Lifecycle>();
    Config::ConfigOptions options;
    options.printf_direction = Config::OutputDirection::Silent;
    Config::Load(options);
    subsystems.Initialize<Log::Lifecycle>();
    subsystems.Initialize<Libs::LibKernel::PthreadLifecycle>();
    subsystems.Initialize<Libs::LibKernel::Memory::Lifecycle>();
    const int result = std::strcmp(argv[1], "--kernel") == 0
        ? Phase2::KernelContracts() : std::strcmp(argv[1], "--stress") == 0
        ? Phase2::KernelLifecycleStress() : 2;
    subsystems.Destroy();
    return result;
}
