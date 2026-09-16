#include "loader/importAudit.h"

#include "common/magicEnum.h"
#include "common/stringUtils.h"
#include "libs/libs.h"
#include "loader/elf.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <system_error>
#include <unordered_map>

namespace Loader::ImportAudit {

namespace {

std::string NormalizePath(const std::filesystem::path& path) {
	return path.lexically_normal().generic_string();
}

std::string Lower(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return value;
}

std::string BinaryDisplayPath(const std::filesystem::path& file,
                              const std::filesystem::path& root) {
	const auto rel = file.lexically_normal().lexically_relative(root.lexically_normal());
	if (!rel.empty() && !Common::StartsWith(rel.generic_string(), "../") && rel != "..") {
		return rel.generic_string();
	}
	return NormalizePath(file);
}

int StatusPriority(Status status) {
	switch (status) {
		case Status::Malformed: return 7;
		case Status::MissingHle: return 6;
		case Status::AliasCandidate: return 5;
		case Status::WeakUnresolved: return 4;
		case Status::GuestModule: return 3;
		case Status::CompatibleHle: return 2;
		case Status::ExactHle: return 1;
	}
	return 0;
}

int BlockSortRank(Status status) {
	switch (status) {
		case Status::AliasCandidate: return 0;
		case Status::MissingHle: return 1;
		case Status::Malformed: return 2;
		case Status::WeakUnresolved: return 3;
		case Status::GuestModule: return 4;
		case Status::CompatibleHle: return 5;
		case Status::ExactHle: return 6;
	}
	return 7;
}

void MergeImportRecord(ImportRecord* dst, const ImportRecord& src) {
	if (dst == nullptr) return;
	const bool had_strong = !dst->weak;
	const bool has_strong = !src.weak;
	dst->weak = dst->weak && src.weak;
	dst->references += src.references;

	if ((!had_strong && has_strong) || StatusPriority(src.status) > StatusPriority(dst->status)) {
		dst->status        = src.status;
		dst->resolved_name = src.resolved_name;
		dst->candidates    = src.candidates;
	}

	dst->candidates.insert(dst->candidates.end(), src.candidates.begin(), src.candidates.end());
	std::sort(dst->candidates.begin(), dst->candidates.end());
	dst->candidates.erase(std::unique(dst->candidates.begin(), dst->candidates.end()),
	                      dst->candidates.end());
}

bool IsModuleExtension(const std::filesystem::path& path) {
	const auto ext = Lower(path.extension().generic_string());
	return ext == ".sprx" || ext == ".prx";
}

SymbolType ConvertSymbolType(unsigned char type, bool* ok) {
	*ok = true;
	switch (type) {
		case STT_NOTYPE: return SymbolType::NoType;
		case STT_FUNC: return SymbolType::Func;
		case STT_OBJECT: return SymbolType::Object;
		default:
			*ok = false;
			return SymbolType::Unknown;
	}
}

std::string SafeSymbolName(const DynamicInfo& info, const Elf64_Sym& symbol, bool* ok) {
	*ok = false;
	if (info.str_table == nullptr || symbol.st_name >= info.str_table_size) return {};
	const auto* begin = info.str_table + symbol.st_name;
	const auto left = static_cast<size_t>(info.str_table_size - symbol.st_name);
	const auto* end = static_cast<const char*>(std::memchr(begin, '\0', left));
	if (end == nullptr) return {};
	*ok = true;
	return std::string(begin, end);
}

nlohmann::ordered_json ImportToJson(const ImportRecord& input) {
	auto candidates = input.candidates;
	std::sort(candidates.begin(), candidates.end());
	candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

	nlohmann::ordered_json value;
	value["qualified_name"] = input.qualified_name;
	value["nid"]            = input.nid;
	value["symbol_type"]    = Common::EnumName(input.symbol_type);
	value["weak"]           = input.weak;
	value["status"]         = StatusName(input.status);
	value["resolved_name"]  = input.resolved_name;
	value["candidates"]     = candidates;
	value["references"]     = input.references;
	value["binary"]         = input.binary;
	return value;
}

nlohmann::ordered_json GameToJsonObject(const GameResult& input) {
	nlohmann::ordered_json value;
	value["root"]       = NormalizePath(input.root);
	value["title_id"]   = input.title_id;
	value["title_name"] = input.title_name;
	value["partial"]    = input.partial;

	auto binaries = input.binaries;
	std::sort(binaries.begin(), binaries.end(),
	          [](const auto& a, const auto& b) { return a.path < b.path; });
	value["binaries"] = nlohmann::ordered_json::array();
	for (auto& binary: binaries) {
		nlohmann::ordered_json b;
		b["path"]       = binary.path;
		b["parsed"]     = binary.parsed;
		b["diagnostic"] = binary.diagnostic;
		std::sort(binary.imports.begin(), binary.imports.end(), [](const auto& a, const auto& b) {
			return ImportKey(a) < ImportKey(b);
		});
		b["imports"] = nlohmann::ordered_json::array();
		for (const auto& import: binary.imports) b["imports"].push_back(ImportToJson(import));
		value["binaries"].push_back(std::move(b));
	}

	auto imports = input.imports;
	std::sort(imports.begin(), imports.end(),
	          [](const auto& a, const auto& b) { return ImportKey(a) < ImportKey(b); });
	value["imports"] = nlohmann::ordered_json::array();
	for (const auto& import: imports) value["imports"].push_back(ImportToJson(import));
	return value;
}

void PrintImport(const ImportRecord& import, FILE* out) {
	std::fprintf(out, "%s\n", import.qualified_name.c_str());
	if (!import.resolved_name.empty()) {
		std::fprintf(out, "  resolved: %s\n", import.resolved_name.c_str());
	}
	for (const auto& candidate: import.candidates) {
		std::fprintf(out, "  candidate: %s\n", candidate.c_str());
	}
	std::fprintf(out, "  references: %" PRIu64 "\n", import.references);
}

} // namespace

const char* StatusName(Status status) {
	switch (status) {
		case Status::ExactHle: return "ExactHle";
		case Status::CompatibleHle: return "CompatibleHle";
		case Status::AliasCandidate: return "AliasCandidate";
		case Status::GuestModule: return "GuestModule";
		case Status::WeakUnresolved: return "WeakUnresolved";
		case Status::MissingHle: return "MissingHle";
		case Status::Malformed: return "Malformed";
	}
	return "Malformed";
}

ImportRecord ClassifyImport(const RuntimeLinker& linker, const Program& requester,
                            const SymbolResolve& request, SymbolType type, bool weak,
                            std::string binary) {
	ImportRecord result;
	result.binary         = std::move(binary);
	result.nid            = request.name;
	result.symbol_type    = type;
	result.weak           = weak;
	result.request        = request;
	result.qualified_name = SymbolDatabase::GenerateName(request);

	const auto inspected = linker.InspectImport(requester, request);
	switch (inspected.source) {
		case ImportResolutionSource::ExactHle:
			result.status        = Status::ExactHle;
			result.resolved_name = inspected.record.name;
			return result;
		case ImportResolutionSource::CompatibleHle:
			result.status        = Status::CompatibleHle;
			result.resolved_name = inspected.record.name;
			return result;
		case ImportResolutionSource::GuestModule:
			result.status        = Status::GuestModule;
			result.resolved_name = inspected.record.name;
			return result;
		case ImportResolutionSource::None: break;
	}

	if (weak) {
		result.status = Status::WeakUnresolved;
		return result;
	}

	if (const auto* symbols = linker.Symbols(); symbols != nullptr) {
		for (const auto& candidate: symbols->FindAllByNid(request.name, type)) {
			result.candidates.push_back(candidate.name);
		}
	}
	std::sort(result.candidates.begin(), result.candidates.end());
	result.candidates.erase(std::unique(result.candidates.begin(), result.candidates.end()),
	                        result.candidates.end());
	result.status = result.candidates.empty() ? Status::MissingHle : Status::AliasCandidate;
	return result;
}

std::string ImportKey(const ImportRecord& record) {
	return fmt::format("{}|{}|{}@{}|{}@{}.{}", record.request.name,
	                   Common::EnumName(record.symbol_type), record.request.library,
	                   record.request.library_version, record.request.module,
	                   record.request.module_version_major, record.request.module_version_minor);
}

BinaryResult AuditProgram(const RuntimeLinker& linker, const Program& program,
                          const std::filesystem::path& game_root) {
	BinaryResult result;
	result.path   = BinaryDisplayPath(program.file_name, game_root);
	result.parsed = true;

	const auto* info = program.dynamic_info.get();
	if (info == nullptr) return result;

	const bool has_relocations = info->jmprela_table_size != 0 || info->rela_table_total_size != 0;
	if (!has_relocations) return result;

	auto malformed = [&](std::string diagnostic) {
		result.parsed     = false;
		result.diagnostic = std::move(diagnostic);
		result.imports.clear();
		return result;
	};

	if (info->str_table == nullptr || info->symbol_table == nullptr) {
		return malformed("missing dynamic string/symbol metadata");
	}
	if (info->symbol_table_entry_size != 0 &&
	    info->symbol_table_entry_size != sizeof(Elf64_Sym)) {
		return malformed("unsupported dynamic symbol entry size");
	}
	if (info->symbol_table_total_size % sizeof(Elf64_Sym) != 0) {
		return malformed("dynamic symbol table size is not entry-aligned");
	}
	if (info->rela_table_entry_size != 0 && info->rela_table_entry_size != sizeof(Elf64_Rela)) {
		return malformed("unsupported relocation entry size");
	}

	const auto symbol_count = info->symbol_table_total_size / sizeof(Elf64_Sym);
	std::map<std::string, ImportRecord> dedup;

	auto inspect_table = [&](const Elf64_Rela* table, uint64_t bytes, const char* table_name) -> bool {
		if (bytes == 0) return true;
		if (table == nullptr || bytes % sizeof(Elf64_Rela) != 0) {
			result.diagnostic = std::string(table_name) + " relocation table is malformed";
			return false;
		}

		const auto count = bytes / sizeof(Elf64_Rela);
		for (uint64_t index = 0; index < count; ++index) {
			const auto& relocation = table[index];
			const auto relocation_type = relocation.GetType();
			if (relocation_type != R_X86_64_64 && relocation_type != R_X86_64_GLOB_DAT &&
			    relocation_type != R_X86_64_JUMP_SLOT) {
				continue;
			}

			const auto symbol_index = relocation.GetSymbol();
			if (symbol_index >= symbol_count) {
				result.diagnostic = fmt::format("{} relocation {} references symbol {} outside table",
				                                table_name, index, symbol_index);
				return false;
			}

			const auto& symbol = info->symbol_table[symbol_index];
			const auto bind = symbol.GetBind();
			if (bind == STB_LOCAL || symbol.st_shndx != 0) continue;
			if (bind != STB_GLOBAL && bind != STB_WEAK) {
				result.diagnostic = fmt::format("{} relocation {} has unsupported symbol binding {}",
				                                table_name, index, static_cast<unsigned>(bind));
				return false;
			}

			bool type_ok = false;
			const auto symbol_type = ConvertSymbolType(symbol.GetType(), &type_ok);
			if (!type_ok) {
				result.diagnostic = fmt::format("{} relocation {} has unsupported symbol type {}",
				                                table_name, index,
				                                static_cast<unsigned>(symbol.GetType()));
				return false;
			}

			bool name_ok = false;
			const auto encoded_name = SafeSymbolName(*info, symbol, &name_ok);
			if (!name_ok || encoded_name.empty()) {
				result.diagnostic = fmt::format("{} relocation {} has invalid symbol name",
				                                table_name, index);
				return false;
			}

			SymbolResolve request {};
			if (!DecodeImportIdentity(program, encoded_name, symbol_type, &request)) {
				result.diagnostic = fmt::format("{} relocation {} cannot decode import identity {}",
				                                table_name, index, encoded_name);
				return false;
			}

			auto record = ClassifyImport(linker, program, request, symbol_type,
			                             bind == STB_WEAK, result.path);
			const auto key = ImportKey(record);
			if (auto it = dedup.find(key); it != dedup.end()) {
				MergeImportRecord(&it->second, record);
			} else {
				dedup.emplace(key, std::move(record));
			}
		}
		return true;
	};

	if (!inspect_table(info->jmprela_table, info->jmprela_table_size, "PLT") ||
	    !inspect_table(info->rela_table, info->rela_table_total_size, "RELA")) {
		result.parsed = false;
		result.imports.clear();
		return result;
	}

	for (auto& [_, import]: dedup) result.imports.push_back(std::move(import));
	return result;
}

GameResult AuditGame(RuntimeLinker* linker, const GameOptions& options) {
	GameResult result;
	if (linker == nullptr) {
		result.partial = true;
		return result;
	}

	std::error_code ec;
	const auto input = options.input.lexically_normal();
	std::filesystem::path root;
	std::filesystem::path eboot;
	if (std::filesystem::is_directory(input, ec) && !ec) {
		root  = input;
		eboot = root / "eboot.bin";
	} else {
		ec.clear();
		if (std::filesystem::is_regular_file(input, ec) && !ec) {
			root  = input.parent_path();
			eboot = input;
		} else {
			result.root    = input;
			result.partial = true;
			result.binaries.push_back({NormalizePath(input), false, "audit input does not exist", {}});
			return result;
		}
	}

	result.root     = root.lexically_normal();
	result.title_id = root.filename().generic_string();

	std::map<std::string, std::vector<std::filesystem::path>> basename_index;
	if (std::filesystem::is_directory(root, ec) && !ec) {
		std::filesystem::recursive_directory_iterator it(
		    root, std::filesystem::directory_options::skip_permission_denied, ec);
		const std::filesystem::recursive_directory_iterator end;
		for (; !ec && it != end; it.increment(ec)) {
			std::error_code file_ec;
			if (!it->is_regular_file(file_ec) || file_ec) continue;
			basename_index[Lower(it->path().filename().generic_string())].push_back(it->path());
		}
	}
	for (auto& [_, paths]: basename_index) {
		std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) {
			return NormalizePath(a) < NormalizePath(b);
		});
	}

	std::vector<std::filesystem::path> queue;
	std::set<std::string>              queued;
	auto add_candidate = [&](const std::filesystem::path& path) {
		const auto key = Lower(NormalizePath(path));
		if (queued.insert(key).second) queue.push_back(path.lexically_normal());
	};
	add_candidate(eboot);

	for (const auto& dir: {root, root / "sce_module", root / "sce_modules"}) {
		ec.clear();
		if (!std::filesystem::is_directory(dir, ec) || ec) continue;
		for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
			std::error_code file_ec;
			if (it->is_regular_file(file_ec) && !file_ec && IsModuleExtension(it->path())) {
				add_candidate(it->path());
			}
		}
	}

	std::vector<Program*> loaded;
	for (size_t cursor = 0; cursor < queue.size(); ++cursor) {
		const auto path = queue[cursor];
		ec.clear();
		if (!std::filesystem::is_regular_file(path, ec) || ec) {
			result.partial = true;
			result.binaries.push_back({BinaryDisplayPath(path, root), false,
			                           "candidate binary does not exist", {}});
			continue;
		}

		auto* program = linker->LoadProgram(path);
		if (program == nullptr) {
			result.partial = true;
			result.binaries.push_back({BinaryDisplayPath(path, root), false,
			                           linker->LastLoadError().empty() ? "executable load failed"
			                                                           : linker->LastLoadError(),
			                           {}});
			continue;
		}
		loaded.push_back(program);

		if (program->dynamic_info != nullptr) {
			for (const auto* needed: program->dynamic_info->needed) {
				if (needed == nullptr || *needed == '\0') continue;
				const auto key = Lower(std::filesystem::path(needed).filename().generic_string());
				if (auto found = basename_index.find(key); found != basename_index.end()) {
					for (const auto& candidate: found->second) add_candidate(candidate);
				}
			}
		}
	}

	for (const auto* program: loaded) {
		auto binary = AuditProgram(*linker, *program, root);
		if (!binary.parsed) result.partial = true;
		result.binaries.push_back(std::move(binary));
	}

	std::sort(result.binaries.begin(), result.binaries.end(),
	          [](const auto& a, const auto& b) { return a.path < b.path; });

	std::map<std::string, ImportRecord> game_imports;
	for (const auto& binary: result.binaries) {
		for (const auto& import: binary.imports) {
			const auto key = ImportKey(import);
			if (auto found = game_imports.find(key); found != game_imports.end()) {
				MergeImportRecord(&found->second, import);
			} else {
				game_imports.emplace(key, import);
			}
		}
	}
	for (auto& [_, import]: game_imports) result.imports.push_back(std::move(import));
	return result;
}

std::vector<std::filesystem::path> DiscoverGameRoots(const std::filesystem::path& root) {
	std::vector<std::filesystem::path> roots;
	std::error_code ec;
	if (!std::filesystem::is_directory(root, ec) || ec) return roots;

	auto maybe_add = [&](const std::filesystem::path& dir) {
		std::error_code file_ec;
		if (std::filesystem::is_regular_file(dir / "eboot.bin", file_ec) && !file_ec) {
			roots.push_back(dir.lexically_normal());
		}
	};
	maybe_add(root);

	std::filesystem::recursive_directory_iterator it(
	    root, std::filesystem::directory_options::skip_permission_denied, ec);
	const std::filesystem::recursive_directory_iterator end;
	for (; !ec && it != end; it.increment(ec)) {
		std::error_code dir_ec;
		if (it->is_directory(dir_ec) && !dir_ec) maybe_add(it->path());
	}

	std::sort(roots.begin(), roots.end(), [](const auto& a, const auto& b) {
		return NormalizePath(a) < NormalizePath(b);
	});
	roots.erase(std::unique(roots.begin(), roots.end(), [](const auto& a, const auto& b) {
		return NormalizePath(a) == NormalizePath(b);
	}), roots.end());
	return roots;
}

LibraryResult Aggregate(std::vector<GameResult> games) {
	LibraryResult result;
	result.games = std::move(games);
	std::sort(result.games.begin(), result.games.end(), [](const auto& a, const auto& b) {
		const auto ak = !a.title_id.empty() ? a.title_id : NormalizePath(a.root);
		const auto bk = !b.title_id.empty() ? b.title_id : NormalizePath(b.root);
		return ak < bk;
	});

	std::map<std::string, GlobalImportRecord> globals;
	for (const auto& game: result.games) {
		result.partial = result.partial || game.partial;
		const auto game_id = !game.title_id.empty() ? game.title_id : NormalizePath(game.root);
		for (const auto& binary: game.binaries) {
			for (const auto& import: binary.imports) {
				const auto key = ImportKey(import);
				auto found = globals.find(key);
				if (found == globals.end()) {
					GlobalImportRecord global;
					global.import = import;
					global.games.push_back(game_id);
					global.binaries.push_back(NormalizePath(game.root / binary.path));
					globals.emplace(key, std::move(global));
				} else {
					MergeImportRecord(&found->second.import, import);
					found->second.games.push_back(game_id);
					found->second.binaries.push_back(NormalizePath(game.root / binary.path));
				}
			}
		}
	}

	for (auto& [_, global]: globals) {
		std::sort(global.games.begin(), global.games.end());
		global.games.erase(std::unique(global.games.begin(), global.games.end()), global.games.end());
		std::sort(global.binaries.begin(), global.binaries.end());
		global.binaries.erase(std::unique(global.binaries.begin(), global.binaries.end()),
		                      global.binaries.end());
		result.global_imports.push_back(std::move(global));
	}

	std::sort(result.global_imports.begin(), result.global_imports.end(), [](const auto& a, const auto& b) {
		if (a.games.size() != b.games.size()) return a.games.size() > b.games.size();
		const auto ar = BlockSortRank(a.import.status);
		const auto br = BlockSortRank(b.import.status);
		if (ar != br) return ar < br;
		return a.import.qualified_name < b.import.qualified_name;
	});
	return result;
}

LibraryResult AuditLibrary(const LibraryOptions& options) {
	std::vector<GameResult> games;
	for (const auto& root: DiscoverGameRoots(options.root)) {
		RuntimeLinker linker;
		Libs::InitAll(linker.Symbols());
		games.push_back(AuditGame(&linker, {root}));
		linker.Clear();
	}
	return Aggregate(std::move(games));
}

nlohmann::ordered_json ToJson(const GameResult& result) {
	nlohmann::ordered_json root;
	root["schema_version"] = 1;
	root["mode"]           = "game";
	root["generated_by"]   = "ProsperoX";
	root["games"]          = nlohmann::ordered_json::array({GameToJsonObject(result)});
	root["global_imports"] = nlohmann::ordered_json::array();
	return root;
}

nlohmann::ordered_json ToJson(const LibraryResult& result) {
	nlohmann::ordered_json root;
	root["schema_version"] = 1;
	root["mode"]           = "library";
	root["generated_by"]   = "ProsperoX";
	root["games"]          = nlohmann::ordered_json::array();
	for (const auto& game: result.games) root["games"].push_back(GameToJsonObject(game));
	root["global_imports"] = nlohmann::ordered_json::array();
	for (const auto& global: result.global_imports) {
		auto value = ImportToJson(global.import);
		value["games"]    = global.games;
		value["binaries"] = global.binaries;
		root["global_imports"].push_back(std::move(value));
	}
	return root;
}

void Print(const GameResult& result, FILE* out) {
	uint64_t counts[7] {};
	for (const auto& import: result.imports) {
		counts[static_cast<size_t>(import.status)]++;
	}
	std::fprintf(out,
	             "GAME %s %s\n  binaries=%zu imports=%zu exact=%" PRIu64
	             " compatible=%" PRIu64 " guest=%" PRIu64 " aliases=%" PRIu64
	             " missing=%" PRIu64 " weak=%" PRIu64 " malformed=%" PRIu64 "\n",
	             result.title_id.c_str(), result.title_name.c_str(), result.binaries.size(),
	             result.imports.size(), counts[static_cast<size_t>(Status::ExactHle)],
	             counts[static_cast<size_t>(Status::CompatibleHle)],
	             counts[static_cast<size_t>(Status::GuestModule)],
	             counts[static_cast<size_t>(Status::AliasCandidate)],
	             counts[static_cast<size_t>(Status::MissingHle)],
	             counts[static_cast<size_t>(Status::WeakUnresolved)],
	             counts[static_cast<size_t>(Status::Malformed)]);

	for (const auto& import: result.imports) {
		if (import.status != Status::AliasCandidate && import.status != Status::MissingHle) continue;
		std::fprintf(out, "\n[%s]\n", StatusName(import.status));
		PrintImport(import, out);
	}
	for (const auto& binary: result.binaries) {
		if (binary.parsed) continue;
		std::fprintf(out, "\n[MALFORMED]\n%s\n  %s\n", binary.path.c_str(),
		             binary.diagnostic.c_str());
	}
}

void Print(const LibraryResult& result, FILE* out) {
	std::fprintf(out, "LIBRARY games=%zu global_imports=%zu partial=%s\n", result.games.size(),
	             result.global_imports.size(), result.partial ? "true" : "false");
	for (const auto& global: result.global_imports) {
		if (global.import.status != Status::AliasCandidate &&
		    global.import.status != Status::MissingHle &&
		    global.import.status != Status::Malformed) {
			continue;
		}
		std::fprintf(out, "\n[%s] affected_games=%zu\n", StatusName(global.import.status),
		             global.games.size());
		PrintImport(global.import, out);
	}
	std::fprintf(out, "\n");
	for (const auto& game: result.games) Print(game, out);
}

bool WriteJson(const std::filesystem::path& path, const nlohmann::ordered_json& value,
               std::string* error) {
	try {
		if (!path.parent_path().empty()) {
			std::error_code ec;
			std::filesystem::create_directories(path.parent_path(), ec);
			if (ec) {
				if (error != nullptr) *error = ec.message();
				return false;
			}
		}
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file) {
			if (error != nullptr) *error = "cannot create JSON output";
			return false;
		}
		file << value.dump(2) << '\n';
		if (!file.good()) {
			if (error != nullptr) *error = "failed while writing JSON output";
			return false;
		}
		return true;
	} catch (const std::exception& exception) {
		if (error != nullptr) *error = exception.what();
		return false;
	}
}

} // namespace Loader::ImportAudit
