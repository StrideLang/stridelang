# Systems, Frameworks, Platforms, and Domains

This document formalizes the architectural layers in Stride, establishing the relationships and boundaries between **Platforms**, **Frameworks**, **Systems**, and **Domains**.

---

## 1. Architectural Taxonomy at a Glance

| Concept | What It Represents | Key Responsibilities | Examples |
| :--- | :--- | :--- | :--- |
| **Platform** | **Hardware Support** | Physical processor cores, pinouts, memory layouts, DMA controllers, peripheral registers, interrupt vectors. | `RP2040`, `RP2350`, `STM32F4`, `Local_x86_64` |
| **Framework** | **Toolchain & Runtime** | Code emission backend, JIT vs AOT codegen, platform module operator implementations, C/C++ runtime bindings. | `JIT`, `Cpp`, `RtAudio`, `Wiring`, `PicoSDK` |
| **System** | **Deployment Topology** | Groups platforms and frameworks into a deployable system; coordinates abstract inter-domain **Connections**. | `DesktopApp`, `DesktopAudioApp`, `DualCoreBldcSystem` |
| **Domain** | **Computation Group** | Execution context for signals, streams, and state machines; bound to a **Framework**; requires and provides **Resources**. | `AudioDomain`, `ControlDomain`, `GuiDomain`, `RootDomain` |

```mermaid
flowchart TD
    subgraph System ["System (Deployment Topology)"]
        direction TB

        subgraph PlatformPC ["Platform: PC Desktop (Hardware)"]
            FrameworkJIT ["Framework: JIT / Cpp (Toolchain)"]
            FrameworkJIT --> DomAudio ["Domain: AudioDomain (Real-time)"]
            FrameworkJIT --> DomControl ["Domain: ControlDomain (Asynchronous)"]
        end

        subgraph PlatformMCU ["Platform: RP2350 / STM32 (Hardware)"]
            FrameworkMCU ["Framework: PicoSDK / BareMetal (Toolchain)"]
            FrameworkMCU --> DomDSP ["Domain: DspDomain (Cortex-M33 Core 0)"]
            FrameworkMCU --> DomIO ["Domain: IoDomain (Hazard3 Core 1)"]
        end

        DomControl -.->|"Connection (Serial / USB / Protobuf)"| DomDSP
        DomDSP -.->|"Connection (Shared SRAM / DMA)"| DomIO
    end
```

---

## 2. Platforms: Hardware Support

A **Platform** (`platform`) encapsulates physical hardware capabilities and resources. It defines the physical substrate rather than code generation mechanics:

- **Target Microcontrollers & CPUs**: Processor cores (ARM Cortex-M, RISC-V, x86_64), clock frequencies, cache configurations.
- **Memory Topology**: Base addresses, SRAM banks, flash boundaries, tightly coupled memories (TCM), zero-latency scratchpads (zpads).
- **Hardware Peripherals**: DMA engines, GPIO multiplexing, timer counters, UART/SPI/I2C controllers.
- **Interrupt Routing**: NVIC / PLIC interrupt mappings and priority schemes.

```stride
platform _PicoPlatform {
    framework: "Pico"
    frameworkVersion: "1.0"
    hardware: "RP2040"
    hardwareVersion: "1.0"
}
```

---

## 3. Frameworks: Toolchains & Runtimes

A **Framework** (`_frameworkDescription`) defines the execution environment, compiler backend, and runtime library bindings:

- **Code Emission Mode**:
  - Ahead-of-Time (AOT) machine code via `stridec`.
  - Just-in-Time (JIT) compilation directly to LLVM IR execution engines via `stridejit`.
  - C++ source generation targeting vendor SDKs (Pico SDK, STM32Cube, Arduino Wiring).
- **Platform Modules & Operator Lowering**:
  - Provides native implementations for primitive operators (`+`, `-`, `*`, `/`, `Sin`, `Sqrt`) and hardware-accelerated intrinsics (CMSIS-DSP, NEON, AVX2).
- **Runtime Libraries**:
  - Native driver bindings (audio backends like RtAudio/WASAPI/ALSA, hardware UART drivers, RTOS synchronizers).

---

## 4. Domains: Computation Groups & Resource Scopes

A **Domain** (`_domainDefinition`) is an execution and scheduling unit. It groups signals, streams, reactions, and state machines sharing execution rates and memory contexts.

### 4.1 Invariants of a Domain
1. **Framework Association**: Every domain is tied to an explicit framework (`framework: ...`).
2. **Resource Boundaries**:
   - `requiresResources`: System or hardware capabilities required to execute the domain (e.g. `_RootContext`, worker thread, hardware timer, DMA channel).
   - `providesResources`: Capabilities exposed to child subdomains or external hosts.
3. **Signal Ownership**:
   - A signal declared with `domain: <DomainName>` is owned exclusively by that domain.
   - The owning domain initializes the signal to its `default` value during startup and acts as its native mutation site.
4. **Hierarchical Subdomains (`parentDomain`)**:
   - Subdomains inherit context and resources from their parent domain, enabling nested computation groups.

### 4.2 Autonomous Subdomains & Parent Resource Provisioning
When an autonomous domain is a child of another domain, its **parent domain is where autonomy is granted and resources are provided**:
- **Execution Resource Ownership**: The parent domain allocates the execution vehicle needed by the autonomous child (e.g. allocating a `_Thread` resource, creating an OS worker thread, or initializing an RTOS task).
- **Shared Memory Context**: The parent domain hosts the shared memory space (`_RootContext`, shared heap, or ring buffer) accessible to both the parent and its subdomains.
- **Lifecycle Coordination**: The parent manages the child's execution lifecycle.

### 4.3 Standardized Domain Lifecycle Functions
Every domain generates four standardized C lifecycle functions:

| Lifecycle Function | Purpose | Concurrency & Behavior |
| :--- | :--- | :--- |
| `<Domain>_load()` | Global one-shot preparation. | Runs `onLoad` streams; thread-safe atomic CAS guard. |
| `<Domain>_init(void *state)` | Instance reset. | Runs `onInit` streams, applies default values, resets state machines and buffers. |
| `<Domain>_process(...)` | Execution tick. | Evaluates domain streams, reactions, and active state transitions. |
| `<Domain>_cleanup(void *state)` | Teardown. | Terminates worker threads and releases acquired resources. |

---

## 5. Systems & Inter-Domain Bridging

A **System** (`system`) is the top-level deployment unit that composes multiple platforms and frameworks into a whole, declaring the abstract **Connections** that link disparate execution targets together:

```stride
system DualCoreAudioSystem {
    platforms: [_PCPlatform, _PicoPlatform]
    connections: [_PCToPicoUart]
}
```

For detailed specifications on cross-domain data transfer, shared memory, semaphore hand-offs, parameterized connections, and symmetrical native platform entities, see [Multi-Domain Computation, Shared Memory & System Connections](file:///c:/Users/Andres/source/repos/boardgame/libgame/external/stridelang/doc/language/multi_domain_computation.md).

---

## 6. Pins and GPIO

A program names the signals it uses, and a hardware file next to it says which board pin each signal is on and how it is set up. Moving to another board means editing the hardware file and the `use` line; the program does not change.

```stride
# BlinkRig.stride: the project's hardware file, next to blink.stride
package BlinkRig

import Gpio            # Gpio::PinInput, Gpio::PullUp, ...
import ArduinoUno      # ArduinoUno::D2, ArduinoUno::LED, ...

hardwareBoard BlinkRig {
    overlays: ArduinoUno               # platform is inherited
}

gpioPin Button { pin: ArduinoUno::D2  mode: Gpio::PinInput  pull: Gpio::PullUp activeLow: on }
gpioPin Status { pin: ArduinoUno::LED mode: Gpio::PinOutput initial: off }
gpioPin Relay  { pin: ArduinoUno::D7  mode: Gpio::PinOutput outputType: Gpio::OutputOpenDrain }
```

```stride
# blink.stride
use AVR version 1.0 on BlinkRig
import DigitalIO
import BlinkRig

switch Blink { default: off domain: PlatformLoopDomain }
Blink >> Not() >> Blink;
Blink >> DigitalIO::Led(pin: BlinkRig::Status);
0.5 >> Sleep(domain: PlatformLoopDomain);

switch Pressed { default: off domain: PlatformTimerDomain }
BlinkRig::Button >> DigitalIO::DigitalIn() >> Pressed;
Pressed >> DigitalIO::DigitalOut(pin: BlinkRig::Relay);
```

### 6.1 Files and names

* Every file imports what it uses and every imported name carries its package: `Gpio::PullUp`, `ArduinoUno::D2`, `BlinkRig::Status`, `DigitalIO::Led`. Hardware files declare their package (`package ArduinoUno`).
* While a `use <Platform>` system is active, imports are also looked up in `platforms/<Platform>/boards`, `platforms/<Platform>/devices` and `cores`, after the program's own directory.
* `use ... on BlinkRig` selects the build target. `BlinkRig` is found next to the program first, so a project can describe its own board (or, for an FPGA soft core, its own device) on top of a stock one.

### 6.2 Three layers

| Where | Declares |
|---|---|
| Device (`ATmega328P.stride`) | `gpioPort`s with their registers and `gpioField`s (which register field sets mode, pull, ...), `pinFeatures` (what each chip pin adds: analog channel, interrupt kinds, `pinRoute`s to peripheral functions, `reservedBy`), `gpio` (values done in software, such as AVR's open drain), and the chip's pin functions (`uartTx Uart0Tx { peripheral: "Usart0" }`) |
| Board (`ArduinoUno.stride`) | `pin` labels mapped to chip pins: `pin D13 { port: PortB bit: 5 }`. Two labels may name one pin (`LED` and `D13`). |
| Project (`BlinkRig.stride`) | `gpioPin`s: how the project uses a board pin |

### 6.3 gpioPin options

Every option is a group of constants in `library/1.0/Gpio.stride` (a `typeClass` of constants), so the schema checks the names:

| Key | Values | Default |
|---|---|---|
| `mode` (required) | `PinInput`, `PinOutput`, `PinFunction`, `PinAnalog`, `PinDisabled` | |
| `outputType` | `OutputPushPull`, `OutputOpenDrain`, `OutputOpenSource` | `OutputPushPull` |
| `pull` | `PullNone`, `PullUp`, `PullDown`, `PullBusKeeper` | `PullNone` |
| `drive` | `DriveDefault`, `DriveLow`, `DriveMedium`, `DriveHigh`, `DriveMax` | `DriveDefault` |
| `slew` | `SlewDefault`, `SlewSlow`, `SlewMedium`, `SlewFast`, `SlewMax` | `SlewDefault` |
| `interrupt` | `InterruptNone`, `InterruptRising`, `InterruptFalling`, `InterruptBothEdges`, `InterruptLevelLow`, `InterruptLevelHigh` (checked, not generated yet) | `InterruptNone` |
| `ioStandard` | `IoDefault`, `IoLvcmos33`, ... `IoLvds`, `IoTmds` (FPGA) | `IoDefault` |
| `function` | a pin function of the device, such as `ATmega328P::Uart0Tx`; with `PinFunction` | |
| `activeLow`, `initial` | `on`/`off` | `off` |

### 6.4 What the compiler checks and does

* A value is supported on a pin exactly when one of its port's `gpioField`s has a code for it, or the device lists it in `gpio.emulated`. Anything else is an error that names the pin and the supported values, for example `gpioPin Button: pull PullDown is not supported by ATmega328P pin PortD.2 (supported: PullNone, PullUp)`.
* It also reports keys that don't fit the mode, a function the pin can't carry, a pin the runtime reserves (D0 and D1 on an Uno), two gpioPins using one chip pin differently, a `DigitalOut` on an input pin and a `DigitalIn` on an analog pin.
* Every gpioPin is set up once at startup in `RootDomain`'s `onLoad`: port clocks, initial levels, pulls/drive/slew, function select, then mode and direction, so outputs never glitch.
* `DigitalOut`, `DigitalIn` and `Led` read the registers through the framework's `_GpioWrite` and `_GpioRead`. The compiler fills in each pin's addresses and masks, so programs never see them. Under the JIT framework the registers are simulated, so a board's program runs and can be tested on the desktop.

### 6.5 Cost on AVR

The addresses are constants, so there is no call and no table lookup. Writing a constant level (the startup writes, `on >> DigitalOut(...)`) is a single `sbi`/`cbi`. Writing a level known only at run time is a read-modify-write of the constant register (`in`, `andi`/`ori`, `out`): a few cycles, and not atomic against an interrupt that writes the same port. On ports with set/clear registers (STM32 BSRR, RP2040 SIO, nRF OUTSET/OUTCLR) every write is one store.

---

## 7. Building One Program for Multiple Platforms

Portable programs using standard domain aliases (`PlatformLoopDomain`, `PlatformTimerDomain`) and portable packages (`DigitalIO`, `Strings`) can target multiple platforms in two ways:

1. **CLI Overrides (`--system` / `--boardOverride`)**: Keep a single `use` statement (or omit it in portable files) and override the platform and board at build time:
   ```powershell
   stride build PortableBlink.stride --system=Pico --boardOverride=PicoBlinkRig
   ```
2. **Conditional `use` / `import` with `@with`**: When different platforms need different hardware rig imports or conditional code, gate `use` and `import` with `@with(...)` and select the variant via `-D`:
   ```stride
   feature Target { variants: [AVR, Pico] default: AVR }

   @with(Target::AVR)  use AVR version 1.0 on AvrBlinkRig
   @with(Target::Pico) use Pico version 1.0 on PicoBlinkRig
   ```
   ```powershell
   stride build Blink.stride -D Target::Pico
   ```
