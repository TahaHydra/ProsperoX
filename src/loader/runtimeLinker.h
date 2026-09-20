#ifndef EMULATOR_INCLUDE_EMULATOR_LOADER_RUNTIMELINKER_H_
#define EMULATOR_INCLUDE_EMULATOR_LOADER_RUNTIMELINKER_H_

#include "common/abi.h"
#include "common/common.h"
#include "common/stringUtils.h"
#include "common/threads.h"
#include "loader/symbolDatabase.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Loader {

class Elf64;
struct Elf64_Sym;
struct Elf64_Rela;
class RuntimeLinker;

using module_func_t                          = KYTY_SYSV_ABI int (*)(size_t args, const void* argp);
using application_heap_free_func_t           = KYTY_SYSV_ABI void (*)(void*);
using application_heap_malloc_func_t         = KYTY_SYSV_ABI void* (*)(uint64_t);
using application_heap_posix_memalign_func_t = KYTY_SYSV_ABI int (*)(void**, uint64_t, uint64_t);

struct ModuleId {
	bool operator==(const ModuleId& other) const {
		return version_major == other.version_major && version_minor == other.version_minor &&
		       name == other.name;
	}

	std::string id;
	int         version_major;
	int         version_minor;
	std::string name;
};

struct LibraryId {
	bool operator==(const LibraryId& other) const {
		return version == other.version && name == other.name;
	}

	std::string id;
	int         version;
	std::string name;
};

struct ThreadLocalStorage {
	struct Block {
		uint8_t*                     ptr        = nullptr;
		application_heap_free_func_t free_func  = nullptr;
		bool                         vm_alloc   = false;
		uint64_t                     alloc_size = 0;
		// Where the TLS image starts inside the allocation. The block is
		// allocated low enough that the thread pointer above the image can
		// still satisfy the TCB's alignment.
		uint64_t                     image_offset = 0;
	};

	~ThreadLocalStorage();

	uint64_t image_vaddr   = 0;
	uint64_t init_size     = 0;
	uint64_t image_size    = 0;
	uint64_t tcb_offset    = 0;
	uint64_t handler_vaddr = 0;

	std::vector<uint8_t>           init_image;
	std::unordered_map<int, Block> tlss;
	Common::Mutex                  mutex;
};

struct DynamicInfo {
	void*    hash_table      = nullptr;
	uint64_t hash_table_size = 0;

	char*    str_table      = nullptr;
	uint64_t str_table_size = 0;

	Elf64_Sym* symbol_table            = nullptr;
	uint64_t   symbol_table_total_size = 0;
	uint64_t   symbol_table_entry_size = 0;

	uint64_t init_vaddr          = 0;
	uint64_t fini_vaddr          = 0;
	uint64_t init_array_vaddr    = 0;
	uint64_t fini_array_vaddr    = 0;
	uint64_t preinit_array_vaddr = 0;
	uint64_t init_array_size     = 0;
	uint64_t fini_array_size     = 0;
	uint64_t preinit_array_size  = 0;
	uint64_t pltgot_vaddr        = 0;

	Elf64_Rela* jmprela_table      = nullptr;
	uint64_t    jmprela_table_size = 0;

	Elf64_Rela* rela_table            = nullptr;
	uint64_t    rela_table_total_size = 0;
	uint64_t    rela_table_entry_size = 0;

	uint64_t relative_count = 0;

	uint64_t debug   = 0;
	uint64_t textrel = 0;
	uint64_t flags   = 0;

	const char* so_name = nullptr;

	std::vector<const char*> needed;
	std::vector<ModuleId>    export_modules;
	std::vector<ModuleId>    import_modules;
	std::vector<LibraryId>   export_libs;
	std::vector<LibraryId>   import_libs;
};

struct Program {
	Program();
	~Program();

	int32_t                      unique_id = -1;
	RuntimeLinker*               rt        = nullptr;
	std::filesystem::path        file_name;
	std::unique_ptr<Elf64>       elf;
	std::unique_ptr<DynamicInfo> dynamic_info;
	uint64_t                     base_vaddr        = 0;
	uint64_t                     base_size         = 0;
	uint64_t                     base_size_aligned = 0;
	uint64_t                     mapped_size       = 0;
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
	uint64_t red_zone_trampoline_vaddr = 0;
	uint64_t red_zone_trampoline_size  = 0;
#endif
	std::unique_ptr<SymbolDatabase> export_symbols;
	std::unique_ptr<SymbolDatabase> import_symbols;
	ThreadLocalStorage              tls;
	bool                            fail_if_global_not_resolved = true;
	bool                            dbg_print_reloc             = false;
	bool                            relocated                   = false;
	uint64_t                        proc_param_vaddr            = 0;
	uint64_t                        custom_call_plt_vaddr       = 0;
	uint32_t                        custom_call_plt_num         = 0;
};

enum class ImportResolutionSource {
	None,
	ExactHle,
	CompatibleHle,
	GuestModule,
};

struct ImportResolutionInspection {
	ImportResolutionSource source = ImportResolutionSource::None;
	SymbolRecord           record {};
};

[[nodiscard]] inline bool DecodeImportIdentity(const Program& program,
                                               const std::string& encoded_name,
                                               SymbolType type, SymbolResolve* out) {
	if (out == nullptr || program.dynamic_info == nullptr) {
		return false;
	}

	const auto ids = Common::Split(encoded_name, '#');
	if (ids.size() != 3) {
		return false;
	}

	const auto lib = std::find_if(program.dynamic_info->import_libs.begin(),
	                              program.dynamic_info->import_libs.end(),
	                              [&](const LibraryId& value) { return value.id == ids[1]; });
	const auto mod = std::find_if(program.dynamic_info->import_modules.begin(),
	                              program.dynamic_info->import_modules.end(),
	                              [&](const ModuleId& value) { return value.id == ids[2]; });
	if (lib == program.dynamic_info->import_libs.end() ||
	    mod == program.dynamic_info->import_modules.end()) {
		return false;
	}

	*out = {ids[0], lib->name, lib->version, mod->name, mod->version_major,
	        mod->version_minor, type};
	return true;
}

class RuntimeLinker {
public:
	RuntimeLinker();
	virtual ~RuntimeLinker();
	void Clear();

	KYTY_CLASS_NO_COPY(RuntimeLinker);

	void DbgDump(const std::string& folder);

	Program* LoadProgram(const std::filesystem::path& elf_name);
	[[nodiscard]] const std::string& LastLoadError() const { return m_last_load_error; }
	void     SaveMainProgram(const std::filesystem::path& elf_name);
	void     SaveProgram(Program* program, const std::filesystem::path& elf_name);
	void     UnloadProgram(Program* program);

	[[nodiscard]] uint64_t GetEntry();
	[[nodiscard]] uint64_t GetProcParam();

	void RelocateAll();
	void RelocateProgram(Program* program);

	void  Execute(const std::filesystem::path& game_patch = {});
	int   StartModule(Program* program, size_t args, const void* argp, module_func_t func);
	int   StopModule(Program* program, size_t args, const void* argp, module_func_t func);
	void  StartAllModules();
	void  StopAllModules();
	void  DeleteTlss(int thread_id);
	void  SetApplicationHeapApi(void* const api[10]);
	void* ApplicationHeapMalloc(uint64_t size);
	void* ApplicationHeapMemalign(uint64_t alignment, uint64_t size);

	void Resolve(const std::string& name, SymbolType type, Program* program, SymbolRecord* out_info,
	             bool* bind_self);
	bool ResolveLoadedSymbol(const std::string& qualified_name, SymbolRecord* out_info);
	uint64_t ResolveImport(uint64_t record_id);
	[[noreturn]] void RejectLegacyImport(Program* program, uint64_t index, uint64_t caller);

	[[nodiscard]] ImportResolutionInspection InspectImport(const Program& requester,
	                                                       const SymbolResolve& request) const;

	SymbolDatabase*       Symbols() { return m_symbols.get(); }
	const SymbolDatabase* Symbols() const { return m_symbols.get(); }

	static uint64_t ReadFromElf(Program* program, uint64_t vaddr);
	Program*        FindProgramByAddr(uint64_t vaddr);
	Program*        FindProgramById(int32_t id);
	Program*        FindProgramByFileName(const std::filesystem::path& elf_name);

	// Where a module's TLS image starts and ends relative to its thread
	// pointer. The guest reads thread-locals at negative offsets from that
	// pointer, so the distance to it is exactly the aligned block size:
	// rounding it up to suit the TCB would move every variable in the module.
	struct TlsGeometry {
		uint64_t block_size = 0;
		uint64_t tcb_offset = 0;
	};
	[[nodiscard]] static TlsGeometry TlsLayoutFor(uint64_t memsz, uint64_t align) noexcept;

	static uint8_t* TlsGetAddr(Program* program);
	static void     DeleteTls(Program* program, int thread_id);

	void StackTrace(uint64_t frame_ptr, uint64_t stack_ptr);

private:
	static void LoadProgramToMemory(Program* program);
	static void ParseProgramDynamicInfo(Program* program);
	static void CreateSymbolDatabase(Program* program);
	static void Relocate(Program* program);
	static void DeleteProgram(Program* program);
	static void SetupTlsHandler(Program* program);
	void        PreloadAdjacentPrograms();

	Program* FindProgram(const ModuleId& m, const LibraryId& l);

	static const ModuleId*  FindModule(const Program& program, const std::string& id);
	static const LibraryId* FindLibrary(const Program& program, const std::string& id);

	std::vector<Program*>           m_programs;
	std::vector<Program*>           m_started_modules;
	std::string                     m_last_load_error;
	std::unique_ptr<SymbolDatabase> m_symbols;
	bool                            m_relocated = false;
	Common::Mutex                   m_mutex;

	application_heap_malloc_func_t         m_application_heap_malloc         = nullptr;
	application_heap_free_func_t           m_application_heap_free           = nullptr;
	application_heap_posix_memalign_func_t m_application_heap_posix_memalign = nullptr;
};

inline ImportResolutionInspection RuntimeLinker::InspectImport(const Program& /*requester*/,
                                                               const SymbolResolve& request) const {
	const auto qualified = SymbolDatabase::GenerateName(request);
	if (m_symbols != nullptr) {
		if (const auto* exact = m_symbols->FindExact(qualified); exact != nullptr) {
			return {ImportResolutionSource::ExactHle, *exact};
		}
		if (const auto* compatible = m_symbols->FindExactOrCompatible(qualified);
		    compatible != nullptr) {
			return {ImportResolutionSource::CompatibleHle, *compatible};
		}
	}

	const ModuleId  wanted_module {"", request.module_version_major, request.module_version_minor,
	                               request.module};
	const LibraryId wanted_library {"", request.library_version, request.library};

	for (const auto* provider: m_programs) {
		if (provider == nullptr || provider->dynamic_info == nullptr ||
		    provider->export_symbols == nullptr) {
			continue;
		}
		const auto& modules = provider->dynamic_info->export_modules;
		const auto& libs    = provider->dynamic_info->export_libs;
		if (std::find(modules.begin(), modules.end(), wanted_module) == modules.end() ||
		    std::find(libs.begin(), libs.end(), wanted_library) == libs.end()) {
			continue;
		}
		if (const auto* record = provider->export_symbols->FindExact(qualified); record != nullptr) {
			return {ImportResolutionSource::GuestModule, *record};
		}
	}

	return {};
}

#if defined(KYTY_VIRTUAL_MEMORY_ALLOCATION_TESTS)
bool TestMainEntryUsesGuestStack();
bool TestModuleRelocationUsesWritableHostMapping();
#endif

} // namespace Loader

#endif /* EMULATOR_INCLUDE_EMULATOR_LOADER_RUNTIMELINKER_H_ */
