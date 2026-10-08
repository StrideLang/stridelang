cmake_minimum_required(VERSION 3.13)
project(stride_cmake_test_project LANGUAGES C)

# Add custom CMake module directory
list(APPEND CMAKE_MODULE_PATH "${STRIDE_CMAKE_MODULE_DIR}")
include("${STRIDE_CMAKE_MODULE_DIR}/StrideFunctions.cmake")

add_executable(test_dsp_firmware
    main.c
)

stride_target_sources(test_dsp_firmware PRIVATE
    dsp.stride
)
