// Original guest ABI fixture exercising the exported submission and drain,
// with the actual graphics lifecycle rather than a test-only renderer pointer.
namespace Phase6Driver {
int Run() {
    using Phase1::Check;
    using namespace Libs::Graphics;
    namespace M=Libs::LibKernel::Memory;
    Loader::SymbolDatabase symbols;
    Libs::InitAgcDriver_1(&symbols);
    Loader::SymbolResolve key{"UglJIZjGssM","AgcDriver",1,"AgcDriver",1,1,Loader::SymbolType::Func};
    const auto* entry=symbols.Find(key);
    Check(entry!=nullptr,"qualified DCB submit resolves");
    const auto submit=reinterpret_cast<decltype(&Gen5Driver::AgcDriverSubmitDcb)>(entry->vaddr);
    key.module_version_minor=2;
    Check(!symbols.Find(key),"DCB submit module version remains strict");
    key={"h9z6+0hEydk","Agc",1,"Agc",1,1,Loader::SymbolType::Func};
    const auto* drainEntry=symbols.Find(key);
    Check(drainEntry!=nullptr,"qualified suspend point resolves");
    const auto drain=reinterpret_cast<decltype(&Gen5::AgcSuspendPoint)>(drainEntry->vaddr);
    constexpr uint64_t base=0x209e00000ull, bytes=0x10000;
    int64_t offset=-1;
    Check(M::KernelAllocateDirectMemory(0,M::KernelGetDirectMemorySize(),bytes,bytes,0,&offset)==0,"allocate submission fixture");
    void* address=reinterpret_cast<void*>(base);
    Check(M::KernelMapDirectMemory(&address,bytes,0x33,0x10,offset,bytes)==0,"map submission fixture");
    auto* words=static_cast<uint32_t*>(address);
    // Native five-DWORD WRITE_DATA, memory destination, one data DWORD.
    words[0]=0xc0033700; words[1]=0x40000200; words[2]=uint32_t(base+256); words[3]=uint32_t(base>>32); words[4]=0x13572468;
    words[64]=0;
    struct alignas(8) PacketAbi { uint64_t address; uint32_t dwords; uint8_t flags; uint8_t reserved[3]; };
    static_assert(sizeof(PacketAbi)==16 && offsetof(PacketAbi,flags)==12);
    PacketAbi packet{base,5,0,{}};
    Check(submit(reinterpret_cast<const Gen5Driver::Packet*>(&packet))==0,"submit actual qualified descriptor");
    Check(drain()==0,"drain submitted work");
    Check(words[64]==0x13572468,"submission executes real command and publishes memory before drain returns");
    Check(Gen5Driver::AgcDriverSetTFRing(reinterpret_cast<void*>(base+512),256)==0,"ring export accepts mapped fixture");
    Check(Gen5Driver::AgcDriverSetHsOffchipParam(0,511)==0,"offchip export accepts bounded configuration");
    Check(M::KernelMunmap(base,bytes)==0 && M::KernelReleaseDirectMemory(offset,bytes)==0,"release submission fixture");
    std::puts("PHASE6_DRIVER_PASS qualified_submit=1 native_write=1 drain_visibility=1 ring_abi=1");
    return 0;
}
}
