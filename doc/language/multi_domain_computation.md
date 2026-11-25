# Multi-Domain Computation, Shared Memory & System Connections

Stride programs are designed to span diverse execution environments — from single-threaded DSP pipelines and multi-threaded desktop applications to heterogeneous multi-core microcontrollers (e.g. Raspberry Pi RP2350 with ARM Cortex-M33 and RISC-V Hazard3 cores) and distributed network systems.

This document specifies the **multi-domain execution model**, **memory hierarchy**, **parameterized connection blueprints**, and **symmetrical native platform entities** that govern cross-domain computation in Stride.

---

## 1. Architectural Taxonomy: Systems, Frameworks, Platforms & Domains

To decouple dataflow algorithms from target hardware and toolchains, Stride defines four foundational layers:

| Entity | Role | Definition | Examples |
| :--- | :--- | :--- | :--- |
| **Platform** | **Hardware Support** | Hardware topology, device pinouts, peripherals, registers, memory layouts (SRAM, Flash, TCM, zpads), DMA controllers, interrupt vectors. | `RP2040`, `RP2350`, `STM32F4`, `Local_x86_64` |
| **Framework** | **Toolchain & Runtime** | Target compiler backend, code emission strategy, platform modules, operator lowering implementations, runtime library bindings. | `JIT`, `Cpp`, `RtAudio`, `Wiring`, `PicoSDK` |
| **System** | **Deployment Topology** | Groups platforms and frameworks into a deployable application; declares abstract **Connections** bridging disjoint targets. | `DesktopApp`, `DesktopAudioApp` |
| **Domain** | **Computation Group** | Execution context for signals, streams, and state machines; **always tied to a Framework**; requires and provides **Resources**. | `AudioDomain`, `ControlDomain`, `RootDomain` |

```mermaid
flowchart TD
    subgraph System ["System: DesktopAudioApp (Deployment Topology)"]
        direction TB

        subgraph PlatformPC ["Platform: PC Desktop (Hardware)"]
            FrameworkJIT ["Framework: JIT / Cpp (Toolchain)"]
            FrameworkJIT --> DomAudio ["Domain: AudioDomain (Real-Time Audio)"]
            FrameworkJIT --> DomControl ["Domain: ControlDomain (Asynchronous UI/Events)"]
        end

        subgraph PlatformMCU ["Platform: RP2350 / STM32 (Hardware)"]
            FrameworkMCU ["Framework: PicoSDK / BareMetal (Toolchain)"]
            FrameworkMCU --> DomDSP ["Domain: DspDomain (Cortex-M33)"]
            FrameworkMCU --> DomIO ["Domain: IoDomain (Hazard3 RISC-V)"]
        end

        DomControl -.->|"Connection (Serial / Protobuf)"| DomDSP
        DomDSP -.->|"Connection (Shared SRAM / DMA / zpad)"| DomIO
    end
```

---

## 2. Domains: Definition & Compilation Model

### 2.1 Unified Domain Model
A domain (`_domainDefinition`) defines a discrete execution boundary. Its internal dataflow streams, state machines, and reactions are completely backend-agnostic. The domain's **`framework`** property selects the compilation engine:

- When `framework: _JitFramework`, the domain compiles directly into in-memory LLVM IR functions.
- When `framework: _CppFramework` or embedded toolchains (e.g. `_PicoSDKFramework`), the domain compiles through Ahead-Of-Time (AOT) C++ template emission or native object compilation via `stridec`.

```stride
_domainDefinition AudioDomain {
    framework: _JitFramework
    rate: 48000
}

_domainDefinition ControlDomain {
    framework: _JitFramework
    rate: 1000
    parentDomain: AudioDomain
}
```

### 2.2 Domain Lifecycle Functions
Every compiled domain provides a standardized lifecycle API:

| Phase | Generated C API | Responsibilities |
| :--- | :--- | :--- |
| **Load** | `<Domain>_load()` | Evaluates **`onLoad`** streams outside any instance. Prepares read-only tables and precomputed constants. Guarded by an atomic compare-and-swap (CAS). |
| **Init** | `<Domain>_init(void *state)` | Calls `<Domain>_load()`, evaluates **`onInit`** streams (external/host resource setup), and finishes by calling `<Domain>_reset(state)`. Requires `<Domain>_cleanup` before re-calling `<Domain>_init`. |
| **Reset** | `<Domain>_reset(void *state)` | Resets domain signals, switches, state machines, and buffer counters to defaults, then evaluates **`onReset`** streams so runtime array/buffer fills persist. Safe to call while the domain is initialized. |
| **Process** | `<Domain>_process(...)` | Evaluates the domain's stream pipelines, active state machine transitions/actions, and triggered reactions for one execution tick. |
| **Cleanup** | `<Domain>_cleanup(void *state)` | Releases acquired hardware/software resources, stops worker threads, joins background tasks. |

---

## 3. Signal Ownership & Cross-Domain Access Rules

### 3.1 Signal Ownership
Every signal declared with `domain: <DomainName>` is **owned** by that domain:
```stride
signal SensorValue {
    type: Real
    default: 0.0
    domain: SensorDomain
}
```
The owning domain manages initialization during `<Domain>_init` and evaluates state mutations/resets during `<Domain>_process`.

### 3.2 Cross-Domain Validity Conditions
When Domain B reads or writes a signal owned by Domain A ($A \neq B$), the compiler validates that the boundary satisfies at least one of two conditions:

```mermaid
flowchart TD
    Start["Cross-Domain Stream Access:<br/>Domain B reads/writes SigA (owned by Domain A)"] --> CheckAncestor{"Do Domain A and Domain B<br/>share a common ancestor domain?"}
    
    CheckAncestor -- "Yes" --> ValidSharedMem["Condition 1: Intrinsic Shared Memory<br/>Direct pointer dereference in common address space (_RootContext)"]
    CheckAncestor -- "No" --> CheckConnection{"Is there an explicit<br/>connection rule declared<br/>between Domain A and Domain B?"}
    
    CheckConnection -- "Yes" --> ValidConnection["Condition 2: Explicit System Connection<br/>Lowered through connection transport (streams + entities)"]
    CheckConnection -- "No" --> Reject["Compile Error:<br/>LangError::InvalidCrossDomainAccess"]
```

If neither condition is met, `CodeValidator` rejects the stream at compile time:
> `Error: Signal 'SigA' owned by domain 'DomainA' cannot be accessed from domain 'DomainB': domains do not share a common parent domain or connection`

---

## 4. Mode 1: Intrinsic Shared Memory (Common Parent Domain)

When two domains share a common ancestor domain (`commonAncestor(DomainA, DomainB) != none`), they reside within the same parent memory context (`_RootContext`).

### Example: Produce-Consume over Shared Memory
```stride
_domainDefinition RootDomain {
    framework: _JitFramework
}

_domainDefinition ProduceDomain {
    framework: _JitFramework
    parentDomain: RootDomain
}

_domainDefinition ConsumeDomain {
    framework: _JitFramework
    parentDomain: RootDomain
}

signal ProduceVal {
    type: Int
    default: 10
    domain: ProduceDomain
}

signal ConsumeVal {
    type: Int
    default: 0
    domain: ConsumeDomain
}

# Producer updates its owned signal
ProduceVal + 1 >> ProduceVal;

# Consumer directly reads ProduceVal from shared memory
ProduceVal + 5 >> ConsumeVal;
```

### Compiler Lowering Invariants:
1. **Shared Signal Storage**: Signals belonging to domains with an ancestor relationship reside in global module memory rather than being hidden inside a domain's private dynamic stack/state struct.
2. **Consumer-Attributed Reads**: The cross-domain assignment `ProduceVal + 5 >> ConsumeVal` is attributed to `ConsumeDomain_process`, which loads `ProduceVal` directly from shared memory.
3. **Initialization Ownership**: `ProduceDomain_init` retains sole responsibility for resetting `ProduceVal` to its declared default (10).

---

## 5. Mode 2: Parameterized Connection Blueprints

When domains do not share a common memory resource (e.g., across physical MCU cores, hardware peripherals, or network boundaries), communication requires an explicit **`connection`**.

### 5.1 Connection Blueprints (`strideroot/systems/<System>/<Ver>/System.stride`)
System repositories declare generic connection blueprints containing default parameters and driver streams:

```stride
connection _PCToWiringSerial {
    port: 0
    baudRate: 115200

    sourceFramework: _PCPlatform
    sourceDomains: [ "DesktopDomain" ]
    destinationFramework: _WiringPlatform
    destinationDomains: [ "WiringDomain" ]

    sourceImports: [ "Serial" ]
    sourceStreams: [
        _Connection_Token >> SerialOut[.port];
    ]

    destinationImports: [ "Serial" ]
    destinationStreams: [
        Wiring::SerialIn[.port] >> _Connection_Token;
    ]
}
```

### 5.2 Project Specialization & Overrides
User applications specialize parameters without modifying system blueprint files:

- **System Connection Block**:
  ```stride
  system DesktopApp {
      platforms: [ _WiringPlatform, _PCPlatform ]
      connections: [
          connection _PCToWiringSerial {
              port: 1
              baudRate: 57600
          }
      ]
  }
  ```

- **Invocation / Call Syntax**:
  ```stride
  system DesktopApp {
      platforms: [ _WiringPlatform, _PCPlatform ]
      connections: [
          _PCToWiringSerial(port: 1 baudRate: 57600)
      ]
  }
  ```

- **Default Inheritance**: Listing a connection without overrides (`connections: [ _PCToWiringSerial ]`) inherits all blueprint defaults (`port: 0`, `baudRate: 115200`).

### 5.3 AST Property Substitution & Stream Splicing
During lowering (`StrideSystem::generateDomainConnections`):
1. **Effective Property Merging**: Blueprint defaults are merged with project overrides.
2. **Recursive AST Substitution**: Occurrences of connection properties (e.g. `.port`) inside streams and entities are replaced with concrete literal values (e.g. replacing `SerialOut[.port]` with `SerialOut[1]`).
3. **Splicing**: Cross-domain streams `SigA >> SigB;` are automatically split across the boundary:
    - `SigA >> SerialOut[1];` (executed in source domain)
    - `Wiring::SerialIn[1] >> SigB;` (executed in destination domain)

---

## 6. Symmetrical Native Entities in Connections (`sourceEntities` & `destinationEntities`)

Connections often require hardware glue logic, register writes, or platform-specific driver modules. To support **heterogeneous compilation targets** (e.g. in-memory LLVM IR JIT on host PC $\longleftrightarrow$ C++ source text generation on an embedded MCU) without requiring external dummy packages, `connection` provides symmetrical entity lists:

```stride
connection _PCToPicoUart {
    baudRate: 115200
    port: 0

    sourceFramework: _JitFramework
    sourceDomains: [ "DesktopDomain" ]
    destinationFramework: _PicoSDKFramework
    destinationDomains: [ "McuDomain" ]

    # Native entities compiled for the source framework (LLVM IR on host PC)
    sourceEntities: [
        constant HostPort { value: .port }
        platformModule NativeHostUartSend {
            inputs: [ Int ]
            outputs: []
            processing: "%%outtokens:0%% = llvm::call void @host_uart_write(i32 %%intokens:0%%)"
        }
    ]

    # Native entities compiled for the destination framework (C++ text codegen on RP2040)
    destinationEntities: [
        constant McuPort { value: .port }
        platformModule NativeMcuUartRecv {
            inputs: []
            outputs: [ Int ]
            processing: "uart_read_blocking(uart_default, (uint8_t*)&%%outtokens:0%%, 1);"
        }
    ]

    # Bridging streams connecting to _Connection_Token
    sourceStreams: [
        _Connection_Token >> NativeHostUartSend();
    ]

    destinationStreams: [
        NativeMcuUartRecv() >> _Connection_Token;
    ]
}
```

### Invariants:
1. **Unambiguous Target Assignment**:
    - Entities in `sourceEntities: [...]` belong strictly to `sourceFramework` and are compiled exclusively by the source domain's toolchain.
    - Entities in `destinationEntities: [...]` belong strictly to `destinationFramework` and are compiled exclusively by the destination domain's toolchain.
2. **PlatformModule Invocation**:
    - `platformModule` represents a native function that strictly accepts main inputs (`inputs: [...]`) and produces main outputs (`outputs: [...]`). It does not take property ports.
    - Connection parameters (e.g. `.port`) parameterize constants (`constant HostPort { value: .port }`) or substituted templates.
3. **Automated AST Splicing**:
    - `StrideSystem::generateDomainConnections` sets the appropriate `"domain"` and `"framework"` properties on each entity and splices them into the compilation AST, allowing downstream builders (`stridejit` or `stridec`) to compile them transparently.

---

## 7. Standard Platform Domains

`PlatformDomain` means something different on each framework (AVR's tick, the desktop's audio, the JIT's one-shot root). Programs that want to run on several platforms use the standard aliases instead. Each has one meaning; a framework defines only the ones it supports, and using one it lacks is a compile error.

| Alias | Meaning | AVR | JIT |
|---|---|---|---|
| `PlatformLoopDomain` | Runs again as soon as it finishes, like Arduino `loop()`. `Sleep` suspends only this domain. | `AvrLoopDomain` | `JitLoopDomain` |
| `PlatformTimerDomain` | Runs periodically from a timer at `PlatformTimerRate` | `AvrTickDomain` | `JitTimerDomain` |
| `PlatformTimerRate` | The timer's rate in Hz | `1000` | `1000` |
| `PlatformAudioDomain` | Runs once per audio sample at `PlatformAudioRate` | — | `JitAudioDomain` |

`RootDomain` already means "runs once at startup" everywhere and needs no alias. A `domain:`, `parentDomain:` or `Sleep(domain:)` naming an alias is replaced by the domain it stands for before anything else is resolved.

### 7.1 How domains run relative to each other

Every `_domainDefinition` can declare `execution`, one of the constants of `_DomainExecution` (`none` means cooperative):

| Value | Meaning |
|---|---|
| `ExecutionCooperative` | Runs on the same thread as its siblings, between their process calls. A long computation in a sibling delays it. |
| `ExecutionInterrupt` | Runs from an interrupt handler and preempts cooperative domains. |
| `ExecutionThread` | Runs on its own OS thread or core; shared signals go through `synchronization` declarations. |
| `ExecutionInline` | Runs inside its parent's process call (`preSubdomains`, `postSubdomains`). |
| `ExecutionHardware` | Runs without the CPU: an FPGA clock domain, a PIO state machine, a DMA stream. |

On AVR both standard domains are cooperative: `main()` runs every pending timer tick, then one loop iteration. A loop iteration that computes for 20 ms makes the timer run 20 ticks late, back to back; past 255 pending ticks, ticks are lost. Keep loop iterations short and use `Sleep` rather than busy waits.

### 7.2 Sleep

A domain declares how it sleeps with `sleepModule` (a platformModule taking a tick count) and `sleepRate` (ticks per second). `Sleep(domain: D)` calls `D`'s `sleepModule`. On AVR, `Sleep(domain: PlatformLoopDomain)` suspends only the loop: `main()` skips it until the time has passed and keeps running timer ticks meanwhile. A domain without `sleepModule` keeps counting ticks (a subdomain) or blocks.

---

## 8. Future Work

### 8.1 Moving the AVR Cooperative Scheduler into Stride
Currently, `main()` in `frameworks/AVR/1.0/runtime/stride_avr_runtime.c` hardcodes the cooperative schedule—draining `s_pendingTicks` for `AvrTickDomain_process()` first and then running one `AvrLoopDomain_process()` iteration—using `__attribute__((weak))` symbol checks to skip unused domains.

Moving this scheduler into Stride requires:
- **Stride-scheduled loop entry point:** Either `main()` performs hardware setup (`serial_start()`, `tick_start()`, `sei()`, `RootDomain_init/process`) and calls a generated `AvrScheduler_process()` inside `for (;;)`, or `RootDomain` generates the loop itself.
- **Atomic tick-drain & sleep primitives:** `platformModule`s to atomically snapshot and clear `s_pendingTicks` under `cli()`/`SREG` and to query/update `s_avrTickSleepTicks` and `s_loopSleeping`/`s_loopWakeMillis` (or represent sleep state as Stride signals).
- **Catch-up loop (`while (ticks--)`):** `preSubdomains`/`postSubdomains` currently invoke each child once per parent tick; preserving tick catch-up when a loop iteration exceeds 1 ms requires a Stride `loop`/multi-invocation construct around `AvrTickDomain` (or a small C/IR helper).
- **Compile-time dead-domain elimination:** With `activeDomains` filtering in `StrideGenerator::compile`, the scheduler can list `[AvrTickDomain, AvrLoopDomain]` and omit unused domains at compile time.
- **Performance trade-offs (ATmega328P @ 16 MHz, 2 KB SRAM):** Eliminating runtime weak-symbol null checks can save cycles and enable inlining, whereas cross-TU `platformModule` calls (without LTO) and Stride wrapper status/stack frames add a small overhead (~10–30 cycles / ~1–2 µs per loop iteration under `-Os` if kept tight).

### 8.2 Static Jitter / WCET Analysis for Cooperative AVR Domains
Because 8-bit AVR MCUs have no caches or branch predictors, instruction cycle counts are deterministic. Walking the CFG of `AvrLoopDomain_process()` and `AvrTickDomain_process()` in the AVR LLVM IR or machine code to compute best-case (BCET) and worst-case (WCET) cycle bounds—compared against the 1 ms tick budget (16,000 cycles at 16 MHz)—can provide a compile-time indicator of cooperative tick jitter and potential tick overruns.
