// Original synthetic checkpoint coverage; no commercial payloads.
namespace Libs::LibC { void InitLibC_1(Loader::SymbolDatabase*); }
namespace Phase6Checkpoint {
int Run() {
    using Phase1::Check;
    using namespace Loader;
    auto* linker = Common::Singleton<RuntimeLinker>::Instance();
    auto* symbols = linker->Symbols();
    SymbolRecord found{};
    auto resolves = [&](const SymbolResolve& key, uint64_t address) {
        return linker->ResolveLoadedSymbol(SymbolDatabase::GenerateName(key), &found) &&
               found.vaddr == address;
    };
    // Characterize the existing compatibility path, not a claim that every
    // export in either identity has had its guest ABI independently validated.
    for (const auto& pair : std::array<std::pair<SymbolResolve,SymbolResolve>,3>{{
        {{"CheckpointAgc","Agc",1,"Agc",1,1,SymbolType::Func},
         {"CheckpointAgc","Graphics5",1,"Graphics5",1,1,SymbolType::Func}},
        {{"CheckpointJson","Json2",1,"Json",1,1,SymbolType::Func},
         {"CheckpointJson","Json2",1,"Json2",1,1,SymbolType::Func}},
        {{"hdpVEUDFW3s","Ssl",1,"Ssl",2,1,SymbolType::Func},
         {"hdpVEUDFW3s","Ssl",1,"Ssl",1,1,SymbolType::Func}}
    }}) {
        symbols->Add(pair.second,0x1111);
        Check(resolves(pair.first,0x1111),"late resolution uses the declared compatibility identity");
        for (unsigned i=0;i<5;++i) {
            auto wrong=pair.first;
            if(i==0) wrong.library_version++;
            if(i==1) wrong.module_version_minor++;
            if(i==2) wrong.module="Unrelated";
            if(i==3) wrong.type=SymbolType::Object;
            if(i==4) wrong.name="AbsentCheckpointNid";
            Check(!linker->ResolveLoadedSymbol(SymbolDatabase::GenerateName(wrong),&found),
                  "compatibility cannot erase NID, type or unrelated qualifiers");
        }
        symbols->Add(pair.first,0x2222);
        Check(resolves(pair.first,0x2222),"exact HLE registration takes priority over compatibility");
    }
    symbols->Add({"OtherSslNid","Ssl",1,"Ssl",1,1,SymbolType::Func},0x3333);
    Check(!linker->ResolveLoadedSymbol("OtherSslNid[Ssl_v1][Ssl_v2.1][Func]",&found),
          "Ssl version bridge is limited to SslInit");
    linker->Clear();

    SymbolDatabase libc;
    Libs::LibC::InitLibC_1(&libc);
    auto lookup = [&](const char* nid) {
        const auto* entry=libc.Find({nid,"LibcInternal",1,"LibcInternal",1,1,SymbolType::Func});
        Check(entry!=nullptr,"mspace export uses its exact LibcInternal identity");
        return entry->vaddr;
    };
    using Create = KYTY_SYSV_ABI void* (*)(const char*,void*,size_t,uint32_t);
    using Allocate = KYTY_SYSV_ABI void* (*)(void*,size_t);
    const auto create=reinterpret_cast<Create>(lookup("-hn1tcVHq5Q"));
    const auto allocate=reinterpret_cast<Allocate>(lookup("OJjm-QOIHlI"));
    // Supported aligned arenas only. Free/realloc/destroy and unaligned bases
    // are deliberately not represented as validated contracts by this fixture.
    alignas(16) std::array<uint8_t,96> arena;
    arena.fill(0xa5);
    Check(!create(nullptr,arena.data(),64,0) && !create("synthetic",nullptr,64,0) &&
          !create("synthetic",arena.data(),0,0) && !create("synthetic",arena.data(),64,2),
          "mspace rejects missing inputs and unsupported flags");
    Check(!allocate(nullptr,16),"null mspace fails without mutation");
    for(uint32_t flag: {0u,1u}) {
        void* space=create("synthetic",arena.data()+16,64,flag);
        Check(space!=nullptr,"create bounded aligned mspace");
        Check(allocate(space,1)==arena.data()+16 && allocate(space,17)==arena.data()+32,
              "mspace returns nonoverlapping aligned allocations");
        Check(!allocate(space,SIZE_MAX),"oversized allocation fails without overflow");
        auto* last=static_cast<uint8_t*>(allocate(space,16));
        Check(last==arena.data()+64 && !allocate(space,1),
              "failed allocation preserves cursor; exact-capacity allocation exhausts arena");
        std::memset(last,0x5a,16);
        Check(std::all_of(arena.begin(),arena.begin()+16,[](auto b){return b==0xa5;}) &&
              std::all_of(arena.begin()+80,arena.end(),[](auto b){return b==0xa5;}),
              "writes to returned storage preserve adjacent guards");
    }
    alignas(16) std::array<uint8_t,16*256> threaded_arena{};
    void* space=create("threaded",threaded_arena.data(),threaded_arena.size(),0);
    Check(space!=nullptr,"create synchronized mspace");
    std::array<uintptr_t,256> pointers{};
    std::array<std::thread,4> workers;
    for(size_t t=0;t<workers.size();++t) workers[t]=std::thread([&,t]{
        for(size_t i=0;i<64;++i) pointers[t*64+i]=reinterpret_cast<uintptr_t>(allocate(space,16));
    });
    for(auto& worker:workers) worker.join();
    std::sort(pointers.begin(),pointers.end());
    const auto base=reinterpret_cast<uintptr_t>(threaded_arena.data());
    for(size_t i=0;i<pointers.size();++i)
        Check(pointers[i]==base+16*i,"synchronized allocations cover arena once with no overlap");
    Check(!allocate(space,16),"synchronized arena exhaustion");
    std::puts("PHASE6_CHECKPOINT_PASS resolver boundaries and supported bounded mspace allocation");
    return 0;
}
}
