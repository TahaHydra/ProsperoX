#ifndef EMULATOR_INCLUDE_EMULATOR_LOADER_IMPORTAUDIT_H_
#define EMULATOR_INCLUDE_EMULATOR_LOADER_IMPORTAUDIT_H_

#include "loader/runtimeLinker.h"

#include <cstdio>
#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace Loader::ImportAudit {

enum class Status {
	ExactHle,
	CompatibleHle,
	AliasCandidate,
	GuestModule,
	WeakUnresolved,
	MissingHle,
	Malformed,
};

struct ImportRecord {
	std::string              binary;
	std::string              nid;
	SymbolType               symbol_type = SymbolType::Unknown;
	bool                     weak        = false;
	SymbolResolve            request {};
	std::string              qualified_name;
	Status                   status = Status::Malformed;
	std::string              resolved_name;
	std::vector<std::string> candidates;
	uint64_t                 references = 1;
};

struct BinaryResult {
	std::string               path;
	bool                      parsed = false;
	std::string               diagnostic;
	std::vector<ImportRecord> imports;
};

struct GameResult {
	std::filesystem::path     root;
	std::string               title_id;
	std::string               title_name;
	std::vector<BinaryResult> binaries;
	std::vector<ImportRecord> imports;
	bool                      partial = false;
};

struct GlobalImportRecord {
	ImportRecord              import;
	std::vector<std::string> games;
	std::vector<std::string> binaries;
};

struct LibraryResult {
	std::vector<GameResult>         games;
	std::vector<GlobalImportRecord> global_imports;
	bool                            partial = false;
};

struct GameOptions {
	std::filesystem::path input;
};

struct LibraryOptions {
	std::filesystem::path root;
};

[[nodiscard]] const char* StatusName(Status status);
[[nodiscard]] ImportRecord ClassifyImport(const RuntimeLinker& linker,
                                          const Program& requester,
                                          const SymbolResolve& request,
                                          SymbolType type, bool weak,
                                          std::string binary);
[[nodiscard]] std::string ImportKey(const ImportRecord& record);
[[nodiscard]] BinaryResult AuditProgram(const RuntimeLinker& linker,
                                        const Program& program,
                                        const std::filesystem::path& game_root);
[[nodiscard]] GameResult AuditGame(RuntimeLinker* linker, const GameOptions& options);
[[nodiscard]] std::vector<std::filesystem::path>
DiscoverGameRoots(const std::filesystem::path& root);
[[nodiscard]] LibraryResult AuditLibrary(const LibraryOptions& options);
[[nodiscard]] LibraryResult Aggregate(std::vector<GameResult> games);

[[nodiscard]] nlohmann::ordered_json ToJson(const GameResult& result);
[[nodiscard]] nlohmann::ordered_json ToJson(const LibraryResult& result);
void Print(const GameResult& result, FILE* out = stdout);
void Print(const LibraryResult& result, FILE* out = stdout);
[[nodiscard]] bool WriteJson(const std::filesystem::path& path,
                             const nlohmann::ordered_json& value,
                             std::string* error);

} // namespace Loader::ImportAudit

#endif /* EMULATOR_INCLUDE_EMULATOR_LOADER_IMPORTAUDIT_H_ */
