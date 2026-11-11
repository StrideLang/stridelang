# Callables: Modules, Reactions, and Loops

In Stride, executable code blocks called from streams are organized into three primary **callable** categories: **`module`**, **`reaction`**, and **`loop`**.

```stride
Input >> Smooth(amount: 0.5) >> Output;
```

---

## 1. Callable Categories Overview

| Category | Execution Trigger | State Between Invocations | Stream Evaluation |
| :--- | :--- | :--- | :--- |
| **`module`** | Evaluated on every call from its stream | **Stateful** (maintains values in entities read before written) | Evaluated once per call |
| **`reaction`** | Evaluated when its trigger switch/event is `on` | **Stateless** (resets to defaults on each execution) | Evaluated once per trigger event |
| **`loop`** | Evaluated on call from its stream | **Stateless** across external calls | Repeated until `until` / `terminateWhen` is `on` |

---

## 2. Declaration Syntax Styles

Stride supports two syntax styles for defining callables:
1. **Compact Stream-Signature Header Syntax (Recommended)**: Declares external ports directly in the header using stream notation (`In >> Name(...) >> Out`), with an optional unified `code: [...]` body.
2. **Canonical Explicit Syntax**: Declares separate `ports: [...]`, `entities: [...]`, and `streams: [...]` properties.

> [!IMPORTANT]
> The two styles are mutually exclusive per callable declaration:
> - If header ports are used (`In >> Name(...) >> Out`), do not specify `ports: [...]`.
> - If `code: [...]` is used, do not specify separate `entities: [...]` or `streams: [...]`.

### 2.1 Compact Header Syntax
```stride
module Input >> Scale(gain: signal Gain { default: 1.0 }) >> Output {
    code: [
        Input * Gain >> Output;
    ]
}

# Invoking in a stream:
AudioIn >> Scale(gain: 2.0) >> AudioOut;
```

### 2.2 Canonical Explicit Syntax
```stride
module Scale {
    ports: [
        mainInputPort InputPort { entity: Input }
        mainOutputPort OutputPort { entity: Output }
        propertyInputPort GainPort { name: "gain" entity: Gain }
    ]
    entities: [
        signal Input { type: Real }
        signal Output { type: Real }
        signal Gain { type: Real default: 1.0 }
    ]
    streams: [
        Input * Gain >> Output;
    ]
}
```

---

## 3. Port Models & Port Attributes

Ports connect a callable to its call site in a stream.

### 3.1 Port Types
- **`mainInputPort`**: Connects to the data source immediately preceding the call: `Input >> Callable()`. Supported in `module` and `loop`. Reactions do not have main inputs; they are triggered by events (§5).
- **`mainOutputPort`**: Connects to the downstream destination: `Callable() >> Output`.
- **`propertyInputPort`**: Named parameters passed in parentheses: `Callable(cutoff: 1000.0)`. Properties match by name, so parameter ordering at the call site does not matter.
- **`propertyOutputPort`**: Named output properties written back by the callable.

### 3.2 Dynamic Port Attributes
The compiler automatically infers read-only properties from connected streams: `size`, `rate`, `domain`, and `type`. Internal entities can adapt their dimensions and types dynamically based on call connections:

```stride
signal InBuffer[InputPort.size] { type: Int }    # sized to incoming input stream
signal OutBuffer[OutputPort.size] { type: Int }  # sized to downstream sink
signal FilterOut { domain: OutputPort.domain }   # adopts the caller's domain
```

---

## 4. Modules: Stateful Stream Processors

A `module` runs once each time its invocation in a stream is evaluated. Each invocation site maintains its own independent state.

### 4.1 State Retention
An entity that a module reads *before* writing preserves its value from one invocation to the next:

```stride
module Input >> Accumulator(reset: switch Reset { default: off }) >> Output {
    code: [
        signal Acc { default: 0 type: Int reset: Reset persistent: on }
        Acc + Input >> Acc;   # Acc is read before written: retained as persistent state
        Acc >> Output;
    ]
}
```

- **Stateless Allocas**: Entities that are always written before being read are treated as local stack variables or registers, incurring zero persistent memory overhead.
- **Explicit Resets**: Entities configured with `reset: TriggerEntity` reset to their `default` value whenever the trigger condition evaluates to `on`.

---

## 5. Reactions: Event-Driven Computation

A `reaction` executes in response to a trigger event. It does not maintain state between invocations; all internal entities start from their configured `default` values on each run.

### 5.1 Trigger-Driven Execution
Reactions do not declare a main input port. The upstream operand in a stream serves as the execution trigger:

```stride
trigger SampleReady {}
switch EnableProcess { default: off }

SampleReady >> ProcessSample(value: SensorIn);   # runs once when SampleReady is pulsed
EnableProcess >> ProcessSample(value: SensorIn); # switch converts to trigger: runs when on
on >> ProcessAlways(value: SensorIn);            # runs unconditionally every tick
```

- When a reaction's trigger evaluates to `off`, the reaction's body is skipped, and its outputs retain their prior values.
- Input data values are passed through named property ports (`(value: SensorIn)`).
- Declaring a `mainInputPort` on a `reaction` is a compile-time error (`ReactionMainInput`).

---

## 6. Loops: Repetitive Computation

A `loop` executes its streams repetitively within a single tick until a termination condition is met. Loops are stateless across external calls.

### 6.1 Short-Circuit Mode (`until:` / `terminateWhen:`)
Loops repeat until their termination switch evaluates to `on`:

```stride
loop Guess >> SqrtSolver(target: signal Target { default: 2.0 }) >> Result {
    until: Converged
    maxIterations: 16
    code: [
        switch Converged { default: off }
        signal Next { default: 0.0 }
        signal Diff { default: 0.0 }

        0.5 * (Guess + Target / Guess) >> Next;
        Next - Guess >> Abs() >> Diff;
        Next >> Guess >> Result;
        [Diff, 0.00001] >> Less() >> Converged;
    ]
}
```

### 6.2 Loops Sized by Input or Output
Loops operating over arrays dynamically bind their iteration limits based on port sizes:

- **Driven by Input (`InputPort.size`):**
  ```stride
  [Val1, Val2, Val3] >> SumLoop() >> Total;     # InputPort.size is 3
  ArrayData[0:63]    >> SumLoop() >> Total;     # InputPort.size is 64
  ```

- **Driven by Output (`OutputPort.size`):**
  The loop determines its boundary from the destination size and fills elements in place:
  ```stride
  constant Table[512] { type: Int }
  GenerateTable() >> Table;                     # OutputPort.size is 512
  ```

### 6.3 Filling Sized Constants
A `constant` declared without an explicit value can be filled once at startup by an output-driven loop or action:

```stride
constant SineTable[1024] { type: Real }

# Populated in domain setup or initialization stream:
GenerateSineTable() >> SineTable;
```

A subsequent stream attempting to write to the same constant produces a compile-time error (`ConstantWrittenTwice`).

---

## 7. Invocation Conventions & Vectorized Broadcasting

Stride callables support scalar invocation, array inputs, and automatic parallel broadcasting:

```stride
# Standard 1:1 stream invocation
In >> Filter(cutoff: 1000.0) >> Out;

# Array inputs passed into a callable
[ChannelA, ChannelB] >> DualFilter() >> Out;

# Parallel broadcast across array slices
AudioChannels[0:7] >> Filter[]() >> ProcessedChannels[0:7];
```

When an array is passed into a callable expecting a scalar, the compiler automatically broadcasts the call, vectorizing execution across all elements.
