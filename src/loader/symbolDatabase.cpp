#include "loader/symbolDatabase.h"

#include "common/file.h"
#include "common/magicEnum.h"

#include <fmt/format.h>

namespace Loader {

constexpr char LIB_PREFIX[] = "libSce";

static std::string UpdateName(const std::string& str) {
	return Common::StartsWith(str, LIB_PREFIX) ? Common::RemoveFirst(str, 6) : str;
}

std::string SymbolDatabase::GenerateName(const SymbolResolve& s) {
	auto library = UpdateName(s.library);
	auto module  = UpdateName(s.module);
	return fmt::format("{}[{}_v{}][{}_v{}.{}][{}]", s.name.c_str(), library.c_str(),
	                   s.library_version, module.c_str(), s.module_version_major,
	                   s.module_version_minor, Common::EnumName(s.type).c_str());
}

void SymbolDatabase::Add(const SymbolResolve& s, uint64_t vaddr) {
	SymbolRecord r {};
	r.name  = GenerateName(s);
	r.vaddr = vaddr;
	m_map.insert_or_assign(r.name, m_symbols.size());
	m_symbols.push_back(r);
}

void SymbolDatabase::Add(const SymbolResolve& s, uint64_t vaddr, const std::string& dbg_name) {
	SymbolRecord r {};
	r.name     = GenerateName(s);
	r.vaddr    = vaddr;
	r.dbg_name = dbg_name;
	m_map.insert_or_assign(r.name, m_symbols.size());
	m_symbols.push_back(r);
}

void SymbolDatabase::DbgDump(const std::string& folder, const std::string& file_name) {
	auto folder_str = Common::FixDirectorySlash(folder);

	Common::File::CreateDirectories(folder_str);

	Common::File f;
	f.Create(folder_str + file_name);

	for (const auto& sym: m_symbols) {
		f.Printf("%" PRIx64 " %s\n", sym.vaddr, sym.name.c_str());
	}

	f.Close();
}

const SymbolRecord* SymbolDatabase::Find(const SymbolResolve& s) const {
	return FindExact(GenerateName(s));
}

const SymbolRecord* SymbolDatabase::FindExact(const std::string& qualified_name) const {
	auto it = m_map.find(qualified_name);
	if (it == m_map.end()) {
		return nullptr;
	}
	auto index = it->second;
	if (index >= m_symbols.size()) {
		return nullptr;
	}
	return &m_symbols[index];
}

const SymbolRecord* SymbolDatabase::FindExactOrCompatible(
    const std::string& qualified_name) const {
if (const auto* rec = FindExact(qualified_name); rec != nullptr) {
return rec;
}

const auto try_alias =
    [&](const char* from_qualifier, const char* to_qualifier,
        const char* required_nid) -> const SymbolRecord* {
if (required_nid != nullptr) {
const std::string prefix = std::string(required_nid) + "[";
if (!Common::StartsWith(qualified_name, prefix)) {
return nullptr;
}
}

const std::string from = from_qualifier;
const auto        pos  = qualified_name.find(from);

if (pos == std::string::npos) {
return nullptr;
}

auto candidate = qualified_name;
candidate.replace(pos, from.size(), to_qualifier);

return FindExact(candidate);
};

// PS5 AGC exports are seen under both Agc and Graphics5 identities.
// Preserve the NID and symbol type; only canonicalize the verified
// library/module qualification.
if (const auto* rec =
        try_alias("[Agc_v1][Agc_v1.1]",
                  "[Graphics5_v1][Graphics5_v1.1]", nullptr);
    rec != nullptr) {
return rec;
}

// Json2 constructor observed with the Json module identity.
if (const auto* rec =
        try_alias("[Json2_v1][Json_v1.1]",
                  "[Json2_v1][Json2_v1.1]", nullptr);
    rec != nullptr) {
return rec;
}

// SslInit observed through Ssl module version 2.1. Do not generalize
// this version compatibility until additional ABI-compatible exports
// are verified.
if (const auto* rec =
        try_alias("[Ssl_v1][Ssl_v2.1]",
                  "[Ssl_v1][Ssl_v1.1]", "hdpVEUDFW3s");
    rec != nullptr) {
return rec;
}

return nullptr;
}
const SymbolRecord* SymbolDatabase::FindByNid(const std::string& nid, SymbolType type) const {
	auto prefix = nid + "[";
	auto suffix = fmt::format("[{}]", Common::EnumName(type).c_str());

	for (const auto& symbol: m_symbols) {
		if (Common::StartsWith(symbol.name, prefix) && Common::EndsWith(symbol.name, suffix)) {
			return &symbol;
		}
	}

	return nullptr;
}

const SymbolRecord* SymbolDatabase::FindByName(const std::string& name, SymbolType type) const {
	auto prefix = name + "[";
	auto suffix = fmt::format("[{}]", Common::EnumName(type).c_str());

	for (const auto& symbol: m_symbols) {
		if (Common::StartsWith(symbol.name, prefix) && Common::EndsWith(symbol.name, suffix)) {
			return &symbol;
		}
	}

	return nullptr;
}

} // namespace Loader
