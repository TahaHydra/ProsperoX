execute_process(COMMAND "${PROBE}" --phase1-legacy-plt
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 45)
if(NOT result STREQUAL "86" OR NOT error MATCHES "UNRESOLVED_STRONG_IMPORT legacy_plt index=7 symbol=<unknown function> program=<unregistered>")
    message(FATAL_ERROR "Unexpected legacy PLT outcome: ${result}\n${output}\n${error}")
endif()
message("PHASE1_PASS legacy PLT cannot silently return success or dereference an unregistered program")
