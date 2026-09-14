// Original ABI conformance fixtures, independent of commercial payloads.
#include "libs/agc.h"
#include "graphics/shader/shader.h"
#include "graphics/guest_gpu/gpu_defs.h"
namespace Libs { void InitAgcDriver_1(Loader::SymbolDatabase*); }
namespace Phase6Agc {
// Independent guest ABI view; no production allocator methods are duplicated.
struct BufferAbi {
    uint32_t* bottom;
    uint32_t* top;
    uint32_t* up;
    uint32_t* down;
    using Callback=KYTY_SYSV_ABI bool (*)(Libs::Graphics::Gen5::CommandBuffer*,uint32_t,void*);
    Callback callback;
    void* user;
    uint32_t reserved;
};
static_assert(offsetof(BufferAbi,reserved)==48 && sizeof(BufferAbi)==56);
int Run(const char* mode) {
    using Phase1::Check;
    Loader::SymbolDatabase symbols;
    Libs::InitAgcDriver_1(&symbols);
    Loader::SymbolResolve key{"23LRUSvYu1M","Agc",1,"Agc",1,1,Loader::SymbolType::Func};
    const auto* record=symbols.Find(key);
    Check(record!=nullptr,"AGC initialization resolves with the exact guest library/module identity");
    using Init = KYTY_SYSV_ABI int (*)(uint32_t*,uint32_t);
    const auto init=reinterpret_cast<Init>(record->vaddr);
    uint32_t state=0x5a37c9e1;
    if (std::strcmp(mode,"null")==0) return init(nullptr,13);
    if (std::strcmp(mode,"version")==0) return init(&state,14);
    if (std::strcmp(mode,"overflow")==0) return init(&state,UINT32_MAX);
    key.library_version=2; Check(!symbols.Find(key),"AGC library version is not wildcarded");
    key.library_version=1; key.module_version_minor=2; Check(!symbols.Find(key),"AGC module version is not wildcarded");
    key.module_version_minor=1; key.module="Unrelated"; Check(!symbols.Find(key),"AGC NID never resolves through unrelated modules");
    key.module="Agc"; key.name="dolOmWH+huQ";
    Check(!symbols.Find(key),"unreviewed Graphics5 shader-fusion exports are not blanket-aliased");
    key.name="23LRUSvYu1M"; key.module=key.library="Graphics5";
    Check(symbols.Find(key)!=nullptr,"existing Graphics5 identity is retained");
    key.name="BfBDZGbti7A"; key.module=key.library="Agc";
    const auto* trinity=symbols.Find(key);
    Check(trinity!=nullptr,"Trinity query resolves with exact Agc identity");
    using QueryMode = KYTY_SYSV_ABI void (*)(uint8_t*);
    uint8_t modeBytes[3]{0x5a,0xff,0xc3};
    reinterpret_cast<QueryMode>(trinity->vaddr)(modeBytes+1);
    Check(modeBytes[0]==0x5a && modeBytes[1]==0 && modeBytes[2]==0xc3,
          "base-console query writes exactly one output byte and preserves adjacent bytes");
    key.module_version_minor=2;
    Check(!symbols.Find(key),"Trinity query does not wildcard module versions");
    key.module_version_minor=1; key.module="Unrelated";
    Check(!symbols.Find(key),"Trinity query keeps qualified resolution strict");
    Loader::SymbolResolve eventKey{"w2rJhmD+dsE","AgcDriver",1,"AgcDriver",1,1,Loader::SymbolType::Func};
    const auto* addEvent=symbols.Find(eventKey);
    Check(addEvent!=nullptr,"graphics event registration has exact AgcDriver identity");
    using AddEvent=KYTY_SYSV_ABI int (*)(Libs::LibKernel::EventQueue::KernelEqueue,int,void*);
    Check(reinterpret_cast<AddEvent>(addEvent->vaddr)(Libs::LibKernel::EventQueue::KERNEL_EQUEUE_INVALID,0,nullptr)
              ==Libs::LibKernel::KERNEL_ERROR_EBADF,"event registration preserves invalid-queue error");
    eventKey.name="DL2RXaXOy88";
    const auto* deleteEvent=symbols.Find(eventKey);
    Check(deleteEvent!=nullptr,"matching graphics event removal has exact AgcDriver identity");
    using DeleteEvent=KYTY_SYSV_ABI int (*)(Libs::LibKernel::EventQueue::KernelEqueue,int);
    Check(reinterpret_cast<DeleteEvent>(deleteEvent->vaddr)(Libs::LibKernel::EventQueue::KERNEL_EQUEUE_INVALID,0)
              ==Libs::LibKernel::KERNEL_ERROR_EBADF,"event removal preserves invalid-queue error");
    for(const char* nid : {"w2rJhmD+dsE","DL2RXaXOy88"}) {
        eventKey.name=nid; eventKey.library_version=2;
        Check(!symbols.Find(eventKey),"event library versions remain strict");
        eventKey.library_version=1; eventKey.module_version_minor=2;
        Check(!symbols.Find(eventKey),"event module versions remain strict");
        eventKey.module_version_minor=1; eventKey.module="Agc";
        Check(!symbols.Find(eventKey),"AgcDriver events do not leak into Agc");
        eventKey.module="AgcDriver";
    }
    auto* page=static_cast<uint32_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    Check(page!=nullptr,"allocate state canary page");
    for (unsigned i=0;i<1024;++i) page[i]=0x6c900000u+i;
    DWORD old=0;
    Check(VirtualProtect(page,4096,PAGE_READONLY,&old)!=0,"make caller state read-only");
    for (unsigned repeat=0;repeat<2;++repeat)
        for (uint32_t version=0;version<=13;++version)
            Check(init(page+511,version)==0,"supported AGC versions preserve caller state across repeated initialization");
    for (unsigned i=0;i<1024;++i) Check(page[i]==0x6c900000u+i,"AGC does not overwrite caller state or surrounding bytes");
    VirtualFree(page,0,MEM_RELEASE);
    // Inspect the published table ABI without importing production private
    // structures. These checks distinguish v13's internal bank from v11.
    struct Defaults {
        Libs::Graphics::ShaderRegister** tables[4];
        uint32_t counts[4];
        uint32_t* types;
        uint32_t count;
    };
    static_assert(offsetof(Defaults,count)==0x38);
    auto snapshot=[](void* source) { Defaults result{}; std::memcpy(&result,source,sizeof(result)); return result; };
    using GetDefaults = KYTY_SYSV_ABI void* (*)(uint32_t);
    auto resolveDefaults=[&](const char* nid) {
        Loader::SymbolResolve defaultsKey{nid,"Agc",1,"Agc",1,1,Loader::SymbolType::Func};
        const auto* entry=symbols.Find(defaultsKey);
        Check(entry!=nullptr,"reviewed defaults export resolves under exact Agc identity");
        defaultsKey.library_version=2;
        Check(!symbols.Find(defaultsKey),"defaults library version remains strict");
        defaultsKey.library_version=1; defaultsKey.module_version_minor=2;
        Check(!symbols.Find(defaultsKey),"defaults module version remains strict");
        defaultsKey.module_version_minor=1; defaultsKey.module="Unrelated";
        Check(!symbols.Find(defaultsKey),"defaults never use NID-only resolution");
        defaultsKey.library=defaultsKey.module="Graphics5";
        const auto* legacy=symbols.Find(defaultsKey);
        Check(legacy && legacy->vaddr==entry->vaddr,"legacy defaults ABI remains intact");
        return reinterpret_cast<GetDefaults>(entry->vaddr);
    };
    const auto getPublic=resolveDefaults("2JtWUUiYBXs");
    const auto getInternal=resolveDefaults("wRbq6ZjNop4");
    auto internal11=snapshot(getInternal(11));
    auto internal13=snapshot(getInternal(13));
    Check(internal13.counts[1]==12 && internal13.count==24,"version 13 internal table shape is retained");
    Check(internal13.tables[1][9]->offset==0x81 && internal13.tables[1][9]->value==1
        && internal13.tables[1][10]->offset==0x101 && internal13.tables[1][10]->value==3,
        "version 13 selects its actual internal register bank");
    Check(internal11.tables[1][9]->value==0 && internal11.tables[1][10]->value==0,
        "version 13 initialization does not mutate version 11 defaults");
    const auto public13=snapshot(getPublic(13));
    Check(public13.counts[0]==488 && public13.count==137,"version 13 retains its public table contract");
    bool patched_register=false;
    for(uint32_t i=0;i<public13.counts[0];++i) {
        const auto& reg=public13.tables[0][0][i];
        if(reg.offset==0x201) patched_register=(reg.value & 0x10000)!=0;
    }
    Check(patched_register,"version 13 public defaults include the version 11-plus register patch");
    Loader::SymbolResolve releaseKey{"wr23dPKyWc0","Agc",1,"Agc",1,1,Loader::SymbolType::Func};
    const auto* releaseEntry=symbols.Find(releaseKey);
    Check(releaseEntry!=nullptr,"release-memory command has exact Agc identity");
    const auto release=reinterpret_cast<decltype(&Libs::Graphics::Gen5::AgcCbReleaseMem)>(releaseEntry->vaddr);
    std::array<uint32_t,10> packet;
    packet.fill(0xaabbccdd);
    BufferAbi buffer{packet.data()+1,packet.data()+9,packet.data()+1,packet.data()+9,nullptr,nullptr,0};
    const auto* label=reinterpret_cast<const volatile Libs::Graphics::Gen5::Label*>(0x123456780ull);
    auto* command=release(reinterpret_cast<Libs::Graphics::Gen5::CommandBuffer*>(&buffer),
                          0x28,0,0,0,label,2,0x123456789abcdef0ull,0,0,2,0x34567);
    const std::array<uint32_t,8> expected{0xc0061060,0x528,0x42000000,0x23456780,1,0x9abcdef0,0x12345678,0x34567};
    Check(command==packet.data()+1 && buffer.up==packet.data()+9 &&
          std::equal(expected.begin(),expected.end(),packet.data()+1) &&
          packet.front()==0xaabbccdd && packet.back()==0xaabbccdd,
          "release-memory ABI carries register/stack arguments into an exact bounded packet");
    releaseKey.module_version_minor=2;
    Check(!symbols.Find(releaseKey),"release-memory module version remains strict");
    releaseKey.module_version_minor=1; releaseKey.name="57labkp+rSQ";
    const auto* acquireEntry=symbols.Find(releaseKey);
    Check(acquireEntry!=nullptr,"acquire-memory command has exact Agc identity");
    const auto acquire=reinterpret_cast<decltype(&Libs::Graphics::Gen5::AgcDcbAcquireMem)>(acquireEntry->vaddr);
    packet.fill(0xaabbccdd); buffer.up=packet.data()+1;
    command=acquire(reinterpret_cast<Libs::Graphics::Gen5::CommandBuffer*>(&buffer),
                    1,0,0x123,reinterpret_cast<const volatile void*>(0x1234567800ull),0x200,80);
    const std::array<uint32_t,8> expectedAcquire{0xc0061050,0x80000000,2,0,0x12345678,0,2,0x123};
    Check(command==packet.data()+1 && buffer.up==packet.data()+9 &&
          std::equal(expectedAcquire.begin(),expectedAcquire.end(),packet.data()+1) &&
          packet.front()==0xaabbccdd && packet.back()==0xaabbccdd,
          "acquire-memory ABI carries the stack poll interval and bounded address/size into the packet");
    releaseKey.name="f3dg2CSgRKY";
    const auto* shaderEntry=symbols.Find(releaseKey);
    Check(shaderEntry!=nullptr,"reviewed shader creation has exact Agc identity");
    const auto createShader=reinterpret_cast<decltype(&Libs::Graphics::Gen5::AgcCreateShader)>(shaderEntry->vaddr);
    using namespace Libs::Graphics;
    ShaderInit();
    struct ShaderFixture {
        Shader header{};
        ShaderUserData user{};
        ShaderRegister regs[2]{{0x20c,1},{0x20d,0xaabbcc00}};
        uint16_t offsets[1]{7};
        ShaderSharp sharp[1]{};
        ShaderSemantic input[1]{};
        ShaderSpecialRegs specials{};
        alignas(256) uint32_t code[256]{};
    } fixture;
    auto relative=[](auto& field,auto* target) {
        using Pointer=std::remove_reference_t<decltype(field)>;
        field=reinterpret_cast<Pointer>(reinterpret_cast<uintptr_t>(target)-reinterpret_cast<uintptr_t>(&field));
    };
    auto& header=fixture.header;
    header.file_header=0x34333231; header.version=0x18;
    header.type=static_cast<uint8_t>(Prospero::ShaderBinaryType::kCs);
    header.header_size=offsetof(ShaderFixture,code); header.shader_size=sizeof(fixture.code);
    header.num_sh_registers=2; header.num_input_semantics=1;
    relative(header.sh_registers,fixture.regs); relative(header.user_data,&fixture.user);
    relative(header.input_semantics,fixture.input); relative(header.specials,&fixture.specials);
    relative(fixture.user.direct_resource_offset,fixture.offsets);
    relative(fixture.user.sharp_resource_offset[0],fixture.sharp);
    fixture.user.direct_resource_count=1; fixture.user.sharp_resource_count[0]=1;
    Shader* output=nullptr;
    Check(createShader(&output,&header,fixture.code)==0 && output==&header,
          "shader creation publishes the relocated caller header");
    Check(header.sh_registers==fixture.regs && header.user_data==&fixture.user &&
          header.input_semantics==fixture.input && header.specials==&fixture.specials &&
          header.cx_registers==nullptr && header.output_semantics==nullptr &&
          fixture.user.direct_resource_offset==fixture.offsets &&
          fixture.user.sharp_resource_offset[0]==fixture.sharp &&
          fixture.user.sharp_resource_offset[1]==nullptr && header.code==fixture.code,
          "shader relative pointers relocate from each field, with null pointers preserved");
    const auto shaderAddress=reinterpret_cast<uint64_t>(fixture.code)+256;
    Check(fixture.regs[0].value==uint32_t(shaderAddress>>8) &&
          fixture.regs[1].value==(0xaabbcc00u|uint32_t((shaderAddress>>40)&255)),
          "shader program relocation preserves byte offset and unrelated high-register bits");
    releaseKey.name="D9sr1xGUriE";
    const auto* primEntry=symbols.Find(releaseKey);
    Check(primEntry!=nullptr,"primitive state has exact Agc identity");
    const auto createPrim=reinterpret_cast<decltype(&Gen5::AgcCreatePrimState)>(primEntry->vaddr);
    ShaderSpecialRegs special{};
    special.vgt_shader_stages_en={0x2d5,0x20}; special.vgt_gs_out_prim_type={0x29b,2};
    special.ge_cntl={0x25b,0x100}; special.ge_user_vgpr_en={0x262,1};
    Shader gs{}; gs.type=static_cast<uint8_t>(Prospero::ShaderBinaryType::kGs); gs.specials=&special;
    std::array<ShaderRegister,4> cx{};
    std::array<ShaderRegister,5> uc{};
    cx.front()=cx.back()=uc.front()=uc.back()={0xaabbccdd,0x12345678};
    Check(createPrim(cx.data()+1,uc.data()+1,nullptr,&gs,4)==0 &&
          cx[1].offset==0x2d5 && cx[1].value==0x20 && cx[2].offset==0x29b && cx[2].value==2 &&
          uc[1].offset==0x25b && uc[1].value==0x100 && uc[2].offset==0x262 && uc[2].value==1 &&
          uc[3].offset==0x242 && uc[3].value==4,
          "primitive ABI writes two context and three user-config registers from the shader");
    for(const auto* guard : {&cx.front(),&cx.back(),&uc.front(),&uc.back()})
        Check(guard->offset==0xaabbccdd && guard->value==0x12345678,"primitive outputs preserve adjacent bytes");
    std::puts("PHASE6_AGC_PASS exact_identity=1 state_preserved=1 versions=0..13 repeated=1 version13_defaults=1");
    return 0;
}
}
