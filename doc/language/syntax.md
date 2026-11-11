# Stride Language Specification: Syntax & AST Terminology

This document defines the official syntactic constructs, grammar terminology, and AST concepts of the Stride language.

---

## 1. Syntactic Model Overview

Stride's top-level grammar is built upon two primary syntactic constructs:

1. **Entity Declarations:** Define structural building blocks, types, execution domains, data storage, callables, etc.
2. **Streams:** Expressions that connect data sources, operators, function calls, and destinations to express dataflow and processing pipelines.

**Properties** (`propertyName: value`) are the internal mechanism used inside both entity declarations (to specify attributes and configuration) and function calls / invocations (to specify named arguments and port connections).

### 1.1 Entity Syntactic Usage
Once declared, an **Entity** can be referenced syntactically in the following contexts:
* **Within Streams:**
  * By **Entity Name only** (e.g., as a stream source, sink, or signal variable: `AudioIn >> AudioOut`).
  * By **Entity Name with functional notation** (as an invocation / callable with properties: `AudioIn >> Gain(gain: 0.5) >> AudioOut`).
* **Within Properties & Lists:**
  * By **Entity Name only** when passed as a property value (e.g., `rate: AudioRate`) or as an element within a list (e.g., `[Voice1, Voice2]`).

> **Note:** Although the term "entity" in many languages implies a runtime object or instance, in Stride an entity is a syntactical declaration that can be referenced throughout code. Instantiation and memory models are defined in the *Stride Semantics Specification*.

---

## 2. Lexical Casing Rules & Reserved Keywords

### 2.1 Casing Rules
Stride enforces a strict syntactic distinction based on casing:

```text
Entity Name   := [A-Z][A-Za-z0-9_]*    // Uppercase start
Entity Type   := [a-z][A-Za-z0-9_]*    // Lowercase start
Property Name := [a-z][A-Za-z0-9_]*    // Lowercase start
```

* **Entity Name (Uppercase):** Identifies declared entity instances (e.g., `Cutoff`, `Voices`, `MainDomain`, `Gain`).
* **Entity Type (Lowercase):** Specifies the category/type of the entity being declared (e.g., `signal`, `domain`, `module`, `reaction`, `type`).
* **Property Name (Lowercase):** Identifies key-value attributes in declarations and ports in function calls (e.g., `default`, `rate`, `in`, `out`, `gain`).

Entities and type names can also start with `_`, but this is reserved for internal schema types and runtime primitives.

### 2.2 Built-in Literals & Keywords
Stride reserves lowercase keywords for control states and empty/null values:

* **State Literals (`on`, `off`):** Represent binary switch and boolean states.
  ```stride
  switch Enabled {
      default: off
  }
  ```
* **Empty / Null Literal (`none`):** Represents an unassigned value, empty port connection, or absent state.
  ```stride
  reset: none
  ```
* **Generator (`in`):** Connects an entity to a list generator for declaration expansion:
  ```stride
  [Index in 0..5]
  ```

### 2.3 Numeric Literals
Integers are written in decimal (`44100`), hexadecimal (`0x1F`), or binary (`0b1010`). Real numbers have a fraction or an exponent (`0.5`, `.5`, `1e-3`, `2.5e6`). Digits can be grouped using underscores (`1_000_000`, `0xFFFF_FFFF`, `0b0000_1111`).

A suffix gives a literal an explicit data type:

| Suffix | Data type | Example |
|---|---|---|
| `i8`, `i16`, `i32`, `i64` | `Int8`..`Int64` | `-128i8`, `9_600_000_000i64` |
| `u8`, `u16`, `u32`, `u64` | `UInt8`..`UInt64` | `255u8`, `0xFFu8`, `0b0000_1111u8` |
| `u` | `UInt` | `42u` |
| `usize` | `Size` | `1024usize` |
| `f32`, `f64` | `Float32`, `Float64` | `0.5f32`, `2f64`, `1e-3f32` |

* Float suffixes only follow decimal digits. In `0x1f32` the `f` is a hex digit, so it is the integer `0x1F32`.
* A literal must fit its type: `300u8`, `128i8`, and `-1u8` produce compile errors. `-128i8` is validated after unary negation.
* A literal without a suffix is untyped. When paired with a typed value, it infers that type if the type can represent it: in `Byte + 1`, where `Byte` is `UInt8`, the `1` is treated as `UInt8`. Untyped integers default to `Int` and untyped reals default to `Real`, whose physical bit widths are determined by the active framework.

#### Arithmetic Streamed to a Wider Type
An expression streamed to a destination that holds every value of the expression's type is computed in the destination's type, preventing overflow or loss of precision before assignment:

```stride
Byte + 1 >> ByteOut;      # UInt8 arithmetic: 255 + 1 wraps to 0
Byte + 1 >> IntOut;       # Int32 arithmetic: 256
Byte * 2 >> IntOut;       # 510
Single * 0.1 >> RealOut;  # computed as Float64
Count / 4 >> RealOut;     # integer arithmetic streamed to float is evaluated in Int64: 10 / 4 is 2
```

---

## 3. Declarations

### 3.1 Entity Declaration
An **Entity Declaration** introduces a named entity into a scope with a lowercase entity type, an uppercase entity name, and an optional body containing properties. Any nested entity declarations are placed within property lists (`entities: [...]` or `code: [...]`).

```stride
<entityType> <EntityName> {
    <propertyName>: <propertyValue>
}
```

*Example:*
```stride
type Name {
    default: 1000.0
    property: Value
}
```

### 3.2 Array Declaration
An **Array Declaration** introduces an indexed collection of entities with an explicit dimension. The size expression within brackets can be an integer literal or a compile-time constant expression (such as `Port.size`).

```stride
<entityType> <EntityName>[<SizeExpression>] {
    <properties...>
}
```

*Examples:*
```stride
signal ArrayName[4] {
    default: 0.0
}

signal Channels[InPort.size] {
    default: 0.0
}
```

### 3.3 Generator Declaration
A **Generator Declaration** expands a parameterized template across a range or collection of values at compile time:

```stride
<entityType> <EntityName>[<IterationVar> in <GeneratorRange>] {
    <properties...>
}
```

*Examples:*
```stride
# Generator with range:
item Items[I in 0..3] {
    label: ["Item0", "Item1", "Item2", "Item3"]
}

# Generator with stepped range:
signal Downsampled[I in 0..2..30] {
    rate: AudioRate
}

# Generator over a list:
transition Move[Sq in [1, 2, 4, 8]] {
    guard: Legal[Sq]
}
```

> **Range Syntax Note:**
> * **Generator Ranges (`..`):** Generator notation strictly uses double dots `..` to express inclusive bounds (`start..end` or `start..step..end`).
> * **Slice / Index Ranges (`:`):** Array slicing and bundle indexing use colon notation `:` (e.g., `Signal[0:31]`).

---

## 4. Streams & Invocations

### 4.1 Streams
A **Stream** is a pipeline expression that routes data between sources, transformations, and destinations using the stream operator `>>`.

```stride
AudioIn >> LowPass(cutoff: 1000.0) >> AudioOut;
```

### 4.2 Invocations (Function Calls)
An **Invocation** applies a callable entity by its `Entity Name` using functional notation, passing a set of properties that bind arguments to its ports:

```stride
Process(parameter: 0.75);
```

### 4.3 Invocation Properties
Inside an invocation, **Properties** map expressions, variables, or literals to the callee's named ports:

```stride
in: InputSignal
gain: 0.75
```

---

## 5. Member & Index Access

* **Member Access (`Target.member`):** Uses dot notation to reference child declarations (by `Entity Name`) or internal properties/ports (by `Property Name`).
  ```stride
  OscillatorSignal.out      // Accessing property or port 'out'
  AudioDomain.rate          // Accessing property 'rate'
  ```
* **Index Access (`Target[index]`):** Uses bracket notation to address an individual element within an array entity.
  ```stride
  Voices[0]
  ```
* **Slice Access (`Target[start:end]`):** Uses bracket colon notation to address a contiguous slice or bundle of elements within an array entity.
  ```stride
  Signals[0:7]
  ```

---

## 6. Syntactic Terminology Reference

| Syntactic Term | Lexical Form / Grammar | Context | Example |
| :--- | :--- | :--- | :--- |
| **Entity Name** | `[A-Z][A-Za-z0-9_]*` | Entity instance names | `Cutoff`, `Voices`, `Gain` |
| **Entity Type** | `[a-z][A-Za-z0-9_]*` | Entity type / declaration keyword | `signal`, `domain`, `module` |
| **Property Name** | `[a-z][A-Za-z0-9_]*` | Keys for properties and ports | `default`, `rate`, `in`, `out` |
| **Literals / Keywords** | `on`, `off`, `none` | Reserved literals for state/null | `default: off`, `reset: none` |
| **Entity Declaration** | `entityType EntityName { ... }` | Single entity declaration | `signal Cutoff { ... }` |
| **Array Declaration** | `entityType EntityName[Size] { ... }` | Array entity declaration | `signal Voices[4] { ... }` |
| **Generator Declaration** | `entityType EntityName[Var in Range] { ... }` | Parameterized template generator | `item Items[I in 0..3] { ... }` |
| **Generator Range** | `start..end` or `start..step..end` | Generator range expression | `0..3`, `0..2..30` |
| **Stream** | `Source >> Transform >> Sink` | Dataflow pipeline expression | `In >> Filter() >> Out` |
| **Invocation** | `CalleeName( ... )` | Function/module call expression | `Gain(in: AudioIn, out: Out)` |
| **Property** | `propertyName: Value` | Inside declarations or invocations | `rate: 44100`, `in: AudioIn` |
| **Member Access** | `Target.member` | Dot-notation accessor | `Filter.out` |
| **Index Access** | `Target[index]` | Single-element accessor | `Voices[0]` |
| **Slice Access** | `Target[start:end]` | Bracket-notation bundle slice | `Signals[0:7]` |
