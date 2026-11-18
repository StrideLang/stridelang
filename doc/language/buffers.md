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
