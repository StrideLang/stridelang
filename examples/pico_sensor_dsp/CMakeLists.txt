cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(pico_sensor_dsp C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize Raspberry Pi Pico SDK
pico_sdk_init()

# Locate Stride Ahead-Of-Time Compiler
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
find_package(Stride REQUIRED)

add_executable(pico_sensor_dsp
    src/main.c
)

# Cross-compile Stride DSP pipeline to ARM Cortex-M0+ object file and generate C header
stride_target_sources(pico_sensor_dsp PRIVATE
    stride/sensor_dsp.stride
)

# Link Pico SDK stdlib and hardware ADC drivers
target_link_libraries(pico_sensor_dsp PRIVATE
    pico_stdlib
    hardware_adc
)

# Enable USB stdio output
pico_enable_stdio_usb(pico_sensor_dsp 1)
pico_enable_stdio_uart(pico_sensor_dsp 0)

# Generate UF2 / HEX binary artifacts for flashing
pico_add_extra_outputs(pico_sensor_dsp)
