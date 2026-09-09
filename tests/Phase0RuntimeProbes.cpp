// Phase 0 characterization of actual production paths. Original synthetic inputs.
// Compile runtimeLinker.cpp here, excluding its separate object from this target,
// to exercise the private emitted thunk without exposing a production test API.
#include "loader/runtimeLinker.cpp"
#include "common/subsystems.h"
#include "kernel/fileSystem.h"
#include "Phase1RuntimeTests.inc"

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Common::InitializeThreads();
    Common::VirtualMemory::Init();
    Common::Subsystems subsystems;
    subsystems.Initialize<Config::Lifecycle>();
    Config::ConfigOptions options;
    options.printf_direction = Config::OutputDirection::Silent;
    Config::Load(options);
    subsystems.Initialize<Log::Lifecycle>();
    if (argc != 2) { return 2; }
    if (std::strcmp(argv[1], "--phase1-legacy-plt") == 0) {
        Loader::RelocateHandlerStack args{{7,0,0}};
        Loader::RelocateHandler(args);
        return 1;
    }
    if (std::strcmp(argv[1], "--phase1-strong-data") == 0) return Phase1::MissingStrongData();
    if (std::strcmp(argv[1], "--phase1-modules") == 0) {
        subsystems.Initialize<Libs::LibKernel::PthreadLifecycle>();
        subsystems.Initialize<Libs::LibKernel::Memory::Lifecycle>();
        const auto result = Phase1::Modules();
        Common::Singleton<Loader::RuntimeLinker>::Instance()->Clear();
        subsystems.Destroy();
        return result;
    }
    if (std::strcmp(argv[1], "--phase1-load-rollback") == 0) {
        subsystems.Initialize<Libs::LibKernel::Memory::Lifecycle>();
        const auto result = Phase1::LoadRollback();
        Common::Singleton<Loader::RuntimeLinker>::Instance()->Clear();
        subsystems.Destroy();
        return result;
    }
    if (std::strcmp(argv[1], "--phase1-patching") == 0) {
        subsystems.Initialize<Libs::LibKernel::Memory::Lifecycle>();
        const auto result = Phase1::Patching();
        subsystems.Destroy();
        return result;
    }
    if (std::strcmp(argv[1], "--phase1-native-state") == 0) {
        subsystems.Initialize<Libs::LibKernel::Memory::Lifecycle>();
        const auto result = Phase1::NativeState();
        subsystems.Destroy();
        return result;
    }
    if (std::strcmp(argv[1], "--phase1-executables") == 0) {
        const auto result = Phase1::Executables();
        subsystems.Destroy();
        return result;
    }
    if (std::strcmp(argv[1], "--phase1-paths") == 0) {
        subsystems.Initialize<Libs::LibKernel::FileSystem::Lifecycle>();
        const auto result = Phase1::Paths();
        subsystems.Destroy();
        return result;
    }
    if (std::strcmp(argv[1], "--phase1-imports") == 0 || std::strcmp(argv[1], "--phase1-tls") == 0) {
        subsystems.Initialize<Libs::LibKernel::Memory::Lifecycle>();
        const auto result = std::strcmp(argv[1], "--phase1-imports") == 0 ? Phase1::Imports() : Phase1::Tls();
        Common::Singleton<Loader::RuntimeLinker>::Instance()->Clear();
        subsystems.Destroy();
        return result;
    }
    if (std::strcmp(argv[1], "--unresolved-import") == 0) {
        subsystems.Initialize<Libs::LibKernel::Memory::Lifecycle>();
        Loader::RelocationInfo relocation;
        relocation.bind = Loader::BindType::Global;
        relocation.type = Loader::SymbolType::Func;
        relocation.name = "Phase0OriginalMissingImport";
        uint64_t slot = 0;
        relocation.vaddr = reinterpret_cast<uint64_t>(&slot);
        const auto thunk = Loader::RegisterStubbedImport(0, nullptr, relocation);
        using GuestFunction = KYTY_SYSV_ABI uint64_t (*)();
        const auto result = reinterpret_cast<GuestFunction>(thunk)();
        std::printf("PHASE0 {\"probe\":\"unresolved_import\",\"seed\":5265456,"
                    "\"expected\":\"explicit_unsupported_failure\",\"actual\":\"returned\","
                    "\"return_value\":%llu}\n", static_cast<unsigned long long>(result));
        // A strong missing function must not return an invented result, even a nonzero one.
        subsystems.Destroy();
        return 1;
    }
    if (std::strcmp(argv[1], "--path-containment") == 0) {
        namespace FS = Libs::LibKernel::FileSystem;
        subsystems.Initialize<FS::Lifecycle>();
        const auto root = std::filesystem::current_path() / "phase0-synthetic-mount";
        FS::Mount(root, "/phase0");
        // Resolve only. Do not open, create, delete, or probe any host files.
        const auto escaped = FS::GetRealFilename("/phase0/../outside.txt").lexically_normal();
        const auto unmapped = FS::GetRealFilename("/phase0-unmapped/original.txt");
        const bool contained = escaped.empty() || escaped.parent_path() == root;
        const bool denied = unmapped.empty();
        std::printf("PHASE0 {\"probe\":\"path_containment\",\"seed\":5265456,"
                    "\"expected_contained\":true,\"actual_contained\":%s,"
                    "\"expected_unmapped_denied\":true,\"actual_unmapped_denied\":%s}\n",
                    contained ? "true" : "false", denied ? "true" : "false");
        subsystems.Destroy();
        return contained && denied ? 0 : 1;
    }
    return 2;
}
