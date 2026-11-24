# Stride Command-Line Interface Reference

Stride provides two CLI executables:

* **`stride`** — Unified toolchain manager and driver (`compile`, `build`, `flash`, `system`, `tool`, `doctor`, `setup`).
* **`stridec`** — Clang-style Ahead-Of-Time (AOT) compiler driver for object files, static archives, C headers, and pre-built Arduino libraries.

Board firmware has one path: `stride build` builds it and `stride flash` builds and uploads it. `stridec` does not link or upload firmware.

---

## `stride`

```text
stride <command> [arguments...]
stride <file.stride> [compiler options...]   # Shorthand for `stride compile`
```

### Global Options

| Option | Description |
| :--- | :--- |
| `-h`, `--help` | Show top-level usage and commands. |
| `-v`, `--version` | Print `stride` version and the active `STRIDEROOT` path. |

### Commands

| Command | Description |
| :--- | :--- |
| `stride compile [options] <files...>` | Run the AOT compiler driver (`stridec`) on `.stride` inputs. |
| `stride build [options] <file.stride>` | Compile `.stride`, generate `stride_board.h`, compile/link the target runtime, and produce `.elf` and `.hex` firmware images. |
| `stride flash [options] <file.stride>` | Same as `stride build`, then uploads the `.hex` to a connected board. |
| `stride system list` | List available systems and versions in `STRIDEROOT/systems`. |
| `stride system upgrade [dir] [ver]` | Update `use <System> version X.Y` declarations in `.stride` files under `dir` (default: current directory, version `1.0`). |
| `stride tool list` | List discovered tool templates from `STRIDEROOT/tools`. |
| `stride tool generate` | Scan the system and generate local hardware/tool configurations. |
| `stride doctor` | Verify `STRIDEROOT`, `PATH`, host C++ compiler (`clang`/`cl`/`gcc`), and VS Code CLI. |
| `stride setup vscode` | Install or update the bundled Stride VS Code extension (or install from marketplace). |

### `stride build` / `stride flash` Options

These commands inspect the `.stride` file's `use <System> version <ver> on <Board>` declaration (or `--boardOverride`), resolve the hardware chain (`HardwareResolver`: board → device → core), generate `stride_board.h`, compile the target C runtime (e.g. `stride_avr_runtime.c`), link with `avr-gcc`, emit `.elf` + `.hex` via `avr-objcopy`, and print `avr-size`. `stride flash` then uploads the `.hex` via `avrdude`.

Each takes exactly one `.stride` file. Any option not listed below is an error.

The board is looked up next to the program first, so a project can keep its wiring in its own hardware file (`BlinkRig.stride` declaring `hardwareBoard BlinkRig { overlays: ArduinoUno }` and its `gpioPin`s) and build with `use AVR version 1.0 on BlinkRig`. See [Pins and GPIO](language/systems_and_platforms.md#6-pins-and-gpio).

| Option | Description |
| :--- | :--- |
| `-o <file.hex\|file.elf\|file.uf2>` | Output firmware path (default: `<input>.hex` for AVR, `<input>.uf2` for Pico). |
| `--system=<name>`, `--platformOverride=<name>` | Override the target platform/system declared in `use <System> on <Board>` (e.g. `AVR` → `Pico`). |
| `--boardOverride=<name>`, `--boardOverride <name>` | Override the board declared in `use <System> on <Board>` (or supply a board when none is specified in the file). |
| `--port=<port>`, `--port <port>`, `-P <port>` | `stride flash` only. Serial port for upload (e.g. `COM3`, `/dev/ttyACM0`). Auto-detected from the board's `usb` VID:PID list if omitted. |
| `--probe` | `stride flash` only. Flash via SWD debug probe using OpenOCD (auto-downloads OpenOCD into `~/.stride/tools/openocd` if missing). |
| `--upload-method=<picotool\|swd\|auto>` | `stride flash` only. Select upload method (`picotool` USB, `swd` OpenOCD, or `auto`). |
| `--no-prompt` | Disable interactive prompts (e.g. BOOTSEL preparation or OpenOCD download confirmation). |
| `--strict` | Make a String write that can be cut off an error. |
| `--build-dir=<dir>`, `--build-dir <dir>` | Directory for intermediate artifacts (`<stem>.o`, `stride_board.h`, runtime `.o`). Defaults to `<outDir>/<stem>_build`. |
| `-O0`, `-O1`, `-O2`, `-O3`, `-Os`, `-Oz` | Optimization level passed to LLVM (`-Os` / `-Oz` map to level `2`; default: `2`). |
| `-I <dir>`, `-I<dir>` | Add directory to `.stride` include search path. |
| `-D <name>`, `-D<name>`, `--feature <name>`, `--feature=<name>` | Enable a feature toggle or exclusive variant (`Group::Variant`). |
| `--features <list>`, `--features=<list>` | Enable comma-separated list of features. |
| `--no-feature <name>`, `--no-feature=<name>` | Explicitly disable a feature or variant. |
| `--no-default-features` | Disable declared default features. |
| `--strideroot=<dir>`, `--strideroot <dir>` | Override `STRIDEROOT`. |
| `-v`, `--verbose` | Print compilation details and invoked toolchain commands (`avr-gcc`, `avr-objcopy`, `avr-size`, `avrdude`). |
| `-h`, `--help` | Show command-specific usage. |

---

## `stridec` (`stride compile`)

```text
stridec [options] <input1.stride> [input2.stride ...]
```

Any `--flag=<val>` option that takes a value also accepts `--flag <val>`.

### Target & Architecture Selection

| Option | Description |
| :--- | :--- |
| `--target=<triple>`, `-target <triple>`, `--target-triple=<triple>` | Set the LLVM target triple (e.g. `thumbv6m-none-eabi`, `arm-none-eabi`, `avr`, `wasm32-unknown-wasi`). If omitted and the file uses `on <Board>` (or `--boardOverride`), inferred via `HardwareResolver`. |
| `-mcpu=<cpu>`, `--mcpu=<cpu>`, `-march=<arch>` | Set the target CPU / architecture (e.g. `cortex-m0plus`, `cortex-m4`, `atmega328p`). |
| `--target-features=<features>` | Comma-separated LLVM target feature flags (e.g. `+vfp4`). |
| `--boardOverride=<name>` | Override the hardware board declared in `use <System> on <Board>`. |

### Code Generation, Optimization & Debug Options

| Option | Description |
| :--- | :--- |
| `-c` | Compile to an object file. This is the default; the flag is accepted for Clang compatibility, and with it an `-o <file>.elf` name is treated as an object file. |
| `-o <file>` | Output object file (`.o` / `.obj`) or static archive (`.a`). A `.hex` (or, without `-c`, `.elf`) output is an error that points to `stride build`. |
| `--emit-header=<file>`, `-emit-header=<file>`, `--header-out=<file>`, `-header-out=<file>` | Generate a C99/C++ interface header (`.h`) declaring domain functions and structs. |
| `--domain=<name>`, `-domain <name>` | Restrict generated C header declarations to a specific domain. |
| `--emit-domain-state` | Emit zero-initialized `__stride_<Domain>_state` global byte arrays into object output for bare-metal runtimes. |
| `-femit-all-functions`, `--emit-all-functions`, `-compile-functions`, `--compile-functions` | Force code generation for all root-level modules/functions even if unused by a domain. |
| `-O0`, `-O1`, `-O2`, `-O3`, `-Os`, `-Oz` | Optimization level (`-Os` and `-Oz` map to level `2`). |
| `--build-type=<type>`, `-build-type=<type>` | Build preset: `debug`, `release`, `relwithdebinfo`, `minsizerel`. |
| `-g`, `-g2`, `-g3` | Emit full DWARF debug information. |
| `-gline-tables-only`, `-g1` | Emit line-table debug information only. |
| `-g0` | Disable debug information. |
| `-fPIC`, `-fpic`, `-fPIE`, `-fpie` | Use PIC relocation model (`pic`). |
| `-fno-PIC`, `-fno-pic`, `-fno-PIE`, `-fno-pie` | Use static relocation model (`static`). |
| `-fropi`, `-frwpi`, `-fropi-rwpi` | Use `ropi`, `rwpi`, or `ropi-rwpi` relocation models. |
| `-mcmodel=<model>`, `--code-model=<model>` | LLVM code model: `tiny`, `small`, `kernel`, `medium`, `large`. |
| `-fvectorize`, `-fno-vectorize` | Enable or disable loop vectorization flags. |
| `-funroll-loops`, `-fno-unroll-loops` | Enable or disable loop unrolling flags. |
| `-Wconversion`, `--report-conversions` | Emit compiler warnings (`file:line:col: warning: ... [-Wconversion]`) for potentially lossy numeric conversions. |

`stridec` rejects `--flash`, `--upload`, `--port` and `-P` with an error that points to `stride flash`.

### Arduino Pre-built Library Options

| Option | Description |
| :--- | :--- |
| `--emit-arduino-lib=<dir>`, `-emit-arduino-lib=<dir>`, `--arduino-lib=<dir>`, `-arduino-lib=<dir>` | Output a precompiled Arduino library bundle (`library.properties`, `src/`, `examples/`). |
| `--arduino-name=<name>`, `-arduino-name=<name>` | Library name (defaults to `--domain` or input file stem). |
| `--arduino-arch=<arch>`, `-arduino-arch=<arch>` | Target architecture (`arm` default, or `all`). |
| `--arduino-mcu=<mcus>`, `-arduino-mcu=<mcus>` | Comma-separated MCU list (e.g. `cortex-m0plus,cortex-m4`). |
| `--arduino-author=<author>`, `-arduino-author=<author>` | Author field in `library.properties` (default: `Stride`). |
| `--arduino-version=<ver>`, `-arduino-version=<ver>` | Version field in `library.properties` (default: `1.0.0`). |

### Feature Toggles, Include Paths & Driver Options

| Option | Description |
| :--- | :--- |
| `-D <name>`, `-D<name>` | Enable a feature or variant (e.g. `-D Audio`, `-D Pico::RT2030`). |
| `--feature <name>`, `--feature=<name>` | Enable a feature or variant. |
| `--features <list>`, `--features=<list>` | Enable comma-separated features. |
| `--no-feature <name>`, `--no-feature=<name>` | Explicitly disable a feature or variant. |
| `--no-default-features` | Disable declared default features. |
| `-I <dir>`, `-I<dir>`, `-isystem <dir>` | Add directory to `.stride` include search paths. |
| `--strideroot=<dir>` | Override `STRIDEROOT`. |
| `-v`, `--verbose` | Enable verbose driver output (with `-v` alone, prints version). |
| `-h`, `--help`, `-help` | Display `stridec` help. |
| `--version`, `-version` | Display `stridec` version. |

---

## Caching & Environment Notes

* **Cache isolation for overrides & cross-targets**: AST and object cache tags incorporate `--system`, `--boardOverride`, `--target`, and `-mcpu`, so overriding a platform or board on the command line never reads or overwrites cached artifacts for the target named inside the `.stride` file.
* **Porting across platforms**: `stride build` and `stride flash` evaluate `@with(...)` conditions on `use` and `import` before selecting the target platform/board (via `-D`), and also support `--system` + `--boardOverride` to retarget a portable program without modifying source files.
* **`STRIDEROOT` & `STRIDE_DEV`**: Set `STRIDEROOT` to point to a custom standard library / platform root, or `STRIDE_DEV=1` when running from a source build to prefer the adjacent development `strideroot` checkout (see [Developer Workflow Guide](developer_workflow.md)).

---

## Examples

```powershell
# Compile a module for host C/C++ integration
stridec -c sensor_dsp.stride -o sensor_dsp.o --emit-header=sensor_dsp.h

# Cross-compile for RP2040 (ARM Cortex-M0+)
stridec -c dsp.stride --target=thumbv6m-none-eabi -mcpu=cortex-m0plus -o dsp.o --emit-header=dsp.h

# Build AVR firmware (.elf + .hex) for Arduino Uno
stride build blinky.stride -o blinky.hex -v

# Override the board declared in the file and flash to a connected Arduino Uno
stride flash blinky.stride --boardOverride=ArduinoUno --port=COM3 -v
```
