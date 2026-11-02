if(NOT DEFINED HARNESS OR NOT DEFINED REPORT_DIR)
    message(FATAL_ERROR "HARNESS and REPORT_DIR are required")
endif()

set(report_path "${REPORT_DIR}/ltr_harness_selftest.txt")
# A prior report must not stand in for a failed or timed-out run.
file(REMOVE "${report_path}")
execute_process(
    COMMAND "${HARNESS}" --self-test
    WORKING_DIRECTORY "${REPORT_DIR}"
    TIMEOUT 90
    RESULT_VARIABLE result
)

if(NOT result STREQUAL "0")
    message(FATAL_ERROR "temporal harness self-test failed with exit code ${result}")
endif()
if(NOT EXISTS "${report_path}")
    message(FATAL_ERROR "self-test did not produce a fresh ${report_path}")
endif()
file(READ "${report_path}" report)
string(FIND "${report}" "RESULT PASS" pass_pos)
if(pass_pos EQUAL -1)
    message(FATAL_ERROR "self-test report did not contain RESULT PASS: ${report_path}")
endif()
message("${report}")
