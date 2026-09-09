#include "loader/elf.h"

#include "common/assert.h"
#include "common/file.h"
#include "common/logging/log.h"

#include <cstring>
#include <memory>

namespace Loader {

static bool InRange(uint64_t offset, uint64_t size, uint64_t total) {
	return offset <= total && size <= total - offset;
}

bool Elf64::Reject(const char* reason) {
	Clear();
	m_error = reason;
	LOGF("ELF rejected: %s\n", reason);
	return false;
}

static std::unique_ptr<SelfHeader> LoadSelf(Common::File& f) {
	if (f.Remaining() < sizeof(SelfHeader)) {
		return nullptr;
	}

	auto self = std::make_unique<SelfHeader>();

	f.Read(self.get(), sizeof(SelfHeader));

	return self;
}

static std::unique_ptr<SelfSegment[]> LoadSelfSegments(Common::File& f, uint16_t num) {
	auto segs = std::make_unique<SelfSegment[]>(num);

	f.Read(segs.get(), sizeof(SelfSegment) * num);

	return segs;
}

static std::unique_ptr<Elf64_Ehdr> LoadEhdr64(Common::File& f) {
	if (f.Remaining() < sizeof(Elf64_Ehdr)) {
		return nullptr;
	}

	auto ehdr = std::make_unique<Elf64_Ehdr>();

	f.Read(ehdr.get(), sizeof(Elf64_Ehdr));

	return ehdr;
}

static void SaveEhdr64(Common::File& f, const Elf64_Ehdr* ehdr) {
	EXIT_IF(ehdr == nullptr);

	uint32_t bytes_written = 0;

	f.Write(ehdr, sizeof(Elf64_Ehdr), &bytes_written);

	EXIT_IF(bytes_written == 0);
}

static std::unique_ptr<Elf64_Phdr[]> LoadPhdr64(Common::File& f, uint64_t offset, Elf64_Half num) {
	auto phdr = std::make_unique<Elf64_Phdr[]>(num);

	f.Seek(offset);
	f.Read(phdr.get(), sizeof(Elf64_Phdr) * num);

	return phdr;
}

static void SavePhdr64(Common::File& f, uint64_t offset, Elf64_Half num, const Elf64_Phdr* phdr) {
	EXIT_IF(phdr == nullptr);

	uint32_t bytes_written = 0;

	f.Seek(offset);
	f.Write(phdr, sizeof(Elf64_Phdr) * num, &bytes_written);

	EXIT_IF(bytes_written == 0);
}

static std::unique_ptr<Elf64_Shdr[]> LoadShdr64(Common::File& f, uint64_t offset, Elf64_Half num) {
	if (num == 0) {
		return nullptr;
	}

	auto shdr = std::make_unique<Elf64_Shdr[]>(num);

	f.Seek(offset);
	f.Read(shdr.get(), sizeof(Elf64_Shdr) * num);

	return shdr;
}

static void SaveShdr64(Common::File& f, uint64_t offset, Elf64_Half num, const Elf64_Shdr* shdr) {
	if (num == 0) {
		return;
	}

	EXIT_IF(shdr == nullptr);

	uint32_t bytes_written = 0;

	f.Seek(offset);
	f.Write(shdr, sizeof(Elf64_Shdr) * num, &bytes_written);

	EXIT_IF(bytes_written == 0);
}

static std::unique_ptr<uint8_t[]> LoadDynamic64(Elf64* f, uint64_t offset, uint64_t size) {
	auto dynamic_data = std::make_unique<uint8_t[]>(size);

	// f.Seek(offset);
	// f.Read(dynamic_data, size);

	f->LoadSegment(reinterpret_cast<uint64_t>(dynamic_data.get()), offset, size);

	return dynamic_data;
}

static std::unique_ptr<char[]> LoadStrTable(Common::File& f, uint64_t offset, uint32_t size) {
	if (size == 0) {
		return nullptr;
	}

	auto str_table = std::make_unique<char[]>(size);
	f.Seek(offset);
	f.Read(str_table.get(), size);
	return str_table;
}

static void DbgPrintEhdr64(Elf64_Ehdr* ehdr, Common::File& f) {
	f.Printf("ehdr->e_ident = ");
	for (auto i: ehdr->e_ident) {
		f.Printf("%02x", i);
	}
	f.Printf("\n");

	f.Printf("ehdr->e_type = 0x%04" PRIx16 "\n", ehdr->e_type);
	f.Printf("ehdr->e_machine = 0x%04" PRIx16 "\n", ehdr->e_machine);
	f.Printf("ehdr->e_version = 0x%08" PRIx32 "\n", ehdr->e_version);

	f.Printf("ehdr->e_entry = 0x%016" PRIx64 "\n", ehdr->e_entry);
	f.Printf("ehdr->e_phoff = 0x%016" PRIx64 "\n", ehdr->e_phoff);
	f.Printf("ehdr->e_shoff = 0x%016" PRIx64 "\n", ehdr->e_shoff);
	f.Printf("ehdr->e_flags = 0x%08" PRIx32 "\n", ehdr->e_flags);
	f.Printf("ehdr->e_ehsize = 0x%04" PRIx16 "\n", ehdr->e_ehsize);
	f.Printf("ehdr->e_phentsize = 0x%04" PRIx16 "\n", ehdr->e_phentsize);
	f.Printf("ehdr->e_phnum = %" PRIu16 "\n", ehdr->e_phnum);
	f.Printf("ehdr->e_shentsize = 0x%04" PRIx16 "\n", ehdr->e_shentsize);
	f.Printf("ehdr->e_shnum = %" PRIu16 "\n", ehdr->e_shnum);
	f.Printf("ehdr->e_shstrndx = %" PRIu16 "\n", ehdr->e_shstrndx);
}

static void DbgPrintPhdr64(Elf64_Phdr* phdr, Common::File& f) {
	f.Printf("phdr->p_type = 0x%08" PRIx32 "\n", phdr->p_type);
	f.Printf("phdr->p_flags = 0x%08" PRIx32 "\n", phdr->p_flags);
	f.Printf("phdr->p_offset = 0x%016" PRIx64 "\n", phdr->p_offset);
	f.Printf("phdr->p_vaddr = 0x%016" PRIx64 "\n", phdr->p_vaddr);
	f.Printf("phdr->p_paddr = 0x%016" PRIx64 "\n", phdr->p_paddr);
	f.Printf("phdr->p_filesz = 0x%016" PRIx64 "\n", phdr->p_filesz);
	f.Printf("phdr->p_memsz = 0x%016" PRIx64 "\n", phdr->p_memsz);
	f.Printf("phdr->p_align = 0x%016" PRIx64 "\n", phdr->p_align);
}

static void DbgPrintShdr64(Elf64_Shdr* shdr, Common::File& f) {
	f.Printf("shdr->sh_name = %d\n", shdr->sh_name);
	f.Printf("shdr->sh_type = 0x%08" PRIx32 "\n", shdr->sh_type);
	f.Printf("shdr->sh_flags = 0x%016" PRIx64 "\n", shdr->sh_flags);
	f.Printf("shdr->sh_addr = 0x%016" PRIx64 "\n", shdr->sh_addr);
	f.Printf("shdr->sh_offset = 0x%016" PRIx64 "\n", shdr->sh_offset);
	f.Printf("shdr->sh_size = 0x%016" PRIx64 "\n", shdr->sh_size);
	f.Printf("shdr->sh_link = %" PRId32 "\n", shdr->sh_link);
	f.Printf("shdr->sh_info = 0x%08" PRIx32 "\n", shdr->sh_info);
	f.Printf("shdr->sh_addralign = 0x%016" PRIx64 "\n", shdr->sh_addralign);
	f.Printf("shdr->sh_entsize = 0x%016" PRIx64 "\n", shdr->sh_entsize);
}

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define DBG_NAME(tag)                                                                              \
	case tag: name = #tag; break;

static void DbgPrintDynamic64(const Elf64_Dyn* dyn, Common::File& f) {
	const char* name = "Unknown";
	switch (dyn->d_tag) {
		DBG_NAME(DT_OS_HASH)
		DBG_NAME(DT_HASH)
		DBG_NAME(DT_OS_STRTAB)
		DBG_NAME(DT_OS_STRSZ)
		DBG_NAME(DT_STRTAB)
		DBG_NAME(DT_STRSZ)
		DBG_NAME(DT_OS_SYMTAB)
		DBG_NAME(DT_SYMTAB)
		DBG_NAME(DT_OS_HASHSZ)
		DBG_NAME(DT_OS_SYMTABSZ)
		DBG_NAME(DT_INIT)
		DBG_NAME(DT_FINI)
		DBG_NAME(DT_OS_PLTGOT)
		DBG_NAME(DT_PLTGOT)
		DBG_NAME(DT_OS_JMPREL)
		DBG_NAME(DT_JMPREL)
		DBG_NAME(DT_OS_PLTRELSZ)
		DBG_NAME(DT_PLTRELSZ)
		DBG_NAME(DT_OS_PLTREL)
		DBG_NAME(DT_PLTREL)
		DBG_NAME(DT_OS_RELA)
		DBG_NAME(DT_RELA)
		DBG_NAME(DT_OS_RELASZ)
		DBG_NAME(DT_RELASZ)
		DBG_NAME(DT_OS_RELAENT)
		DBG_NAME(DT_RELAENT)
		DBG_NAME(DT_INIT_ARRAY)
		DBG_NAME(DT_INIT_ARRAYSZ)
		DBG_NAME(DT_FINI_ARRAY)
		DBG_NAME(DT_FINI_ARRAYSZ)
		DBG_NAME(DT_PREINIT_ARRAY)
		DBG_NAME(DT_PREINIT_ARRAYSZ)
		DBG_NAME(DT_OS_SYMENT)
		DBG_NAME(DT_SYMENT)
		DBG_NAME(DT_DEBUG)
		DBG_NAME(DT_TEXTREL)
		DBG_NAME(DT_FLAGS)
		DBG_NAME(DT_NEEDED)
		DBG_NAME(DT_OS_NEEDED_MODULE)
		DBG_NAME(DT_OS_NEEDED_MODULE_1)
		DBG_NAME(DT_OS_IMPORT_LIB)
		DBG_NAME(DT_OS_IMPORT_LIB_1)
		DBG_NAME(DT_OS_IMPORT_LIB_ATTR)
		DBG_NAME(DT_OS_FINGERPRINT)
		DBG_NAME(DT_OS_ORIGINAL_FILENAME)
		DBG_NAME(DT_OS_ORIGINAL_FILENAME_1)
		DBG_NAME(DT_OS_MODULE_INFO)
		DBG_NAME(DT_OS_MODULE_INFO_1)
		DBG_NAME(DT_OS_MODULE_ATTR)
		DBG_NAME(DT_SONAME)
		DBG_NAME(DT_OS_EXPORT_LIB)
		DBG_NAME(DT_OS_EXPORT_LIB_1)
		DBG_NAME(DT_OS_EXPORT_LIB_ATTR)
		DBG_NAME(DT_RELACOUNT)
		DBG_NAME(DT_NULL)
	}
	f.Printf("d_tag = 0x%016" PRIx64 ", d_val = 0x%016" PRIx64 ", name = %s\n", dyn->d_tag,
	         dyn->d_un.d_val, name);
}

Elf64::~Elf64() {
	Clear();
}

void Elf64::LoadSegment(uint64_t vaddr, uint64_t file_offset, uint64_t size) {
	EXIT_IF(m_f == nullptr);
	if (size == 0) return;
	EXIT_IF(size > UINT32_MAX || vaddr > UINT64_MAX - size);

	if (m_self != nullptr) {
		EXIT_IF(m_self_segments == nullptr);
		EXIT_IF(m_phdr == nullptr);

		for (uint16_t i = 0; i < m_self->segments_num; i++) {
			const auto& seg = m_self_segments[i];
			if ((seg.type & 0x800u) != 0) {
				auto phdr_id = ((seg.type >> 20u) & 0xFFFu);

				EXIT_IF(phdr_id >= m_ehdr->e_phnum);
				const auto& phdr = m_phdr[phdr_id];

				if (file_offset >= phdr.p_offset && file_offset < phdr.p_offset + phdr.p_filesz) {
					EXIT_NOT_IMPLEMENTED(seg.decompressed_size != phdr.p_filesz);
					EXIT_NOT_IMPLEMENTED(seg.compressed_size != seg.decompressed_size);

					auto offset = file_offset - phdr.p_offset;

					EXIT_NOT_IMPLEMENTED(!InRange(offset, size, seg.decompressed_size));

					m_f->Seek(offset + seg.offset);
					m_f->Read(reinterpret_cast<void*>(static_cast<uintptr_t>(vaddr)), size);

					return;
				}
			}
		}

		if (m_self->file_size <= m_f->Size() && m_f->Size() - m_self->file_size == size) {
			m_f->Seek(m_self->file_size);
			m_f->Read(reinterpret_cast<void*>(static_cast<uintptr_t>(vaddr)), size);

			return;
		}

		EXIT("missing self segment\n");
	} else {
		EXIT_IF(!InRange(file_offset, size, m_f->Size()));
		m_f->Seek(file_offset);
		m_f->Read(reinterpret_cast<void*>(static_cast<uintptr_t>(vaddr)), size);
	}
}

const Elf64_Dyn* Elf64::GetDynValue(Elf64_Sxword tag) const {
	for (uint64_t i = 0; i < m_dynamic_size / sizeof(Elf64_Dyn); ++i) {
		const auto* dyn = GetDynamic() + i;
		if (dyn->d_tag == DT_NULL) break;
		if (dyn->d_tag == tag) {
			return dyn;
		}
	}
	return nullptr;
}

std::vector<const Elf64_Dyn*> Elf64::GetDynList(Elf64_Sxword tag) const {
	std::vector<const Elf64_Dyn*> ret;
	for (uint64_t i = 0; i < m_dynamic_size / sizeof(Elf64_Dyn); ++i) {
		const auto* dyn = GetDynamic() + i;
		if (dyn->d_tag == DT_NULL) break;
		if (dyn->d_tag == tag) {
			ret.push_back(dyn);
		}
	}
	return ret;
}

bool Elf64::IsShared() const {
	return (m_ehdr->e_type == ET_DYNAMIC);
}

bool Elf64::IsNextGen() const {
	return (m_ehdr->e_ident[EI_ABIVERSION] == 2);
}

const char* Elf64::GetSectionName(int index) const {
	if (m_ehdr == nullptr || m_shdr == nullptr || m_str_table == nullptr || index < 0 ||
	    index >= m_ehdr->e_shnum) {
		return nullptr;
	}

	const auto name_offset = m_shdr[index].sh_name;
	if (name_offset >= m_str_table_size) {
		return nullptr;
	}

	const auto remaining = m_str_table_size - name_offset;
	if (std::memchr(m_str_table.get() + name_offset, '\0', remaining) == nullptr) {
		return nullptr;
	}

	return m_str_table.get() + name_offset;
}

void Elf64::Clear() {
	m_error.clear();
	m_dynamic_size = 0;
	m_dynamic_data_size = 0;
	if (m_f != nullptr) {
		m_f->Close();
	}

	m_f.reset();
	m_self.reset();
	m_ehdr.reset();
	m_self_segments.reset();
	m_phdr.reset();
	m_shdr.reset();
	m_str_table.reset();
	m_str_table_size = 0;
	m_dynamic.reset();
	m_dynamic_data.reset();
}

void Elf64::DbgDump(const std::string& folder) {
	auto folder_str = Common::FixDirectorySlash(folder);

	Common::File::CreateDirectories(folder_str);

	for (uint16_t i = 0; i < m_ehdr->e_phnum; i++) {
		if (m_phdr[i].p_filesz == 0u) {
			continue;
		}

		char str[512];
		int  s = snprintf(str, 512, "phdr_%03d", i);
		EXIT_NOT_IMPLEMENTED(s >= 512);

		Common::File fout;
		fout.Create(folder_str + str);

		auto buf = std::make_unique<char[]>(static_cast<uint32_t>(m_phdr[i].p_filesz));

		// m_f->Seek(m_phdr[i].p_offset);
		// m_f->Read(buf, static_cast<uint32_t>(m_phdr[i].p_filesz));

		LoadSegment(reinterpret_cast<uint64_t>(buf.get()), m_phdr[i].p_offset, m_phdr[i].p_filesz);

		fout.Write(buf.get(), static_cast<uint32_t>(m_phdr[i].p_filesz));

		fout.Close();
	}

	for (uint16_t i = 0; i < m_ehdr->e_shnum; i++) {
		if (m_shdr[i].sh_size == 0u || m_shdr[i].sh_type == 8) {
			continue;
		}

		char str[512];
		int  s = snprintf(str, 512, "shdr_%03d", i);
		EXIT_NOT_IMPLEMENTED(s >= 512);

		Common::File fout;
		fout.Create(folder_str + str);

		auto buf = std::make_unique<char[]>(static_cast<uint32_t>(m_shdr[i].sh_size));

		m_f->Seek(m_shdr[i].sh_offset);
		m_f->Read(buf.get(), static_cast<uint32_t>(m_shdr[i].sh_size));
		fout.Write(buf.get(), static_cast<uint32_t>(m_shdr[i].sh_size));

		fout.Close();
	}

	Common::File fout;

	fout.Create(folder_str + "ehdr.txt");
	DbgPrintEhdr64(m_ehdr.get(), fout);
	fout.Close();

	fout.Create(folder_str + "phdr.txt");
	for (uint16_t i = 0; i < m_ehdr->e_phnum; i++) {
		fout.Printf("--- phdr [%d] ---\n", i);
		DbgPrintPhdr64(m_phdr.get() + i, fout);
	}
	fout.Close();

	fout.Create(folder_str + "shdr.txt");
	for (uint16_t i = 0; i < m_ehdr->e_shnum; i++) {
		const char* section_name = GetSectionName(i);
		fout.Printf("--- shdr [%d] %s ---\n", i, section_name != nullptr ? section_name : "");
		DbgPrintShdr64(m_shdr.get() + i, fout);
	}
	fout.Close();

	fout.Create(folder_str + "dynamic.txt");
	for (uint64_t i = 0; i < m_dynamic_size / sizeof(Elf64_Dyn); ++i) {
		const auto* dyn = GetDynamic() + i;
		if (dyn->d_tag == DT_NULL) break;
		DbgPrintDynamic64(dyn, fout);
	}
	fout.Close();
}

uint64_t Elf64::GetEntry() {
	return m_ehdr->e_entry;
}

bool Elf64::IsSelf() const {
	if (m_f == nullptr || m_f->IsInvalid()) {
		return false;
	}

	if (m_self == nullptr) {
		return false;
	}

	const bool known_magic = (m_self->ident[0] == 0x4f && m_self->ident[1] == 0x15 &&
	                          m_self->ident[2] == 0x3d && m_self->ident[3] == 0x1d) ||
	                         (m_self->ident[0] == 0x54 && m_self->ident[1] == 0x14 &&
	                          m_self->ident[2] == 0xf5 && m_self->ident[3] == 0xee);
	if (!known_magic) {
		return false;
	}

	const bool known_ident_tail =
	    (m_self->ident[4] == 0x00 && m_self->ident[5] == 0x01 && m_self->ident[6] == 0x01 &&
	     m_self->ident[7] == 0x12 && m_self->ident[8] == 0x01 && m_self->ident[9] == 0x01 &&
	     m_self->ident[10] == 0x00 && m_self->ident[11] == 0x00 && m_self->unknown == 0x22) ||
	    (m_self->ident[4] == 0x10 && m_self->ident[5] == 0x01 && m_self->ident[6] == 0x01 &&
	     m_self->ident[7] == 0x12 && m_self->ident[8] == 0x01 && m_self->ident[9] == 0x01 &&
	     m_self->ident[10] == 0x00 && m_self->ident[11] == 0x10 && m_self->unknown == 0x32);

	if (!known_ident_tail) {
		LOGF("Unknown SELF file\n");
		return false;
	}

	return true;
}

bool Elf64::IsValid() const {
	if (m_f == nullptr || m_f->IsInvalid()) {
		return false;
	}

	if (m_ehdr == nullptr) {
		return false;
	}

	if (m_ehdr->e_ident[EI_MAG0] != '\x7f' || m_ehdr->e_ident[EI_MAG1] != 'E' ||
	    m_ehdr->e_ident[EI_MAG2] != 'L' || m_ehdr->e_ident[EI_MAG3] != 'F') {
		LOGF("Not an ELF file\n");
		return false;
	}

	if (m_ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
		LOGF("ehdr->e_ident[EI_CLASS] (0x%x) != ELFCLASS64\n", m_ehdr->e_ident[EI_CLASS]);
		return false;
	}

	if (m_ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
		LOGF("ehdr->e_ident[EI_DATA] (0x%x) != ELFDATA2LSB\n", m_ehdr->e_ident[EI_DATA]);
		return false;
	}

	if (m_ehdr->e_ident[EI_VERSION] != EV_CURRENT) {
		LOGF("ehdr->e_ident[EI_VERSION] != EV_CURRENT\n");
		return false;
	}

	if (m_ehdr->e_ident[EI_OSABI] != ELFOSABI_FREEBSD) {
		LOGF("ehdr->e_ident[EI_OSABI] (0x%x) != ELFOSABI_FREEBSD\n", m_ehdr->e_ident[EI_OSABI]);
		return false;
	}

	if (m_ehdr->e_ident[EI_ABIVERSION] != 0 && m_ehdr->e_ident[EI_ABIVERSION] != 2) {
		LOGF("ehdr->e_ident[EI_ABIVERSION] (0x%x) != (0 or 2)\n", m_ehdr->e_ident[EI_ABIVERSION]);
		return false;
	}

	if (m_ehdr->e_type != ET_DYNEXEC && m_ehdr->e_type != ET_DYNAMIC) {
		LOGF("ehdr->e_type (%04x) != ET_DYNEXEC && m_ehdr->e_type != ET_DYNAMIC\n", m_ehdr->e_type);
		return false;
	}

	if (m_ehdr->e_machine != EM_X86_64) {
		LOGF("ehdr->e_machine (%04x) != EM_X86_64\n", m_ehdr->e_machine);
		return false;
	}

	if (m_ehdr->e_version != EV_CURRENT) {
		LOGF("ehdr->e_version != EV_CURRENT\n");
		return false;
	}

	if (m_ehdr->e_phentsize != sizeof(Elf64_Phdr)) {
		LOGF("ehdr->e_phentsize != sizeof(Elf64_Phdr)\n");
		return false;
	}

	if (m_ehdr->e_shentsize > 0 && m_ehdr->e_shentsize != sizeof(Elf64_Shdr)) {
		LOGF("ehdr->e_shentsize (%d) != sizeof(Elf64_Shdr)\n", m_ehdr->e_shentsize);
		return false;
	}

	return true;
}

bool Elf64::ValidateDynamic() {
	if (m_dynamic_size == 0) return true;
	auto value = [this](Elf64_Sxword tag, uint64_t fallback = 0) {
		const auto* entry = GetDynValue(tag);
		return entry == nullptr ? fallback : entry->d_un.d_val;
	};
	auto mapped = [this](uint64_t address, uint64_t size, bool file_backed, uint64_t* offset) {
		for (uint16_t i = 0; i < m_ehdr->e_phnum; ++i) {
			const auto& p = m_phdr[i];
			if ((p.p_type == PT_LOAD || p.p_type == PT_OS_RELRO) && address >= p.p_vaddr &&
			    InRange(address - p.p_vaddr, size, file_backed ? p.p_filesz : p.p_memsz)) {
				if (offset != nullptr) *offset = p.p_offset + address - p.p_vaddr;
				return true;
			}
		}
		return false;
	};
	auto table = [&](Elf64_Sxword os_tag, Elf64_Sxword tag, uint64_t size, std::vector<uint8_t>& bytes) {
		const auto* os = GetDynValue(os_tag);
		const auto* ordinary = GetDynValue(tag);
		if (os && ordinary) return false;
		if (!os && !ordinary) return size == 0;
		if (size > 64 * 1024 * 1024) return false;
		uint64_t offset = 0;
		if (os && !InRange(os->d_un.d_ptr, size, m_dynamic_data_size)) return false;
		if (ordinary && !mapped(ordinary->d_un.d_ptr, size, true, &offset)) return false;
		bytes.resize(size);
		if (size != 0) {
			if (os) std::memcpy(bytes.data(), m_dynamic_data.get() + os->d_un.d_ptr, size);
			else LoadSegment(reinterpret_cast<uint64_t>(bytes.data()), offset, size);
		}
		return true;
	};
	const auto strings_size = value(DT_OS_STRSZ, value(DT_STRSZ));
	const auto symbols_size = value(DT_OS_SYMTABSZ);
	std::vector<uint8_t> strings, symbols, relocations, jumps, hash;
	if ((HasDynValue(DT_OS_STRSZ) && HasDynValue(DT_STRSZ)) ||
	    !table(DT_OS_STRTAB, DT_STRTAB, strings_size, strings) ||
	    !table(DT_OS_SYMTAB, DT_SYMTAB, symbols_size, symbols) ||
	    !table(DT_OS_RELA, DT_RELA, value(DT_OS_RELASZ, value(DT_RELASZ)), relocations) ||
	    !table(DT_OS_JMPREL, DT_JMPREL, value(DT_OS_PLTRELSZ, value(DT_PLTRELSZ)), jumps) ||
	    !table(DT_OS_HASH, DT_HASH, value(DT_OS_HASHSZ), hash))
		return Reject("dynamic metadata table is out of bounds or ambiguous");
	if ((HasDynValue(DT_SYMTAB) || HasDynValue(DT_OS_SYMTAB)) && symbols_size == 0)
		return Reject("unsupported unsized dynamic symbol table");
	if (symbols_size % sizeof(Elf64_Sym) != 0 ||
	    (symbols_size && value(DT_OS_SYMENT, value(DT_SYMENT)) != sizeof(Elf64_Sym)) ||
	    relocations.size() % sizeof(Elf64_Rela) != 0 || jumps.size() % sizeof(Elf64_Rela) != 0 ||
	    (!relocations.empty() && value(DT_OS_RELAENT, value(DT_RELAENT)) != sizeof(Elf64_Rela)) ||
	    (!jumps.empty() && value(DT_OS_PLTREL, value(DT_PLTREL)) != DT_RELA))
		return Reject("invalid dynamic symbol or relocation entry size");
	auto string_valid = [&](uint64_t offset) {
		return offset < strings.size() && std::memchr(strings.data()+offset, 0, strings.size()-offset) != nullptr;
	};
	for (size_t offset = 0; offset < symbols.size(); offset += sizeof(Elf64_Sym)) {
		Elf64_Sym symbol{};
		std::memcpy(&symbol, symbols.data()+offset, sizeof(symbol));
		if (!string_valid(symbol.st_name)) return Reject("dynamic symbol string is out of bounds or unterminated");
		if (symbol.st_shndx != 0 && (symbol.GetType() == STT_FUNC || symbol.GetType() == STT_OBJECT) &&
		    !mapped(symbol.st_value, symbol.st_size == 0 ? 1 : symbol.st_size, false, nullptr))
			return Reject("defined symbol is outside mapped segments");
	}
	for (const auto* bytes: {&relocations, &jumps}) {
		for (size_t offset = 0; offset < bytes->size(); offset += sizeof(Elf64_Rela)) {
			Elf64_Rela relocation{};
			std::memcpy(&relocation, bytes->data()+offset, sizeof(relocation));
			if (!mapped(relocation.r_offset, sizeof(uint64_t), false, nullptr))
				return Reject("relocation destination is outside mapped segments");
			const auto type = relocation.GetType();
			if (type != R_X86_64_RELATIVE && type != R_X86_64_DTPMOD64 &&
			    type != R_X86_64_64 && type != R_X86_64_GLOB_DAT && type != R_X86_64_JUMP_SLOT)
				return Reject("unsupported relocation type");
			if (type != R_X86_64_RELATIVE && type != R_X86_64_DTPMOD64 &&
			    relocation.GetSymbol() >= symbols.size() / sizeof(Elf64_Sym))
				return Reject("relocation symbol index is out of bounds");
			if (type != R_X86_64_RELATIVE && type != R_X86_64_DTPMOD64) {
				Elf64_Sym symbol{};
				std::memcpy(&symbol,symbols.data()+relocation.GetSymbol()*sizeof(symbol),sizeof(symbol));
				if (symbol.GetBind() > STB_WEAK || symbol.GetType() > STT_FUNC)
					return Reject("unsupported relocation symbol type or binding");
			}
			if (type == R_X86_64_DTPMOD64 && relocation.GetSymbol() != 0)
				return Reject("unsupported cross-module TLS relocation");
		}
	}
	for (uint64_t i = 0; i < m_dynamic_size / sizeof(Elf64_Dyn); ++i) {
		const auto& d = GetDynamic()[i];
		if (d.d_tag == DT_NULL) break;
		switch (d.d_tag) {
			case DT_NEEDED: case DT_SONAME:
				if (!string_valid(d.d_un.d_val)) return Reject("dynamic dependency string is out of bounds or unterminated");
				break;
			case DT_OS_NEEDED_MODULE: case DT_OS_NEEDED_MODULE_1:
			case DT_OS_MODULE_INFO: case DT_OS_MODULE_INFO_1:
			case DT_OS_IMPORT_LIB: case DT_OS_IMPORT_LIB_1:
			case DT_OS_EXPORT_LIB: case DT_OS_EXPORT_LIB_1:
				if (!string_valid(d.d_un.d_val & 0xffffffff)) return Reject("module or library name is out of bounds or unterminated");
				break;
			case DT_INIT: case DT_FINI:
				if (d.d_un.d_ptr != 0 && !mapped(d.d_un.d_ptr, 1, false, nullptr)) return Reject("module entry is outside mapped segments");
				break;
			case DT_PLTGOT: case DT_OS_PLTGOT:
				if (!mapped(d.d_un.d_ptr, 24, false, nullptr)) return Reject("PLT GOT is outside mapped segments");
				break;
			default: break;
		}
	}
	return true;
}

bool Elf64::ValidateHeaders() {
	unsigned dynamic_count = 0, data_count = 0, tls_count = 0;
	if (m_self != nullptr) {
		for (uint16_t i = 0; i < m_self->segments_num; ++i) {
			const auto& segment = m_self_segments[i];
			if (!InRange(segment.offset, segment.compressed_size, m_f->Size()))
				return Reject("SELF payload is out of bounds");
			if ((segment.type & 0x800u) != 0) {
				const auto index = (segment.type >> 20u) & 0xfffu;
				if (index >= m_ehdr->e_phnum) return Reject("SELF program header index is out of bounds");
				if (segment.compressed_size != segment.decompressed_size ||
				    segment.decompressed_size != m_phdr[index].p_filesz)
					return Reject("unsupported compressed or inconsistent SELF payload");
			}
		}
	}
	for (uint16_t i = 0; i < m_ehdr->e_phnum; ++i) {
		const auto& p = m_phdr[i];
		if (p.p_filesz > UINT32_MAX || p.p_offset > UINT64_MAX - p.p_filesz ||
		    p.p_vaddr > UINT64_MAX - p.p_memsz ||
		    (p.p_align != 0 && (p.p_memsz > UINT64_MAX - (p.p_align - 1) ||
		                       p.p_vaddr > UINT64_MAX - ((p.p_memsz + p.p_align - 1) & ~(p.p_align - 1)))) ||
		    (p.p_align != 0 && (p.p_align & (p.p_align - 1)) != 0))
			return Reject("unsupported segment size, overflow or alignment");
		if ((p.p_type == PT_LOAD || p.p_type == PT_TLS || p.p_type == PT_OS_RELRO) && p.p_filesz > p.p_memsz)
			return Reject("segment file size exceeds memory size");
		if (m_self == nullptr && !InRange(p.p_offset, p.p_filesz, m_f->Size()))
			return Reject("ELF segment payload is out of bounds");
		if (p.p_type == PT_DYNAMIC) {
			if (++dynamic_count > 1 || p.p_filesz == 0 || p.p_filesz % sizeof(Elf64_Dyn) != 0)
				return Reject("invalid ELF dynamic table shape");
		}
		if (p.p_type == PT_OS_DYNLIBDATA && ++data_count > 1) return Reject("duplicate dynamic data segment");
		if (p.p_type == PT_TLS && ++tls_count > 1) return Reject("multiple TLS images in one module");
		if ((p.p_type == PT_DYNAMIC || p.p_type == PT_OS_DYNLIBDATA) && p.p_filesz > 64 * 1024 * 1024)
			return Reject("dynamic metadata exceeds supported 64 MiB limit");
		if (m_self != nullptr && p.p_filesz != 0) {
			bool represented = false;
			for (uint16_t j = 0; j < m_self->segments_num; ++j) {
				const auto& segment = m_self_segments[j];
				if ((segment.type & 0x800u) == 0) continue;
				const auto& owner = m_phdr[(segment.type >> 20u) & 0xfffu];
				if (p.p_offset >= owner.p_offset && InRange(p.p_offset - owner.p_offset, p.p_filesz, owner.p_filesz)) represented = true;
			}
			if (!represented && !(p.p_type == PT_OS_DYNLIBDATA && m_f->Size() - m_self->file_size == p.p_filesz))
				return Reject("SELF segment has no supported payload mapping");
		}
	}
	// TLS initialization is copied from mapped virtual bytes, not directly from
	// its file offset. Validate that relationship before MapProgram can read it.
	for (uint16_t i = 0; i < m_ehdr->e_phnum; ++i) {
		const auto& tls = m_phdr[i];
		if (tls.p_type != PT_TLS) continue;
		if (tls.p_align > 0x4000) return Reject("TLS alignment exceeds supported guest page alignment");
		bool contained = false;
		for (uint16_t j = 0; j < m_ehdr->e_phnum; ++j) {
			const auto& owner = m_phdr[j];
			if ((owner.p_type != PT_LOAD && owner.p_type != PT_OS_RELRO) || tls.p_vaddr < owner.p_vaddr) continue;
			const auto delta = tls.p_vaddr - owner.p_vaddr;
			if (InRange(delta, tls.p_memsz, owner.p_memsz) &&
			    (tls.p_filesz == 0 || (InRange(delta, tls.p_filesz, owner.p_filesz) &&
			                           tls.p_offset == owner.p_offset + delta))) contained = true;
		}
		if (!contained) return Reject("TLS image is outside its mapped payload");
	}
	return true;
}

void Elf64::Open(const std::filesystem::path& file_name) {
	Clear();

	m_f = std::make_unique<Common::File>();
	m_f->Open(file_name, Common::File::Mode::Read);

	if (m_f->IsInvalid()) {
		Reject("cannot open executable");
		return;
	}

	m_self = LoadSelf(*m_f);

	if (!IsSelf()) {
		m_self.reset();
		m_f->Seek(0);
	} else {
		if (!InRange(sizeof(SelfHeader), uint64_t(m_self->segments_num) * sizeof(SelfSegment), m_f->Size()) ||
		    m_self->file_size > m_f->Size()) {
			Reject("SELF segment table or file size is out of bounds");
			return;
		}
		m_self_segments = LoadSelfSegments(*m_f, m_self->segments_num);
	}

	auto ehdr_pos = m_f->Tell();

	m_ehdr = LoadEhdr64(*m_f);

	if (!IsValid()) {
		Reject("unsupported or truncated ELF header");
		return;
	}
	const auto file_size = m_f->Size();
	if (m_ehdr->e_ehsize != sizeof(Elf64_Ehdr) ||
	    !InRange(ehdr_pos, m_ehdr->e_phoff, file_size) ||
	    !InRange(ehdr_pos + m_ehdr->e_phoff, uint64_t(m_ehdr->e_phnum) * sizeof(Elf64_Phdr), file_size)) {
		Reject("ELF program header table is out of bounds");
		return;
	}
	if (m_self == nullptr && m_ehdr->e_shnum != 0 &&
	    (m_ehdr->e_shentsize != sizeof(Elf64_Shdr) ||
	     !InRange(m_ehdr->e_shoff, uint64_t(m_ehdr->e_shnum) * sizeof(Elf64_Shdr), file_size))) {
		Reject("ELF section table is out of bounds");
		return;
	}

	if (m_ehdr != nullptr /*&& m_self == nullptr*/) {
		m_phdr = LoadPhdr64(*m_f, ehdr_pos + m_ehdr->e_phoff, m_ehdr->e_phnum);
		if (!ValidateHeaders()) return;
		if (m_self == nullptr) {
			m_shdr = LoadShdr64(*m_f, ehdr_pos + m_ehdr->e_shoff, m_ehdr->e_shnum);

			if (m_shdr != nullptr) {
				for (uint16_t i = 0; i < m_ehdr->e_shnum; ++i) {
					if (m_shdr[i].sh_type != 8 && !InRange(m_shdr[i].sh_offset, m_shdr[i].sh_size, file_size)) {
						Reject("ELF section payload is out of bounds");
						return;
					}
				}
				if (m_ehdr->e_shstrndx < m_ehdr->e_shnum) {
					if (m_shdr[m_ehdr->e_shstrndx].sh_size > 64 * 1024 * 1024 ||
					    !InRange(m_shdr[m_ehdr->e_shstrndx].sh_offset, m_shdr[m_ehdr->e_shstrndx].sh_size, file_size)) {
						Reject("unsupported or truncated section string table");
						return;
					}
					m_str_table_size = static_cast<uint32_t>(m_shdr[m_ehdr->e_shstrndx].sh_size);
					m_str_table =
					    LoadStrTable(*m_f, m_shdr[m_ehdr->e_shstrndx].sh_offset, m_str_table_size);
				}
			}
		} else if (m_ehdr->e_shnum != 0) {
			LOGF("SELF: skipping ELF section table: shoff=0x%016" PRIx64 ", shnum=%" PRIu16 "\n",
			     m_ehdr->e_shoff, m_ehdr->e_shnum);
		}

		for (Elf64_Half i = 0; i < m_ehdr->e_phnum; i++) {
			switch (m_phdr[i].p_type) {
				case PT_DYNAMIC:
					m_dynamic_size = m_phdr[i].p_filesz;
					m_dynamic = LoadDynamic64(this, m_phdr[i].p_offset, m_phdr[i].p_filesz);
					break;
				case PT_OS_DYNLIBDATA:
					m_dynamic_data_size = m_phdr[i].p_filesz;
					m_dynamic_data = LoadDynamic64(this, m_phdr[i].p_offset, m_phdr[i].p_filesz);
					break;
				default: break;
			}
		}
		if (m_dynamic_size != 0) {
			bool terminated = false;
			for (uint64_t i = 0; i < m_dynamic_size / sizeof(Elf64_Dyn); ++i) {
				if (GetDynamic()[i].d_tag == DT_NULL) { terminated = true; break; }
			}
			if (!terminated) { Reject("unterminated ELF dynamic table"); return; }
		}
		if (!ValidateDynamic()) return;
	}
}

void Elf64::Save(const std::filesystem::path& file_name) {
	EXIT_IF(!IsValid());

	if (IsValid()) {
		Common::File f;
		f.Create(file_name);

		if (f.IsInvalid()) {
			EXIT("Can't create %s\n", Common::PathToString(file_name).c_str());
		}

		SaveEhdr64(f, m_ehdr.get());

		SavePhdr64(f, m_ehdr->e_phoff, m_ehdr->e_phnum, m_phdr.get());
		SaveShdr64(f, m_ehdr->e_shoff, m_ehdr->e_shnum, m_shdr.get());

		for (uint16_t i = 0; i < m_ehdr->e_phnum; i++) {
			if (m_phdr[i].p_filesz == 0u) {
				continue;
			}

			auto buf = std::make_unique<char[]>(static_cast<uint32_t>(m_phdr[i].p_filesz));

			LoadSegment(reinterpret_cast<uint64_t>(buf.get()), m_phdr[i].p_offset,
			            m_phdr[i].p_filesz);

			uint32_t bytes_written = 0;

			f.Seek(m_phdr[i].p_offset);
			f.Write(buf.get(), static_cast<uint32_t>(m_phdr[i].p_filesz), &bytes_written);

			EXIT_IF(bytes_written == 0);
		}

		for (uint16_t i = 0; i < m_ehdr->e_shnum; i++) {
			if (m_shdr[i].sh_size == 0u || m_shdr[i].sh_type == 8) {
				continue;
			}

			auto buf = std::make_unique<char[]>(static_cast<uint32_t>(m_shdr[i].sh_size));

			m_f->Seek(m_shdr[i].sh_offset);
			m_f->Read(buf.get(), static_cast<uint32_t>(m_shdr[i].sh_size));

			uint32_t bytes_written = 0;

			f.Seek(m_shdr[i].sh_offset);
			f.Write(buf.get(), static_cast<uint32_t>(m_shdr[i].sh_size), &bytes_written);

			EXIT_IF(bytes_written == 0);
		}

		f.Close();
	}
}

} // namespace Loader
