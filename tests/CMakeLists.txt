cmake_minimum_required(VERSION 3.13)
project(stridelang_tests LANGUAGES C CXX)

if (NOT TARGET gtest)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        release-1.12.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# Unit tests executable (runs in-process, deterministic, fast)
add_executable(stridelang_tests
    stridec_test.cpp
    cli_test.cpp
    cross_compile_test.cpp
)

target_link_libraries(stridelang_tests PRIVATE
    stridec_driver
    StrideJit
    StrideParser
    StrideUtils
    StrideCodegen
    gtest
    gtest_main
)

target_compile_definitions(stridelang_tests PRIVATE
    "-DSTRIDELANG_TESTS_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/data/\""
)

add_test(NAME stridelang_tests COMMAND stridelang_tests)

# End-to-end CMake integration test using CMake's built-in build-and-test runner
add_test(
    NAME StrideCMakeIntegrationTest
    COMMAND ${CMAKE_CTEST_COMMAND}
        --build-and-test "${CMAKE_CURRENT_SOURCE_DIR}/data/cmake_test_project"
                         "${CMAKE_CURRENT_BINARY_DIR}/cmake_test_project_build"
        --build-generator "${CMAKE_GENERATOR}"
        --build-config "$<CONFIG>"
        --build-options "-DSTRIDE_CMAKE_MODULE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../cmake"
                        "-DSTRIDE_COMPILER_BIN=$<TARGET_FILE:stridec>"
        --test-command test_dsp_firmware
)

set(STRIDE_CMAKE_MODULE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../cmake")
add_subdirectory("data/cmake_test_project/")
