# Bazel Sensor DSP Example

This example demonstrates integrating Stride digital signal processing pipelines into Bazel C/C++ applications and embedded firmware using `stride_cc_library`.

---

## Overview

- **`sensor_dsp.stride`**: Implements an IIR low-pass filter and threshold trigger alert.
- **`BUILD.bazel`**: Uses `stride_cc_library` to compile the `.stride` source file into a native object file and C header (`sensor_dsp.h`).
- **`main.c`**: Calls the generated `SensorDSP_init()` and `SensorDSP_process()` C functions.

---

## Building & Running with Bazel

### Build the Target:
```bash
bazel build //examples/bazel_sensor_dsp:sensor_app
```

### Run the Application:
```bash
bazel run //examples/bazel_sensor_dsp:sensor_app
```

### Cross-Compiling for Embedded Targets:
When building for an embedded microcontroller (e.g. ARM Cortex-M or Pigweed toolchain):
```bash
bazel build //examples/bazel_sensor_dsp:sensor_app --platforms=@pigweed//pw_build/platforms:rp2040
```
`rules_stride` automatically detects the target MCU architecture from the active toolchain and compiles the `.stride` code for Cortex-M0+ without requiring any manual flags in your `BUILD` file.
