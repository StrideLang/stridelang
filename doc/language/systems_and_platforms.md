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
