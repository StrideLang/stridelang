# Namespaces, Packages, and Scopes

This document formalizes the rules governing lexical scopes, import directives, package root (barrel) files, namespace resolution, and package security in the Stride language.

---

## 1. Lexical Scoping & Variable Visibility

When resolving variable or signal identifiers, the compiler inspects lexical scopes in strict bottom-up order:

1. **Local Entity Scope (`scopeStack.back()`)**: Temporary variables, loop allocas/PHI nodes, and reaction locals.
2. **Immediate Parent Scope**: The container directly enclosing the entity.
3. **Enclosing Declaration Scope (Domain / Module)**:
   - **Root-level Domain Members (Reactions & Loops)**: Have direct read and write access to signals, constants, switches, and IO entities declared at that domain's scope.
   - **Domain Gate Invariant**: When an entity is nested inside another declaration (such as a module), it inherits variable access **only from its immediate parent scope**. Cross-boundary variable access is gated to the immediate parent's domain.
   - **Module Encapsulation Boundary**: Modules are strictly encapsulated units of computation. A `module` (and any nested reactions/loops) **cannot** implicitly access outer domain variables; all data transfer must cross declared input/output ports.
4. **Shadowing**: Local declarations strictly shadow parent or domain variables sharing the same name.

---

## 2. Filesystem Conventions & Imports

### 2.1 Capitalization & Block Identifiers
- **Naming Rule**: All importable directories and `.stride` source files **must start with a capital letter** (e.g. `Filters/`, `Generators/`, `Biquad.stride`).
- Import statements accept native **Block Identifiers** (never quoted string literals):
  ```stride
  import Filters
  import Filters::Biquad
  import Filters as Flt
  ```

### 2.2 Conditional Imports with `@with(...)`
Imports can be included conditionally based on feature toggles, either individually or grouped in blocks:

```stride
@with(Audio)
import Filters

@with(!Audio)
import DummyAudio

@with(Audio && Advanced) {
    import Filters::Biquad
    import Generators as Gen
}
```

When a feature condition evaluates to `false`, the compiler's preprocessor prunes guarded import statements, preventing unneeded dependencies and target modules from loading.

### 2.3 Include Path Search Priority
When resolving an `import`, the compiler searches include roots in the following order:
1. **Current Directory**: Sibling files in the directory of the compiling file.
2. **Project Include Directories**: Configured project source paths (`-I <dir>`).
3. **Active System Directory**: `$STRIDEROOT/systems/<System>/<Ver>/`.
4. **Standard Library**: `$STRIDEROOT/library/<Ver>/`.

---

## 3. Package Root (Barrel File) Model

When a directory `Name/` and a file `Name.stride` share the same name at the same directory level:

```text
src/
├── Filters.stride          # Package Root (Barrel File)
└── Filters/                # Sub-namespace Directory
    ├── Biquad.stride       # Sub-module Filters::Biquad
    └── Lowpass.stride      # Sub-module Filters::Lowpass
```

### 3.1 Resolution Semantics
1. **`Name.stride` as Package Root**: Contains top-level exports, shared types, default module definitions, and package constants for the `Name` namespace.
2. **`Name/` as Sub-Namespace**: Contains nested modules accessible via `Name::SubModule`.
3. **Import Variations**:
   - `import Filters`: Parses `Filters.stride` and registers `Filters/` for on-demand sub-module resolution.
   - `import Filters::Biquad`: Resolves directly to `Filters/Biquad.stride`.
   - `import Filters as Flt`: Aliases both the root file and child modules under `Flt::` (e.g., `Flt::Biquad`).
4. **Symbol Collision**: If both `Filters.stride` and `Filters/Biquad.stride` declare a symbol named `Biquad`, the explicit file `Filters/Biquad.stride` takes precedence for `Filters::Biquad`.

---

## 4. Package Declarations & Derivation

### 4.1 Explicit `package` Statement
A file can declare its namespace explicitly at the top of the file:

```stride
package Audio::Filters
```

This establishes the logical namespace for all declarations in the file, overriding directory-derived paths.

### 4.2 Path-Based Derivation (Fallback)
If no `package` statement is present, files located inside capitalized subdirectories derive their namespace from their relative folder path (e.g., `Audio/Filters/Biquad.stride` $\to$ `Audio::Filters::Biquad`).

---

## 5. Package Security & Symbol Integrity

To prevent namespace poisoning and dependency injection attacks, Stride enforces strict compiler security invariants:

1. **Sealed Packages**:
   Packages cannot be reopened arbitrarily across unverified files. Application code or third-party packages cannot declare or inject symbols into system namespaces (`package Stride::*`, `package Pico::*`).
2. **Authority Roots**:
   System namespaces (`Stride`, `Buffer`, `Math`, `System`, `Platform`) are reserved authority roots loaded exclusively from verified `$STRIDEROOT` directories.
3. **Immutable Symbol Tables**:
   Once a declaration is registered in a package's symbol table, it cannot be redefined or monkey-patched. Subsequent redeclarations produce a `SymbolRedeclarationError`.
4. **Final Declarations (`final`)**:
   Entities marked `final` in their schema cannot be overridden or extended by external packages.
5. **Lockfile Integrity (`stride.lock`)**:
   The Stride package manager verifies cryptographic SHA-256 hashes of package source directories against `stride.lock` prior to compilation.
6. **Encapsulation (`public: off`)**:
   Symbols without explicit `public` visibility remain internal to their declaring package and cannot be targeted by external stream writes or dynamic member accesses.
