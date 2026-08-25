if(NOT DEFINED ENVDIFF_EXECUTABLE OR NOT DEFINED TEST_DIRECTORY)
    message(FATAL_ERROR "ENVDIFF_EXECUTABLE and TEST_DIRECTORY are required")
endif()

file(REMOVE_RECURSE "${TEST_DIRECTORY}")
file(MAKE_DIRECTORY "${TEST_DIRECTORY}")

set(example "${TEST_DIRECTORY}/example.env")
set(current "${TEST_DIRECTORY}/current.env")
set(expected "${TEST_DIRECTORY}/expected.env")

file(WRITE "${example}"
    "# Service endpoint\n"
    "SERVICE_URL=https://example.invalid\n"
    "\n"
    "# Request timeout\n"
    "SERVICE_TIMEOUT=30\n"
)
file(WRITE "${current}"
    "SERVICE_URL=https://private.invalid\n"
    "LOCAL_SECRET=keep-this-value\n"
)
file(WRITE "${expected}"
    "# Service endpoint\n"
    "SERVICE_URL=https://private.invalid\n"
    "LOCAL_SECRET=keep-this-value\n"
    "# Request timeout\n"
    "SERVICE_TIMEOUT=30\n"
)

execute_process(
    COMMAND "${ENVDIFF_EXECUTABLE}" --version
    RESULT_VARIABLE version_result
    OUTPUT_VARIABLE version_output
    ERROR_VARIABLE version_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT version_result EQUAL 0 OR NOT version_output MATCHES "^envdiff [0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "Version command failed: ${version_error}${version_output}")
endif()

execute_process(
    COMMAND "${ENVDIFF_EXECUTABLE}" --check "${example}" "${current}"
    RESULT_VARIABLE check_before_result
    OUTPUT_VARIABLE check_before_output
    ERROR_VARIABLE check_before_error
)
if(NOT check_before_result EQUAL 1)
    message(FATAL_ERROR "Initial check returned ${check_before_result}: ${check_before_error}${check_before_output}")
endif()

execute_process(
    COMMAND "${ENVDIFF_EXECUTABLE}" "${example}" "${current}"
    RESULT_VARIABLE merge_result
    OUTPUT_VARIABLE merge_output
    ERROR_VARIABLE merge_error
)
if(NOT merge_result EQUAL 0)
    message(FATAL_ERROR "Merge returned ${merge_result}: ${merge_error}${merge_output}")
endif()

file(READ "${current}" actual_content)
file(READ "${expected}" expected_content)
if(NOT actual_content STREQUAL expected_content)
    message(FATAL_ERROR "Merged file does not match the expected content")
endif()

execute_process(
    COMMAND "${ENVDIFF_EXECUTABLE}" --check "${example}" "${current}"
    RESULT_VARIABLE check_after_result
    OUTPUT_VARIABLE check_after_output
    ERROR_VARIABLE check_after_error
)
if(NOT check_after_result EQUAL 0)
    message(FATAL_ERROR "Final check returned ${check_after_result}: ${check_after_error}${check_after_output}")
endif()

file(WRITE "${example}" "DUPLICATE=one\nDUPLICATE=two\n")
execute_process(
    COMMAND "${ENVDIFF_EXECUTABLE}" "${example}" "${current}"
    RESULT_VARIABLE duplicate_result
    OUTPUT_VARIABLE duplicate_output
    ERROR_VARIABLE duplicate_error
)
if(duplicate_result EQUAL 0)
    message(FATAL_ERROR "Duplicate example keys were accepted: ${duplicate_error}${duplicate_output}")
endif()

file(REMOVE_RECURSE "${TEST_DIRECTORY}")
