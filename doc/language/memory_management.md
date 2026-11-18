# Memory Management, Storage Entities & Formal Models

This document analyzes Stride's static memory and storage model—including `constant`, `signal`, `buffer`, `string`, and domain/module state—compares its capabilities and limitations against other languages, and maps Stride onto formal models of memory management, representation, and access.

---

## 1. Stride's Memory & Storage Primitives

Stride is designed around **deterministic, zero-heap, statically bounded memory**. Every entity's footprint is known at compile time and lives in one of four static storage lifetimes:

| Primitive | Mutability & Lifetime | Access Pattern | Typical Use Cases |
| :--- | :--- | :--- | :--- |
| **`constant`** | Immutable after compile time or `<Domain>_load` (`onLoad`) | Scalar or fixed array (`T[i]`) | Lookup tables, window shapes, topology maps (`constant_fill.stride`) |
| **`signal`** | Local stack/register or inferred persistent ($z^{-1}$) in `__state` | Scalar or fixed array (`S[i]`) | Filter states (`Section`), delay lines (`FractionalDelay::Line`), wavetables (`TableOscillator`), scratch |
| **`buffer`** | Rolling ring, FIFO, overlap-add, or table storage (`mode: Buffer::Mode*`) | Sequential push (`x >> B`), frame peek (`B.frame`, `B.ready`), or random-access (`Buffer::ModeTable`) | FFT windows, sliding/hopped framing, DMA ping-pong, cross-domain double-buffered blocks (`buffers.md`) |
| **`string`** | Fixed `maxSize` char buffer (`<name>__chars`) + caller-stack scratch | Value/view (`StrideStringView`) | Bounded text formatting/parsing without heap allocation |

### Table Fills, `Buffer::ModeTable`, `RateAdapter`, and `onReset`

Instead of a separate `table` entity type, Stride composes table storage, rate-driven generation, and initialization lifecycle from orthogonal primitives:

1. **Storage (`constant`, `signal[N]`, `buffer { mode: Buffer::ModeTable }`)**:
   - **`constant Name[N] { type: ... }`** holds immutable lookup tables filled at compile time (`evaluateConstantFills`) or once per program in `<Domain>_load` (`onLoad:`).
   - **`signal Name[N]`** holds mutable or module-local random-access arrays in `__state` whenever persistence is inferred from use (e.g., `DelayTable` in `FractionalDelay::Line` written elementwise and read across ticks, or `SineTable` in `TableOscillator` populated conditionally by a `reaction`).
   - **`buffer Name { capacity: N mode: Buffer::ModeTable }`** declares a block store explicitly configured for random-access / block-fill usage, keeping the door open for compiler/connection heuristics (such as cross-domain double buffering).
2. **Filling with `loop` + `iterator` + `RateAdapter(rate: ...)`**:
   - Output-sized `loop`s (`Output[OutputPort.size]` with `iterator Out { entity: Output }`) generate `N` elements in place and reset internal module state on every invocation automatically (because `loop`s are stateless across calls).
   - When a stream inside a `loop` calls a rate-dependent generator such as `Oscillator(frequency: 1.0)`, inserting **`RateAdapter(rate: OutputPort.size)`** sets the upstream input port rate to `N` without altering the outer domain's physical rate.
3. **Lifecycle Ordering (`onLoad` vs. `onInit` vs. `onReset`)**:
   - **`onLoad:`** (`<Domain>_load`) runs once per program for `constant` fills (and pure fills fold at compile time).
   - **`onInit:`** (`<Domain>_init`) runs per instance *before* defaults and state machines are reset (for external/host setup) and ends by calling `<Domain>_reset`. Re-calling `_init` requires `_cleanup` first.
   - **`onReset:`** (`<Domain>_reset`) runs **after** defaults, state machines, and buffer counters are reset, so runtime fills into `signal` arrays and `buffer`s persist and can be safely re-invoked via `<Domain>_reset` while initialized.
   - Module-local tables whose initialization runs lazily on first tick (like `TableOscillator`) use a guard switch (`TableNeedsInit { default: on }`) driving an internal `InitTable` `reaction`; because `TableNeedsInit` is read before the `reaction` clears it, both `TableNeedsInit` and the array filled inside `InitTable` are automatically inferred persistent.

---

## 2. Capabilities in Other Languages Not Possible in Stride

Some omissions in Stride are deliberate trade-offs for hard real-time determinism and bare-metal portability; others represent expressive gaps even within static-memory programming:

1. **Dynamic / Runtime-Sized Allocation** *(Rust `Vec`/arenas, C++/Zig allocators, SuperCollider, Python)*:
   - Recording buffers whose capacity is chosen at runtime, growable data structures (trees, hash maps, dynamic queues), and unbounded dynamic polyphony are intentionally excluded. Every `constant[N]`, `buffer`, `signal[N]`, `string` (`maxSize`), and module instance has a statically fixed footprint.
2. **First-Class Views, Runtime Sub-Slices, and Handle Swapping** *(Rust `&mut [T]`/`&[T]`, C++ `std::span`, Zig slices)*:
   - Stride supports compile-time slices (`Arr[2:4]`) and zero-copy `Buf.frame` views into reactions and loops, but cannot create first-class runtime-offset slices (`Arr[offset : offset + len]`), store references to arrays/buffers inside composite types, or swap handles/pointers in $O(1)$ for lock-free double-buffering without copying.
3. **Wait-Free Cross-Domain Ownership Transfer** *(Rust `rtrb`/channels, C++ atomic pointer swap / RCU)*:
   - If a slower domain (`ControlDomain`) refills a `buffer` or `signal[N]` on `reset` while a faster domain (`AudioDomain`) reads `Buf[i]` via shared memory, Stride's domain model does not yet enforce exclusive write ownership or atomic front/back handle swapping to prevent torn reads mid-refill.
4. **Multi-Dimensional Arrays / Tensors & Strided Views** *(Halide, Julia, Dex, Futhark)*:
   - Stride only supports 1D arrays (`Arr[N]`). Matrices, spectrograms, or 2D/3D grids must be manually linearized (`row * width + col`).
5. **Explicit Physical Memory Placement & Layout Control** *(Halide `store_in` / schedules, CUDA/Metal memory spaces, embedded C/Rust linker sections)*:
   - While `constant` maps cleanly to read-only storage (Flash/ROM) and platform bindings are intended to handle placement (`buffers_and_frequency_domain.md`), Stride programs cannot yet specify AoS vs. SoA struct layout, SRAM vs. TCM vs. SDRAM bank placement, or cache-line/DMA alignment per `signal` array or `buffer`.
6. **Compile-Time Dependent Bounds Proofs** *(Dex `Fin N`, dependent type systems)*:
   - Dynamic indexing (`DelayTable[ReadIndex]`) relies on runtime wrapping or clamping (`Wrap(...)`) rather than type-level proofs that indices stay within `[0, size - 1]`.

---

## 3. Formal Models of Memory Management, Representation & Access

Stride's design sits at the intersection of four well-established formal frameworks in programming language and compiler theory:

### 3.1 Region-Based Memory Management
*Reference models: Tofte–Talpin Region Calculus, Cyclone, SPARK / MISRA static regions.*

In region-based memory management, all objects live in statically scoped **regions** whose lifetimes are tied to execution scopes rather than individual `malloc`/`free` calls or garbage collection.

**Comparison to Stride:**
Stride implicitly enforces a **4-tier static region hierarchy** without requiring user-written lifetime annotations:
1. **Program Read-Only Region**: `constant` declarations with compile-time or `onLoad` values (ROM/Flash-eligible).
2. **Domain Instance Region**: `<Domain>_state` structs allocated once per domain instance (`signal`, `buffer`, state machine state).
3. **Module Instance Region**: `<Module>_state` sub-structs nested inside the caller's `__state` (lifetime tied to the enclosing domain instance).
4. **Invocation Stack Region**: Non-persistent locals, `loop`/`reaction` stack allocas, and caller-stack string scratch (`takeScratch`).

This provides region safety, zero fragmentation, and predictable RAM usage, with the deliberate restriction that regions cannot grow dynamically.

### 3.2 Substructural / Linear Types & Ownership
*Reference models: Girard's Linear Logic, Clean uniqueness types, Rust ownership and borrowing.*

Substructural type systems track **ownership**, **exclusive mutation (`&mut`)**, and **shared reading (`&`)** to guarantee memory safety, prevent aliasing bugs, and rule out data races at compile time.

**Comparison to Stride:**
- Stride's `domain:` attribute and `CodeValidator` cross-domain checks act as a **coarse-grained domain ownership model**: every entity belongs to one domain, and cross-domain access is only permitted across domains sharing a common parent memory context or via an explicit `connection`.
- Unlike Rust's borrow checker, Stride does not track per-reference aliasing or move semantics (`move` of a `buffer` handle between domains), nor does it prevent concurrent cross-domain read/write races when a shared-memory `buffer`/`signal` array is mutated in place.

### 3.3 Synchronous Dataflow (SDF) & Synchronous Reactive Clocks
*Reference models: Lee & Messerschmitt SDF, Kahn Process Networks, Lustre / Scade, Faust (`rdtable` / `rwtable`).*

In SDF and synchronous reactive languages, computation nodes execute on discrete **clocks** (rates) and communicate over statically bounded delay edges ($z^{-k}$) and FIFOs. Because production and consumption rates are static, buffer capacities and execution schedules are resolved at compile time with guaranteed bounded memory.

**Comparison to Stride:**
This is Stride's closest formal ancestor:
- Multi-rate stream segments (`multirate_compilation.md`), persistent $z^{-1}$ module fields, `buffer` (`capacity`, `frameSize`, `hop`, `mode`), `constant` fills, and `RateAdapter` port-rate boundaries are all **statically bounded SDF storage and clocking constructs**.
- Stride extends pure SDF with event-driven `reaction`s, hierarchical `stateMachine`s, and bounded intra-tick `loop`s, and mirrors Faust's formal split between immutable precomputed tables (`rdtable` $\leftrightarrow$ `constant` arrays) and mutable random-access storage (`rwtable` $\leftrightarrow$ `signal` arrays and `buffer { mode: Buffer::ModeTable }`).

### 3.4 Separation of Algorithm, Schedule, and Storage Layout
*Reference models: Halide (`compute_at` / `store_at` / `store_in`), Polyhedral / MLIR Affine compilers, Dex / Futhark.*

These frameworks formally decouple *what* is computed (index-space mathematics) from *when* it runs (loop schedule), *where* it is stored (circular folding, scratchpad/SRAM vs. DRAM), and *how* indices map to memory (AoS/SoA, bounds).

**Comparison to Stride:**
- Stride decouples algorithmic dataflow (`streams:`) from execution schedule (`domain:`, `rate:`, `RateAdapter`, `onLoad`/`onReset`) and storage semantics (`constant` vs. `signal` vs. `buffer` modes).
- Instead of exposing low-level polyhedral schedule transformations directly to users, Stride packages the most common DSP storage schedules as `buffer` modes (`ModeSliding`, `ModeFifo`, `ModeOverlapAdd`, `ModeTable`) and `constant`/`signal` arrays that lower directly to flat LLVM arrays.

---

## 4. Summary Matrix

| Capability / Formal Property | Static Region Languages (SPARK / C) | Ownership / Borrowing (Rust) | Schedule / Polyhedral (Halide / Dex) | Synchronous DSP (Lustre / Faust) | Stride |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Zero-heap bounded RAM** | Yes | Optional | Static-shape subsets | Yes | **Yes** |
| **Static region lifetimes** | Manual / lexical | Borrow lifetimes (`'a`) | Scope-scheduled | Clock-bound | **Implicit 4-tier** |
| **Domain / thread ownership** | Manual | Type-enforced (`Send`/`Sync`) | Parallel schedules | Single/multi-clock | **Domain ownership + connections** |
| **First-class runtime slices / views** | Pointers / spans | `&[T]` / `&mut [T]` | Strided N-D views | Limited | **Static slices & `Buf.frame` only** |
| **Wait-free buffer handle swap** | Manual atomics | Linear ownership move | Double-buffer schedule | No | **Not yet** |
| **Declarative DSP ring/window framing** | Manual | Library crates | Circular storage folding | Delay / `rdtable`/`rwtable` | **First-class (`buffer` modes, `constant`, `signal`)** |
| **Physical memory / layout control** | Linker pragmas | `#[repr]` / sections | `store_in` / layout | Backend-dependent | **Platform bindings (planned)** |
