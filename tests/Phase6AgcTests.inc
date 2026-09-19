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
    {
        Loader::SymbolResolve event_key{"Zw7uUVPulbw","AgcDriver",1,"AgcDriver",1,1,Loader::SymbolType::Func};
        const auto* context_entry=symbols.Find(event_key);
        Check(context_entry!=nullptr,"qualified event context export");
        auto context_id=reinterpret_cast<decltype(&Libs::Graphics::Gen5Driver::AgcDriverGetEqContextId)>(context_entry->vaddr);
        event_key.module_version_minor=2;
        Check(!symbols.Find(event_key),"event context module version stays strict");
        event_key.module_version_minor=1; event_key.name="5CdQTZIQPxM";
        const auto* type_entry=symbols.Find(event_key);
        Check(type_entry!=nullptr,"qualified event type export");
        auto event_type=reinterpret_cast<decltype(&Libs::Graphics::Gen5Driver::AgcDriverGetEqEventType)>(type_entry->vaddr);
        Libs::LibKernel::EventQueue::KernelEvent event{};
        event.filter=Libs::LibKernel::EventQueue::KERNEL_EVFILT_GRAPHICS;
        event.ident=0x40; event.data=0x1234567887654321ll;
        const auto before=event;
        Check(context_id(&event)==0x87654321u && event_type(&event)==0x40,
              "graphics event preserves 32-bit context separately from event type");
        Check(std::memcmp(&before,&event,sizeof(event))==0,"event queries do not mutate delivered event");
        event.filter=0; event.ident=0xabcdef0123456789ull; event.data=9;
        Check(context_id(&event)==0x23456789u && event_type(&event)==9,
              "legacy event field interpretation retained");
        Check(context_id(nullptr)==0 && event_type(nullptr)==0,"null event query contract retained");
    }
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
    key.module="Agc";
    // A reviewed Agc-qualified entry resolves, and resolves to the very same
    // implementation the Graphics5 identity exposes: the entry is one
    // registration of one function, never a second implementation that could
    // drift. Bendy (PPSA27624) imports the interpolant mapping this way.
    key.name="dbOlWdppb4o";
    const auto* interpolant_as_agc=symbols.Find(key);
    Check(interpolant_as_agc!=nullptr,"reviewed Agc-qualified interpolant mapping resolves");
    Loader::SymbolResolve interpolantGfx5{"dbOlWdppb4o","Graphics5",1,"Graphics5",1,1,
                                          Loader::SymbolType::Func};
    const auto* interpolant_as_gfx5=symbols.Find(interpolantGfx5);
    Check(interpolant_as_gfx5!=nullptr &&
              interpolant_as_gfx5->vaddr==interpolant_as_agc->vaddr,
          "the Agc and Graphics5 identities name one implementation");
    // Reviewing entries must not widen the identity. Graphics5 exports that
    // were never reviewed under Agc still have to fail, or the review gate is a
    // blanket alias wearing a list.
    //
    // These probes are Graphics5 exports with no AgcQualified registration. If
    // one of them is ever reviewed and added, replace it here with another
    // unregistered NID rather than deleting the check -- the contract is that
    // reachability under Agc requires an explicit registration, not that any
    // particular export stays out.
    for (const char* unreviewed: {"nApJjpKNBl4","NKIzURsgV7I","HV4j+E0MBHE","2BS4EtAaF28",
                                  "s+VGAMDQ0AQ","3AyTaWcF-H8","1DeUNpRIDDA"}) {
        key.name=unreviewed;
        Check(!symbols.Find(key),"unreviewed Graphics5 exports stay unreachable as Agc");
        Loader::SymbolResolve gfx5{unreviewed,"Graphics5",1,"Graphics5",1,1,
                                   Loader::SymbolType::Func};
        Check(symbols.Find(gfx5)!=nullptr,
              "the unreviewed export exists under Graphics5, so the check is meaningful");
    }
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
    Loader::SymbolResolve suspendKey{"h9z6+0hEydk","Agc",1,"Agc",1,1,Loader::SymbolType::Func};
    const auto* suspend=symbols.Find(suspendKey);
    Check(suspend && suspend->vaddr==reinterpret_cast<uint64_t>(&Libs::Graphics::Gen5::AgcSuspendPoint),
        "qualified suspend point retains the existing real GPU drain implementation");
    suspendKey.module_version_minor=2;
    Check(!symbols.Find(suspendKey),"suspend point module identity remains strict");
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
    {
        Loader::SymbolResolve patchKey{"0fWWK5uG9rQ","Agc",1,"Agc",1,1,Loader::SymbolType::Func};
        const auto* addressEntry=symbols.Find(patchKey);
        patchKey.name="MlEw1feXcjg";
        const auto* dataEntry=symbols.Find(patchKey);
        Check(addressEntry && dataEntry,"qualified end-of-pipe patch pair resolves");
        const auto addressPatch=reinterpret_cast<decltype(&Libs::Graphics::Gen5::AgcQueueEndOfPipeActionPatchAddress)>(addressEntry->vaddr);
        const auto dataPatch=reinterpret_cast<decltype(&Libs::Graphics::Gen5::AgcQueueEndOfPipeActionPatchData)>(dataEntry->vaddr);
        Check(addressPatch(command,reinterpret_cast<const volatile Libs::Graphics::Gen5::Label*>(0xabcdef000ull))==0 &&
            command[3]==0xbcdef000 && command[4]==0xa,"end-of-pipe patch retains destination high bits");
        Check(dataPatch(command,0x8877665544332211ull)==0 && command[5]==0x44332211 && command[6]==0x88776655 &&
            command[0]==expected[0] && command[1]==expected[1] && command[2]==expected[2] && command[7]==expected[7] &&
            packet.front()==0xaabbccdd && packet.back()==0xaabbccdd,"end-of-pipe patch preserves control, context and canaries");
    }
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
    // Independent five-DWORD native packet oracle. Sharp agrees on the
    // three-argument ABI but uses private NOP packets, which we do not adopt.
    for (const auto& [nid, opcode] : std::array<std::pair<const char*,uint32_t>,3>{{
        {"ZvwO9euwYzc",0x9f},{"-HOOCn0JY48",0x63},{"hvUfkUIQcOE",0x64}}}) {
        Loader::SymbolResolve indirectKey{nid,"Agc",1,"Agc",1,1,Loader::SymbolType::Func};
        const auto* indirectEntry=symbols.Find(indirectKey);
        Check(indirectEntry!=nullptr,"qualified indirect register writer resolves");
        using Indirect=KYTY_SYSV_ABI uint32_t* (*)(Libs::Graphics::Gen5::CommandBuffer*,
            const volatile Libs::Graphics::ShaderRegister*,uint32_t);
        packet.fill(0xaabbccdd); buffer.up=packet.data()+1; buffer.down=packet.data()+6;
        command=reinterpret_cast<Indirect>(indirectEntry->vaddr)(
            reinterpret_cast<Libs::Graphics::Gen5::CommandBuffer*>(&buffer),
            reinterpret_cast<const volatile Libs::Graphics::ShaderRegister*>(0x123456780ull),7);
        const std::array<uint32_t,5> expectedIndirect{0xc0030000u|(opcode<<8),0x23456780,1,0x80000000,7};
        Check(command==packet.data()+1 && buffer.up==packet.data()+6 &&
            std::equal(expectedIndirect.begin(),expectedIndirect.end(),packet.data()+1) &&
            packet.front()==0xaabbccdd && packet[6]==0xaabbccdd,
            "indirect register ABI preserves exact native packet, return pointer and bounds");
        indirectKey.module_version_minor=2;
        Check(!symbols.Find(indirectKey),"indirect register identity remains strict");
    }
    const auto* shaderEntry=symbols.Find(releaseKey);
    {
        Loader::SymbolResolve rangeKey{"n2fD4A+pb+g","Agc",1,"Agc",1,1,Loader::SymbolType::Func};
        const auto* rangeEntry=symbols.Find(rangeKey);
        Check(rangeEntry!=nullptr,"qualified SH range writer resolves");
        const auto range=reinterpret_cast<decltype(&Libs::Graphics::Gen5::AgcCbSetShRegisterRangeDirect)>(rangeEntry->vaddr);
        const uint32_t values[]{0x12345678,0x90abcdef,0x55aa55aa};
        packet.fill(0xaabbccdd); buffer.up=packet.data()+1; buffer.down=packet.data()+6;
        command=range(reinterpret_cast<Libs::Graphics::Gen5::CommandBuffer*>(&buffer),0x20c,values,3);
        const std::array<uint32_t,5> expectedRange{0xc0037600,0x20c,values[0],values[1],values[2]};
        Check(command==packet.data()+1 && buffer.up==packet.data()+6 &&
            std::equal(expectedRange.begin(),expectedRange.end(),packet.data()+1) &&
            packet[0]==0xaabbccdd && packet[6]==0xaabbccdd,"SH range writes bounded native packet");
        Check(range(reinterpret_cast<Libs::Graphics::Gen5::CommandBuffer*>(&buffer),0x20c,values,3)==nullptr &&
            buffer.up==packet.data()+6 && packet[6]==0xaabbccdd,"exhausted SH range preserves buffer");
        rangeKey.name="bxGoVxpdSPQ";
        const auto* sizeEntry=symbols.Find(rangeKey);
        Check(sizeEntry!=nullptr,"qualified SH range size resolves");
        using RangeSize=KYTY_SYSV_ABI uint32_t (*)(uint32_t);
        Check(reinterpret_cast<RangeSize>(sizeEntry->vaddr)(3)==20,"SH range size is bytes");
    }
    Check(shaderEntry!=nullptr,"reviewed shader creation has exact Agc identity");
    {
        Loader::SymbolResolve dispatchKey{"k3GhuSNmBLU","Agc",1,"Agc",1,1,Loader::SymbolType::Func};
        const auto* entry=symbols.Find(dispatchKey);
        Check(entry!=nullptr,"qualified dispatch resolves");
        const auto dispatch=reinterpret_cast<decltype(&Libs::Graphics::Gen5::AgcCbDispatch)>(entry->vaddr);
        for(uint32_t modifier : {0u,0x20u,0xa018u}) {
            packet.fill(0xaabbccdd); buffer.up=packet.data()+1; buffer.down=packet.data()+6;
            command=dispatch(reinterpret_cast<Libs::Graphics::Gen5::CommandBuffer*>(&buffer),13,7,3,modifier);
            const std::array<uint32_t,5> expectedDispatch{0xc0031500,13,7,3,modifier|0x41u};
            Check(command==packet.data()+1 && buffer.up==packet.data()+6 &&
                std::equal(expectedDispatch.begin(),expectedDispatch.end(),packet.data()+1) &&
                packet[0]==0xaabbccdd && packet[6]==0xaabbccdd,"dispatch retains workgroup and explicit thread-dimension semantics");
        }
        Check(dispatch(nullptr,1,1,1,0)==nullptr,"null dispatch buffer returns no packet");
        dispatchKey.name="Abendgtz+3o";
        const auto* size=symbols.Find(dispatchKey);
        Check(size && reinterpret_cast<decltype(&Libs::Graphics::Gen5::AgcCbDispatchGetSize)>(size->vaddr)()==20,
            "dispatch size uses zero-argument ABI and bytes");
    }
    const auto createShader=reinterpret_cast<decltype(&Libs::Graphics::Gen5::AgcCreateShader)>(shaderEntry->vaddr);
    {
        Loader::SymbolResolve markerKey{"+kSrjIVxKFE","Agc",1,"Agc",1,1,Loader::SymbolType::Func};
        const auto* pushEntry=symbols.Find(markerKey);
        markerKey.name="H7uZqCoNuWk";
        const auto* popEntry=symbols.Find(markerKey);
        Check(pushEntry && popEntry,"qualified marker pair resolves");
        const auto push=reinterpret_cast<decltype(&Libs::Graphics::Gen5::AgcDcbPushMarker)>(pushEntry->vaddr);
        const auto pop=reinterpret_cast<decltype(&Libs::Graphics::Gen5::AgcDcbPopMarker)>(popEntry->vaddr);
        packet.fill(0xaabbccdd); buffer.up=packet.data()+1; buffer.down=packet.data()+6;
        command=push(reinterpret_cast<Libs::Graphics::Gen5::CommandBuffer*>(&buffer),"abcd",0x11223344);
        Check(command==packet.data()+1 && packet[1]==0xc001102c && packet[2]==0x64636261 && packet[3]==0,
            "marker preserves text, NUL termination and DWORD padding");
        Check(pop(reinterpret_cast<Libs::Graphics::Gen5::CommandBuffer*>(&buffer))==packet.data()+4 &&
            packet[4]==0xc0001030 && packet[5]==0 && buffer.up==packet.data()+6 &&
            packet[0]==0xaabbccdd && packet[6]==0xaabbccdd,"marker pop is a bounded two-DWORD packet");
        Check(push(reinterpret_cast<Libs::Graphics::Gen5::CommandBuffer*>(&buffer),"x",0)==nullptr,
            "exhausted marker reports no packet");
    }
    using namespace Libs::Graphics;
    {
        Loader::SymbolResolve eventKey{"aJf+j5yntiU","Agc",1,"Agc",1,1,Loader::SymbolType::Func};
        const auto* entry=symbols.Find(eventKey);
        Check(entry!=nullptr,"qualified event-write resolves");
        const auto writeEvent=reinterpret_cast<decltype(&Gen5::AgcDcbEventWrite)>(entry->vaddr);
        for(uint8_t type : {uint8_t(7),uint8_t(15),uint8_t(16)}) {
            packet.fill(0xaabbccdd); buffer.up=packet.data()+1; buffer.down=packet.data()+3;
            Check(writeEvent(reinterpret_cast<Gen5::CommandBuffer*>(&buffer),type,nullptr)==packet.data()+1 &&
                packet[1]==0xc0004600 && packet[2]==(0x400u|type) && buffer.up==packet.data()+3 &&
                packet[0]==0xaabbccdd && packet[3]==0xaabbccdd,"partial-flush event carries index four");
        }
        packet.fill(0xaabbccdd); buffer.up=packet.data()+1; buffer.down=packet.data()+5;
        Check(writeEvent(reinterpret_cast<Gen5::CommandBuffer*>(&buffer),0x39,
            reinterpret_cast<const volatile void*>(0x123456780ull))==packet.data()+1 &&
            packet[1]==0xc0024600 && packet[2]==0x139 && packet[3]==0x23456780 && packet[4]==1 &&
            buffer.up==packet.data()+5 && packet[5]==0xaabbccdd,"addressed event retains address and index one");
        eventKey.name="C4l9fB17t8w";
        const auto* sizeEntry=symbols.Find(eventKey);
        Check(sizeEntry!=nullptr,"qualified event size resolves");
        const auto size=reinterpret_cast<decltype(&Gen5::AgcDcbEventWriteGetSize)>(sizeEntry->vaddr);
        Check(size(7)==8 && size(0x39)==16,"event size reflects addressed payload in bytes");
    }
    ShaderInit();
    {
        Loader::SymbolResolve resetKey{"TRO721eVt4g","Agc",1,"Agc",1,1,Loader::SymbolType::Func};
        const auto* entry=symbols.Find(resetKey);
        Check(entry!=nullptr,"qualified queue reset resolves");
        const auto reset=reinterpret_cast<decltype(&Gen5::AgcDcbResetQueue)>(entry->vaddr);
        packet.fill(0xaabbccdd); buffer.up=packet.data()+1; buffer.down=packet.data()+3;
        Check(reset(reinterpret_cast<Gen5::CommandBuffer*>(&buffer),0x3ff,0)==packet.data()+1 &&
            packet[1]==0xc0001200 && packet[2]==0 && buffer.up==packet.data()+3 &&
            packet[0]==0xaabbccdd && packet[3]==0xaabbccdd,"default queue reset emits bounded native CLEAR_STATE");
    }
    {
        Loader::SymbolResolve flipKey{"YUeqkyT7mEQ","Agc",1,"Agc",1,1,Loader::SymbolType::Func};
        const auto* entry=symbols.Find(flipKey);
        Check(entry!=nullptr,"qualified flip writer resolves");
        const auto flip=reinterpret_cast<decltype(&Gen5::AgcDcbSetFlip)>(entry->vaddr);
        packet.fill(0xaabbccdd); buffer.up=packet.data()+1; buffer.down=packet.data()+7;
        command=flip(reinterpret_cast<Gen5::CommandBuffer*>(&buffer),3,-1,1,0x123456789abcdef0ll);
        const std::array<uint32_t,6> expectedFlip{0xc004105c,3,0xffffffff,1,0x9abcdef0,0x12345678};
        Check(command==packet.data()+1 && buffer.up==packet.data()+7 &&
            std::equal(expectedFlip.begin(),expectedFlip.end(),packet.data()+1) &&
            packet[0]==0xaabbccdd && packet[7]==0xaabbccdd,"flip retains signed buffer index and 64-bit argument in consumer packet");
    }
    {
        Loader::SymbolResolve nopKey{"LtTouSCZjHM","Agc",1,"Agc",1,1,Loader::SymbolType::Func};
        const auto* entry=symbols.Find(nopKey);
        Check(entry!=nullptr,"qualified NOP writer resolves");
        const auto nop=reinterpret_cast<decltype(&Gen5::AgcCbNop)>(entry->vaddr);
        packet.fill(0xaabbccdd); buffer.up=packet.data()+1; buffer.down=packet.data()+6;
        Check(nop(reinterpret_cast<Gen5::CommandBuffer*>(&buffer),5)==packet.data()+1 &&
            packet[1]==0xc0031000 && packet[2]==0 && packet[3]==0 && packet[4]==0 && packet[5]==0 &&
            buffer.up==packet.data()+6 && packet[0]==0xaabbccdd && packet[6]==0xaabbccdd,"NOP reserves exact DWORD count and clears payload");
        std::vector<uint32_t> large(0x4004,0x12345678);
        BufferAbi largeBuffer{large.data(),large.data()+large.size(),large.data(),large.data()+large.size(),nullptr,nullptr,0};
        Check(nop(reinterpret_cast<Gen5::CommandBuffer*>(&largeBuffer),0x4002)==nullptr &&
            largeBuffer.up==large.data() && large[0]==0x12345678,"NOP rejects unrepresentable PM4 length without mutation");
        nopKey.name="t7PlZ9nt5Lc";
        const auto* size=symbols.Find(nopKey);
        Check(size && reinterpret_cast<decltype(&Gen5::AgcCbNopGetSize)>(size->vaddr)(5)==20,"NOP size is bytes");
    }
    {
        Loader::SymbolResolve writeKey{"i1jyy49AjXU","Agc",1,"Agc",1,1,Loader::SymbolType::Func};
        const auto* entry=symbols.Find(writeKey);
        Check(entry!=nullptr,"qualified write-data resolves");
        const auto write=reinterpret_cast<decltype(&Gen5::AgcDcbWriteData)>(entry->vaddr);
        const uint32_t data[]{0x13572468,0xabcdef01};
        packet.fill(0xaabbccdd); buffer.up=packet.data()+1; buffer.down=packet.data()+7;
        command=write(reinterpret_cast<Gen5::CommandBuffer*>(&buffer),5,2,0x123456780ull,data,2,1,1);
        const std::array<uint32_t,6> expectedWrite{0xc0043700,0x44110200,0x23456780,1,data[0],data[1]};
        Check(command==packet.data()+1 && buffer.up==packet.data()+7 &&
            std::equal(expectedWrite.begin(),expectedWrite.end(),packet.data()+1) &&
            packet[0]==0xaabbccdd && packet[7]==0xaabbccdd,"write-data encodes both stack flags and bounded payload");
        buffer.up=packet.data()+1;
        Check(write(reinterpret_cast<Gen5::CommandBuffer*>(&buffer),5,0,0x123456780ull,data,UINT32_MAX,0,0)==nullptr &&
            buffer.up==packet.data()+1,"write-data rejects overflow before allocation or payload access");
        writeKey.name="p9tI+yTvx68";
        const auto* size=symbols.Find(writeKey);
        Check(size && reinterpret_cast<decltype(&Gen5::AgcDcbWriteDataGetSize)>(size->vaddr)(2)==24,
            "write-data size reports bytes");
        writeKey.name="fPSCdQxgpSw";
        const auto* patchEntry=symbols.Find(writeKey);
        Check(patchEntry!=nullptr,"qualified write-data address patch resolves");
        const auto patch=reinterpret_cast<decltype(&Gen5::AgcWriteDataPatchSetAddressOrOffset)>(patchEntry->vaddr);
        Check(patch(packet.data()+1,0xabcdeff00ull)==0 && packet[3]==0xbcdeff00 && packet[4]==0xa &&
            packet[1]==expectedWrite[0] && packet[2]==expectedWrite[1] && packet[5]==data[0] && packet[6]==data[1],
            "write-data patch changes only destination words");
        packet[1]=0xc0001000;
        const auto before=packet;
        Check(patch(packet.data()+1,0x1234)!=0 && packet==before,"write-data patch rejects unrelated packet without mutation");
    }
    {
        Loader::SymbolResolve waitKey{"VmW0Tdpy420","Agc",1,"Agc",1,1,Loader::SymbolType::Func};
        const auto* entry=symbols.Find(waitKey);
        Check(entry!=nullptr,"qualified wait-reg-mem resolves");
        const auto wait=reinterpret_cast<decltype(&Gen5::AgcDcbWaitRegMem)>(entry->vaddr);
        std::array<uint32_t,18> stream; stream.fill(0xaabbccdd);
        BufferAbi waitBuffer{stream.data()+1,stream.data()+17,stream.data()+1,stream.data()+17,nullptr,nullptr,0};
        command=wait(reinterpret_cast<Gen5::CommandBuffer*>(&waitBuffer),1,3,0,0,
            reinterpret_cast<const volatile void*>(0x123456780ull),0x1122334455667788ull,UINT64_MAX,400);
        const std::array<uint32_t,16> expectedWait{0xc0027904,0x342,0xc8020001,0x23456780,
            0xc0079300,0x13,0x23456780,1,0x55667788,0x11223344,0xffffffff,0xffffffff,25,
            0xc0017904,0x342,0xc8000000};
        Check(command==stream.data()+1 && waitBuffer.up==stream.data()+17 &&
            std::equal(expectedWait.begin(),expectedWait.end(),stream.data()+1) &&
            stream.front()==0xaabbccdd && stream.back()==0xaabbccdd,"qualified wait retains 64-bit stack reference/mask and native stream");
        waitKey.name="43WJ08sSugE";
        const auto* sizeEntry=symbols.Find(waitKey);
        Check(sizeEntry!=nullptr,"qualified wait size resolves");
        const auto size=reinterpret_cast<decltype(&Gen5::AgcDcbWaitOnAddressGetSize)>(sizeEntry->vaddr);
        Check(size(0)==56 && size(1)==64 && size(2)==0,"wait sizes preserve supported widths");
        waitKey.name="3KDcnM3lrcU";
        const auto* addressEntry=symbols.Find(waitKey);
        waitKey.name="7nOoijNPvEU";
        const auto* referenceEntry=symbols.Find(waitKey);
        Check(addressEntry && referenceEntry,"qualified wait patch pair resolves");
        const auto addressPatch=reinterpret_cast<decltype(&Gen5::AgcWaitRegMemPatchAddress)>(addressEntry->vaddr);
        const auto referencePatch=reinterpret_cast<decltype(&Gen5::AgcWaitRegMemPatchReference)>(referenceEntry->vaddr);
        Check(addressPatch(command,reinterpret_cast<const volatile void*>(0x987654320ull))==0 &&
            command[2]==0xc8020009 && command[3]==0x87654320 && command[6]==0x87654320 && command[7]==9,
            "wait address patch updates metadata and native address together");
        Check(referencePatch(command,0xffeeddccbbaa9988ull)==0 && command[8]==0xbbaa9988 && command[9]==0x11223344,
            "native reference patch retains high word and changes the low word only");
        Check(stream.front()==0xaabbccdd && stream.back()==0xaabbccdd,"wait patch preserves bounds");
    }
    {
        Loader::SymbolResolve payloadKey{"V++UgBtQhn0","Agc",1,"Agc",1,1,Loader::SymbolType::Func};
        const auto* entry=symbols.Find(payloadKey);
        Check(entry!=nullptr,"qualified payload-address helper resolves");
        const auto payload=reinterpret_cast<decltype(&Gen5::AgcGetDataPacketPayloadAddress)>(entry->vaddr);
        uint32_t* output=nullptr;
        packet.fill(0xaabbccdd); packet[0]=0xc0031000;
        Check(payload(&output,packet.data(),0)==0 && output==packet.data()+1,"NOP data starts after header");
        Check(payload(&output,packet.data(),1)==0 && output==packet.data()+2,"register data starts after offset");
        packet[0]=0xffff1000;
        Check(payload(&output,packet.data(),0)==0 && output==nullptr,"sentinel packet has no payload");
        Check(packet[1]==0xaabbccdd,"payload query does not modify packet");
    }
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
