# Stride Language (`stridelang`)

The official Ahead-Of-Time (AOT) compiler, runtime toolchain, and build system integration for the Stride programming language.

---

## Overview

`stridelang` provides the `stridec` compiler binary and first-class build system modules for integrating Stride DSP, state machines, and dataflow streams directly into C and C++ projects — targeting both host environments and bare-metal embedded microcontrollers.

---

## Features

- **Ahead-Of-Time (AOT) Native Compilation**: Compiles `.stride` source files into native machine object files (`.o`).
- **C99/C++ API Generation**: Automatically emits clean C header files (`.h`) declaring `<Domain>_init(void)` and `<Domain>_process(...)`.
- **Target Architectures & MCUs**:
  - **Raspberry Pi Pico / RP2040 / RP2350** (`thumbv6m-none-eabi`, `thumbv8m.main-none-eabi`)
  - **STM32** (ARM Cortex-M0+, Cortex-M3, Cortex-M4, Cortex-M7, Cortex-M33)
  - **Renesas** (RA Cortex-M, RX)
  - **WebAssembly** (`wasm32-unknown-wasi`)
  - **Host Architectures** (x86_64, AArch64)
- **Modern CMake Integration**: Drop-in `find_package(Stride)` and `stride_target_sources()` support.

---

## CLI Usage (`stridec`)

```text
Usage: stridec [options] <input.stride>

Target & Architecture Selection:
  --target=<triple>, -target <triple>   Set LLVM target triple (e.g. thumbv6m-none-eabi, arm-none-eabi)
  -mcpu=<cpu>, -mcpu <cpu>             Set target CPU (e.g. cortex-m0plus, cortex-m4, cortex-m7)
  -march=<arch>                        Set target architecture
  -mfloat-abi=<abi>                    Floating-point ABI: soft, softfp, hard
  -mfpu=<fpu>                          Target FPU

Code Generation & Relocation Options:
  -fPIC, -fno-PIC                      Generate position-independent code
  -fropi, -frwpi                       Read-only / read-write position independence
  -mcmodel=<model>                     Code model: tiny, small, kernel, medium, large
  -O0, -O1, -O2, -O3, -Os, -Oz         Optimization level
  -c                                   Compile to object file (do not link)

Output & Header Options:
  -o <file>                            Output object file (.o) or static archive (.a)
  --emit-header=<file>, --header-out   Output C interface header file (.h)
  --domain=<name>, -domain <name>      Specific domain to generate header for

Arduino Pre-built Library Options:
  --emit-arduino-lib=<dir>, --arduino-lib=<dir>
                                       Output an Arduino-compatible precompiled library bundle
  --arduino-name=<name>                Name of the Arduino library (default: derived from input)
  --arduino-arch=<arch>                Target architecture: arm (default), all
  --arduino-mcu=<mcus>                 Comma-separated list of target MCUs (e.g. cortex-m0plus,cortex-m4)
  --arduino-author=<author>            Author name for library.properties (default: Stride)
  --arduino-version=<ver>              Library version for library.properties (default: 1.0.0)

Include & Path Options:
  -I <dir>, -I<dir>                    Add directory to .stride include search path
  -isystem <dir>                       Add system include directory
  --strideroot=<dir>                   Custom Stride root directory

Driver Options:
  -v, --verbose                        Enable verbose output
  -h, --help                           Display this help message
  --version                            Display compiler version
```

---

## Build System Integrations

### 1. Bazel & Pigweed Integration (`rules_stride`)

`stridelang` provides official Bazel build rules with zero-boilerplate C++ toolchain integration:

#### In `MODULE.bazel`:
```python
bazel_dep(name = "rules_stride", version = "1.0.0")
```

#### In `BUILD.bazel`:
```python
load("@rules_stride//stride:defs.bzl", "stride_cc_library")

# Zero configuration: compiles .stride into target object code and exports CcInfo
stride_cc_library(
    name = "sensor_dsp",
    srcs = ["sensor_dsp.stride"],
    visibility = ["//visibility:public"],
)

# Consume directly in standard cc_binary, cc_library, or Pigweed firmware
cc_binary(
    name = "firmware.elf",
    srcs = ["main.c"],
    deps = [
        ":sensor_dsp",
    ],
)
```

Target architectures (ARM Cortex-M0+/M3/M4/M7/M33, RISC-V, WebAssembly, Host) are automatically deduced from the consuming target's active Bazel C++ toolchain or Pigweed platform (`@pigweed//pw_toolchain`).

---

### 2. CMake Integration

```cmake
find_package(Stride REQUIRED)

add_executable(my_firmware
    src/main.c
)

# Compiles sensor_dsp.stride into the target MCU object file and exposes sensor_dsp.h
stride_target_sources(my_firmware PRIVATE
    stride/sensor_dsp.stride
)
```

#### Automatic Dependency Linking & Platform Extensibility

When using `stride_target_sources()` or `stride_add_library()`, Stride automatically manages all required runtime and C library dependencies:
* **Stride Runtime (`Stride::Runtime`)**: Automatically linked if available to supply string and runtime helpers (`StrideRuntime.lib` / `libStrideRuntime.a`).
* **Math & C Libraries (`libm` / `libc`)**: On standard desktop OSes (Linux/macOS), the math library (`-lm`) is automatically linked. On Windows, the standard MSVC CRT is linked.
* **Embedded SDK Detection**: If an embedded SDK target is active (e.g. `pico_stdlib` for Raspberry Pi Pico, Zephyr RTOS, or ESP-IDF), generic compiler libraries are suppressed so the SDK's hardware-accelerated memory and math routines (such as `pico_mem_ops`) take precedence without duplicate symbol collisions.
* **Bare-Metal Toolchains**: When `CMAKE_SYSTEM_NAME` is set to `Generic`, host libraries are not injected; toolchain specs (e.g. `--specs=nano.specs`) govern runtime linking.
* **Manual Overrides**:
  ```cmake
  # Suppress default libraries on custom RTOS
  stride_target_sources(my_firmware PRIVATE
      stride/sensor_dsp.stride
      NO_DEFAULT_LIBS
  )

  # Explicitly link additional platform SDK libraries
  stride_target_sources(my_firmware PRIVATE
      stride/sensor_dsp.stride
      LINK_LIBRARIES pico_stdlib hardware_adc
  )
  ```

### 3. Arduino Pre-built Library Generation

Stride can package compiled `.stride` modules into standard, precompiled Arduino libraries (compliant with the [Arduino Library Specification rev 2.2](https://arduino.github.io/arduino-cli/latest/library-specification/#precompiled-libraries)), with initial support for ARM microcontrollers.

#### CLI Generation:
```bash
# Build precompiled Arduino library supporting all standard ARM Cortex MCUs:
# (cortex-m0plus, cortex-m3, cortex-m4, cortex-m7, cortex-m33)
stridec --emit-arduino-lib=SensorDSP stride/sensor_dsp.stride

# Target a specific MCU or set of MCUs:
stridec --emit-arduino-lib=CustomFilter --arduino-mcu=cortex-m4 stride/sensor_dsp.stride
```

#### Generated Arduino Directory Structure:
```text
SensorDSP/
├── library.properties          # Metadata configured with precompiled=full & ldflags
├── examples/
│   └── SensorDSPDemo/
│       └── SensorDSPDemo.ino   # Ready-to-run demo sketch for the Arduino IDE
└── src/
    ├── SensorDSP.h             # Public C/C++ interface header
    ├── SensorDSP.cpp           # Compatibility source stub
    ├── cortex-m0plus/
    │   └── libSensorDSP.a      # Precompiled static archive (RP2040, SAMD21, etc.)
    ├── cortex-m3/
    │   └── libSensorDSP.a      # Precompiled static archive (Arduino Due, SAM3X)
    ├── cortex-m4/
    │   └── libSensorDSP.a      # Precompiled static archive (Uno R4, Nano 33 BLE, Teensy)
    ├── cortex-m7/
    │   └── libSensorDSP.a      # Precompiled static archive (Portenta H7, Giga R1, Teensy 4)
    └── cortex-m33/
        └── libSensorDSP.a      # Precompiled static archive (RP2350, Cortex-M33)
```

#### CMake Integration:
```cmake
# Using dedicated helper
stride_add_arduino_library(SensorDSP
    SOURCES stride/sensor_dsp.stride
    MCUS cortex-m0plus cortex-m4 cortex-m7
)

# Or using stride_add_library with ARDUINO option:
stride_add_library(SensorDSP ARDUINO
    SOURCES stride/sensor_dsp.stride
)
```

---

## Building `stridelang`

`stridelang` links against the `stridejit` runtime and LLVM libraries. You can build it either by compiling dependencies from source or by pointing to an existing prebuilt build directory for instantaneous builds.

### 1. Fast Build Using Prebuilt Binaries & Headers (Recommended)

To avoid rebuilding `stridejit` and LLVM, supply the path to your existing build output directory where `StrideJit.lib`, `StrideParser.lib`, `StrideUtils.lib`, and LLVM libraries reside:

```powershell
# Point to your existing stridejit or boardgame build directory
cmake -B build -DSTRIDE_PREBUILT_BUILD_DIR="C:/path/to/boardgame/build/Desktop_Qt_6_7_3_MSVC2022_64bit-Debug" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

#### CMake Options for Prebuilt Dependencies:

| CMake Option | Description |
| :--- | :--- |
| `-DSTRIDE_PREBUILT_BUILD_DIR=<path>` | Path to an existing build directory containing compiled Stride (`StrideJit`, `StrideParser`, `StrideUtils`, `StrideCodegen`) and LLVM static libraries. |
| `-DSTRIDE_SRC_DIR=<path>` | (Optional) Path to the root `stridejit` source repository containing `include/stride/` headers (defaults to `../stridejit` or `../../external/stridejit`). |
| `-DSTRIDE_USE_SYSTEM_DEPS=ON` | Prefer finding an already installed `StrideJit` package via `find_package(StrideJit)`. |

> [!NOTE]
> **Automatic Discovery**: If `STRIDE_PREBUILT_BUILD_DIR` is not specified, CMake will automatically scan sibling build directories (e.g. `../../build/*`, `../build/*`, `build/`) for existing prebuilt `StrideJit` and LLVM binaries.

### 2. Building from Source

If no prebuilt binaries are found and `STRIDE_PREBUILT_BUILD_DIR` is not set, CMake will automatically pull in and compile `stridejit` and its dependencies from source:

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

---

## Building the Installer & Toolchain Packages

### On Windows (NSIS `.exe` Installer & `.zip` Portable Archive):

You can build installers using the PowerShell script with prebuilt dependencies:

```powershell
# Fast installer build with existing prebuilt binaries:
.\scripts\build_installer.ps1 -PrebuiltBuildDir "C:/path/to/boardgame/build/Desktop_Qt_6_7_3_MSVC2022_64bit-Debug" -Config Debug

# Standard release installer build:
.\scripts\build_installer.ps1 -Config Release

# Or manually via CMake / CPack:
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cpack --config build/CPackConfig.cmake -C Release
```

The Windows NSIS installer automatically configures the system `PATH` so `stridec` and `find_package(Stride)` work out of the box in any terminal or IDE.

### On Linux / macOS (`.tar.gz`, `.deb`, `.rpm`):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cpack --config build/CPackConfig.cmake -C Release
```

