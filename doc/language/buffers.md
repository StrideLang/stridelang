# Buffer Entities & Window Framing

A `buffer` is a fixed-capacity ring store that accumulates an incoming sample-by-sample data stream over time and surfaces the collected window as a contiguous frame.

A buffer differs from other Stride storage entities:
- A `signal` holds an atomic value; each write overwrites the prior value.
- A `table` is a static, explicitly indexed fixed-size array.
- A `buffer` accumulates streamed samples sequentially. As new samples arrive, the oldest samples are retired once capacity is reached.

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
| **`mode`** | Operational ring buffering mode (`Buffer::ModeSliding`). | `Buffer::ModeSliding` |
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
> **Unit Input Size Invariant:**
> On the input side, a buffer always accepts exactly **one sample per write** (input port size = 1), regardless of capacity. Consecutive streaming operations progressively accumulate into the ring buffer.

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
