#include "emulator.h"

#include "common/abi.h"
#include "common/assert.h"
#include "common/emulatorConfig.h"
#include "common/file.h"
#include "common/logging/log.h"
#include "common/profiler.h"
#include "common/singleton.h"
#include "common/stringUtils.h"
#include "common/subsystems.h"
#include "common/systemInfo.h"
#include "common/threads.h"
#include "graphics/presentation/window.h"
#include "kernel/fileSystem.h"
#include "kernel/memory.h"
#include "kernel/pthread.h"
#include "kytyGitVersion.h"
#include "libs/agc.h"
#include "libs/audio.h"
#include "libs/controller.h"
#include "libs/libs.h"
#include "libs/network.h"
#include "loader/runtimeLinker.h"
#include "loader/systemContent.h"
#include "loader/timer.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <tlhelp32.h>
#include <wct.h>
#include <windows.h>
#endif

namespace Emulator {

#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
namespace {

using OpenThreadWaitChainSessionFn = HWCT(WINAPI*)(DWORD, PWAITCHAINCALLBACK);
using GetThreadWaitChainFn = BOOL(WINAPI*)(HWCT, DWORD_PTR, DWORD, DWORD, LPDWORD,
                                           PWAITCHAIN_NODE_INFO, LPBOOL);
using CloseThreadWaitChainSessionFn = VOID(WINAPI*)(HWCT);

static std::string WideToUtf8(const wchar_t* text) {
	if (text == nullptr || text[0] == L'\0') {
		return {};
	}

	const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	if (size <= 1) {
		return {};
	}

	std::string result(static_cast<size_t>(size - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
	return result;
}

static void DumpWindowsWaitChains() {
	HMODULE advapi = LoadLibraryW(L"advapi32.dll");
	if (advapi == nullptr) {
		LOGF("WAITCHAIN_DIAG error=LoadLibraryW(advapi32.dll) failed code=%lu\n",
		     GetLastError());
		return;
	}

	auto open_session = reinterpret_cast<OpenThreadWaitChainSessionFn>(
	    GetProcAddress(advapi, "OpenThreadWaitChainSession"));
	auto get_chain = reinterpret_cast<GetThreadWaitChainFn>(
	    GetProcAddress(advapi, "GetThreadWaitChain"));
	auto close_session = reinterpret_cast<CloseThreadWaitChainSessionFn>(
	    GetProcAddress(advapi, "CloseThreadWaitChainSession"));

	if (open_session == nullptr || get_chain == nullptr || close_session == nullptr) {
		LOGF("WAITCHAIN_DIAG error=wait-chain API unavailable\n");
		FreeLibrary(advapi);
		return;
	}

	HWCT session = open_session(0, nullptr);
	if (session == nullptr) {
		LOGF("WAITCHAIN_DIAG error=OpenThreadWaitChainSession failed code=%lu\n",
		     GetLastError());
		FreeLibrary(advapi);
		return;
	}

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		LOGF("WAITCHAIN_DIAG error=CreateToolhelp32Snapshot failed code=%lu\n",
		     GetLastError());
		close_session(session);
		FreeLibrary(advapi);
		return;
	}

	const DWORD process_id = GetCurrentProcessId();
	THREADENTRY32 entry {};
	entry.dwSize = sizeof(entry);
	uint32_t thread_count = 0;
	uint32_t blocked_count = 0;

	LOGF("WAITCHAIN_DIAG_BEGIN pid=%lu\n", process_id);

	if (Thread32First(snapshot, &entry) != FALSE) {
		do {
			if (entry.th32OwnerProcessID != process_id) {
				continue;
			}
			thread_count++;

			DWORD node_count = WCT_MAX_NODE_COUNT;
			WAITCHAIN_NODE_INFO nodes[WCT_MAX_NODE_COUNT] {};
			BOOL cycle = FALSE;
			if (get_chain(session, 0, WCTP_GETINFO_ALL_FLAGS, entry.th32ThreadID, &node_count,
			              nodes, &cycle) == FALSE) {
				LOGF("WAITCHAIN thread=%lu error=%lu\n", entry.th32ThreadID, GetLastError());
				continue;
			}

			if (node_count <= 1 && cycle == FALSE) {
				continue;
			}
			blocked_count++;
			LOGF("WAITCHAIN thread=%lu nodes=%lu cycle=%s\n", entry.th32ThreadID, node_count,
			     cycle != FALSE ? "yes" : "no");

			for (DWORD i = 0; i < node_count; i++) {
				const auto& node = nodes[i];
				if (node.ObjectType == WctThreadType) {
					LOGF("  node[%lu] thread pid=%lu tid=%lu wait_ms=%lu switches=%lu status=%d\n",
					     i, node.ThreadObject.ProcessId, node.ThreadObject.ThreadId,
					     node.ThreadObject.WaitTime, node.ThreadObject.ContextSwitches,
					     static_cast<int>(node.ObjectStatus));
				} else {
					const auto object_name = WideToUtf8(node.LockObject.ObjectName);
					LOGF("  node[%lu] object type=%d status=%d name=%s alertable=%s\n", i,
					     static_cast<int>(node.ObjectType), static_cast<int>(node.ObjectStatus),
					     object_name.empty() ? "-" : object_name.c_str(),
					     node.LockObject.Alertable != FALSE ? "yes" : "no");
				}
			}
		} while (Thread32Next(snapshot, &entry) != FALSE);
	}

	LOGF("WAITCHAIN_DIAG_END threads=%u blocked_chains=%u\n", thread_count, blocked_count);

	CloseHandle(snapshot);
	close_session(session);
	FreeLibrary(advapi);
}

static void StartWaitChainDiagnosticWatchdog() {
	const char* enabled = std::getenv("KYTY_WAITCHAIN_DIAG");
	if (enabled == nullptr || enabled[0] == '\0' || enabled[0] == '0') {
		return;
	}

	std::thread([] {
		std::this_thread::sleep_for(std::chrono::seconds(10));
		DumpWindowsWaitChains();
	}).detach();
}

} // namespace
#else
static void StartWaitChainDiagnosticWatchdog() {}
#endif

static void PrintSystemInfo() {
	const Common::SystemInfo info = Common::GetSystemInfo();

#if defined(__APPLE__)
	static constexpr auto platform_name = "macOS";
#elif KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	static constexpr auto platform_name = "Windows";
#elif KYTY_PLATFORM == KYTY_PLATFORM_LINUX
	static constexpr auto platform_name = "Linux";
#else
	static constexpr auto platform_name = "Unknown";
#endif

	LOGF("Build\n"
	     "  version: %s\n\n"
	     "Host\n"
	     "  os:      %s\n"
	     "  cpu:     %s\n"
	     "  threads: %u\n\n",
	     KYTY_BUILD_LABEL, platform_name, info.ProcessorName.c_str(),
	     std::thread::hardware_concurrency());
}

static void KytyClose() {
	auto* rt = Common::Singleton<Loader::RuntimeLinker>::Instance();

	rt->Clear();

	LOGF("done!\n");

	Common::Subsystems::EmergencyShutdownActive();
}

static void MountOrCreateDir(const std::filesystem::path& dir, const std::string& point) {
	if (!Common::File::IsDirectoryExisting(dir)) {
		Common::File::CreateDirectories(dir);
	}

	EXIT_NOT_IMPLEMENTED(!Common::File::IsDirectoryExisting(dir));

	Libs::LibKernel::FileSystem::Mount(dir, point);
	auto dir_text = Common::PathToString(dir);
	LOGF("Mounted %s -> %s\n", point.c_str(), dir_text.c_str());
}

static void MountSandboxDirs() {
	std::string title_id;
	if (!Loader::SystemContentParamSfoGetString("TITLE_ID", &title_id) || title_id.empty()) {
		title_id = "UNKNOWN";
	}

	MountOrCreateDir("_DownloadData/" + title_id, "/download0");
	MountOrCreateDir("_TempData/" + title_id, "/temp0");
	MountOrCreateDir("_TempData/" + title_id, "/temp");
}

static bool ClearDirectoryContents(const std::filesystem::path& dir) {
	bool ok = true;

	for (const auto& entry: Common::File::GetDirEntries(dir)) {
		if (entry.name == "." || entry.name == "..") {
			continue;
		}

		auto path = dir / entry.name;

		if (entry.is_file) {
			Common::File::RemoveReadonly(path);
			ok = Common::File::DeleteFile(path) && ok;
		} else {
			ok = ClearDirectoryContents(path) && ok;
			ok = Common::File::DeleteDirectory(path) && ok;
		}
	}

	return ok;
}

static void ClearDebugTextureFolder() {
	const std::string debug_texture_folder = "_Textures";

	if (!Common::File::IsDirectoryExisting(debug_texture_folder)) {
		Common::File::CreateDirectories(debug_texture_folder);
		return;
	}

	if (!ClearDirectoryContents(debug_texture_folder)) {
		LOGF_COLOR(Log::Color::BrightYellow, "TextureDump: failed to completely clear %s\n",
		           debug_texture_folder.c_str());
	}
}

static void Init(const Config::ConfigOptions& cfg, const std::filesystem::path& param_json,
                 Common::Subsystems& subsystems) {
	EXIT_IF(!Common::Thread::IsMainThread());

	subsystems.Initialize<Config::Lifecycle>();
	Config::Load(cfg);
	subsystems.Initialize<Log::Lifecycle>();

	if (Common::File::IsFileExisting(param_json)) {
		Loader::SystemContentLoadParamSfo(param_json);
		if (const auto flexible_memory_size = Loader::SystemContentGetFlexibleMemorySize();
		    flexible_memory_size != 0) {
			Libs::LibKernel::Memory::SetFlexibleMemorySize(flexible_memory_size);
		}
	}

	// Initialization order is explicit; destruction is automatic and reversed.
	subsystems.Initialize<Loader::Timer::Lifecycle>();
	subsystems.Initialize<Libs::LibKernel::PthreadLifecycle>();
	subsystems.Initialize<Profiler::Lifecycle>();
	subsystems.Initialize<Libs::Network::Lifecycle>();
	subsystems.Initialize<Libs::LibKernel::Memory::Lifecycle>();
	subsystems.Initialize<Libs::LibKernel::FileSystem::Lifecycle>();
	subsystems.Initialize<Libs::Controller::Lifecycle>();
	subsystems.Initialize<Libs::Audio::Lifecycle>();
	subsystems.Initialize<Libs::Graphics::Lifecycle>();
}

static void LoadElf(const std::filesystem::path& elf, bool dbg_print_reloc = false,
                    const std::filesystem::path& save_name = {}) {
	auto* rt = Common::Singleton<Loader::RuntimeLinker>::Instance();

	auto* program = rt->LoadProgram(
	    Libs::LibKernel::FileSystem::GetRealFilename(Common::PathToGenericString(elf)));
	if (program == nullptr) EXIT("Executable load failed: %s\n", rt->LastLoadError().c_str());

	if (dbg_print_reloc) {
		program->dbg_print_reloc = true;
	}

	if (!save_name.empty()) {
		rt->SaveProgram(program, Libs::LibKernel::FileSystem::GetRealFilename(
		                             Common::PathToGenericString(save_name)));
	}
}

static void Execute(const std::filesystem::path& game_patch) {
	auto           patch_path = game_patch;
	Common::Thread guest_thread(
	    [](void* param) {
		    auto* rt = Common::Singleton<Loader::RuntimeLinker>::Instance();
		    rt->Execute(*static_cast<const std::filesystem::path*>(param));
	    },
	    &patch_path);
	StartWaitChainDiagnosticWatchdog();
	Libs::Graphics::WindowRun();
	std::quick_exit(0);
}

void Run(const RunOptions& options) {
	if (options.app0_dir.empty()) {
		EXIT("app0 directory is required\n");
	}

	if (options.elf.empty()) {
		EXIT("ELF is required\n");
	}

	const auto         param_json = options.app0_dir / "sce_sys" / "param.json";
	Common::Subsystems subsystems(true);
	Init(options.config, param_json, subsystems);

	ClearDebugTextureFolder();

	PrintSystemInfo();

	int ok = atexit(KytyClose);
	EXIT_NOT_IMPLEMENTED(ok != 0);

	// Guest threads are still running, so skip KytyClose() and only flush emergency state.
	ok = at_quick_exit(Common::Subsystems::EmergencyShutdownActive);
	EXIT_NOT_IMPLEMENTED(ok != 0);

	Libs::LibKernel::FileSystem::Mount(options.app0_dir, "/app0");
	Libs::LibKernel::FileSystem::Mount(options.app0_dir, "/hostapp");

	MountSandboxDirs();

	auto* rt = Common::Singleton<Loader::RuntimeLinker>::Instance();
	Libs::InitAll(rt->Symbols());

	LoadElf(options.elf);

	Execute(options.game_patch);
}

} // namespace Emulator
