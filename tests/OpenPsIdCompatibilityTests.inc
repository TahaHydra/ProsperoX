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

    std::puts("PHASE1_PASS OpenPsId-qualified libkernel export resolves to canonical implementation");
    return 0;
}

} // namespace OpenPsIdCompatibility
