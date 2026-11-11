# Declarative State Machines in Stride

Stride provides first-class support for declarative **State Machines**, combining hierarchical statecharts (UML / SCXML semantics) with stream-driven signal processing pipelines.

---

## 1. Core Architecture & Philosophy

- **Declarative Representation**: State machines declare structural states, transitions, guards, and lifecycle hooks rather than hardcoded imperative logic. The compiler lowers them into compact enum/switch dispatchers for bare-metal MCUs or optimized jump tables for high-performance JIT runtimes.
- **Stream-Driven Actions**: Lifecycle hooks (`onEntry`, `onProcess`, `onExit`) and transition hooks (`onTransition`) execute lists of Stride stream expressions (`streams: [...]`). Continuous computation belongs in `onProcess`, eliminating polling loops.

```stride
state SystemController {
    states: [Standby, Operating, Fault]
    initialState: Standby
}
```

---

## 2. State Lifecycle Hooks

Every state can define three lifecycle stream hooks:

| Hook | Execution Behavior | Hierarchical Order |
| :--- | :--- | :--- |
| **`onEntry`** | Executed once immediately when the state becomes active. | Top-Down (parent before child) |
| **`onProcess`**| Executed continuously on each domain tick while the state remains active. | Top-Down |
| **`onExit`** | Executed once when the state is left due to a transition. | Bottom-Up (child before parent) |

```stride
state Operating {
    onEntry: [
        EnablePowerRail();
        ResetSampleCounter();
    ]
    onProcess: [
        SensorIn >> Filter() >> ControlOutput;
    ]
    onExit: [
        DisablePowerRail();
    ]
}
```

---

## 3. Transitions & Guards

Transitions dictate how execution moves between states. They are evaluated sequentially in definition order.

### 3.1 Transition Properties
- **`targetState`** (or `newState`): Destination state name. If omitted or set to `none`, the transition is an *internal transition* (executing actions without exiting or re-entering the state).
- **`guard`**: A switch, signal condition, or expression (e.g. `[ ErrorCode != 0 ]`). Evaluated to determine whether the transition fires.
- **`triggerOnGuard`**: Switch (`on` / `off`, default `off`). When enabled, the transition triggers automatically whenever its guard evaluates to true.
- **`onTransition`**: Action streams executed during transition traversal (after exiting old states and before entering target states).

```stride
transition {
    guard: [ Temperature > 85.0 ]
    targetState: OverheatFault
    onTransition: [ LogFault(code: 0x42); ]
}
```

---

## 4. Discrete Tick Semantics & Transition Timing

In Stride domains, processing occurs across discrete clock ticks (or audio/DSP sample boundaries). Within each domain tick, state machines follow a deterministic phase order:

1. **Initial Entry Tick (Domain Startup):**
   - The initial state hierarchy's `onEntry` streams execute top-down.
   - `onProcess` streams of the initial state execute immediately within this initial tick.

2. **Transition Tick (When a Transition Fires):**
   - Transition guards are evaluated (including inherited ancestor transitions).
   - If a guard evaluates to true:
     1. Exiting state `onExit` streams execute bottom-up.
     2. History state variables are saved for composite states with `resumeLastState: on`.
     3. Transition action streams (`onTransition`) execute.
     4. Target state is marked active (resolving initial leaf or restored history sub-states).
     5. Target state `onEntry` streams execute top-down in the **same tick**.
     6. The tick completes.

3. **Steady-State Processing Tick (No Transition):**
   - Transition guards are checked (none trigger).
   - `onProcess` streams of the active state(s) execute.

> [!NOTE]
> **Execution Separation:**
> `onEntry` executes in the **same tick** that a transition fires to establish immediate post-transition invariants. Continuous signal processing in `onProcess` begins on the **subsequent tick**, preventing double-sample calculations within a single time slice.

---

## 5. Hierarchical, Parallel, and History Formalizations

### 5.1 Hierarchical States & Preemption
Sub-states inherit transitions declared on all parent ancestor states. Sub-state transitions take precedence over parent transitions:

```stride
state Active {
    states: [Idle, Processing, Error]
    initialState: Idle

    # Preemption: pressing E-Stop exits any child state directly to Shutdown
    transitions: [
        transition {
            guard: EmergencyStop
            targetState: Shutdown
        }
    ]
}
```

### 5.2 Orthogonal Regions (`isParallel: on`)
When a parent state specifies `isParallel: on`, all states in its `states:` list become active simultaneously:
- Each parallel region executes its `onProcess` streams concurrently each tick.
- Exiting a parallel parent state executes `onExit` hooks across all parallel sub-states before exiting the parent.

### 5.3 History States (`resumeLastState: on`)
Instructs a composite state to record its active child sub-state when exited. Upon re-entry, the hierarchy restores the saved child state rather than resetting to `initialState`:

```stride
state OperatingMode {
    states: [Manual, AutoTrack, Calibration]
    initialState: Manual
    resumeLastState: on  # Remembers whether we were in Calibration or AutoTrack
}
```

### 5.4 Completion States (`isFinal: on` & `onDone`)
- Marking a leaf state with `isFinal: on` indicates successful completion of that composite state.
- Ancestor states can declare `onDone: NextState` transitions that automatically fire when all active child regions reach final states.

---

## 6. Comprehensive Example: Multi-Mode Controller

```stride
signal PowerPressed { default: 0 type: Int }
signal BatteryEmpty { default: 0 type: Int }
signal AudioReady   { default: 0 type: Int }

state DeviceController {
    states: [PowerOff, Booting, Running]
    initialState: PowerOff
}

state PowerOff {
    onEntry: [ EnterLowPowerSleep(); ]
    transitions: [
        transition {
            guard: PowerPressed
            targetState: Booting
        }
    ]
}

state Booting {
    onEntry: [ InitializeHardware(); ]
    transitions: [
        transition {
            guard: AudioReady
            targetState: Running
        }
    ]
}

state Running {
    states: [Playback, Paused]
    initialState: Playback

    # Global power cut preempts sub-states
    transitions: [
        transition {
            guard: BatteryEmpty
            targetState: PowerOff
        }
    ]
}

state Playback {
    onProcess: [ AudioSource >> AudioSink; ]
    transitions: [
        transition {
            guard: PowerPressed
            targetState: Paused
        }
    ]
}

state Paused {
    onEntry: [ MuteAudio(); ]
    transitions: [
        transition {
            guard: PowerPressed
            targetState: Playback
        }
    ]
}
```
