# Buffer Entities & Window Framing

A `buffer` is a fixed-capacity ring store that accumulates an incoming sample-by-sample data stream over time and surfaces the collected window as a contiguous frame.

A buffer differs from other Stride storage entities:
- A `signal` (or `signal[N]`) holds an atomic value or fixed array; each write overwrites the addressed slot.
- A `constant[N]` holds an immutable lookup table populated at compile time or `<Domain>_load`.
- A `buffer` configures block-level storage semantics via `mode` (`Buffer::ModeSliding` for rolling window framing, `Buffer::ModeFifo` for queued transport, `Buffer::ModeOverlapAdd` for frame accumulation, or `Buffer::ModeTable` for random-access/block-filled tables).

---

## 1. Buffer Declaration

```stride
buffer AudioBuffer {
    type: Real
    capacity: 256
    hop: 64
    mode: Buffer::ModeSliding
    triggerMode: Buffer::TriggerPeriodic
    default: 0.0
}
```

### 1.1 Declaration Properties
| Property | Description | Default |
| :--- | :--- | :--- |
| **`type`** | Constituent data type (`Real`, `Int`, etc.). | `Real` |
| **`capacity`** | Total number of samples retained. Must be a positive integer literal. | *Required* |
| **`frameSize`** | Number of samples exposed in the output window. | Same as `capacity` |
| **`hop`** | Stride step (samples between consecutive triggers for periodic mode). | Same as `capacity` |
| **`triggerMode`** | Window completion reporting condition (§3). | `Buffer::TriggerFull` |
| **`mode`** | Operational buffering mode (`Buffer::ModeSliding`, `Buffer::ModeFifo`, `Buffer::ModeOverlapAdd`, `Buffer::ModeTable`). | `Buffer::ModeSliding` |
| **`default`** | Initial value for all buffer slots. | Zero of `type` |
| **`reset`** | Trigger/switch that clears the buffer to initial defaults when pulsed. | `none` |

---

## 2. Streaming Into a Buffer (Writing)

A buffer is populated by streaming scalar values into it:

```stride
AudioIn >> AudioBuffer;
```

Each write performs the following steps:
1. Converts the incoming value to the buffer's `type` if necessary.
2. Writes the value into the slot indicated by the current write pointer.
3. Advances the write pointer (wrapping cyclically from `capacity - 1` back to `0`).
4. Increments the fill counter (clamped at `capacity`).
5. Evaluates the configured trigger mode condition.

> [!NOTE]
> **Input Size by Mode:**
> For ring-buffer modes (`Buffer::ModeSliding`, `Buffer::ModeFifo`, `Buffer::ModeOverlapAdd`), a buffer accepts **one sample per write** (input port size = 1), progressively accumulating into the ring. For `Buffer::ModeTable`, the buffer accepts a **full block of `capacity` elements** (or individual indexed slots via `TableBuf[i]`).

---

## 3. Triggering: The `ready` Switch

Every buffer entity automatically exposes a `ready` switch property. The compiler evaluates `ready` on each write:

| `triggerMode` | `ready` Becomes `on` When: | Triggers Per `capacity` Writes |
| :--- | :--- | :--- |
| **`Buffer::TriggerFull`** | The write fills the final slot of the buffer. | 1 |
| **`Buffer::TriggerHalf`** | The write reaches the midpoint or the end of the buffer. | 2 |
| **`Buffer::TriggerQuarter`**| The write reaches any 25% boundary in the buffer. | 4 |
| **`Buffer::TriggerPeriodic`**| Exactly `hop` samples have arrived since the prior trigger. | `capacity / hop` |

On any write that does not complete a window, `ready` evaluates to `off`.

---

## 4. Reading Frames (Peeking)

Streaming directly from `BufferName.frame` accesses the active window:
- **`BufferName.frame`**: Contains the most recent `frameSize` samples, ordered from oldest (`[0]`) to newest (`[frameSize - 1]`).
- Reading a frame is a **peek** operation: it does not consume, clear, or modify buffer state.

### 4.1 Zero-Copy Reaction Invocation
The standard idiomatic pattern in Stride connects `ready` to a reaction's trigger and passes the buffer as a property port:

```stride
reaction ComputeFFT {
    ports: [
        propertyInputPort InBufPort { name: "buffer" entity: InBuf }
        mainOutputPort OutPort { entity: Magnitude }
    ]
    entities: [
        signal InBuf[InBufPort.size] { type: Real }
        signal Magnitude { type: Real }
    ]
    streams: [
        # InBuf provides direct zero-copy pointer access to the frame
        InBuf >> FFTModule() >> Magnitude;
    ]
}

# Pipeline execution:
AudioIn >> AudioBuffer;
AudioBuffer.ready >> ComputeFFT(buffer: AudioBuffer) >> SpectralMagnitude;
```

In the example above, `InBufPort.size` resolves to `AudioBuffer.frameSize` at compile time, passing the underlying pointer directly with zero heap or stack copy overhead.

---

## 5. Buffer Modes (Implemented and Planned)

| Mode | Status | Storage Layout | Behavior |
| :--- | :--- | :--- | :--- |
| **`Buffer::ModeSliding`** | **Implemented** | `1 x capacity` (or `2 x capacity` mirrored when frames wrap) | Accumulates scalar samples into a rolling window and exposes the newest `frameSize` samples as a contiguous `.frame` peek. |
| **`Buffer::ModeTable`** | **Implemented** | `1 x capacity` (no mirroring) | Random-access block store without ring-pointer advancement. Accepts full-block writes (`capacity` elements) and direct element reads/writes (`Buf[i]`). |
| **`Buffer::ModeFifo`** | **Planned** | Ring queue (`capacity`) | Queued FIFO transport where reading drains samples rather than peeking. Currently rejected at compile time. |
| **`Buffer::ModeOverlapAdd`** | **Planned** | `frameSize + hop` | Accumulates overlapping output frames (e.g., IFFT synthesis) and drains samples one by one. Currently rejected at compile time. |

### 5.1 Random-Access Table Example (`Buffer::ModeTable`)

```stride
buffer TableBuf {
    type: Int
    capacity: 4
    mode: Buffer::ModeTable
    domain: AudioDomain
}

[10, 20, 30, 40] >> TableBuf;
99 >> TableBuf[2];
TableBuf[1] >> Out;
```

---

## 6. Formal Foundations of Buffer I/O

For single-domain (synchronous and multirate) computation, the four buffer modes partition storage semantics along two orthogonal axes—**how writes arrive** (sample-by-sample vs. frame/random-access) and **how reads consume data** (non-destructive window peek, destructive queue drain, or additive frame accumulation):

| Write $\backslash$ Read | Non-Destructive Window / Random Read | Destructive Drain ($1$ or $K$ items) | Additive Frame Accumulation + Sample Drain |
| :--- | :--- | :--- | :--- |
| **Sample-by-sample ($1 \to N$)** | **`ModeSliding`** (STFT analysis, FIR history, RMS/pitch windows, delay lines) | **`ModeFifo`** (decimation queues, packetization, bursty event queues) | — |
| **Frame / random-access ($N \to N$ or $i \to 1$)** | **`ModeTable`** (wavetables, LUTs, state/scratch arrays, block-filled coefficients) | **`ModeFifo`** (generalized to block push / scalar pop) | **`ModeOverlapAdd`** (ISTFT / overlap-add synthesis, partitioned convolution tail accumulation) |

### 6.1 Relationship to Classical Dataflow & DSP Formalisms

1. **SDF & CSDF (Synchronous and Cyclo-Static Dataflow — Lee & Messerschmitt 1987; Bilsen et al. 1996)**
   - In SDF, every channel $e = (u, v)$ has static production rate $p(e) \in \mathbb{N}^+$, consumption rate $c(e) \in \mathbb{N}^+$, and initial token delay $d(e) \in \mathbb{N}$; CSDF extends rates to periodic phase sequences $[p_1, \dots, p_k]$.
   - Stride's `ModeSliding` (`hop` + `triggerMode`) and `ModeOverlapAdd` are direct specializations of CSDF channels: a buffer with `hop = H` triggers its downstream reaction once every $H$ upstream ticks (a $[0, \dots, 0, 1]$ CSDF schedule). Because `capacity`, `frameSize`, and `hop` are compile-time constants (`BufferSpec`), Stride preserves SDF's **zero-heap, statically bounded memory guarantee**.

2. **StreamIt Tapes (`push` / `pop` / `peek` — Thies, Karczmarek & Amarasinghe 2002)**
   - StreamIt formalized three per-firing rates on filter tapes to support sliding-window DSP: $\text{push}(u)$ items appended, $\text{pop}(v)$ items consumed/advanced (`hop`), and $\text{peek}(v) \ge \text{pop}(v)$ items inspected without removal (`frameSize`).
   - Stride's `buffer` maps directly onto StreamIt's tape primitives:
     - `ModeSliding`: $\text{push} = 1$, $\text{pop} = \text{hop}$, $\text{peek} = \text{frameSize}$ (`Buf.frame`).
     - `ModeFifo`: $\text{push} = 1$ (or block), $\text{pop} = \text{peek} = \text{frameSize}$ (destructive read).
   - **Contiguity Invariant over StreamIt**: Whereas StreamIt circular tapes require either modulo indexing inside kernels or copy-on-wrap unless unrolled, Stride's compiler-managed mirroring (`frameSize > hop || capacity % hop != 0` allocates $2 \times \text{capacity}$ and mirrors writes) guarantees that `Buf.frame` is **always a contiguous linear pointer** (`ptr` in LLVM IR) suitable for zero-copy FFT and SIMD kernels.

3. **Faust ($z^{-k}$ Delay Lines & `rdtable` / `rwtable` — Orlarey et al.)**
   - Faust separates bounded time-shift rings ($x(t - k)$) from random-access tables (`rdtable` for immutable tables, `rwtable` for mutable tables).
   - Stride matches this formal split (`constant[N]` $\leftrightarrow$ `rdtable`, `ModeTable` / `signal[N]` $\leftrightarrow$ `rwtable`, `ModeSliding` $\leftrightarrow$ windowed delay line) while adding first-class **block/window framing (`frameSize`, `hop`, `ModeOverlapAdd`)** for multirate frequency-domain pipelines.

4. **Halide & Polyhedral Storage Folding (`store_at` / `compute_at`)**
   - Rather than requiring external schedule pragmas to fold an unbounded time dimension $t$ into circular or linear memory, Stride packages the four canonical DSP storage schedules as `Buffer::Mode*` on the `buffer` entity while keeping stream expressions (`AudioIn >> AudioBuffer`, `AudioBuffer.frame >> FFT()`) schedule-independent.

---

## 7. Coverage, Idioms, and Future Work

### 7.1 Composing Overlap-Save (OLS) Without a Separate Mode
Unlike Overlap-Add (`ModeOverlapAdd`), Overlap-Save discards the first $M - 1$ aliased samples of each $N$-point IFFT output frame and concatenates the remaining $L = N - (M - 1)$ valid samples. This requires no additional buffer mode:
- Analysis uses `ModeSliding` (`frameSize: N, hop: L`).
- Synthesis slices the valid tail of the IFFT output (`OutFrame[M - 1 : N - 1]`) into a `ModeFifo` (or upsampled stream).

### 7.2 Planned Intra-Domain Extensions
1. **`Buffer::ModeOverlapAdd` Implementation**:
   - **Port sizing**: Input port size equals `frameSize` (accepting a full synthesis frame per write, like `ModeTable`), while the output side drains `1` sample per consumer tick (or `hop` samples per block).
   - **Accumulation semantics**: Incoming frames add into the active `frameSize` window starting at the write head (`+=`), advance the write head by `hop`, and zero slots as they are drained by the read head.
2. **`Buffer::ModeFifo` Implementation**:
   - **Static vs. event-driven queues**: When producer and consumer rates are statically matched via multirate/`RateAdapter`, `ModeFifo` acts as a static SDF queue that cannot underflow or overflow. When written or read conditionally inside a `reaction`, `ModeFifo` should expose `.count` / `.ready` and define deterministic underflow/overflow behavior (gating the consumer via `.ready` vs. padding with `default`).
3. **Relative Delay-Tap Views (`Buf.tap[k]`)**:
   - Exposing indexed tap reads relative to the current write head on `ModeSliding` buffers (`Buf.tap[delay]`) will allow multi-tap and fractional delay lines (such as `FractionalDelay`) to use `ModeSliding` directly without manual modulo index arithmetic.
4. **Cross-Domain Buffering (Separate Specification)**:
   - These four modes intentionally specify **intra-domain** storage and framing only. Asynchronous cross-domain transport (lock-free SPSC rings, triple-buffered latest-frame exchange, and rate-drift policies) is orthogonal and handled at domain boundaries.
