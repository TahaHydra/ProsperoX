// Original synthetic conformance inputs; no SDK, firmware, or commercial artifacts.
#include "loader/importAudit.h"
#include "loader/elf.h"
#include "libs/libs.h"

#include <array>
#include <chrono>
#include <fstream>

namespace PhaseImportAudit {

static Loader::SymbolResolve MakeRequest(const char* nid, const char* library,
                                         const char* module,
                                         Loader::SymbolType type = Loader::SymbolType::Func) {
    return {nid, library, 1, module, 1, 1, type};
}

int Run() {
    using namespace Loader;
    using namespace Loader::ImportAudit;

    SymbolDatabase local_symbols;
    local_symbols.Add(MakeRequest("AuditCandidateNid", "CanonicalLib", "CanonicalMod"),
                      0x2000, "AuditCandidate");

    const auto all = local_symbols.FindAllByNid("AuditCandidateNid", SymbolType::Func);
    Phase1::Check(all.size() == 1 && all[0].vaddr == 0x2000,
                  "audit enumerates all same-NID same-type HLE candidates");
    Phase1::Check(local_symbols.FindAllByNid("AuditCandidateNid", SymbolType::Object).empty(),
                  "audit candidate enumeration preserves symbol type");

    auto* rt = Common::Singleton<RuntimeLinker>::Instance();
    Libs::InitAll(rt->Symbols());

    Program requester;
    requester.rt = rt;
    requester.dynamic_info = std::make_unique<DynamicInfo>();
    requester.dynamic_info->import_libs.push_back({"L", 1, "OpenPsId"});
    requester.dynamic_info->import_modules.push_back({"M", 1, 1, "libkernel"});

    SymbolResolve decoded{};
    Phase1::Check(DecodeImportIdentity(requester, "DLORcroUqbc#L#M",
                                       SymbolType::Func, &decoded) &&
                      SymbolDatabase::GenerateName(decoded) ==
                          "DLORcroUqbc[OpenPsId_v1][libkernel_v1.1][Func]",
                  "audit shares runtime qualified-import decoding");

    const auto compatible = rt->InspectImport(requester, decoded);
    Phase1::Check(compatible.source == ImportResolutionSource::CompatibleHle &&
                      compatible.record.vaddr != 0,
                  "audit identifies bounded compatible HLE resolution");

    const auto exact_request = MakeRequest("AuditExactNid", "AuditExactLib", "AuditExactMod");
    rt->Symbols()->Add(exact_request, 0x1234, "AuditExact");
    const auto exact = ClassifyImport(*rt, requester, exact_request, SymbolType::Func, false,
                                      "synthetic-eboot.bin");
    Phase1::Check(exact.status == Status::ExactHle && exact.resolved_name ==
                      SymbolDatabase::GenerateName(exact_request),
                  "audit classifies exact HLE resolution");

    const auto canonical = MakeRequest("AuditAliasNid", "CanonicalLib", "CanonicalMod");
    rt->Symbols()->Add(canonical, 0x2345, "AuditAliasCanonical");
    const auto alias_request = MakeRequest("AuditAliasNid", "RequestedLib", "RequestedMod");
    const auto alias = ClassifyImport(*rt, requester, alias_request, SymbolType::Func, false,
                                      "synthetic-eboot.bin");
    Phase1::Check(alias.status == Status::AliasCandidate && alias.candidates.size() == 1 &&
                      alias.resolved_name.empty(),
                  "same-NID different-qualification stays review-only alias candidate");

    const auto wrong_type = ClassifyImport(*rt, requester,
        MakeRequest("AuditAliasNid", "RequestedLib", "RequestedMod", SymbolType::Object),
        SymbolType::Object, false, "synthetic-eboot.bin");
    Phase1::Check(wrong_type.status == Status::MissingHle && wrong_type.candidates.empty(),
                  "same NID under another symbol type is not an alias candidate");

    const auto missing_request = MakeRequest("AuditMissingNid", "AuditLib", "AuditMod");
    const auto missing = ClassifyImport(*rt, requester, missing_request, SymbolType::Func, false,
                                        "synthetic-eboot.bin");
    Phase1::Check(missing.status == Status::MissingHle,
                  "unknown strong import is missing HLE");

    const auto weak = ClassifyImport(*rt, requester, missing_request, SymbolType::Func, true,
                                     "synthetic-eboot.bin");
    Phase1::Check(weak.status == Status::WeakUnresolved,
                  "unknown weak import remains weak unresolved");

    const auto key = ImportKey(missing);
    Phase1::Check(key == "AuditMissingNid|Func|AuditLib@1|AuditMod@1.1",
                  "audit import key is deterministic and qualification-complete");

    // Exercise bounded relocation extraction directly from synthetic DynamicInfo.
    Program synthetic;
    synthetic.rt = rt;
    synthetic.file_name = std::filesystem::path("synthetic") / "eboot.bin";
    synthetic.dynamic_info = std::make_unique<DynamicInfo>();
    std::array<char, 32> names{};
    const char encoded[] = "AuditStaticNid#L#M";
    std::memcpy(names.data() + 1, encoded, sizeof(encoded));
    Elf64_Sym symbol{};
    symbol.st_name = 1;
    symbol.st_info = static_cast<unsigned char>((STB_GLOBAL << 4) | STT_FUNC);
    symbol.st_shndx = 0;
    std::array<Elf64_Rela, 2> relas{};
    for (size_t i = 0; i < relas.size(); ++i) {
        relas[i].r_offset = 0x1000 + i * 8;
        relas[i].r_info = static_cast<uint64_t>(R_X86_64_JUMP_SLOT);
    }
    auto& dyn = *synthetic.dynamic_info;
    dyn.str_table = names.data();
    dyn.str_table_size = names.size();
    dyn.symbol_table = &symbol;
    dyn.symbol_table_total_size = sizeof(symbol);
    dyn.symbol_table_entry_size = sizeof(symbol);
    dyn.jmprela_table = relas.data();
    dyn.jmprela_table_size = sizeof(relas);
    dyn.rela_table_entry_size = sizeof(Elf64_Rela);
    dyn.import_libs.push_back({"L", 1, "AuditLib"});
    dyn.import_modules.push_back({"M", 1, 1, "AuditMod"});

    const auto extracted = AuditProgram(*rt, synthetic, "synthetic");
    Phase1::Check(extracted.parsed && extracted.imports.size() == 1,
                  "audit extracts and deduplicates synthetic imported relocations");
    Phase1::Check(extracted.imports[0].qualified_name ==
                      "AuditStaticNid[AuditLib_v1][AuditMod_v1.1][Func]" &&
                      extracted.imports[0].references == 2 &&
                      extracted.imports[0].status == Status::MissingHle,
                  "audit preserves qualified identity reference count and missing status");

    relas[0].r_info = (uint64_t(1) << 32) | R_X86_64_JUMP_SLOT;
    const auto malformed = AuditProgram(*rt, synthetic, "synthetic");
    Phase1::Check(!malformed.parsed && !malformed.diagnostic.empty(),
                  "audit rejects out-of-range relocation symbol indexes with a diagnostic");
    relas[0].r_info = R_X86_64_JUMP_SLOT;

    // Global aggregation counts games once while preserving every static reference.
    auto g1_record = missing;
    g1_record.references = 2;
    auto g2_record = missing;
    g2_record.references = 3;
    GameResult g1;
    g1.root = "synthetic/A";
    g1.title_id = "AUDIT00001";
    g1.binaries.push_back({"eboot.bin", true, "", {g1_record}});
    GameResult g2;
    g2.root = "synthetic/B";
    g2.title_id = "AUDIT00002";
    g2.binaries.push_back({"eboot.bin", true, "", {g2_record}});
    auto aggregated = Aggregate({g2, g1});
    Phase1::Check(aggregated.global_imports.size() == 1 &&
                      aggregated.global_imports[0].games.size() == 2,
                  "global audit counts affected games rather than relocations");
    Phase1::Check(aggregated.global_imports[0].import.references == 5,
                  "global audit preserves total static reference count");

    const auto json_a = ToJson(aggregated).dump();
    const auto json_b = ToJson(Aggregate({g1, g2})).dump();
    Phase1::Check(json_a == json_b,
                  "audit JSON ordering is deterministic regardless of discovery order");

    // Recursive discovery is based only on directories that actually contain eboot.bin.
    const auto temp = std::filesystem::temp_directory_path() /
        ("prosperox-audit-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temp / "A");
    std::filesystem::create_directories(temp / "B" / "deeper");
    std::filesystem::create_directories(temp / "not-a-game");
    { std::ofstream(temp / "A" / "eboot.bin", std::ios::binary); }
    { std::ofstream(temp / "B" / "deeper" / "eboot.bin", std::ios::binary); }
    { std::ofstream(temp / "not-a-game" / "random.bin", std::ios::binary); }
    const auto roots = DiscoverGameRoots(temp);
    Phase1::Check(roots.size() == 2 && roots[0].filename() == "A" &&
                      roots[1].filename() == "deeper",
                  "library audit discovers every and only synthetic game root");
    std::filesystem::remove_all(temp);

    std::puts("IMPORT_AUDIT_PASS resolution classification extraction aggregation JSON discovery");
    return 0;
}

} // namespace PhaseImportAudit
