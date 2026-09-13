if(NOT DEFINED HARNESS OR NOT DEFINED REPORT_DIR)
    message(FATAL_ERROR "HARNESS and REPORT_DIR are required")
endif()

execute_process(
    COMMAND "${HARNESS}" --self-test
    WORKING_DIRECTORY "${REPORT_DIR}"
    RESULT_VARIABLE result
)

set(report_path "${REPORT_DIR}/ltr_harness_selftest.txt")
if(EXISTS "${report_path}")
    file(READ "${report_path}" report)
    message("${report}")
else()
    message("self-test did not produce ${report_path}")
endif()

if(NOT result EQUAL 0)
    message(FATAL_ERROR "temporal harness self-test failed with exit code ${result}")
endif()
