execute_process(COMMAND "${PROBE}" --phase1-strong-data
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 45)
if(NOT result STREQUAL "86" OR NOT error MATCHES "UNRESOLVED_STRONG_DATA symbol=Phase1OriginalMissingObject program= relocation=0")
    message(FATAL_ERROR "Unexpected data import outcome: ${result}\n${output}\n${error}")
endif()
message("PHASE1_PASS unresolved strong data rejected with identity and exit 86")
