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
1. **`Name.stride` as Package Root**: Contains top-level exports or re-export imports (`import Filters::Biquad as Biquad`) for the `Name` package.
2. **`Name/` as Sub-Namespace**: Contains nested modules, each declaring its own `package` matching its path (e.g., `package Filters::Biquad` in `Filters/Biquad.stride`).
3. **Single Canonical Namespace**: Each declaration has **one** canonical namespace (`namespaceTree`). Qualified lookups match strictly against `namespaceTree` (after expanding file-local `import ... as Alias` entries), without falling back to the file's `_sourcePath`. A sub-module declaring `package Control::Foc` is exposed only under `Control::Foc::` (or a file-local alias), not under `Control::`.
4. **Import Variations**:
   - `import Filters`: Parses `Filters.stride` and loads `Filters/`.
   - `import Filters::Biquad`: Resolves directly to `Filters/Biquad.stride`; callers qualify with `Filters::Biquad::...`.
   - `import Filters::Biquad as Bq`: Maps `Bq` to `Filters::Biquad` in the importing file's `_imports` table; `Bq::...` canonicalizes to `Filters::Biquad::...`.

---

## 4. Package Declarations, Derivation & Root Qualification (`::`)

### 4.1 Explicit `package` Statement
A file can declare its canonical namespace explicitly at the top of the file:

```stride
package Audio::Filters
```

This sets `namespaceTree` (`["Audio", "Filters"]`) on all top-level declarations in the file. For sub-modules, the `package` path should match the file's path under the library root (e.g., `Control/Foc.stride` $\to$ `package Control::Foc`).

### 4.2 Path-Based Derivation (Fallback)
If no `package` statement is present:
- **Nested imports** (`import Audio::Filters`, containing `::`): `StrideLibrary::loadImport` derives `namespaceTree` from the import path (`["Audio", "Filters"]`), keeping unpackaged sub-modules out of the root namespace.
- **Top-level unpackaged imports** (`import GameSchema`, no `::`): declarations remain in the root namespace `{}`.
- **Directory loading** (`ASTFunctions::loadAllInDirectory`): declarations without `namespaceTree` inherit their parent directory name.

### 4.3 Root-Qualified Identifiers (`::` Prefix)
Identifiers and calls may be prefixed with `::` to bypass local scope, enclosing `package` scope, and file-local `import ... as ...` aliases:

```stride
::Input >> ::Control::Foc::Park() >> ::Output;
```

- `::Name` resolves strictly in the root namespace `{}` (ignoring local module/reaction variables and enclosing package symbols).
- `::Pkg::Name` resolves strictly against canonical `namespaceTree == ["Pkg"]` (ignoring any local `import ... as Pkg` alias).

---

## 5. Symbol Integrity & Collision Protection

1. **Package-Scoped Duplicate Detection**:
   `CodeValidator::findDuplicateSymbols` compares canonical `namespaceTree` (and `framework`) rather than syntactic scope. Two different packages may declare the same symbol name (`PkgA::Filter` and `PkgB::Filter`), while duplicate declarations within the same package or root `{}` emit `LangError::DuplicateSymbol`.
2. **Codegen Symbol Mangling**:
   `NameMangler::mangle` includes the declaration's canonical `namespaceTree` scope when generating mangled LLVM IR symbols for polymorphic callables, preventing cross-package symbol collisions.
3. **Immutable Symbol Tables**:
   Once a declaration is registered in a package's symbol table, it cannot be redefined within the same namespace.
4. **Authority Roots & Sealed Packages (Planned)**:
   System namespaces (`Stride`, `Buffer`, `Math`, `System`, `Platform`) are loaded from verified `$STRIDEROOT` directories; lockfile verification (`stride.lock`), `final` schema locks, and `public: off` visibility enforcement are planned extensions.
