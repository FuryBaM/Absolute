# Absolute compiler architecture

This document describes the ownership boundaries inside the compiler and the
preferred place for new language features.

## Compilation pipeline

```text
source / .absproj
        |
        v
Lexer -> Parser + syntax plugins -> AST
        |
        v
two-pass Analyzer -> symbols, types, ownership and flow facts
        |
        v
LLVM CodeGen -> LLVM IR -> optimized object / executable
        |
        v
Absolute Runtime + native/plugin libraries
```

The command-line driver owns project loading, import discovery, plugin loading,
diagnostic presentation and selection of parse/analyze/emit/build modes. It
must not contain syntax-specific semantic rules or LLVM lowering details.

## Parser and AST

`Absolute-Parser` owns tokens, AST node definitions, source parsing and the
versioned syntax-plugin ABI.

- `src/parser/parser_*.cpp` files parse one syntax family each.
- `include/syntax_tree.h` contains the core AST model.
- `include/plugin_api.h` is the stable C boundary used by native plugins.
- `src/syntax_plugins.cpp` adapts plugin-produced syntax to core or opaque AST.

A core feature belongs here only when its syntax must be understood without a
plugin. Optional domain syntax should prefer a plugin adapter. Parser code must
construct AST only; type resolution and ownership rules belong to Analyzer.

## Semantic Analyzer

`Absolute-Analyzer` performs two passes: declaration collection followed by
body resolution. It owns symbols, overload selection, type compatibility,
control-flow facts and raw/managed lifetime diagnostics.

The implementation is split by responsibility:

- `analyzer.cpp` — pass orchestration and shared semantic services;
- `symbol_table.cpp` — lexical scopes and symbol storage;
- `type_declarations.cpp` — structs, classes, interfaces and constructors;
- `analyzer_access.cpp` — identifiers, calls, arrays and member access;
- `analyzer_operators.cpp` — operators, conditionals and literals;
- `analyzer_values.cpp` — assignments, declarations and ownership operations;
- `analyzer_statements.cpp` — scopes, namespaces and control flow.

`analyzer_internal.h` is a private PCH boundary. Put stable declarations and
small shared helpers there. Do not move large visitor implementations into it:
doing so makes every Analyzer translation unit pay for every implementation.

New diagnostics should carry a stable code, source location when available and
the relevant `SymbolId`. A feature is not semantically complete until positive
and negative tests cover it.

## LLVM CodeGen

`Absolute-CodeGen` consumes the analyzed AST. It must use Analyzer facts rather
than repeat overload, ownership or type-policy decisions.

Public API is intentionally small in `include/codegen.h`. Internal state lives
in `CodeGenerator::Impl`, assembled by `include/codegen_internal.h`. Its `.inc`
files contain nested types, fields and method declarations only:

- `codegen_state.inc` — state and nested metadata types;
- `codegen_types.inc` — type/class/struct/interface declarations;
- `codegen_runtime.inc` — values, arrays, pointers, tasks and runtime helpers;
- `codegen_module.inc` — functions, globals and module/object emission.

Implementations are normal translation units:

- `codegen_core.cpp` — construction and common failures;
- `codegen_types.cpp` — LLVM types and object layouts;
- `codegen_runtime.cpp` — runtime calls and value conversion;
- `codegen_builtins.cpp` — built-in functions;
- `codegen_module.cpp` — functions, globals and final LLVM modules;
- `codegen_access.cpp`, `codegen_operators.cpp`, `codegen_values.cpp` and
  `codegen_statements.cpp` — AST visitor lowering.

Do not add method bodies back to the private PCH. A normal `.cpp` edit should
rebuild one object file, while a PCH edit is expected to rebuild all CodeGen
objects.

## Runtime

`Absolute-Runtime` implements facilities that generated programs call directly:

- managed pointer creation, validation, dereference and destruction;
- task scheduling, awaiting and task cleanup.

Runtime ABI changes affect emitted LLVM signatures and are therefore versioned
design changes. Update Runtime, CodeGen and native runtime tests together.

## Plugins

Plugins extend the compiler through the versioned C ABI rather than C++ class
interfaces. This keeps compiler and plugin toolchains decoupled.

- lowering plugins translate new syntax into ordinary Absolute source/AST;
- opaque plugins own an AST payload and participate in analysis/debug output
  and LLVM emission;
- `.absplugin` manifests declare libraries, dependencies and editor metadata.

A plugin must not silently replace a core keyword or bypass normal diagnostics.
ABI additions require version/capability negotiation and compatibility tests.

## Build boundaries

Parser, Analyzer, CodeGen and Compiler each have a private PCH. Public headers
should prefer forward declarations and stable value types. Frequently edited
implementation details belong in `.cpp` files.

Recommended WSL workflow:

```bash
cmake --preset wsl-release
cmake --build --preset wsl-release --parallel 4
ctest --preset wsl-release --parallel 4
```

The preset stores build artifacts under `$HOME/.cache/absolute`, because writing
PCH and object files through `/mnt` is substantially slower. Use
`benchmarks/build-suite/run.bat` to measure clean, no-op, one-unit and PCH
rebuilds before and after dependency-boundary changes.

## Definition of done for a language feature

A feature is complete only when all applicable layers are covered:

1. AST and parsing, or a documented plugin syntax boundary.
2. Declaration collection and semantic/type/flow validation.
3. Clear negative diagnostics with source positions.
4. LLVM emission and required Runtime ABI support.
5. Semantic, error, IR and executable tests.
6. README/reference documentation and at least one example.
7. IDE/plugin metadata when the syntax is externally extensible.
