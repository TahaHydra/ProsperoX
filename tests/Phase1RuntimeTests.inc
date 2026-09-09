// Original synthetic conformance inputs; no SDK or commercial artifacts.
#include <array>
#include <barrier>
#include <fstream>
#include <thread>
#include <winioctl.h>

namespace Phase1 {
void Check(bool ok, const char* contract) {
    if (!ok) {
        std::fprintf(stderr, "PHASE1_FAILURE %s\n", contract);
        std::exit(1);
    }
}

int Executables() {
    using namespace Loader;
    const auto path = std::filesystem::temp_directory_path() /
        ("prosperox-elf-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Elf64_Ehdr header{};
    const uint8_t ident[] = {0x7f, 'E', 'L', 'F', 2, 1, 1, 9, 2};
    std::memcpy(header.e_ident, ident, sizeof(ident));
    header.e_type = ET_DYNEXEC;
    header.e_machine = EM_X86_64;
    header.e_version = 1;
    header.e_ehsize = sizeof(header);
    header.e_phoff = sizeof(header);
    header.e_phentsize = sizeof(Elf64_Phdr);
    header.e_phnum = 1;
    Elf64_Phdr segment{};
    segment.p_type = PT_LOAD;
    segment.p_offset = sizeof(header) + sizeof(segment);
    segment.p_vaddr = 0x1000;
    segment.p_filesz = 16;
    segment.p_memsz = 32;
    segment.p_align = 8;
    auto bytes = [&](const Elf64_Ehdr& h, const Elf64_Phdr& p) {
        std::vector<uint8_t> data(sizeof(h) + sizeof(p) + 16, 0);
        std::memcpy(data.data(), &h, sizeof(h));
        std::memcpy(data.data() + sizeof(h), &p, sizeof(p));
        return data;
    };
    auto parse = [&](const std::vector<uint8_t>& data, bool valid, bool inspect_payload = true) {
        { std::ofstream file(path, std::ios::binary); file.write(reinterpret_cast<const char*>(data.data()), data.size()); }
        Elf64 elf;
        elf.Open(path);
        Check(elf.IsValid() == valid, valid ? "valid synthetic executable accepted" : "malformed executable rejected");
        if (!valid) Check(!elf.GetError().empty(), "bounded rejection diagnostic");
        else if (inspect_payload) {
            Check(elf.GetDynValue(DT_NEEDED) == nullptr, "absent dynamic table is safe");
            std::array<uint8_t,16> payload;
            payload.fill(0xff);
            elf.LoadSegment(reinterpret_cast<uint64_t>(payload.data()), segment.p_offset, payload.size());
            Check(payload == std::array<uint8_t,16>{}, "exact segment payload read");
        }
    };
    const auto original = bytes(header, segment);
    parse(original, true);
    for (size_t length = 0; length < original.size(); ++length)
        parse(std::vector<uint8_t>(original.begin(), original.begin() + length), false);
    for (int variant = 0; variant < 10; ++variant) {
        auto h = header; auto p = segment;
        switch (variant) {
            case 0: h.e_phoff = UINT64_MAX - 4; break;
            case 1: h.e_phnum = 65535; break;
            case 2: h.e_ehsize = 1; break;
            case 3: h.e_shnum = 1; h.e_shoff = UINT64_MAX; break;
            case 4: p.p_offset = UINT64_MAX - 4; break;
            case 5: p.p_filesz = UINT64_MAX; break;
            case 6: p.p_memsz = 1; break;
            case 7: p.p_vaddr = UINT64_MAX - 4; break;
            case 8: p.p_align = 3; break;
            case 9: p.p_type = PT_DYNAMIC; p.p_filesz = 15; break;
        }
        parse(bytes(h,p), false);
    }
    auto dynamic = segment;
    dynamic.p_type = PT_DYNAMIC;
    auto unterminated = bytes(header, dynamic);
    unterminated[sizeof(header)+sizeof(segment)] = DT_NEEDED;
    parse(unterminated, false);

    auto tls_header = header;
    tls_header.e_phnum = 2;
    std::array<Elf64_Phdr,2> tls_headers{segment,segment};
    tls_headers[0].p_offset = sizeof(header) + sizeof(tls_headers);
    tls_headers[1] = tls_headers[0];
    tls_headers[1].p_type = PT_TLS;
    std::vector<uint8_t> tls_container(tls_headers[0].p_offset + 16, 0);
    std::memcpy(tls_container.data(), &tls_header, sizeof(tls_header));
    std::memcpy(tls_container.data() + sizeof(header), tls_headers.data(), sizeof(tls_headers));
    parse(tls_container, true, false);
    for (unsigned variant = 0; variant < 3; ++variant) {
        auto altered = tls_headers;
        if (variant == 0) altered[1].p_vaddr += 0x4000;
        if (variant == 1) altered[1].p_memsz += 1;
        if (variant == 2) { altered[1].p_offset += 1; altered[1].p_filesz -= 1; }
        std::memcpy(tls_container.data() + sizeof(header), altered.data(), sizeof(altered));
        parse(tls_container, false, false);
    }

    SelfHeader self{};
    const uint8_t self_ident[] = {0x4f,0x15,0x3d,0x1d,0,1,1,0x12,1,1,0,0};
    std::memcpy(self.ident, self_ident, sizeof(self_ident));
    self.unknown = 0x22;
    self.segments_num = 1;
    self.file_size = sizeof(self) + sizeof(SelfSegment) + original.size();
    SelfSegment mapping{0x800, self.file_size - 16, 16, 16};
    std::vector<uint8_t> container(self.file_size);
    std::memcpy(container.data(), &self, sizeof(self));
    std::memcpy(container.data()+sizeof(self), &mapping, sizeof(mapping));
    std::memcpy(container.data()+sizeof(self)+sizeof(mapping), original.data(), original.size());
    parse(container, true);
    mapping.type |= 1 << 20;
    std::memcpy(container.data()+sizeof(self), &mapping, sizeof(mapping));
    parse(container, false);
    mapping.type = 0x800;
    mapping.compressed_size = 8;
    std::memcpy(container.data()+sizeof(self), &mapping, sizeof(mapping));
    parse(container, false);
    // Dynamic pointers are original offsets into a separate synthetic metadata segment.
    auto dynamic_header = header;
    dynamic_header.e_phnum = 3;
    std::array<Elf64_Phdr,3> headers{};
    headers[0] = segment;
    headers[0].p_offset = sizeof(header) + sizeof(headers);
    headers[0].p_memsz = 16;
    headers[1].p_type = PT_DYNAMIC;
    headers[1].p_offset = headers[0].p_offset + 16;
    headers[1].p_filesz = 10 * sizeof(Elf64_Dyn);
    headers[2].p_type = PT_OS_DYNLIBDATA;
    headers[2].p_offset = headers[1].p_offset + headers[1].p_filesz;
    headers[2].p_filesz = 56;
    std::array<Elf64_Dyn,10> tags{};
    const std::array<std::pair<Elf64_Sxword,uint64_t>,9> tag_values{{
        {DT_OS_STRTAB,0}, {DT_OS_STRSZ,8}, {DT_OS_SYMTAB,8}, {DT_OS_SYMTABSZ,24},
        {DT_OS_SYMENT,24}, {DT_OS_RELA,32}, {DT_OS_RELASZ,24}, {DT_OS_RELAENT,24}, {DT_NEEDED,1}}};
    for (size_t i=0; i<tag_values.size(); ++i) { tags[i].d_tag=tag_values[i].first; tags[i].d_un.d_val=tag_values[i].second; }
    std::vector<uint8_t> metadata(headers[2].p_offset + 56, 0);
    std::memcpy(metadata.data(), &dynamic_header, sizeof(dynamic_header));
    std::memcpy(metadata.data()+sizeof(header), headers.data(), sizeof(headers));
    std::memcpy(metadata.data()+headers[1].p_offset, tags.data(), sizeof(tags));
    const char names[8] = {'\0','N','i','d','\0','\0','\0','\0'};
    std::memcpy(metadata.data()+headers[2].p_offset, names, sizeof(names));
    Elf64_Sym sym{};
    sym.st_name=1; sym.st_info=(STB_WEAK<<4)|STT_FUNC;
    std::memcpy(metadata.data()+headers[2].p_offset+8, &sym, sizeof(sym));
    Elf64_Rela rel{};
    rel.r_offset=0x1000; rel.r_info=R_X86_64_64; rel.r_addend=17;
    std::memcpy(metadata.data()+headers[2].p_offset+32, &rel, sizeof(rel));
    parse(metadata,true,false);
    for (int variant=0; variant<9; ++variant) {
        auto broken=metadata;
        auto altered_tags=tags;
        auto altered_symbol=sym;
        auto altered_relocation=rel;
        switch (variant) {
            case 0: altered_tags[0].d_un.d_val=UINT64_MAX; break;
            case 1: altered_tags[1].d_un.d_val=UINT64_MAX; break;
            case 2: altered_tags[3].d_un.d_val=23; break;
            case 3: altered_symbol.st_name=8; break;
            case 4: altered_relocation.r_info=(uint64_t(1)<<32)|R_X86_64_64; break;
            case 5: altered_relocation.r_offset=0x100f; break;
            case 6: altered_relocation.r_info=999; break;
            case 7: altered_tags[7].d_un.d_val=8; break;
            case 8: altered_tags[8].d_un.d_val=8; break;
        }
        std::memcpy(broken.data()+headers[1].p_offset, altered_tags.data(), sizeof(altered_tags));
        std::memcpy(broken.data()+headers[2].p_offset+8, &altered_symbol, sizeof(altered_symbol));
        std::memcpy(broken.data()+headers[2].p_offset+32, &altered_relocation, sizeof(altered_relocation));
        parse(broken,false,false);
    }
    uint32_t random=0x505831;
    for (unsigned case_id=0; case_id<1000; ++case_id) {
        auto mutated=case_id%2==0?original:metadata;
        for (unsigned mutation=0; mutation<1+case_id%4; ++mutation) {
            random^=random<<13; random^=random>>17; random^=random<<5;
            const auto index=random%mutated.size();
            random^=random<<13; random^=random>>17; random^=random<<5;
            mutated[index]^=static_cast<uint8_t>(random);
        }
        {
            std::ofstream file(path,std::ios::binary);
            file.write(reinterpret_cast<const char*>(mutated.data()),mutated.size());
        }
        std::printf("PHASE1_FUZZ seed=0x505831 case=%u\n",case_id);
        Elf64 elf;
        elf.Open(path);
        Check(elf.IsValid() || !elf.GetError().empty(),"fuzz rejection has a diagnostic");
        // Never map or execute a fuzzed input, including structurally valid mutations.
    }
    std::filesystem::remove(path);
    std::puts("PHASE1_PASS generated ELF/SELF payloads, all truncations, overflow, alignment, dynamic termination and SELF mapping");
    return 0;
}

int LoadRollback() {
    using namespace Loader;
    const auto path = std::filesystem::temp_directory_path() /
        ("prosperox-load-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Elf64_Ehdr header{};
    const uint8_t ident[] = {0x7f,'E','L','F',2,1,1,9,2};
    std::memcpy(header.e_ident,ident,sizeof(ident));
    header.e_type=ET_DYNAMIC; header.e_machine=EM_X86_64; header.e_version=1;
    header.e_ehsize=sizeof(header); header.e_phoff=sizeof(header);
    header.e_phentsize=sizeof(Elf64_Phdr); header.e_phnum=3;
    std::array<Elf64_Phdr,3> phdr{};
    phdr[0].p_type=PT_LOAD; phdr[0].p_flags=PF_R;
    phdr[0].p_offset=sizeof(header)+sizeof(phdr); phdr[0].p_filesz=16;
    phdr[0].p_memsz=32; phdr[0].p_vaddr=0x1000; phdr[0].p_align=16;
    phdr[1].p_type=PT_DYNAMIC; phdr[1].p_offset=phdr[0].p_offset+16;
    phdr[1].p_filesz=2*sizeof(Elf64_Dyn);
    phdr[2]=phdr[0]; phdr[2].p_type=PT_TLS;
    phdr[2].p_filesz=3; phdr[2].p_memsz=17; phdr[2].p_align=8;
    std::array<Elf64_Dyn,2> dynamic{};
    dynamic[0].d_tag=DT_DEBUG; dynamic[0].d_un.d_val=1;
    auto write = [&] {
        std::ofstream file(path,std::ios::binary);
        file.write(reinterpret_cast<const char*>(&header),sizeof(header));
        file.write(reinterpret_cast<const char*>(phdr.data()),sizeof(phdr));
        const std::array<uint8_t,16> payload{0x50,0x58,0x31};
        file.write(reinterpret_cast<const char*>(payload.data()),sizeof(payload));
        file.write(reinterpret_cast<const char*>(dynamic.data()),sizeof(dynamic));
    };
    write();
    auto* linker=Common::Singleton<RuntimeLinker>::Instance();
    const auto previous=g_desired_base_addr;
    for (unsigned iteration=0; iteration<16; ++iteration) {
        Check(linker->LoadProgram(path)==nullptr,"unsupported metadata rejects load after mapping");
        Check(!linker->LastLoadError().empty(),"transaction failure retains diagnostic");
        Check(g_desired_base_addr==previous && linker->FindProgramByFileName(path)==nullptr,"failed load leaves no registration or address cursor change");
        Libs::LibKernel::Memory::VirtualQueryInfo query{};
        Check(Libs::LibKernel::Memory::KernelVirtualQuery(reinterpret_cast<void*>(previous),0,&query,sizeof(query))!=0,
              "failed load releases mapped guest memory");
    }
    dynamic[0]={}; write();
    auto* program=linker->LoadProgram(path);
    Check(program!=nullptr && program->base_vaddr==previous,"valid module loads at reclaimed address");
    linker->RelocateProgram(program);
    Check(*reinterpret_cast<const uint8_t*>(program->base_vaddr+0x1000)==0x50,"mapped file bytes preserved");
    Check(*reinterpret_cast<const uint8_t*>(program->base_vaddr+0x101f)==0,"BSS is zero filled");
    linker->UnloadProgram(program);
    Check(linker->FindProgramByFileName(path)==nullptr,"module unload removes registration");
    for (uint64_t alignment: {uint64_t{1}, uint64_t{8}, uint64_t{32}, uint64_t{0x4000}}) {
        phdr[2].p_align=alignment; write();
        program=linker->LoadProgram(path);
        Check(program!=nullptr,"TLS alignment fixture loads");
        auto* tls=RuntimeLinker::TlsGetAddr(program);
        const auto tcb=reinterpret_cast<uint64_t>(tls)+program->tls.tcb_offset;
        Check(reinterpret_cast<uint64_t>(tls)%alignment==0 && tcb%32==0,
            "TLS image and TCB satisfy independent alignment requirements");
        Check(tls[0]==0x50 && tls[2]==0x31 && tls[3]==0 && tls[16]==0,
            "loaded TLS image preserves initialization and BSS");
        RuntimeLinker::DeleteTls(program,Common::Thread::GetThreadIdUnique());
        linker->UnloadProgram(program);
    }
    phdr[2].p_align=0x8000; write();
    Check(linker->LoadProgram(path)==nullptr && !linker->LastLoadError().empty(),
        "unsupported TLS alignment is explicitly rejected");
    std::filesystem::remove(path);
    std::puts("PHASE1_PASS 16 transactional load rejections, address reuse, empty relocations, read-only payload and BSS");
    return 0;
}

static std::vector<int> module_events;
KYTY_SYSV_ABI int ModuleInitialize(size_t id,const void*,Loader::module_func_t) { module_events.push_back(static_cast<int>(id)); return 0; }
KYTY_SYSV_ABI int ModuleFinalize(size_t id,const void*,Loader::module_func_t) { module_events.push_back(-static_cast<int>(id)); return 0; }

int Modules() {
    using namespace Loader;
    const auto folder=std::filesystem::temp_directory_path()/
        ("prosperox-modules-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(folder);
    auto* linker=Common::Singleton<RuntimeLinker>::Instance();
    for (bool cycle: {false,true}) {
        std::vector<Program*> modules;
        for (const auto& [name,id]: std::array<std::pair<const char*,int>,4>{{{"D",4},{"A",1},{"B",2},{"C",3}}}) {
            Elf64_Ehdr header{};
            const uint8_t ident[]={0x7f,'E','L','F',2,1,1,9,2};
            std::memcpy(header.e_ident,ident,sizeof(ident));
            header.e_type=ET_DYNAMIC; header.e_machine=EM_X86_64; header.e_version=1;
            header.e_ehsize=sizeof(header); header.e_phoff=sizeof(header);
            header.e_phentsize=sizeof(Elf64_Phdr); header.e_phnum=1;
            Elf64_Phdr segment{};
            segment.p_type=PT_LOAD; segment.p_flags=PF_R|PF_X;
            segment.p_offset=sizeof(header)+sizeof(segment); segment.p_vaddr=0x1000;
            segment.p_filesz=segment.p_memsz=64; segment.p_align=16;
            std::array<uint8_t,64> payload{};
            for (size_t part=0; part<2; ++part) {
                Xbyak::CodeGenerator code(32,payload.data()+part*32);
                code.mov(code.edi,id);
                code.mov(code.rax,reinterpret_cast<uint64_t>(part==0?&ModuleInitialize:&ModuleFinalize));
                code.jmp(code.rax); code.ready();
            }
            const auto path=folder/name;
            {
                std::ofstream file(path,std::ios::binary);
                file.write(reinterpret_cast<const char*>(&header),sizeof(header));
                file.write(reinterpret_cast<const char*>(&segment),sizeof(segment));
                file.write(reinterpret_cast<const char*>(payload.data()),payload.size());
            }
            auto* module=linker->LoadProgram(path);
            Check(module!=nullptr,"load generated module with native initializer/finalizer");
            module->dynamic_info->init_vaddr=id==3?0:0x1000;
            module->dynamic_info->fini_vaddr=0x1020;
            if (id==4) module->dynamic_info->needed={"A"};
            if (id==1) module->dynamic_info->needed={"B","C"};
            if (id==2 && cycle) module->dynamic_info->needed={"A"};
            linker->RelocateProgram(module);
            modules.push_back(module);
        }
        module_events.clear();
        linker->StartAllModules();
        const std::vector<int> expected_start=cycle?std::vector<int>{1,2,4}:std::vector<int>{2,1,4};
        Check(module_events==expected_start,"dependency order including no-init modules and cycle components");
        linker->StartAllModules();
        Check(module_events==expected_start,"repeated global start does not invoke initializers twice");
        linker->StopAllModules();
        const std::vector<int> expected=cycle?std::vector<int>{1,2,4,-4,-2,-1,-3}:std::vector<int>{2,1,4,-4,-1,-3,-2};
        Check(module_events==expected,"finalizers run in reverse actual startup order");
        linker->StopAllModules();
        Check(module_events==expected,"repeated global stop does not invoke finalizers twice");
        for (auto* module: modules) linker->UnloadProgram(module);
    }
    std::filesystem::remove_all(folder);
    std::puts("PHASE1_PASS native module dependency order, no-init dependencies, cycles and reverse shutdown");
    return 0;
}

int Paths() {
    namespace fs = std::filesystem;
    namespace F = Libs::LibKernel::FileSystem;
    const auto root = fs::temp_directory_path() /
        ("prosperox-paths-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Check(fs::create_directories(root / "mount" / "sub"), "create fixture");
    fs::create_directories(root / "outside");
    fs::create_directories(root / "nested");
    std::ofstream(root / "outside" / "sentinel.txt") << "unchanged";
    F::Mount(root / "mount", "/phase1");
    F::Mount(root / "nested", "/phase1/sub");
    const auto expected_longest = fs::absolute(root / "nested" / "new.txt");
    const auto actual_longest = F::GetRealFilename("/phase1/sub/new.txt");
    Check(actual_longest == expected_longest, "longest mount wins");
    Check(F::GetRealFilename("/phase1/a/../ok.txt") == fs::weakly_canonical(root / "mount" / "ok.txt"), "in-mount parent traversal");
    for (const char* path: {"/phase1/../outside/sentinel.txt", "/phase1/sub/../../outside/sentinel.txt",
                           "/phase1-sibling/file", "/unmapped/file", "C:/Windows/file", "relative/file",
                           "/phase1/..\\outside\\sentinel.txt", "/phase1/C:/Windows/file",
                           "/phase1/file:stream", "/phase1/.. /outside", "/phase1/CON.txt", "/phase1/NUL",
                           "/phase1/COM1.log", "/phase1/LPT9"}) {
        Check(F::GetRealFilename(path).empty(), path);
        Check(F::KernelOpen(path, 0x601, 0777) < 0, "HLE denies escaped create/truncate");
    }
    // NTFS junctions exercise reparse-point escape without SeCreateSymbolicLinkPrivilege.
    const auto junction = root / "mount" / "escape";
    fs::create_directory(junction);
    const auto target = fs::absolute(root / "outside").wstring();
    const auto substitute = std::wstring(L"\\??\\") + target;
    struct JunctionHeader {
        DWORD tag; WORD length, reserved;
        WORD substitute_offset, substitute_length, print_offset, print_length;
    };
    const auto path_bytes = (substitute.size() + target.size() + 2) * sizeof(wchar_t);
    std::vector<uint8_t> reparse(sizeof(JunctionHeader) + path_bytes, 0);
    auto* data = reinterpret_cast<JunctionHeader*>(reparse.data());
    data->tag = IO_REPARSE_TAG_MOUNT_POINT;
    data->length = static_cast<WORD>(8 + path_bytes);
    data->substitute_length = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    data->print_offset = data->substitute_length + sizeof(wchar_t);
    data->print_length = static_cast<WORD>(target.size() * sizeof(wchar_t));
    std::memcpy(reparse.data() + sizeof(JunctionHeader), substitute.c_str(), data->substitute_length);
    std::memcpy(reparse.data() + sizeof(JunctionHeader) + data->print_offset, target.c_str(), data->print_length);
    const auto handle = CreateFileW(junction.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    Check(handle != INVALID_HANDLE_VALUE, "open NTFS junction fixture");
    DWORD returned = 0;
    const bool created = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, reparse.data(),
        static_cast<DWORD>(reparse.size()), nullptr, 0, &returned, nullptr);
    CloseHandle(handle);
    Check(created, "create NTFS junction fixture");
    Check(F::GetRealFilename("/phase1/escape/sentinel.txt").empty(), "symlink escape denied");
    Check(F::KernelOpen("/phase1/escape/sentinel.txt", 0x601, 0777) < 0, "HLE denies symlink truncate");
    const auto fd = F::KernelOpen("/phase1/new.txt", 0x601, 0777);
    Check(fd >= 3, "ordinary mounted create");
    Check(F::KernelClose(fd) == 0, "close mounted file");
    Check(fs::exists(root / "mount" / "new.txt"), "mounted file created inside root");
    std::ifstream sentinel(root / "outside" / "sentinel.txt");
    std::string value;
    sentinel >> value;
    Check(value == "unchanged", "outside sentinel unchanged");
    sentinel.close();
    F::Umount("/phase1/sub");
    F::Umount("/phase1");
    Check(fs::remove(junction), "remove own junction without traversing it");
    fs::remove_all(root); // This test's uniquely created temporary fixture only.
    std::puts("PHASE1_PASS mounted path containment and HLE side effects");
    return 0;
}

KYTY_SYSV_ABI uint64_t NativeArguments(uint64_t a, uint64_t b, uint64_t c, uint64_t d,
    uint64_t e, uint64_t f, uint64_t g, uint64_t h,
    double x0, double x1, double x2, double x3, double x4, double x5, double x6, double x7) {
    return a + 2*b + 3*c + 4*d + 5*e + 6*f + 7*g + 8*h +
           static_cast<uint64_t>(x0 + 2*x1 + 3*x2 + 4*x3 + 5*x4 + 6*x5 + 7*x6 + 8*x7);
}

KYTY_MS_ABI uint8_t* ClobberVectorState() {
    asm volatile("vpxor %%ymm0, %%ymm0, %%ymm0" ::: "ymm0");
    return reinterpret_cast<uint8_t*>(0x505831);
}

static uint8_t* fixture_tcb = nullptr;
KYTY_MS_ABI uint8_t* FixtureTcb() { return fixture_tcb; }

int Patching() {
    using namespace Loader;
    std::array<uint8_t,64> tcb{};
    fixture_tcb = tcb.data();
    const auto memory = Libs::LibKernel::Memory::AllocateRuntimeMemory(0,0x8000,
        Common::VirtualMemory::Mode::ExecuteReadWrite,"phase1_patch_fixture");
    Check(memory != 0,"allocate patch fixture");
    RegisterRedZonePatchModule(reinterpret_cast<void*>(memory), 0x4000,
        reinterpret_cast<void*>(memory + 0x5000), 0x3000);
    Program program;
    program.elf = std::make_unique<Elf64>();
    program.tls.handler_vaddr = memory+0x4000;
    auto* safe = new (reinterpret_cast<void*>(program.tls.handler_vaddr)) Jit::SafeCall;
    safe->SetFunc(&FixtureTcb);
    const uint8_t embedded[] = {0x48,0xb8,0x64,0x48,0x8b,0x04,0x25,0,0,0,0,0xc3};
    std::memcpy(reinterpret_cast<void*>(memory),embedded,sizeof(embedded));
    PatchProgram(&program,memory,sizeof(embedded));
    Check(std::memcmp(reinterpret_cast<void*>(memory),embedded,sizeof(embedded))==0,"TLS-like immediate bytes are never patched");
    const uint8_t tls_load[] = {0x64,0x48,0x8b,0x04,0x25,0,0,0,0,0xc3};
    for (size_t prefixes=0; prefixes<=3; ++prefixes) {
        auto* code = reinterpret_cast<uint8_t*>(memory);
        std::memset(code,0x66,prefixes);
        std::memcpy(code+prefixes,tls_load,sizeof(tls_load));
        PatchProgram(&program,memory,prefixes+sizeof(tls_load));
        Common::VirtualMemory::FlushInstructionCache(memory,0x8000);
        using Function = KYTY_SYSV_ABI uint8_t* (*)();
        Check(reinterpret_cast<Function>(memory)()==fixture_tcb,"decoded TLS load with operand prefixes executes correctly");
    }
    const uint8_t tls_store[] = {0x64,0xc7,0x04,0x25,0x28,0,0,0,0xef,0xbe,0xad,0xde,0xc3};
    std::memcpy(reinterpret_cast<void*>(memory),tls_store,sizeof(tls_store));
    PatchProgram(&program,memory,sizeof(tls_store));
    Common::VirtualMemory::FlushInstructionCache(memory,0x8000);
    using StoreFunction = KYTY_SYSV_ABI void (*)();
    reinterpret_cast<StoreFunction>(memory)();
    uint32_t actual=0;
    std::memcpy(&actual,tcb.data()+0x28,sizeof(actual));
    Check(actual==0xdeadbeef && tcb[0x27]==0 && tcb[0x2c]==0,"guest TLS immediate store writes exactly four bytes");
    const uint8_t trap[] = {0xcd,0x45,0x90,0x0f,0x0b};
    std::memcpy(reinterpret_cast<void*>(memory),tls_store,12);
    std::memcpy(reinterpret_cast<void*>(memory+12),trap,sizeof(trap));
    PatchProgram(&program,memory,12+sizeof(trap));
    Check(std::memcmp(reinterpret_cast<void*>(memory+12),trap,sizeof(trap))==0,"guest trap sequence is preserved");
    // Real leaf-function red-zone values must survive the rewritten instruction,
    // including the slot a CALL-based replacement would overwrite immediately.
    for (unsigned operation = 0; operation < 8; ++operation) {
        const bool store_operation = operation == 4;
        Xbyak::CodeGenerator leaf(1024, reinterpret_cast<void*>(memory + 0x1000));
        leaf.push(leaf.rbx); leaf.push(leaf.rbp); leaf.push(leaf.rsi); leaf.push(leaf.rdi);
        for (unsigned slot = 1; slot <= 16; ++slot)
            leaf.mov(leaf.qword[leaf.rsp - slot*8], 0x12340000 + slot);
        if (store_operation) {
            for (unsigned i = 0; i < 12; ++i) leaf.db(tls_store[i]);
        } else {
            for (unsigned i = 0; i < 9; ++i)
                leaf.db(i == 3 ? uint8_t(0x04 | (operation << 3)) : tls_load[i]);
            leaf.mov(leaf.rax, Xbyak::Reg64(operation));
            leaf.mov(leaf.edx, leaf.dword[leaf.rax + 8]);
            leaf.mov(leaf.rdx, reinterpret_cast<uint64_t>(fixture_tcb));
            leaf.cmp(leaf.rax, leaf.rdx); leaf.setne(leaf.cl); leaf.movzx(leaf.ecx, leaf.cl);
        }
        if (store_operation) leaf.xor_(leaf.ecx, leaf.ecx);
        for (unsigned slot = 1; slot <= 16; ++slot) {
            leaf.cmp(leaf.qword[leaf.rsp - slot*8], 0x12340000 + slot);
            leaf.setne(leaf.dl); leaf.movzx(leaf.edx, leaf.dl); leaf.or_(leaf.ecx, leaf.edx);
        }
        leaf.mov(leaf.eax, leaf.ecx);
        leaf.pop(leaf.rdi); leaf.pop(leaf.rsi); leaf.pop(leaf.rbp); leaf.pop(leaf.rbx); leaf.ret(); leaf.ready();
        PatchProgram(&program, memory + 0x1000, leaf.getSize());
        const std::array<uintptr_t,1> starts{memory + 0x1000};
        const auto protected_result = PatchRedZoneMemoryInstructions(memory + 0x1000, leaf.getSize(), starts);
        if (!store_operation) Check(protected_result.patched_memory_instruction_count > 0,
            "red-zone analysis continues after the TLS trampoline and protects later accesses");
        Common::VirtualMemory::FlushInstructionCache(memory, 0x8000);
        using LeafFunction = KYTY_SYSV_ABI uint64_t (*)();
        Check(reinterpret_cast<LeafFunction>(memory + 0x1000)() == 0,
            "TLS load/store preserves all 128 red-zone bytes and returns the selected register");
    }
    UnregisterRedZonePatchModule(reinterpret_cast<void*>(memory));
    Check(Libs::LibKernel::Memory::FreeGuestMemory(memory,0x8000),"release patch fixture");
    fixture_tcb = nullptr;
    std::puts("PHASE1_PASS instruction boundaries, prefixed TLS loads, TLS store and preserved trap");
    return 0;
}

int NativeState() {
    using namespace Loader;
    const auto memory = Libs::LibKernel::Memory::AllocateRuntimeMemory(0, 0x4000,
        Common::VirtualMemory::Mode::ExecuteReadWrite, "phase1_native_state");
    Check(memory != 0, "allocate native state fixture");
    auto* safe = new (reinterpret_cast<void*>(memory)) Jit::SafeCall;
    safe->SetFunc(&ClobberVectorState);
    std::array<uint8_t,32> vector_result{};
    Xbyak::CodeGenerator code(512, reinterpret_cast<void*>(memory+0x1000));
    code.push(code.rbx);
    code.vpcmpeqd(code.ymm0,code.ymm0,code.ymm0);
    code.mov(code.eax,1); code.cmp(code.eax,2);
    code.pushfq(); code.pop(code.rbx);
    code.mov(code.rax,memory); code.call(code.rax);
    code.pushfq(); code.pop(code.rcx);
    code.mov(code.rdx,reinterpret_cast<uint64_t>(vector_result.data()));
    code.vmovdqu(code.ptr[code.rdx],code.ymm0);
    code.xor_(code.rcx,code.rbx); code.and_(code.rcx,0x8d5);
    code.cmp(code.rax,0x505831); code.setne(code.al); code.movzx(code.eax,code.al);
    code.or_(code.rax,code.rcx);
    code.vzeroupper(); code.pop(code.rbx); code.ret(); code.ready();
    Common::VirtualMemory::FlushInstructionCache(memory,0x4000);
    using Function = KYTY_SYSV_ABI uint64_t (*)();
    Check(reinterpret_cast<Function>(memory+0x1000)() == 0, "TLS helper preserves arithmetic flags and return pointer");
    Check(std::all_of(vector_result.begin(),vector_result.end(),[](uint8_t b){return b==0xff;}),
          "TLS helper restores all 256 bits of YMM0 after host clobber");
    Check(Libs::LibKernel::Memory::FreeGuestMemory(memory,0x4000), "release native fixture");
    std::puts("PHASE1_PASS native TLS helper flags and AVX state preservation");
    return 0;
}

int Imports() {
    using namespace Loader;
    auto* linker = Common::Singleton<RuntimeLinker>::Instance();
    Program program;
    program.rt = linker;
    program.dynamic_info = std::make_unique<DynamicInfo>();
    program.dynamic_info->import_libs.push_back({"L", 1, "Phase1Library"});
    program.dynamic_info->import_modules.push_back({"M", 1, 0, "Phase1Module"});
    SymbolResolve key{"SyntheticNid", "Phase1Library", 1, "Phase1Module", 1, 0, SymbolType::Func};
    auto wrong = key;
    wrong.library = "OtherLibrary";
    linker->Symbols()->Add(wrong, 0x1111);
    SymbolRecord found{};
    linker->Resolve("SyntheticNid#L#M", SymbolType::Func, &program, &found, nullptr);
    Check(found.vaddr == 0, "NID collision cannot cross library namespace");
    linker->Symbols()->Add(key, reinterpret_cast<uint64_t>(&NativeArguments));
    linker->Resolve("SyntheticNid#L#M", SymbolType::Func, &program, &found, nullptr);
    Check(found.vaddr == reinterpret_cast<uint64_t>(&NativeArguments), "qualified import resolves");
    for (int variant = 0; variant < 4; ++variant) {
        auto absent = key;
        if (variant == 0) absent.library_version++;
        if (variant == 1) absent.module = "OtherModule";
        if (variant == 2) absent.module_version_minor++;
        if (variant == 3) absent.type = SymbolType::Object;
        Check(!linker->ResolveLoadedSymbol(SymbolDatabase::GenerateName(absent), &found), "late resolution preserves namespace/version/type");
    }
    RelocationInfo relocation;
    relocation.bind = BindType::Global;
    relocation.type = SymbolType::Func;
    relocation.name = SymbolDatabase::GenerateName(key);
    const auto thunk = RegisterStubbedImport(123, &program, relocation);
    using Function = decltype(&NativeArguments);
    Check(reinterpret_cast<Function>(thunk)(1,2,3,4,5,6,7,8,1,2,3,4,5,6,7,8) == 408,
          "late thunk preserves eight integer and eight floating arguments including stack arguments");

    Elf64_Sym symbol{};
    symbol.st_info = (STB_WEAK << 4) | STT_FUNC;
    char name[] = "Absent#L#M";
    program.dynamic_info->symbol_table = &symbol;
    program.dynamic_info->symbol_table_total_size = sizeof(symbol);
    program.dynamic_info->str_table = name;
    program.dynamic_info->str_table_size = sizeof(name);
    Elf64_Rela rel{};
    rel.r_info = R_X86_64_64;
    rel.r_addend = 17;
    auto info = GetRelocationInfo(&rel, &program);
    Check(!info.resolved && info.bind == BindType::Weak && info.value == 17, "weak S=0 retains absolute relocation addend");
    rel.r_info = R_X86_64_GLOB_DAT;
    info = GetRelocationInfo(&rel, &program);
    Check(!info.resolved && info.value == 0, "weak global data relocation is zero");
    const auto page = Libs::LibKernel::Memory::AllocateRuntimeMemory(0,0x4000,
        Common::VirtualMemory::Mode::ReadWrite,"phase1_relocation_fixture");
    Check(page != 0,"allocate relocation fixture");
    program.base_vaddr = page;
    program.base_size = 0x4000;
    auto relocate = [&](uint32_t type,int64_t addend,uint64_t expected) {
        rel.r_info=type; rel.r_addend=addend;
        *reinterpret_cast<uint64_t*>(page)=0xfefefefefefefefe;
        RelocateRecord(0,&rel,&program,type==R_X86_64_JUMP_SLOT,false,nullptr);
        Check(*reinterpret_cast<uint64_t*>(page)==expected,"production relocation writes the specified value");
    };
    relocate(R_X86_64_64,17,17);
    relocate(R_X86_64_GLOB_DAT,17,0);
    relocate(R_X86_64_JUMP_SLOT,17,0);
    relocate(R_X86_64_RELATIVE,23,page+23);
    relocate(R_X86_64_DTPMOD64,0,reinterpret_cast<uint64_t>(&program));
    char defined_name[]="SyntheticNid#L#M";
    program.dynamic_info->str_table=defined_name;
    symbol.st_info=(STB_GLOBAL<<4)|STT_FUNC;
    relocate(R_X86_64_JUMP_SLOT,99,reinterpret_cast<uint64_t>(&NativeArguments));
    relocate(R_X86_64_GLOB_DAT,99,reinterpret_cast<uint64_t>(&NativeArguments));
    relocate(R_X86_64_64,5,reinterpret_cast<uint64_t>(&NativeArguments)+5);
    symbol.st_info=(STB_LOCAL<<4)|STT_OBJECT; symbol.st_shndx=1; symbol.st_value=64;
    relocate(R_X86_64_64,3,page+67);
    Check(Libs::LibKernel::Memory::FreeGuestMemory(page,0x4000),"release relocation fixture");
    program.base_vaddr=program.base_size=0;
    std::puts("PHASE1_PASS qualified imports, weak absence, native thunk argument preservation");
    return 0;
}

int MissingStrongData() {
    using namespace Loader;
    Program program;
    program.rt=Common::Singleton<RuntimeLinker>::Instance();
    program.dynamic_info=std::make_unique<DynamicInfo>();
    Elf64_Sym symbol{};
    symbol.st_info=(STB_GLOBAL<<4)|STT_OBJECT;
    char name[]="Phase1OriginalMissingObject";
    program.dynamic_info->symbol_table=&symbol;
    program.dynamic_info->str_table=name;
    Elf64_Rela relocation{};
    relocation.r_info=R_X86_64_64;
    RelocateRecord(0,&relocation,&program,false,false,nullptr);
    return 1;
}

int Tls() {
    using namespace Loader;
    Program first, late;
    for (auto* program: {&first, &late}) {
        program->tls.image_size = 64;
        program->tls.tcb_offset = 64;
        program->tls.init_image = {0x50, 0x58, 0x31};
    }
    std::barrier rendezvous(32);
    std::vector<std::thread> workers;
    for (unsigned id = 0; id < 32; ++id) workers.emplace_back([&, id] {
        for (int iteration = 0; iteration < 16; ++iteration) {
            auto* a = RuntimeLinker::TlsGetAddr(&first);
            Check(a[0] == 0x50 && a[2] == 0x31 && a[3] == 0 && a[63] == 0, "TLS initialization and zero fill");
            a[3] = static_cast<uint8_t>(id + 1);
            rendezvous.arrive_and_wait();
            auto* b = RuntimeLinker::TlsGetAddr(&late);
            Check(a != b && b[3] == 0, "late module TLS has distinct storage");
            Check(a[3] == id + 1 && RuntimeLinker::TlsGetAddr(&first) == a, "32-thread TLS isolation");
            Check(*reinterpret_cast<uint64_t*>(a + 64) == reinterpret_cast<uint64_t>(a + 64), "TCB self pointer");
            rendezvous.arrive_and_wait();
            RuntimeLinker::DeleteTls(&first, Common::Thread::GetThreadIdUnique());
            RuntimeLinker::DeleteTls(&late, Common::Thread::GetThreadIdUnique());
        }
    });
    for (auto& worker: workers) worker.join();
    Check(first.tls.tlss.empty() && late.tls.tlss.empty(), "TLS lifecycle releases all blocks");
    std::puts("PHASE1_PASS 32 threads, 16 TLS lifecycles, two module blocks, zero live blocks");
    return 0;
}
} // namespace Phase1
