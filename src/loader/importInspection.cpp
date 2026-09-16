#include "loader/runtimeLinker.h"

#include <algorithm>

namespace Loader {

ImportResolutionInspection RuntimeLinker::InspectImport(const Program& /*requester*/,
                                                        const SymbolResolve& request) const {
	// Static audit runs against a quiescent linker after all candidate modules are
	// loaded. This API is intentionally non-mutating and never performs relocation.
	const auto qualified = SymbolDatabase::GenerateName(request);
	if (m_symbols != nullptr) {
		if (const auto* exact = m_symbols->FindExact(qualified); exact != nullptr) {
			return {ImportResolutionSource::ExactHle, *exact};
		}
		if (const auto* compatible = m_symbols->FindExactOrCompatible(qualified);
		    compatible != nullptr) {
			return {ImportResolutionSource::CompatibleHle, *compatible};
		}
	}

	for (const auto* program: m_programs) {
		if (program == nullptr || program->dynamic_info == nullptr ||
		    program->export_symbols == nullptr) {
			continue;
		}

		const auto& info = *program->dynamic_info;
		const bool module_match = std::any_of(
		    info.export_modules.begin(), info.export_modules.end(), [&](const ModuleId& module) {
			    return module.name == request.module &&
			           module.version_major == request.module_version_major &&
			           module.version_minor == request.module_version_minor;
		    });
		if (!module_match) continue;

		const bool library_match = std::any_of(
		    info.export_libs.begin(), info.export_libs.end(), [&](const LibraryId& library) {
			    return library.name == request.library && library.version == request.library_version;
		    });
		if (!library_match) continue;

		if (const auto* record = program->export_symbols->FindExact(qualified); record != nullptr) {
			return {ImportResolutionSource::GuestModule, *record};
		}
	}

	return {};
}

} // namespace Loader
