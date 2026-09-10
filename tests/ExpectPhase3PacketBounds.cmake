foreach(variant eop release wait)
    execute_process(COMMAND "${PROBE}" "--phase3-short-${variant}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 30)
    if(NOT result STREQUAL "321" OR NOT "${output}${error}" MATCHES "PM4 truncated or invalid packet")
        message(FATAL_ERROR "${variant}: expected bounded rejection, got ${result}\n${output}\n${error}")
    endif()
endforeach()
message("PHASE3_PASS guard-page truncated EOP/RELEASE_MEM/WAIT_REG_MEM64 reject before payload access")
