// Original synthetic conformance inputs; no SDK, firmware, or commercial artifacts.
#include "loader/importAudit.h"
#include "libs/libs.h"

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

    std::puts("IMPORT_AUDIT_PASS resolution and classification");
    return 0;
}

} // namespace PhaseImportAudit
