// Original synthetic conformance coverage for qualified libkernel exports.
// No SDK, firmware, or commercial bytes.
#include "libs/libs.h"

namespace OpenPsIdCompatibility {

int Run() {
    using namespace Loader;

    auto* linker = Common::Singleton<RuntimeLinker>::Instance();
    Libs::InitAll(linker->Symbols());

    Program program;
    program.rt = linker;
    program.dynamic_info = std::make_unique<DynamicInfo>();
    program.dynamic_info->import_libs.push_back({"O", 1, "OpenPsId"});
    program.dynamic_info->import_modules.push_back({"K", 1, 1, "libkernel"});

    SymbolRecord found{};
    linker->Resolve("DLORcroUqbc#O#K", SymbolType::Func, &program, &found, nullptr);
    Phase1::Check(found.vaddr != 0, "OpenPsId-qualified libkernel import resolves");

    using GetOpenPsId = KYTY_SYSV_ABI int (*)(void*);
    std::array<uint8_t, 16> id{};
    Phase1::Check(reinterpret_cast<GetOpenPsId>(found.vaddr)(id.data()) == 0,
                  "resolved OpenPsId export returns success");

    const std::array<uint8_t, 16> expected = {
        0x4b, 0x79, 0x74, 0x79, 0x4f, 0x70, 0x65, 0x6e,
        0x50, 0x73, 0x49, 0x64, 0x00, 0x00, 0x00, 0x01,
    };
    Phase1::Check(id == expected, "qualified OpenPsId route reaches the canonical implementation");

    SymbolResolve request{"DLORcroUqbc", "OpenPsId", 1, "libkernel", 1, 1, SymbolType::Func};
    auto wrong = request;
    wrong.name = "PhaseOpenPsIdWrongNid";
    Phase1::Check(linker->Symbols()->FindExactOrCompatible(SymbolDatabase::GenerateName(wrong)) == nullptr,
                  "OpenPsId compatibility is NID-bounded");
    wrong = request;
    wrong.library_version = 2;
    Phase1::Check(linker->Symbols()->FindExactOrCompatible(SymbolDatabase::GenerateName(wrong)) == nullptr,
                  "OpenPsId compatibility is library-version-bounded");

    Program coredump_program;
    coredump_program.rt = linker;
    coredump_program.dynamic_info = std::make_unique<DynamicInfo>();
    coredump_program.dynamic_info->import_libs.push_back({"C", 1, "Coredump"});
    coredump_program.dynamic_info->import_modules.push_back({"K", 1, 1, "libkernel"});

    SymbolRecord coredump_found{};
    linker->Resolve("8zLSfEfW5AU#C#K", SymbolType::Func, &coredump_program, &coredump_found, nullptr);
    Phase1::Check(coredump_found.vaddr != 0,
                  "Coredump-qualified libkernel import resolves");

    struct AliasCase {
        const char* requested;
        const char* canonical;
    };
    static constexpr AliasCase audited_aliases[] = {
        {"AhGvpITrf4M[AgcDriver_v1][AgcDriver_v1.1][Func]",
         "AhGvpITrf4M[Graphics5Driver_v1][Graphics5Driver_v1.1][Func]"},
        {"D-CzAxQL0XI[UserServicePlatformPrivacyWs1_v1][UserService_v1.1][Func]",
         "D-CzAxQL0XI[UserService_v1][UserService_v1.1][Func]"},
        {"NhpspxdjEKU[libkernel_v1][libkernel_v1.1][Func]",
         "NhpspxdjEKU[Posix_v1][libkernel_v1.1][Func]"},
        {"TDfQqO-gMbY[Ssl_v1][Ssl_v2.1][Func]",
         "TDfQqO-gMbY[Ssl_v1][Ssl_v1.1][Func]"},
        {"U8IfNl6-Css[VoiceQoS_v1][VoiceQoS_v1.1][Func]",
         "U8IfNl6-Css[VoiceQoS_v1][VoiceQoS_v0.0][Func]"},
        {"W5z4eZrjEas[AgcDriver_v1][AgcDriver_v1.1][Func]",
         "W5z4eZrjEas[Graphics5_v1][Graphics5_v1.1][Func]"},
        {"X-Nm5KLREeg[AgcDriver_v1][AgcDriver_v1.1][Func]",
         "X-Nm5KLREeg[Graphics5_v1][Graphics5_v1.1][Func]"},
        {"al3JzFI9MQ0[LibcInternalExt_v1][LibcInternal_v1.1][Func]",
         "al3JzFI9MQ0[LibcInternal_v1][LibcInternal_v1.1][Func]"},
        {"cfwBSQyr5Ys[libkernel_v1][libkernel_v1.1][Func]",
         "cfwBSQyr5Ys[Posix_v1][libkernel_v1.1][Func]"},
        {"gSRnr79F8tQ[AgcDriver_v1][AgcDriver_v1.1][Func]",
         "gSRnr79F8tQ[Graphics5Driver_v1][Graphics5Driver_v1.1][Func]"},
        {"yS8U2TGCe1A[libkernel_v1][libkernel_v1.1][Func]",
         "yS8U2TGCe1A[Posix_v1][libkernel_v1.1][Func]"},
    };
    for (const auto& alias: audited_aliases) {
        const auto* resolved = linker->Symbols()->FindExactOrCompatible(alias.requested);
        Phase1::Check(resolved != nullptr && resolved->vaddr != 0 && resolved->name == alias.canonical,
                      "audited compatibility alias resolves only to its canonical export");
    }

    Phase1::Check(
        linker->Symbols()->FindExactOrCompatible(
            "PhaseUnrelatedNid[AgcDriver_v1][AgcDriver_v1.1][Func]") == nullptr,
        "audited compatibility aliases do not widen whole-library resolution");

    for (const char* symbol: {
             "5tRaBjtdTzY[VideoOut_v1][VideoOut_v1.1][Func]",
             "T4ucGB8CsnM[VideoOut_v1][VideoOut_v1.1][Func]",
         }) {
        const auto* resolved = linker->Symbols()->FindExact(symbol);
        Phase1::Check(resolved != nullptr && resolved->vaddr != 0,
                      "VideoOut VRR fixed-rate export resolves exactly");
    }

    std::puts("PHASE1_PASS qualified compatibility routes resolve canonical implementations");
    return 0;
}

} // namespace OpenPsIdCompatibility
