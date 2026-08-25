if(NOT DEFINED ENVDIFF_EXECUTABLE
    OR NOT DEFINED ENVDIFF_EXPECTED_VERSION
    OR NOT DEFINED TEST_DIRECTORY)
    message(FATAL_ERROR
        "ENVDIFF_EXECUTABLE, ENVDIFF_EXPECTED_VERSION, and TEST_DIRECTORY are required"
    )
endif()

file(REMOVE_RECURSE "${TEST_DIRECTORY}")
file(MAKE_DIRECTORY "${TEST_DIRECTORY}")

set(example "${TEST_DIRECTORY}/example.env")
set(current "${TEST_DIRECTORY}/current.env")
set(example_named_current "${TEST_DIRECTORY}/current.Example.env")
set(expected "${TEST_DIRECTORY}/expected.env")
set(output "${TEST_DIRECTORY}/output.env")

file(WRITE "${example}"
    "# Service endpoint\n"
    "SERVICE_URL=https://example.invalid\n"
    "LOCAL_SECRET=example-placeholder\n"
    "\n"
    "# Request timeout\n"
    "SERVICE_TIMEOUT=30\n"
)
file(WRITE "${current}"
    "SERVICE_URL=https://private.invalid\n"
    "LOCAL_SECRET=keep-this-value\n"
)
file(WRITE "${example_named_current}"
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
    COMMAND "${ENVDIFF_EXECUTABLE}" "${example}" "${example_named_current}"
    RESULT_VARIABLE example_name_guard_result
    OUTPUT_VARIABLE example_name_guard_output
    ERROR_VARIABLE example_name_guard_error
)
string(FIND "${example_name_guard_error}" "--force" safety_hint_index)
if(NOT example_name_guard_result EQUAL 2
    OR NOT example_name_guard_output STREQUAL ""
    OR safety_hint_index EQUAL -1)
    message(FATAL_ERROR "Current example-name guard failed")
endif()

execute_process(
    COMMAND "${ENVDIFF_EXECUTABLE}" --force
        "${example}" "${example_named_current}"
    RESULT_VARIABLE example_name_override_result
    OUTPUT_VARIABLE example_name_override_output
    ERROR_VARIABLE example_name_override_error
)
file(READ "${expected}" expected_override_content)
if(NOT example_name_override_result EQUAL 0
    OR NOT example_name_override_output STREQUAL expected_override_content
    OR NOT example_name_override_error STREQUAL "")
    message(FATAL_ERROR "Current example-name override failed")
endif()

execute_process(
    COMMAND "${ENVDIFF_EXECUTABLE}" --version
    RESULT_VARIABLE version_result
    OUTPUT_VARIABLE version_output
    ERROR_VARIABLE version_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT version_result EQUAL 0
    OR NOT version_output STREQUAL "envdiff ${ENVDIFF_EXPECTED_VERSION}")
    message(FATAL_ERROR "Version command failed: ${version_error}${version_output}")
endif()

execute_process(
    COMMAND "${ENVDIFF_EXECUTABLE}" "${example}" "${current}" -c
    RESULT_VARIABLE check_before_result
    OUTPUT_VARIABLE check_before_output
    ERROR_VARIABLE check_before_error
)
if(NOT check_before_result EQUAL 1)
    message(FATAL_ERROR "Initial check returned ${check_before_result}: ${check_before_error}${check_before_output}")
endif()
string(CONCAT expected_check_before
    "missing keys: 1, only in current: 0, new comments: 2\n"
    "\n"
    "Missing from ${current}:\n"
    "\n"
    "+ # Request timeout\n"
    "+ SERVICE_TIMEOUT=30\n"
    "\n"
    "Comments missing from ${current}:\n"
    "\n"
    "+ # Service endpoint\n"
    "  SERVICE_URL (existing value is not compared)\n"
    "\n"
)
string(REPLACE "\r\n" "\n" normalized_check_before "${check_before_output}")
if(NOT normalized_check_before STREQUAL expected_check_before OR NOT check_before_error STREQUAL "")
    message(FATAL_ERROR "Initial structural diff does not match the expected output")
endif()

execute_process(
    COMMAND "${ENVDIFF_EXECUTABLE}" "${example}" "${current}"
    RESULT_VARIABLE merge_result
    OUTPUT_VARIABLE filtered_content
    ERROR_VARIABLE merge_error
)
if(NOT merge_result EQUAL 0)
    message(FATAL_ERROR "Filter returned ${merge_result}: ${merge_error}${filtered_content}")
endif()

file(READ "${expected}" expected_content)
if(NOT filtered_content STREQUAL expected_content)
    message(FATAL_ERROR "Standard output does not match the expected content")
endif()

file(READ "${current}" unchanged_content)
string(CONCAT original_content
    "SERVICE_URL=https://private.invalid\n"
    "LOCAL_SECRET=keep-this-value\n"
)
if(NOT unchanged_content STREQUAL original_content)
    message(FATAL_ERROR "Default filter mode modified the current file")
endif()

execute_process(
    COMMAND "${ENVDIFF_EXECUTABLE}" --output "${output}" "${example}" "${current}"
    RESULT_VARIABLE output_result
    OUTPUT_VARIABLE output_stdout
    ERROR_VARIABLE output_error
)
if(NOT output_result EQUAL 0 OR NOT output_stdout STREQUAL "")
    message(FATAL_ERROR "Output option failed: ${output_error}${output_stdout}")
endif()
file(READ "${output}" output_content)
if(NOT output_content STREQUAL expected_content)
    message(FATAL_ERROR "Output file does not match the expected content")
endif()

execute_process(
    COMMAND "${ENVDIFF_EXECUTABLE}" "${example}" "${current}" -i
    RESULT_VARIABLE inplace_result
    OUTPUT_VARIABLE inplace_stdout
    ERROR_VARIABLE inplace_error
)
if(NOT inplace_result EQUAL 0 OR NOT inplace_stdout STREQUAL "")
    message(FATAL_ERROR "In-place output failed: ${inplace_error}${inplace_stdout}")
endif()
file(READ "${current}" actual_content)
if(NOT actual_content STREQUAL expected_content)
    message(FATAL_ERROR "In-place output does not match the expected content")
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
if(NOT check_after_output STREQUAL "" OR NOT check_after_error STREQUAL "")
    message(FATAL_ERROR "Final check was not quiet for matching files")
endif()

file(APPEND "${current}" "# Legacy option\nLEGACY_TOKEN=remove-this-secret\n")
execute_process(
    COMMAND "${ENVDIFF_EXECUTABLE}" --check "${example}" "${current}"
    RESULT_VARIABLE only_current_result
    OUTPUT_VARIABLE only_current_output
    ERROR_VARIABLE only_current_error
)
if(NOT only_current_result EQUAL 1)
    message(FATAL_ERROR "Current-only check returned ${only_current_result}")
endif()
string(CONCAT expected_only_current
    "missing keys: 0, only in current: 1, new comments: 0\n"
    "\n"
    "Only in ${current} (review before removing):\n"
    "\n"
    "- # Legacy option\n"
    "- LEGACY_TOKEN=<value hidden>\n"
    "\n"
)
string(REPLACE "\r\n" "\n" normalized_only_current "${only_current_output}")
if(NOT normalized_only_current STREQUAL expected_only_current OR NOT only_current_error STREQUAL "")
    message(FATAL_ERROR "Current-only structural diff does not match the expected output")
endif()

execute_process(
    COMMAND "${ENVDIFF_EXECUTABLE}" --check -o "${output}" "${example}" "${current}"
    RESULT_VARIABLE invalid_options_result
    OUTPUT_VARIABLE invalid_options_output
    ERROR_VARIABLE invalid_options_error
)
if(NOT invalid_options_result EQUAL 2)
    message(FATAL_ERROR "Invalid option combination returned ${invalid_options_result}")
endif()

execute_process(
    COMMAND "${ENVDIFF_EXECUTABLE}" "${example}" "${current}" --check -i
    RESULT_VARIABLE check_in_place_result
    OUTPUT_VARIABLE check_in_place_output
    ERROR_VARIABLE check_in_place_error
)
if(NOT check_in_place_result EQUAL 2)
    message(FATAL_ERROR "Check accepted in-place output")
endif()

execute_process(
    COMMAND "${ENVDIFF_EXECUTABLE}" "${example}" "${current}"
        -i -o "${output}"
    RESULT_VARIABLE conflicting_outputs_result
    OUTPUT_VARIABLE conflicting_outputs_output
    ERROR_VARIABLE conflicting_outputs_error
)
if(NOT conflicting_outputs_result EQUAL 2)
    message(FATAL_ERROR "In-place mode accepted an explicit output file")
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
