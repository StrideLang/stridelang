# Raspberry Pi Pico - Stride Sensor DSP Example

This example demonstrates how to cross-compile a Stride DSP filter and threshold trigger for the Raspberry Pi Pico (RP2040 ARM Cortex-M0+) and link it directly into a Pico C firmware application.

---

## Project Structure

```text
examples/pico_sensor_dsp/
├── CMakeLists.txt              # CMake build script using find_package(Stride) & stride_target_sources()
├── pico_sdk_import.cmake       # Pico SDK configuration helper
├── src/
│   └── main.c                  # C firmware reading ADC, running Stride DSP, and toggling LED
├── stride/
│   └── sensor_dsp.stride       # Stride domain definition, LowPassFilter, and ThresholdTrigger
└── README.md
```

---

## Building

```powershell
# Set PICO_SDK_PATH and STRIDE_ROOT
$env:PICO_SDK_PATH = "C:/path/to/pico-sdk"
$env:STRIDE_ROOT = "C:/path/to/stridelang"

# Configure and build
cmake -B build -DPICO_BOARD=pico
cmake --build build --config Release
```
