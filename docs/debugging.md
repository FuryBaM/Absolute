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
`F5` builds with `-g -O0`, so breakpoints can be placed directly on executable
lines in `.abs` files. `Ctrl+F5` keeps the regular optimized build.

The command-line equivalent is:

```bat
absolutec program.abs -g -O0 --build-exe -o program.exe
```

On Windows this emits CodeView information plus `program.pdb`. On Linux and
macOS it emits DWARF. `debugBreak()` is a zero-argument builtin that lowers to a
native debugger trap and can be guarded by normal program logic:

```absolute
if (shouldPause) {
    debugBreak();
}
```

### Locals, places, values, and roles

Named parameters and locals carry their Absolute source names and types.

- A local or mutable parameter is a debugger-visible **place**. Watch `counter`
  for its current value and `&counter` for its storage address.
- A by-value parameter has independent storage, so its address differs from the
  caller's place.
- `ref` parameters resolve to the caller's place; their address is stable across
  the call.
- Managed owner, subscriber, and weak values use the same visible handle type.
  The compiler also exposes a synthetic `<parameter>.isOwner` boolean for
  callable parameters. This reflects the runtime role delivered by an ordinary
  call or `move(...)`.
- The managed NatVis view shows slot, generation, current validity, and the
  current pointee address. The last two fields call read-only runtime lookup
  helpers and may be unavailable when debugger function evaluation is disabled.
- Arrays show `data`, allocation `owner`, every dimension, and 1D elements.

An arbitrary unnamed expression is an SSA temporary and is not promised a
stable address. Assign it to a named local when it must be watched across source
steps.

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

For old binaries built without `-g`, locals may still appear as untyped
storage. Cast them in the watch window:

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
