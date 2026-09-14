// Original fixtures: guest allocation/permissions, configuration lifetime and
// first-use boundary. No commercial bytes or proprietary register tables.
#include "graphics/guest_gpu/tessellationState.h"

namespace Phase6Ring {
int Run() {
    using Phase1::Check;
    using Libs::Graphics::TessellationState;
    namespace M=Libs::LibKernel::Memory;
    Loader::SymbolDatabase symbols;
    Libs::InitAgcDriver_1(&symbols);
    Loader::SymbolResolve key{"XlNp7jzGiPo","AgcDriver",1,"AgcDriver",1,1,Loader::SymbolType::Func};
    const auto* entry=symbols.Find(key);
    Check(entry!=nullptr,"ring setter exact qualified identity");
    Check(entry->vaddr==reinterpret_cast<uint64_t>(&Libs::Graphics::Gen5Driver::AgcDriverSetTFRing),
          "ring setter uses pointer and 32-bit byte-size SysV ABI");
    key.library_version=2; Check(!symbols.Find(key),"ring library version remains strict");
    key.library_version=1; key.module_version_minor=2; Check(!symbols.Find(key),"ring module version remains strict");
    key.module_version_minor=1; key.module="Agc"; Check(!symbols.Find(key),"ring does not leak into Agc");
    key.module=key.library="Graphics5Driver";
    Check(symbols.Find(key)->vaddr==entry->vaddr,"legacy ring binding retained");
    constexpr uint64_t base=0x209c00000ull, bytes=0x20000;
    int64_t offset=-1;
    Check(M::KernelAllocateDirectMemory(0,M::KernelGetDirectMemorySize(),bytes,0x10000,0,&offset)==0,"allocate ring fixture");
    void* mapped=reinterpret_cast<void*>(base);
    Check(M::KernelMapDirectMemory(&mapped,bytes,0x33,0x10,offset,0x10000)==0 &&
          reinterpret_cast<uint64_t>(mapped)==base,"map ring fixture with CPU/GPU read/write");
    std::memset(mapped,0xa5,bytes);
    TessellationState ring;
    Check(ring.Snapshot().generation==0,"new renderer state is unconfigured");
    Check(!ring.Configure(base+256,0x10004),"byte-sized range spans mappings without requiring page alignment");
    auto before=ring.Snapshot();
    Check(before.base==base+256 && before.size_bytes==0x10004 && before.generation==1,"configuration preserves exact bytes");
    for(const auto& invalid : std::array<std::pair<uint64_t,uint32_t>,7>{{
        {0,256},{base+1,256},{base,0},{base,7},{UINT64_MAX-255,256},{base,bytes+4},{base+bytes,4}}}) {
        Check(ring.Configure(invalid.first,invalid.second)!=nullptr,"invalid range rejected");
        const auto after=ring.Snapshot();
        Check(after.base==before.base && after.size_bytes==before.size_bytes && after.generation==before.generation,
              "failed replacement preserves prior configuration");
    }
    Check(M::KernelMprotect(reinterpret_cast<void*>(base+0x10000),0x10000,0x13)==0,"remove GPU write access in second slice");
    Check(TessellationState::Validate(before)!=nullptr && ring.Configure(base,bytes)!=nullptr,"full range permissions are checked");
    Check(M::KernelMprotect(mapped,bytes,0x33)==0,"restore access");
    Check(!ring.Configure(base,bytes),"reconfiguration succeeds after restored access");
    Check(ring.Snapshot().generation==2,"generation changes only on successful configuration");
    ring.RequireDrawSupport(0,4);
    ring.RequireDrawSupport(0x20,17);
    Check(!ring.ConfigureOffchip(0,0x1ff) && ring.OffchipSnapshot().buffering==511,
          "offchip configuration retains biased block count, not an address");
    Check(ring.ConfigureOffchip(1,0)!=nullptr && ring.ConfigureOffchip(0,0x400)!=nullptr &&
          ring.OffchipSnapshot().buffering==511 && ring.OffchipSnapshot().generation==1,
          "unsupported offchip fields preserve last accepted configuration");
    Check(!ring.ConfigureOffchip(0,0) && !ring.ConfigureOffchip(0,0x3ff),"offchip field bounds include 1 and 1024 blocks");
    Check(TessellationState::UsesNativeTessellation(1,4) && TessellationState::UsesNativeTessellation(4,4) &&
          TessellationState::UsesNativeTessellation(0,9),"LS/HS and patch draws consume native tessellation");
    Check(!TessellationState::UsesNativeTessellation(0,7),"rectangle-list host tessellation is independent");
    const auto* data=static_cast<const unsigned char*>(mapped);
    Check(std::all_of(data,data+bytes,[](auto b){return b==0xa5;}),"configuration and unused ring never modify guest payload");
    before=ring.Snapshot();
    Check(M::KernelMunmap(base,bytes)==0,"unmap fixture");
    Check(TessellationState::Validate(before)!=nullptr,"unmapped ring fails revalidation");
    ring.Reset(); Check(ring.Snapshot().generation==0 && ring.Snapshot().base==0,"reset clears process configuration");
    Check(ring.OffchipSnapshot().generation==0,"reset clears offchip configuration");
    key={"MM4IZSEYytQ","AgcDriver",1,"AgcDriver",1,1,Loader::SymbolType::Func};
    Check(symbols.Find(key)!=nullptr,"offchip exact qualified identity");
    key.module_version_minor=2; Check(!symbols.Find(key),"offchip version remains strict");
    Check(M::KernelReleaseDirectMemory(offset,bytes)==0,"release fixture backing");
    std::puts("PHASE6_RING_PASS qualified=1 bytes=1 replacement=1 permissions=1 payload_unchanged=1 lifetime=1");
    return 0;
}
}
