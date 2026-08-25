cmake_minimum_required(VERSION 3.20)

project(envdiff VERSION 0.3.0 LANGUAGES C)

add_executable(envdiff envdiff.c)
set_target_properties(envdiff PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
target_compile_definitions(envdiff PRIVATE ENVDIFF_VERSION=\"${PROJECT_VERSION}\")

if(MSVC)
    target_compile_definitions(envdiff PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(envdiff PRIVATE /W4 /O1)
else()
    target_compile_options(envdiff PRIVATE
        -Os
        -Wall
        -Wextra
        -Wpedantic
        -ffunction-sections
        -fdata-sections
        -fstack-protector-strong
    )

    if(APPLE)
        target_link_options(envdiff PRIVATE -Wl,-dead_strip -Wl,-x)
    elseif(UNIX)
        target_compile_definitions(envdiff PRIVATE _FORTIFY_SOURCE=2)
        target_link_options(envdiff PRIVATE -Wl,--gc-sections -Wl,-s)
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS envdiff RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

include(CTest)
if(BUILD_TESTING)
    add_test(
        NAME integration
        COMMAND "${CMAKE_COMMAND}"
            "-DENVDIFF_EXECUTABLE=$<TARGET_FILE:envdiff>"
            "-DENVDIFF_EXPECTED_VERSION=${PROJECT_VERSION}"
            "-DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/integration-test"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.cmake"
    )
endif()
