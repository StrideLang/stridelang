# Signal Bundles & Schema-Driven Expansion

In Stride, a **bundle** represents a statically allocated array of $N$ signal or entity instances of the same type.

When assigning properties to a bundle (e.g. `signal Voices[4] { default: [100.0, 200.0, 300.0, 400.0] }`), Stride utilizes schema type constraints to determine deterministically whether an assigned list represents an **expansion** across the bundle's elements or an **atomic property value** cloned to each element.

---

## 1. Terminology & Value Nesting Ranks

- **Bundle Array ($B$):** A statically dimensioned array of $N$ identical signal entities, where $N = \text{length}(B)$.
- **Target Property ($P$):** A property defined within the entity's schema that accepts a Scalar, a $\text{List}[T]$, or a Union ($\text{Scalar} \cup \text{List}[T]$).
- **Assigned Value ($V$):** The literal expression passed to the property, classified by its list nesting rank:
  - **$R_0$ (Scalar Atom):** A single atomic literal (e.g., `440.0`, `off`).
  - **$R_1$ (Flat 1D List):** A flat list of items (e.g., `[1.0, 2.0, 3.0, 4.0]`).
  - **$R_2$ (Nested List of Lists):** A list containing child lists (e.g., `[[1, 2], [3, 4]]`).

---

## 2. Expansion and Validation Rules

### Rule 1: Scalar Schema Expectation
When the target property strictly expects a single scalar value:
* **Assignment is a Scalar ($R_0$):** Evaluated as an atomic value and **cloned** identically across all $N$ bundle instances.
* **Assignment is a Flat List ($R_1$):** Triggers an **expansion** directive.
  - *Dimension Constraint:* The list length $K$ must exactly equal bundle dimension $N$.
  - *Type Validation:* Each element $V[i]$ in the list is strictly validated against the target scalar type before being assigned to bundle element $i$.
* **Assignment is a Nested List ($R_2$):** Produces a static compile-time dimension and type error.

### Rule 2: List Schema Expectation ($\text{List}[T]$)
When the target property expects a list of elements of type $T$:
* **Assignment is a Flat List ($R_1$):** Evaluated as an atomic property and **cloned** identically across all $N$ bundle instances, regardless of list length.
* **Assignment is a List of Lists ($R_2$):** Triggers an **expansion** directive.
  - *Dimension Constraint:* The outer list length $K$ must equal bundle dimension $N$.
  - *Type Validation:* Each inner list $V[i]$ is validated against $\text{List}[T]$ before being assigned to bundle element $i$.

### Rule 3: Union Schema Expectation ($\text{Scalar} \cup \text{List}[T]$)
When the target property polymorphically accepts either a scalar or a list:
* **Assignment is a Flat List ($R_1$):** Resolves directly to the list variant of the schema. It is **cloned** as an atomic value to all $N$ bundle instances, prioritizing value semantics over ambiguous expansion.
* **Assignment is a List of Lists ($R_2$):** Triggers an **expansion** directive across the bundle.
  - *Dimension Constraint:* The outer list length $K$ must equal bundle dimension $N$.

---

## 3. Expansion Resolution Matrix

| Schema Expectation | Assigned Value Rank | Evaluated Behavior | Validation Constraints |
| :--- | :--- | :--- | :--- |
| **Scalar** | $R_0$ (Atom) | **Cloned** | Validates as matching scalar type across all $N$. |
| **Scalar** | $R_1$ (Flat List) | **Expanded** | List length must equal $N$; elements validated against scalar type. |
| **List** | $R_1$ (Flat List) | **Cloned** | Entire list cloned to each member; items validated against type $T$. |
| **List** | $R_2$ (List of Lists) | **Expanded** | Outer list length must equal $N$; inner elements validated against type $T$. |
| **Union** | $R_1$ (Flat List) | **Cloned** | Satisfies list branch natively; cloned to all $N$ members. |
| **Union** | $R_2$ (List of Lists) | **Expanded** | Outer list length must equal $N$; inner elements validated against type $T$. |

---

## 4. Examples in Practice

### 4.1 Scalar Property Expansion
```stride
# default expects a scalar: R_1 triggers expansion across 4 elements
signal Frequencies[4] {
    type: Real
    default: [220.0, 440.0, 660.0, 880.0]
}

# default scalar R_0 clones identically across all 4 elements
signal Amplitudes[4] {
    type: Real
    default: 0.5
}
```

### 4.2 Bundle Slicing and Dynamic Indexing
```stride
# Accessing an individual slice or member
Frequencies[0] >> SynthVoice();

# Slicing a contiguous sub-bundle (indices 0 through 1)
Frequencies[0:1] >> DualOsc();
```
