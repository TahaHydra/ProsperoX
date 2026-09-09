execute_process(COMMAND "${PROBE}" --unresolved-import
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 45)
if(NOT result STREQUAL "86" OR NOT error MATCHES "UNRESOLVED_STRONG_IMPORT symbol=Phase0OriginalMissingImport program= relocation=0 patch=0x")
    message(FATAL_ERROR "Unexpected import outcome: ${result}\n${output}\n${error}")
endif()
message("PHASE0 {\"probe\":\"unresolved_import\",\"seed\":5265456,\"expected\":\"explicit_unsupported_failure\",\"actual\":\"explicit_unsupported_failure\",\"exit_code\":86}")
