# Data Types in Stride

Every value in Stride has a data type, such as `Int`, `Real`, `UInt8`, or `Switch`. Some data types have a fixed bit width across all target platforms. Others are **aliases** whose physical width is determined by the target platform's framework.

```stride
signal Gain    { type: Real }               # the framework's preferred float (e.g. Float64 on desktop, Float32 on MCU)
signal Count   { type: Int default: 0 }     # the framework's preferred integer (e.g. Int32)
signal Level   { type: UInt8 default: 255 } # exactly 8 bits, unsigned
signal Samples { type: Float32 }            # exactly 32 bits, on every platform
```

---

## 1. The Data Types

| Kind | Fixed Size | Size Decided by Framework |
|---|---|---|
| Signed integer | `Int8`, `Int16`, `Int32`, `Int64` | `Int` |
| Unsigned integer | `UInt8`, `UInt16`, `UInt32`, `UInt64` | `UInt`, `Size` |
| Floating point | `Float32`, `Float64` | `Real` |
| Boolean / Switch state | `Switch` (`on` / `off`) | |
| Text | `String` | |

The name of a type encodes its size and signedness:
- `Int8` through `Int64` are signed integers.
- `UInt8` through `UInt64` are unsigned integers.
- `Float32` and `Float64` are IEEE-754 single and double precision floats.
- Aliases inherit signedness from the target type (`UInt` and `Size` are always unsigned).

Data type names never start with `_`. Names that begin with `_` (`_NumericTypes`, `_IntLiteral`, `_Switch`, etc.) are **schema types** used to specify and validate declarations.

---

## 2. Framework-Decided Types (`Int`, `UInt`, `Real`, `Size`)

`Int`, `UInt`, `Real`, and `Size` are platform aliases. An alias refers to one of the fixed-size types, and the compiling framework selects the width best suited to its target hardware architecture:

| Framework | `Int` | `UInt` | `Real` | `Size` |
|---|---|---|---|---|
| JIT | `Int32` | `UInt32` | `Float64` | `UInt64` |
| Cpp, DesktopTemplate, TestFramework | `Int32` | `UInt32` | `Float64` | `UInt64` |
| Pico (RP2040/RP2350), STM32 | `Int32` | `UInt32` | `Float32` | `UInt32` |
| Wiring (AVR 8-bit) | `Int16` | `UInt16` | `Float32` | `UInt16` |

For example, `signal Gain { type: Real }` compiles to a 64-bit `double` in the JIT/desktop framework and a 32-bit `float` on Cortex-M microcontrollers where single-precision hardware FPUs are standard.

### 2.1 Framework Type Declarations
Each framework specifies its concrete data types in `Types.stride`. For instance:

```stride
frameworkDataType Int32 {
    frameworkName: "int32_t"
    includes: ["<cstdint>"]
    kind: "integer"
    bits: 32
}

alias Int {
    entity: Int32
}

alias Real {
    entity: Float32
}
```

- **JIT Compilation**: The JIT compiler reads `frameworkDataType` definitions to construct LLVM types (`i32`, `float`, `double`, `i1`).
- **AOT & C/C++ Code Generation**: The compiler emits the type's `frameworkName` and includes necessary headers (e.g. `<cstdint>`).

---

## 3. Selecting a Specific Size

Use platform aliases (`Int`, `Real`) for general application logic, counters, and intermediate variables. Use fixed-size types when hardware constraints or exact memory layouts require them:

- **Known Protocol or Hardware Layouts:** Use `UInt8` for MIDI bytes or pixel channels, `Int16` for audio PCM buffers, and `UInt32` for hardware register offsets.
- **Cross-Platform Determinism:** When math results must remain bit-for-bit identical across desktop and embedded targets, declare explicit types like `Float64`.
- **Foreign Function Interface (FFI) & Platform Modules:** When interfacing with native C functions or LLVM instructions that expect specific bit widths, match the exact types (`Int32`, `UInt32`).
- **Modulo / Wraparound Arithmetic:** `UInt8` with value `255 + 1` wraps cleanly to `0`.

```stride
signal Byte     { type: UInt8 default: 0 }
signal Position { type: Int64 default: 0 }
signal Phase    { type: Float32 default: 0.0 }
```

A `signal` declared without an explicit `type` defaults to the first entry of the framework's `_NumericTypes` (typically `Real`).

---

## 4. Literals, Type Promotion, and Conversion

### 4.1 Literals
Literals without suffixes inherit alias types: `3` is an `Int`, `0.5` is a `Real`, `on` is a `Switch`, and `"text"` is a `String`. When stored directly into an entity, the literal adapts to the entity's declared type (`3000000000 >> Wide;` assigns cleanly to an `Int64`).

### 4.2 Mixed-Type Promotion
When an expression or list combines two numeric types, both operands are promoted according to the following precedence:

1. **Floating point takes precedence over integer:** `Int + Real` yields `Real`.
2. **Wider width takes precedence:** `Float32 + Float64` yields `Float64`; `UInt8 + UInt16` yields `UInt16`.
3. **Signedness preservation:**
   - If a signed type is wider than the unsigned type, the signed type is selected (`UInt8 + Int16` yields `Int16`).
   - If the widths are equal, the result promotes to a signed integer twice as wide so no unsigned values are lost (`UInt8 + Int8` yields `Int16`; `Int32 + UInt32` yields `Int64`).
   - Mixing signed and unsigned values never produces an unsigned result, avoiding C-style negative wrap surprises (`Int8` `-3` minus `UInt8` `10` evaluates to `-13`, not `243`).

### 4.3 Assignment Conversion
Streaming a value into an entity converts it to the entity's target type:
- Integers widen or truncate according to source signedness.
- Integers convert to floats preserving numerical sign (`UInt8` `255` becomes `255.0`).
- Floats convert to integers by truncating toward zero.
- Any non-zero numeric value streamed into a `Switch` becomes `on`; zero becomes `off`.

---

## 5. Detecting Lossy Conversions

The compiler validates value ranges and reports narrowing conversions:

| Conversion | Behavior |
|---|---|
| Narrower integer (`Int32` $\to$ `Int16`) | Values outside target range wrap |
| Signed to unsigned (`Int8` $\to$ `UInt32`) | Negative values wrap |
| Unsigned to signed of same/smaller width (`UInt32` $\to$ `Int32`) | High-bit values become negative |
| Float to integer (`Real` $\to$ `Int`) | Fractional portion truncated |
| Narrower float (`Float64` $\to$ `Float32`) | Precision and dynamic range reduced |
| Large integer to float (`Int64` $\to$ `Float64`) | Values above $2^{53}$ rounded |

Pass `-Wconversion` (or `--report-conversions`) to `stridec` to surface lossy conversions during build:

```bash
stridec -c dsp.stride -o dsp.o -Wconversion
dsp.stride:30:1: warning: conversion from 'Int' to 'UInt8' wraps negative values and values above the maximum of UInt8 [-Wconversion]
```

To resolve warnings, insert an explicit conversion entity or widen the target variable.

---

## 6. Custom Aliases

Applications and libraries can declare project-specific aliases using `alias`:

```stride
# Audio sample format for active hardware codec
alias Sample {
    entity: Int16
}

# Aliases of aliases resolve transitively
alias AudioSample {
    entity: Sample
}
```

Cyclic alias definitions (`alias A { entity: B }` and `alias B { entity: A }`) are rejected at compile time.

---

## 7. Schema Types & Validation

Schema types (`_NumericTypes`, `_IntegerTypes`, `_FloatTypes`, `_DataTypes`, `_IntLiteral`, etc.) describe grammatical type classes for compiler schema validation. They cannot be used as runtime variable data types.

The compiler rejects:
- Using a schema type as a signal's type (`signal X { type: _NumericTypes }`).
- Naming an entity after a built-in type (`signal Real {}`).
- Invalid bit widths or type classes that do not match framework capabilities.
