#ifndef EMULATOR_INCLUDE_EMULATOR_LOADER_IMPORTAUDITRUNNER_H_
#define EMULATOR_INCLUDE_EMULATOR_LOADER_IMPORTAUDITRUNNER_H_

#include <filesystem>

namespace Loader::ImportAuditRunner {

int RunGameAudit(const std::filesystem::path& input,
                 const std::filesystem::path& json_output = {});
int RunLibraryAudit(const std::filesystem::path& root,
                    const std::filesystem::path& json_output = {});

} // namespace Loader::ImportAuditRunner

#endif /* EMULATOR_INCLUDE_EMULATOR_LOADER_IMPORTAUDITRUNNER_H_ */
