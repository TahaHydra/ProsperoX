#include "loader/importAuditRunner.h"

#include "common/emulatorConfig.h"
#include "common/logging/log.h"
#include "common/subsystems.h"
#include "kernel/memory.h"
#include "libs/libs.h"
#include "loader/importAudit.h"
#include "loader/runtimeLinker.h"

#include <cstdio>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace Loader::ImportAuditRunner {

namespace {

bool Exists(const std::filesystem::path& path) {
	std::error_code ec;
	return (std::filesystem::is_regular_file(path, ec) ||
	        std::filesystem::is_directory(path, ec)) &&
	       !ec;
}

bool IsDirectory(const std::filesystem::path& path) {
	std::error_code ec;
	return std::filesystem::is_directory(path, ec) && !ec;
}

struct AuditSubsystems {
	Common::Subsystems subsystems;

	AuditSubsystems() {
		subsystems.Initialize<Config::Lifecycle>();
		Config::ConfigOptions options;
		options.printf_direction = Config::OutputDirection::Silent;
		Config::Load(options);
		subsystems.Initialize<Log::Lifecycle>();
		subsystems.Initialize<Libs::LibKernel::Memory::Lifecycle>();
	}

	~AuditSubsystems() { subsystems.Destroy(); }
};

template <typename Result>
int Finish(const Result& result, const std::filesystem::path& json_output) {
	ImportAudit::Print(result);
	if (!json_output.empty()) {
		std::string error;
		if (!ImportAudit::WriteJson(json_output, ImportAudit::ToJson(result), &error)) {
			std::fprintf(stderr, "audit JSON write failed: %s\n", error.c_str());
			return 3;
		}
	}
	return result.partial ? 4 : 0;
}

} // namespace

int RunGameAudit(const std::filesystem::path& input,
                 const std::filesystem::path& json_output) {
	if (!Exists(input)) {
		std::fprintf(stderr, "audit input does not exist: %s\n", input.string().c_str());
		return 3;
	}

	AuditSubsystems environment;
	RuntimeLinker linker;
	Libs::InitAll(linker.Symbols());
	const auto result = ImportAudit::AuditGame(&linker, {input});
	const auto code   = Finish(result, json_output);
	linker.Clear();
	return code;
}

int RunLibraryAudit(const std::filesystem::path& root,
                    const std::filesystem::path& json_output) {
	if (!IsDirectory(root)) {
		std::fprintf(stderr, "audit library root is not a directory: %s\n", root.string().c_str());
		return 3;
	}

	AuditSubsystems environment;
	const auto roots = ImportAudit::DiscoverGameRoots(root);
	if (roots.empty()) {
		std::fprintf(stderr, "audit library contains no game roots: %s\n", root.string().c_str());
		return 3;
	}
	const auto result = ImportAudit::AuditLibrary({root});
	return Finish(result, json_output);
}

} // namespace Loader::ImportAuditRunner
