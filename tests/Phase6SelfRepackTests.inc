// Original miniature containers. No SDK, firmware or commercial bytes.
namespace Phase6Repack {
int Run() {
    using namespace Loader;
    using Phase1::Check;
    const auto path = std::filesystem::temp_directory_path() /
        ("prosperox-repack-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Fixture {
        SelfHeader self{};
        Elf64_Ehdr elf{};
        std::array<SelfSegment, 4> entries{};
        std::array<Elf64_Phdr, 5> ph{};
        size_t elf_pos = sizeof(SelfHeader) + sizeof(entries);
        size_t ex_pos = 0;
        std::vector<uint8_t> bytes;
        explicit Fixture(unsigned remainder, bool padded = false) {
            const uint8_t ident[] = {0x4f,0x15,0x3d,0x1d,0,1,1,0x12,1,1,0,0};
            std::memcpy(self.ident, ident, sizeof(ident));
            self.unknown = 0x22; self.segments_num = entries.size();
            const uint8_t ei[] = {0x7f,'E','L','F',2,1,1,9,2};
            std::memcpy(elf.e_ident, ei, sizeof(ei));
            elf.e_type = ET_DYNEXEC; elf.e_machine = EM_X86_64; elf.e_version = 1;
            elf.e_ehsize = sizeof(elf); elf.e_phoff = sizeof(elf);
            elf.e_phentsize = sizeof(Elf64_Phdr); elf.e_phnum = ph.size();
            ex_pos = (elf_pos + sizeof(elf) + sizeof(ph) + 15) & ~size_t(15);
            self.size1 = ex_pos + 64; self.size2 = 512;
            ph[0] = {PT_LOAD, PF_R | PF_X, 0x1000, 0x4000, 0, 64, 64, 16};
            ph[1] = {PT_LOAD, PF_R, 0x2000, 0x8000, 0, 16 + remainder, 32, 16};
            ph[2] = {0x6fffff01, 0, 0x3000, 0, 0, 32, 32, 1};
            ph[3] = {4, 0, 0x3020, 0, 0, 12, 0, 4};
            // A file-backed runtime child must still have one exact owner.
            ph[4] = {PT_GNU_EH_FRAME, PF_R, 0x1010, 0x4010, 0, 16, 16, 4};
            uint64_t cursor = self.size1 + self.size2;
            for (unsigned i = 0; i < 2; ++i) {
                entries[2*i] = {0x10004 | (uint64_t(2*i+1)<<20), cursor, 32, 32};
                cursor += 32;
                entries[2*i+1] = {0x2804 | (uint64_t(i)<<20), cursor, ph[i].p_filesz, ph[i].p_filesz};
                cursor += ph[i].p_filesz;
            }
            self.file_size = (cursor + 15) & ~uint64_t(15);
            const auto version = padded ? self.file_size : cursor;
            bytes.resize(version + ph[2].p_filesz, 0);
            for (unsigned i = 0; i < 64; ++i) bytes[entries[1].offset+i] = uint8_t(i+1);
            for (unsigned i = 0; i < 32; ++i) bytes[version+i] = uint8_t(0xa0+i);
            const uint64_t fake_type = 1;
            std::memcpy(bytes.data()+ex_pos+8, &fake_type, 8);
            Headers();
        }
        void Headers() {
            std::memcpy(bytes.data(), &self, sizeof(self));
            std::memcpy(bytes.data()+sizeof(self), entries.data(), sizeof(entries));
            std::memcpy(bytes.data()+elf_pos, &elf, sizeof(elf));
            std::memcpy(bytes.data()+elf_pos+sizeof(elf), ph.data(), sizeof(ph));
        }
    };
    unsigned accepted = 0, rejected = 0;
    Elf64 elf; // Reuse the reader to exercise clearing recognized-profile state.
    auto parse = [&](Fixture fixture, bool valid, const char* label) {
        fixture.Headers();
        { std::ofstream file(path, std::ios::binary); file.write(reinterpret_cast<const char*>(fixture.bytes.data()), fixture.bytes.size()); }
        elf.Open(path);
        if (elf.IsValid() != valid) std::fprintf(stderr, "REPACK case=%s error=%s\n", label, elf.GetError().c_str());
        Check(elf.IsValid() == valid, label);
        if (!valid) { Check(!elf.GetError().empty(), "repack rejection has diagnostic"); ++rejected; return; }
        ++accepted;
        std::array<uint8_t, 16> out{};
        elf.LoadSegment(reinterpret_cast<uint64_t>(out.data()), 0x1010, out.size());
        for (unsigned i = 0; i < out.size(); ++i) Check(out[i] == i+17, "exact runtime child read");
        elf.LoadSegment(reinterpret_cast<uint64_t>(out.data()), 0x3004, out.size());
        for (unsigned i = 0; i < out.size(); ++i) Check(out[i] == 0xa4+i, "exact VERSION subrange read");
    };
    for (unsigned gap = 0; gap < 16; ++gap) {
        parse(Fixture(gap), true, "all unpadded writer alignments");
        Fixture ps5_magic(gap);
        const uint8_t magic[] = {0x54,0x14,0xf5,0xee};
        std::memcpy(ps5_magic.self.ident, magic, sizeof(magic));
        parse(ps5_magic, true, "PS5 SELF magic uses the same bounded entry contract");
        parse(Fixture(gap, true), gap == 0, "a competing padded trailer is not inferred");
    }
    for (unsigned variant = 0; variant < 24; ++variant) {
        Fixture f(8);
        switch (variant) {
            case 0: f.bytes.pop_back(); break;
            case 1: f.bytes.push_back(0); break;
            case 2: f.self.file_size++; break;
            case 3: f.self.size1 = 16; break;
            case 4: f.self.size2 = 0; break;
            case 5: f.bytes[f.ex_pos+8] = 4; break;
            case 6: f.entries[3].type |= 2; break; // Encryption bit.
            case 7: f.entries[3].type |= 8; break; // Compression bit.
            case 8: f.entries[3].offset = f.entries[1].offset; break;
            case 9: f.entries[3].type = f.entries[1].type; break;
            case 10: f.ph[1].p_offset = f.ph[0].p_offset; break;
            case 11: f.ph[4].p_offset = 0x1038; break; // Partly contained child.
            case 12: f.ph[3].p_flags = PF_R; break;
            case 13: f.ph[3].p_vaddr = 0x9000; break;
            case 14: f.ph[3].p_memsz = 12; break;
            case 15: f.ph[3].p_type = PT_DYNAMIC; f.ph[3].p_filesz = 16; break;
            case 16: f.ph[3].p_offset = 0x3004; break;
            case 17: f.ph[2].p_offset = 0x1020; break;
            case 18: f.ph[3] = f.ph[2]; break; // Duplicate VERSION.
            case 19: f.ph[2].p_filesz = UINT64_MAX; break;
            case 20: f.ph[1].p_offset = UINT64_MAX-8; break;
            case 21: f.ph[2].p_flags = PF_X; break;
            case 22: f.entries[0].compressed_size = 16; break;
            case 23: f.elf.e_ident[EI_ABIVERSION] = 0; break; // No expansion of legacy runtime scope.
        }
        parse(f, false, "malformed repack rejected");
    }
    for (const auto kind : {ET_DYNEXEC, ET_DYNAMIC}) {
        Fixture aligned_metadata(8);
        aligned_metadata.elf.e_type = kind;
        aligned_metadata.ph[2].p_align = 16;
        aligned_metadata.ph[2].p_filesz = 26;
        aligned_metadata.ph[2].p_memsz = 32;
        aligned_metadata.ph[3].p_offset = 0x2ff0; // NOTE before VERSION, after all owners.
        aligned_metadata.bytes.resize(aligned_metadata.bytes.size() - 6);
        parse(aligned_metadata, true, "aligned metadata is independent of ELF header order");
        aligned_metadata.ph[2].p_memsz = 33;
        parse(aligned_metadata, false, "metadata memory size is neither payload size nor aligned size");
    }
    for (const uint64_t retained_size : {16, 48}) {
        Fixture metadata(8);
        metadata.ph[2].p_align = 16;
        metadata.ph[2].p_memsz = retained_size;
        parse(metadata, true, "non-mapped VERSION memory size does not select payload bytes");
    }
    Fixture excessive_metadata(8);
    excessive_metadata.ph[2].p_memsz = 64 * 1024 * 1024 + 16;
    parse(excessive_metadata, false, "VERSION metadata memory size is bounded");
    Fixture padding(8, true);
    padding.bytes[padding.entries[3].offset+padding.entries[3].compressed_size] = 1;
    parse(padding, false, "nonzero physical VERSION padding rejected");
    Fixture export_case(8);
    parse(export_case, true, "recognized reader can be reused after rejection");
    const auto export_path = std::filesystem::path(path.string() + ".elf");
    { std::ofstream file(export_path); file << "preserve"; }
    elf.Save(export_path);
    std::ifstream preserved(export_path);
    std::string sentinel; preserved >> sentinel; preserved.close();
    Check(sentinel == "preserve", "repack export refuses before overwriting an existing file");
    std::filesystem::remove(export_path);
    Fixture original(8);
    for (size_t length = 0; length < original.bytes.size(); ++length) {
        { std::ofstream file(path, std::ios::binary); file.write(reinterpret_cast<const char*>(original.bytes.data()), length); }
        Elf64 elf; elf.Open(path);
        Check(!elf.IsValid() && !elf.GetError().empty(), "every repack truncation rejected");
        ++rejected;
    }
    std::filesystem::remove(path);
    std::printf("PHASE6_REPACK_PASS accepted=%u rejected=%u\n", accepted, rejected);
    return 0;
}
}
