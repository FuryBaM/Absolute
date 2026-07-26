# Debugging Absolute

## Native debugger

The VS Code extension launches Microsoft's C/C++ debugger (`cppvsdbg` on Windows,
`cppdbg` on Linux/macOS) after a native build.

Visualizer files ship in [`debugging/`](../debugging/):

| File | Purpose |
|------|---------|
| `Absolute.natvis` | Visual Studio / cppvsdbg display of array descriptors and managed handles |
| `absolute_gdb.py` | GDB pretty-printers |
| `absolute_debug_types.h` | Optional cast helpers for the watch window |

The extension sets `visualizerFile` to `debugging/Absolute.natvis` when present.

### Layouts

**Array / slice descriptor** (see [array-ownership.md](array-ownership.md)):

| Offset | Field |
|--------|--------|
| 0 | `data` — pointer to first visible element |
| 8 | `owner` — allocation base, or null if borrowed/stack |
| 16+ | `int64` length per dimension |

**Managed pointer** — `uint64` handle:

- low 32 bits: slot index  
- high 32 bits: generation  

**Task** — opaque `void*` task record (`absolute_task_*` runtime).

Without full DWARF/CodeView emission from `absolutec`, locals may appear as
untyped storage. Cast in the watch window:

```text
(AbsoluteArray1D*)&myArray
(AbsoluteManagedHandle)myManagedBits
```

## Opaque plugin blocks

Syntax plugins (for example `shader`) own opaque AST. The editor maps them to
virtual documents:

```text
absolute-opaque:<host-path>#shader@<startLine>
```

- **Open Opaque Block** extracts the block body into a virtual buffer.  
- Breakpoints set on the virtual document map back to the host `.abs` line
  (start of the block) via the Absolute debug configuration provider.  
- Source map metadata is available as JSON from the LSP custom request
  `absolute/opaqueSourceMaps`.

## REPL / expression evaluator

```bat
absolute repl
absolute eval "2 + 2"
```

VS Code: **Absolute: Evaluate Expression** and **Absolute: Open REPL**.

Expressions are wrapped in a temporary Absolute program, compiled with
`absolutec --build-exe`, executed, and stdout is shown. Session mode keeps a
preamble of previous declarations for the duration of the REPL process.
